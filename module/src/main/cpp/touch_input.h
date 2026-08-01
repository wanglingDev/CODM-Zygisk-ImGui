#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT via Zygisk Companion (root) → game process
//
//  Flow:
//  1. Companion (root) opens /dev/input/eventX
//  2. Sends fd to game process via SCM_RIGHTS ancillary msg
//  3. Game process polls fd non-blocking from render thread
// ════════════════════════════════════════════════════════════════
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include "imgui.h"
#include "hook.h"

extern int g_width, g_height;

static int   g_touch_fd      = -1;
static float g_ax_min = 0,   g_ax_rng = 1;
static float g_ay_min = 0,   g_ay_rng = 1;
static bool  g_scale_init    = false;
static int   g_log_cnt       = 0;

// ── Companion-side: open touchscreen, return fd ───────────────────
// Called by companion_handler in main.cpp (runs as root)
static int CompanionFindTouchFd() {
    DIR* d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strncmp(de->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        char name[256] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name)-1), name);
        uint8_t absbits[(ABS_MAX/8)+1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        bool has_mt = (absbits[ABS_MT_POSITION_X/8] >> (ABS_MT_POSITION_X%8)) & 1;
        if (has_mt) {
            closedir(d);
            LOGI("[ENI-companion] touchscreen: %s ('%s') fd=%d", path, name, fd);
            return fd;
        }
        close(fd);
    }
    closedir(d);
    return -1;
}

// ── Send fd via SCM_RIGHTS ────────────────────────────────────────
static bool SendFd(int sock, int fd) {
    char buf[1] = {'F'};
    struct iovec iov = { buf, 1 };
    char cms_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cms_buf;
    msg.msg_controllen = sizeof(cms_buf);
    struct cmsghdr* cms = CMSG_FIRSTHDR(&msg);
    cms->cmsg_level = SOL_SOCKET;
    cms->cmsg_type  = SCM_RIGHTS;
    cms->cmsg_len   = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cms), &fd, sizeof(int));
    return sendmsg(sock, &msg, 0) >= 0;
}

// ── Receive fd via SCM_RIGHTS ─────────────────────────────────────
static int RecvFd(int sock) {
    char buf[1];
    struct iovec iov = { buf, 1 };
    char cms_buf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg = {};
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cms_buf;
    msg.msg_controllen = sizeof(cms_buf);
    if (recvmsg(sock, &msg, 0) < 0) return -1;
    struct cmsghdr* cms = CMSG_FIRSTHDR(&msg);
    if (!cms || cms->cmsg_type != SCM_RIGHTS) return -1;
    int fd;
    memcpy(&fd, CMSG_DATA(cms), sizeof(int));
    return fd;
}

// ── Companion handler (called by Zygisk, runs as root) ───────────
// Defined here, registered in main.cpp via REGISTER_ZYGISK_COMPANION
static void HandleCompanion(int sock) {
    int touch_fd = CompanionFindTouchFd();
    if (touch_fd < 0) {
        // Send -1 signal: write error byte
        char err = 'E';
        write(sock, &err, 1);
        LOGI("[ENI-companion] no touchscreen found");
        return;
    }
    // Send fd to game process
    if (!SendFd(sock, touch_fd)) {
        LOGI("[ENI-companion] SendFd failed");
    }
    close(touch_fd); // game process has its own fd now
}

// ── Game-side: receive fd from companion ─────────────────────────
static void InitTouchScale() {
    struct input_absinfo ax = {}, ay = {};
    ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
    ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
    g_ax_min = ax.minimum; g_ax_rng = ax.maximum - ax.minimum;
    g_ay_min = ay.minimum; g_ay_rng = ay.maximum - ay.minimum;
    if (g_ax_rng <= 0) g_ax_rng = 1;
    if (g_ay_rng <= 0) g_ay_rng = 1;
    LOGI("[ENI] touch scale: X[%.0f+%.0f] Y[%.0f+%.0f] screen:%dx%d",
         g_ax_min, g_ax_rng, g_ay_min, g_ay_rng, g_width, g_height);
    g_scale_init = true;
}

static void InstallMotionHooks(int companion_sock) {
    g_touch_fd = RecvFd(companion_sock);
    if (g_touch_fd >= 0) {
        // Make non-blocking for render-thread polling
        fcntl(g_touch_fd, F_SETFL, fcntl(g_touch_fd, F_GETFL) | O_NONBLOCK);
        LOGI("[ENI] touch fd from companion: %d OK", g_touch_fd);
    } else {
        LOGE("[ENI] companion did not send touch fd");
    }
}

// ── Poll events from render thread (non-blocking) ─────────────────
static inline void FlushTouchToImGui() {
    if (g_touch_fd < 0) return;
    if (!g_scale_init) InitTouchScale();

    struct input_event ev;
    static float cx = 0.f, cy = 0.f;
    static bool  tracking = false;
    static int   slot = 0;

    while (read(g_touch_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_MT_SLOT) { slot = ev.value; break; }
            if (slot != 0) break;
            if (ev.code == ABS_MT_TRACKING_ID) {
                tracking = (ev.value != -1);
                if (!tracking) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                    io.AddMouseButtonEvent(0, false);
                }
            }
            if (ev.code == ABS_MT_POSITION_X)
                cx = ((ev.value - g_ax_min) / g_ax_rng)
                     * (float)(g_width > g_height ? g_width : g_height);
            if (ev.code == ABS_MT_POSITION_Y)
                cy = ((ev.value - g_ay_min) / g_ay_rng)
                     * (float)(g_width > g_height ? g_height : g_width);
            break;
        case EV_SYN:
            if (ev.code == SYN_REPORT && slot == 0 && tracking) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(cx, cy);
                io.AddMouseButtonEvent(0, true);
                if (g_log_cnt < 20) {
                    LOGI("[ENI] touch (%.0f,%.0f)", cx, cy);
                    g_log_cnt++;
                }
            }
            break;
        }
    }
}

static inline void CustomAndroidNewFrame(int w, int h) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static double s_prev = 0.0;
    double now = (double)ts.tv_sec + ts.tv_nsec / 1e9;
    io.DeltaTime = (s_prev > 0.0) ? (float)(now - s_prev) : 1.f/60.f;
    if (io.DeltaTime <= 0.f) io.DeltaTime = 1.f/60.f;
    s_prev = now;
}
