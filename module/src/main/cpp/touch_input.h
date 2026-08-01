#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT — /dev/input/eventX polling dari render thread
//
//  Why this works:
//  • Bypass NDK/JNI layer sepenuhnya — baca langsung dari kernel
//  • Non-blocking O_NONBLOCK: dipanggil tiap frame dari eglSwapBuffers
//  • Semua ImGui IO calls dari render thread — zero race condition
//  • Handle landscape rotation: swap X/Y jika needed
// ════════════════════════════════════════════════════════════════
#include <android/input.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "imgui.h"
#include "hook.h"

extern int g_width, g_height;

static int   g_touch_fd       = -1;
static float g_ax_min = 0, g_ax_rng = 1;
static float g_ay_min = 0, g_ay_rng = 1;
static bool  g_scale_init     = false;
static int   g_touch_log_cnt  = 0;

static int FindTouchFd() {
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
            LOGI("[ENI] touchscreen: %s (\'%s\')", path, name);
            closedir(d);
            return fd;
        }
        close(fd);
    }
    closedir(d);
    return -1;
}

static void InitTouchScale() {
    struct input_absinfo ax = {}, ay = {};
    ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_X), &ax);
    ioctl(g_touch_fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay);
    g_ax_min = (float)ax.minimum;
    g_ax_rng = (float)(ax.maximum - ax.minimum);
    g_ay_min = (float)ay.minimum;
    g_ay_rng = (float)(ay.maximum - ay.minimum);
    if (g_ax_rng <= 0) g_ax_rng = 1;
    if (g_ay_rng <= 0) g_ay_rng = 1;
    LOGI("[ENI] touch scale: X[%.0f..%.0f] Y[%.0f..%.0f] screen:%dx%d",
         g_ax_min, g_ax_min+g_ax_rng,
         g_ay_min, g_ay_min+g_ay_rng,
         g_width, g_height);
    g_scale_init = true;
}

static void InstallMotionHooks() {
    g_touch_fd = FindTouchFd();
    if (g_touch_fd < 0) {
        LOGE("[ENI] /dev/input: no touchscreen found!");
    } else {
        LOGI("[ENI] /dev/input: fd=%d OK", g_touch_fd);
    }
}

// ── Called every frame from hook_eglSwapBuffers — non-blocking ──
static inline void FlushTouchToImGui() {
    if (g_touch_fd < 0) return;

    if (!g_scale_init) InitTouchScale();

    struct input_event ev;
    static float  cx = 0.f, cy = 0.f;
    static bool   tracking = false;
    static int    slot = 0;

    // Drain all pending events in one shot
    while (read(g_touch_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_MT_SLOT) {
                slot = ev.value;
            }
            if (slot != 0) break; // only track first finger

            if (ev.code == ABS_MT_TRACKING_ID) {
                tracking = (ev.value != -1);
                if (!tracking) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                    io.AddMouseButtonEvent(0, false);
                    if (g_touch_log_cnt < 30)
                        LOGI("[ENI] touch UP (%.0f,%.0f)", cx, cy);
                }
            }

            // Raw → screen coords
            // If screen is landscape (width > height), swap X/Y axes
            if (ev.code == ABS_MT_POSITION_X) {
                float norm = (ev.value - g_ax_min) / g_ax_rng;
                // landscape: raw X → screen X (or Y if rotated)
                cx = norm * (float)(g_width > g_height ? g_width : g_height);
            }
            if (ev.code == ABS_MT_POSITION_Y) {
                float norm = (ev.value - g_ay_min) / g_ay_rng;
                cy = norm * (float)(g_width > g_height ? g_height : g_width);
            }
            break;

        case EV_SYN:
            if (ev.code == SYN_REPORT && slot == 0 && tracking) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
                io.AddMousePosEvent(cx, cy);
                io.AddMouseButtonEvent(0, true);

                if (g_touch_log_cnt < 30) {
                    LOGI("[ENI] touch DOWN (%.0f,%.0f) screen(%dx%d)",
                         cx, cy, g_width, g_height);
                    g_touch_log_cnt++;
                }
            }
            break;
        }
    }
}

// ── CustomAndroidNewFrame: no ANativeWindow needed ────────────────
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
