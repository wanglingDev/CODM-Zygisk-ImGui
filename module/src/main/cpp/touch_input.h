#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT — proven approach dari traitimtrongvag template:
//  1. Hook ANativeWindow_getWidth/Height → capture g_Window ptr
//  2. Hook inject_event / consume → forward ke ImGui IO
//  3. /dev/input/ fallback kalau semua hook gagal
// ════════════════════════════════════════════════════════════════
#include <EGL/egl.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <atomic>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <cstring>
#include <cstdio>
#include "imgui.h"
#include "backends/imgui_impl_android.h"
#include "dobby.h"
#include "hook.h"

// ── Global EGL surface dimensions ────────────────────────────────
extern int g_width, g_height;

// ── ANativeWindow pointer — captured via hook ─────────────────────
static ANativeWindow* g_Window = nullptr;

// ─────────────────────────────────────────────────────────────────
//  Layer 1: Hook ANativeWindow_getWidth/Height
//  These are in libandroid.so — always available, never moved to APEX.
//  Called by Unity every frame for display size queries.
//  Side effect: we capture the window pointer for ImGui_ImplAndroid_Init.
// ─────────────────────────────────────────────────────────────────
static int32_t (*orig_ANW_getWidth)(ANativeWindow*)  = nullptr;
static int32_t (*orig_ANW_getHeight)(ANativeWindow*) = nullptr;

static int32_t hook_ANW_getWidth(ANativeWindow* window) {
    if (window && !g_Window) {
        g_Window = window;
        LOGI("[ENI] ANativeWindow captured: %p", (void*)window);
    }
    return orig_ANW_getWidth ? orig_ANW_getWidth(window) : g_width;
}

static int32_t hook_ANW_getHeight(ANativeWindow* window) {
    if (window && !g_Window) g_Window = window;
    return orig_ANW_getHeight ? orig_ANW_getHeight(window) : g_height;
}

static void InstallWindowHooks() {
    void* libandroid = dlopen("libandroid.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!libandroid) libandroid = dlopen("libandroid.so", RTLD_LAZY);
    if (!libandroid) { LOGE("[ENI] libandroid.so not found"); return; }

    void* sym_w = dlsym(libandroid, "ANativeWindow_getWidth");
    void* sym_h = dlsym(libandroid, "ANativeWindow_getHeight");

    if (sym_w) {
        int r = DobbyHook(sym_w, (void*)hook_ANW_getWidth, (void**)&orig_ANW_getWidth);
        LOGI("[ENI] ANativeWindow_getWidth hook: %s", r==0?"OK":"FAIL");
    }
    if (sym_h) {
        int r = DobbyHook(sym_h, (void*)hook_ANW_getHeight, (void**)&orig_ANW_getHeight);
        LOGI("[ENI] ANativeWindow_getHeight hook: %s", r==0?"OK":"FAIL");
    }
}

// ─────────────────────────────────────────────────────────────────
//  Layer 2: /dev/input/ polling fallback
//  Used when libinput hooks fail (Android 16 APEX issue).
// ─────────────────────────────────────────────────────────────────
static std::atomic<bool> g_input_thread_running{false};

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
        char name[256] = {};
        ioctl(fd, EVIOCGNAME(sizeof(name)-1), name);
        uint8_t abs_bits[(ABS_MAX/8)+1] = {};
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        bool has_mt = (abs_bits[ABS_MT_POSITION_X/8] >> (ABS_MT_POSITION_X%8)) & 1;
        if (has_mt) {
            LOGI("[ENI] touchscreen: /dev/input/%s ('%s')", de->d_name, name);
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

static void* touch_poll_thread(void*) {
    int fd = FindTouchscreenFd();
    if (fd < 0) { LOGE("[ENI] touch poll: no device found"); return nullptr; }

    struct input_absinfo abs_x = {}, abs_y = {};
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x);
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y);
    float ax_min = abs_x.minimum, ax_rng = abs_x.maximum - abs_x.minimum;
    float ay_min = abs_y.minimum, ay_rng = abs_y.maximum - abs_y.minimum;
    if (ax_rng <= 0) ax_rng = 1; if (ay_rng <= 0) ay_rng = 1;

    struct input_event ev;
    float cx = 0, cy = 0;
    bool tracking = false;
    int slot = 0;

    while (g_input_thread_running.load()) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n < (ssize_t)sizeof(ev)) { usleep(1000); continue; }

        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_MT_SLOT)          slot = ev.value;
            if (ev.code == ABS_MT_TRACKING_ID && slot == 0) {
                tracking = (ev.value != -1);
                if (!tracking) {
                    ImGuiIO& io = ImGui::GetIO();
                    io.AddMouseButtonEvent(0, false);
                }
            }
            if (ev.code == ABS_MT_POSITION_X && slot == 0)
                cx = ((ev.value - ax_min) / ax_rng) * (float)g_width;
            if (ev.code == ABS_MT_POSITION_Y && slot == 0)
                cy = ((ev.value - ay_min) / ay_rng) * (float)g_height;
            break;
        case EV_SYN:
            if (ev.code == SYN_REPORT && slot == 0 && tracking) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(cx, cy);
                io.AddMouseButtonEvent(0, true);
            }
            break;
        }
    }
    close(fd);
    return nullptr;
}

static void StartTouchPollThread() {
    if (g_input_thread_running.exchange(true)) return;
    pthread_t tid;
    pthread_create(&tid, nullptr, touch_poll_thread, nullptr);
    pthread_detach(tid);
    LOGI("[ENI] /dev/input/ touch poll thread started (fallback)");
}

// ─────────────────────────────────────────────────────────────────
//  Try all known libinput paths
// ─────────────────────────────────────────────────────────────────
static const char* LIBINPUT_PATHS[] = {
    "/system/lib64/libinput.so",
    "/apex/com.android.tethering/lib64/libinput.so",
    "/apex/com.android.media/lib64/libinput.so",
    "/apex/com.android.art/lib64/libinput.so",
    nullptr
};
static void* TryOpenLibinput() {
    for (int i = 0; LIBINPUT_PATHS[i]; i++) {
        void* h = dlopen(LIBINPUT_PATHS[i], RTLD_LAZY);
        if (h) { LOGI("[ENI] libinput: %s", LIBINPUT_PATHS[i]); return h; }
    }
    return nullptr;
}
