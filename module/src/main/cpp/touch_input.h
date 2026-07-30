#pragma once
// g_width / g_height defined in hook.cpp
#include <EGL/egl.h>
extern EGLint g_width, g_height;
//  TOUCH INPUT — multi-layer approach:
//  Layer 1: libinput.so hook (best, calls origInput first)
//  Layer 2: /dev/input/eventX polling thread (fallback)
//  Both feed into ImGui IO atomically.
// ════════════════════════════════════════════════════════════════
#include <atomic>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/input.h>
#include <cstring>
#include <cstdio>
#include "imgui.h"
#include "hook.h"

// ── Shared touch state (written by input thread, read by render thread) ──
static std::atomic<float>  g_touch_x{0.f};
static std::atomic<float>  g_touch_y{0.f};
static std::atomic<bool>   g_touch_down{false};
static std::atomic<bool>   g_touch_dirty{false};
static std::atomic<bool>   g_input_thread_running{false};

// ── Called every frame from hook_eglSwapBuffers to flush pending touch ──
static inline void FlushTouchToImGui() {
    if (!g_touch_dirty.load()) return;
    g_touch_dirty.store(false);

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(g_touch_x.load(), g_touch_y.load());
    io.AddMouseButtonEvent(0, g_touch_down.load());
}

// ── /dev/input scanner: find touchscreen device ──────────────────
static int FindTouchscreenFd() {
    DIR* d = opendir("/dev/input");
    if (!d) return -1;

    struct dirent* de;
    while ((de = readdir(d)) != nullptr) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        // Query device name
        char name[256] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name)-1), name);
        LOGI("[ENI] touch scan: %s → '%s'", path, name);

        // Check for ABS_MT_POSITION_X capability (multitouch)
        uint8_t abs_bits[(ABS_MAX/8)+1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        bool has_mt_x = (abs_bits[ABS_MT_POSITION_X/8] >> (ABS_MT_POSITION_X%8)) & 1;
        if (has_mt_x) {
            LOGI("[ENI] touch: using /dev/input/%s ('%s')", de->d_name, name);
            // Reopen blocking for actual reading
            close(fd);
            fd = open(path, O_RDONLY);
            closedir(d);
            return fd;
        }
        close(fd);
    }
    closedir(d);
    return -1;
}

// ── /dev/input polling thread ─────────────────────────────────────
static void* touch_poll_thread(void*) {
    int fd = FindTouchscreenFd();
    if (fd < 0) {
        LOGE("[ENI] touch poll: no touchscreen found");
        return nullptr;
    }

    // Get axis info for coordinate scaling
    struct input_absinfo abs_x_info = {}, abs_y_info = {};
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x_info);
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y_info);
    LOGI("[ENI] touch: x[%d..%d] y[%d..%d]",
         abs_x_info.minimum, abs_x_info.maximum,
         abs_y_info.minimum, abs_y_info.maximum);

    float ax_min = abs_x_info.minimum, ax_max = abs_x_info.maximum;
    float ay_min = abs_y_info.minimum, ay_max = abs_y_info.maximum;
    if (ax_max <= 0) ax_max = 1;
    if (ay_max <= 0) ay_max = 1;

    struct input_event ev;
    float cx = 0, cy = 0;
    bool tracking = false;
    int slot = 0; // only track slot 0

    while (g_input_thread_running.load()) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) {
            usleep(1000);
            continue;
        }

        switch (ev.type) {
        case EV_ABS:
            switch (ev.code) {
            case ABS_MT_SLOT:
                slot = ev.value;
                break;
            case ABS_MT_TRACKING_ID:
                if (slot == 0) {
                    tracking = (ev.value != -1);
                    if (!tracking) {
                        g_touch_down.store(false);
                        g_touch_dirty.store(true);
                    }
                }
                break;
            case ABS_MT_POSITION_X:
                if (slot == 0) {
                    // Scale raw → screen pixels
                    float pct = (ev.value - ax_min) / (ax_max - ax_min);
                    cx = pct * (float)g_width;
                }
                break;
            case ABS_MT_POSITION_Y:
                if (slot == 0) {
                    float pct = (ev.value - ay_min) / (ay_max - ay_min);
                    cy = pct * (float)g_height;
                }
                break;
            }
            break;
        case EV_SYN:
            if (ev.code == SYN_REPORT && slot == 0 && tracking) {
                g_touch_x.store(cx);
                g_touch_y.store(cy);
                g_touch_down.store(true);
                g_touch_dirty.store(true);
            }
            break;
        }
    }
    close(fd);
    return nullptr;
}

// ── Start fallback polling thread ─────────────────────────────────
static void StartTouchPollThread() {
    if (g_input_thread_running.exchange(true)) return;
    pthread_t tid;
    pthread_create(&tid, nullptr, touch_poll_thread, nullptr);
    pthread_detach(tid);
    LOGI("[ENI] touch poll thread started");
}

// ── ImGui_ImplAndroid_NewFrame replacement (no ANativeWindow) ────
static inline void CustomAndroidNewFrame(EGLint w, EGLint h) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static double g_prev_time = 0.0;
    double now = (double)ts.tv_sec + ts.tv_nsec / 1e9;
    io.DeltaTime = g_prev_time > 0.0 ? (float)(now - g_prev_time) : 1.0f/60.0f;
    if (io.DeltaTime <= 0.f) io.DeltaTime = 1.0f/60.0f;
    g_prev_time = now;
}

// ── Try all known libinput paths on Android 12-16 ─────────────────
static const char* LIBINPUT_PATHS[] = {
    "/system/lib64/libinput.so",
    "/apex/com.android.tethering/lib64/libinput.so",
    "/apex/com.android.media/lib64/libinput.so",
    "/system/lib64/vndk-sp/libinput.so",
    nullptr
};

static void* TryOpenLibinput() {
    for (int i = 0; LIBINPUT_PATHS[i]; i++) {
        void* h = dlopen(LIBINPUT_PATHS[i], RTLD_LAZY);
        if (h) {
            LOGI("[ENI] libinput found at: %s", LIBINPUT_PATHS[i]);
            return h;
        }
    }
    return nullptr;
}
