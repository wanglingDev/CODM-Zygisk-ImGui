#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT v3 — AMotionEvent_getAction hook dari libandroid.so
//
//  Kenapa lebih baik dari libinput + /dev/input:
//    • libandroid.so ada di semua Android (tidak kena APEX shuffle)
//    • AMotionEvent_getX/Y sudah screen-space yang benar
//    • Tidak perlu kalibrasi abs_min/abs_max manual
//    • Satu hook saja, footprint minimal
//    • io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen)
//      selama ini miss → ImGui salah interpret button state
//
//  Flow:
//    InstallMotionHooks()  → hook AMotionEvent_getAction di libandroid.so
//    hook_AMotionEvent_getAction → observe setiap touch event Unity,
//                                  buffer {x, y, action} secara atomik
//    FlushTouchToImGui()   → dipanggil tiap frame dari hook_eglSwapBuffers,
//                            flush buffer → ImGui IO dengan source=TouchScreen
// ════════════════════════════════════════════════════════════════
#include <android/input.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include "imgui.h"
#include "dobby.h"
#include "hook.h"

// ── Global EGL surface dimensions (set in hook_eglSwapBuffers) ────
extern int g_width, g_height;

// ── ANativeWindow pointer — untuk ImGui_ImplAndroid_Init ──────────
static ANativeWindow* g_Window = nullptr;

// ════════════════════════════════════════════════════════════════
//  Layer 1: ANativeWindow_getWidth/Height hook
//  Dipanggil Unity setiap frame → kita capture window pointer.
// ════════════════════════════════════════════════════════════════
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
    if (!libandroid) { LOGE("[ENI] InstallWindowHooks: libandroid.so not found"); return; }

    void* sym_w = dlsym(libandroid, "ANativeWindow_getWidth");
    void* sym_h = dlsym(libandroid, "ANativeWindow_getHeight");
    if (sym_w) { int r = DobbyHook(sym_w,(void*)hook_ANW_getWidth,(void**)&orig_ANW_getWidth);
                 LOGI("[ENI] ANativeWindow_getWidth hook: %s", r==0?"OK":"FAIL"); }
    if (sym_h) { int r = DobbyHook(sym_h,(void*)hook_ANW_getHeight,(void**)&orig_ANW_getHeight);
                 LOGI("[ENI] ANativeWindow_getHeight hook: %s", r==0?"OK":"FAIL"); }
}

// ════════════════════════════════════════════════════════════════
//  Layer 2: AMotionEvent_getAction hook
//
//  Strategi:
//  • Simpan real_AMotionEvent_getX/Y via dlsym SEBELUM hook apapun
//    (pointer ke fungsi asli, bukan trampoline Dobby)
//  • Hook hanya AMotionEvent_getAction — satu hook, Unity memanggil
//    ini untuk SETIAP touch event yang diproses
//  • Di hook: baca x/y via real_* lalu simpan atomik ke g_lastTouch
//  • FlushTouchToImGui() per-frame membaca buffer lalu push ke ImGui IO
// ════════════════════════════════════════════════════════════════

struct TouchSample {
    float   x, y;
    int32_t action; // AMOTION_EVENT_ACTION_* masked
    bool    valid;
};

// Lock-free single-slot buffer — sufficient for 60/90 fps game
// (frame rate >> input event rate)
static std::atomic<uint64_t> g_touchPacked{0};  // packed: valid|action|y16|x16

static inline uint64_t PackTouch(float x, float y, int32_t action, bool valid) {
    uint16_t ix = (uint16_t)std::max(0.f, std::min(x, 65535.f));
    uint16_t iy = (uint16_t)std::max(0.f, std::min(y, 65535.f));
    return ((uint64_t)(valid ? 1 : 0) << 48) |
           ((uint64_t)(uint8_t)action  << 40) |
           ((uint64_t)iy               << 16) |
           (uint64_t)ix;
}
static inline TouchSample UnpackTouch(uint64_t v) {
    TouchSample s;
    s.valid  = (v >> 48) & 1;
    s.action = (int32_t)((v >> 40) & 0xFF);
    s.y      = (float)((v >> 16) & 0xFFFF);
    s.x      = (float)(v & 0xFFFF);
    return s;
}

// Pre-saved BEFORE any hooks — real libandroid.so function pointers
static float   (*real_AMotionEvent_getX)(const AInputEvent*, size_t) = nullptr;
static float   (*real_AMotionEvent_getY)(const AInputEvent*, size_t) = nullptr;
static int32_t (*orig_AMotionEvent_getAction)(const AInputEvent*)    = nullptr;

static int32_t hook_AMotionEvent_getAction(const AInputEvent* ev) {
    int32_t raw    = orig_AMotionEvent_getAction(ev);
    int32_t masked = raw & AMOTION_EVENT_ACTION_MASK;

    // Only care about primary pointer (pointer index 0)
    // Pointer index is encoded in the upper byte of action — index==0 means
    // masked action is exactly DOWN/MOVE/UP (not POINTER_DOWN/UP).
    if (masked == AMOTION_EVENT_ACTION_DOWN ||
        masked == AMOTION_EVENT_ACTION_MOVE ||
        masked == AMOTION_EVENT_ACTION_UP) {
        if (real_AMotionEvent_getX && real_AMotionEvent_getY) {
            float x = real_AMotionEvent_getX(ev, 0);
            float y = real_AMotionEvent_getY(ev, 0);
            g_touchPacked.store(PackTouch(x, y, masked, true),
                                std::memory_order_release);
        }
    }
    return raw;
}

// ── InstallMotionHooks — call once from hack_thread ───────────────
static void InstallMotionHooks() {
    void* libandroid = dlopen("libandroid.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!libandroid) libandroid = dlopen("libandroid.so", RTLD_LAZY);
    if (!libandroid) { LOGE("[ENI] InstallMotionHooks: libandroid.so not found"); return; }

    // Save real pointers BEFORE hooking anything in libandroid
    real_AMotionEvent_getX = (float(*)(const AInputEvent*,size_t))
                              dlsym(libandroid, "AMotionEvent_getX");
    real_AMotionEvent_getY = (float(*)(const AInputEvent*,size_t))
                              dlsym(libandroid, "AMotionEvent_getY");

    if (!real_AMotionEvent_getX || !real_AMotionEvent_getY) {
        LOGE("[ENI] InstallMotionHooks: AMotionEvent_getX/Y not found in libandroid.so");
        return;
    }
    LOGI("[ENI] real_AMotionEvent_getX=%p  real_AMotionEvent_getY=%p",
         (void*)real_AMotionEvent_getX, (void*)real_AMotionEvent_getY);

    void* sym_action = dlsym(libandroid, "AMotionEvent_getAction");
    if (!sym_action) { LOGE("[ENI] AMotionEvent_getAction not found"); return; }

    // Cross-instance guard: if Dobby already patched this address, skip
    uint32_t firstWord = *(volatile uint32_t*)sym_action;
    bool alreadyHooked = (firstWord == 0x580000D1) ||
                         ((firstWord & 0xFC000000) == 0x14000000);
    if (alreadyHooked) {
        LOGI("[ENI] AMotionEvent_getAction already hooked — skip");
        return;
    }

    int r = DobbyHook(sym_action,
                      (void*)hook_AMotionEvent_getAction,
                      (void**)&orig_AMotionEvent_getAction);
    LOGI("[ENI] AMotionEvent_getAction hook: %s (addr=%p)", r==0?"OK":"FAIL", sym_action);
}

// ════════════════════════════════════════════════════════════════
//  FlushTouchToImGui — dipanggil SETIAP frame dari hook_eglSwapBuffers
//  SEBELUM ImGui::NewFrame()
// ════════════════════════════════════════════════════════════════
static void FlushTouchToImGui() {
    uint64_t packed = g_touchPacked.exchange(0, std::memory_order_acquire);
    if (!packed) return;

    TouchSample s = UnpackTouch(packed);
    if (!s.valid) return;

    ImGuiIO& io = ImGui::GetIO();

    // ← THE MISSING PIECE: tell ImGui this is a touchscreen event
    // Without this, ImGui interprets touch as mouse and gets confused
    // about button state transitions (DOWN fires but UP is ignored, etc.)
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(s.x, s.y);

    if (s.action == AMOTION_EVENT_ACTION_DOWN ||
        s.action == AMOTION_EVENT_ACTION_MOVE) {
        io.AddMouseButtonEvent(0, true);
    } else { // ACTION_UP
        io.AddMouseButtonEvent(0, false);
    }
}

// ════════════════════════════════════════════════════════════════
//  CustomAndroidNewFrame — replaces ImGui_ImplAndroid_NewFrame()
//  Tidak butuh ANativeWindow — pakai g_width/g_height dari EGL.
//  Handles DisplaySize + DeltaTime saja.
// ════════════════════════════════════════════════════════════════
static void CustomAndroidNewFrame() {
    ImGuiIO& io = ImGui::GetIO();

    // Display size — updated every frame from eglQuerySurface
    io.DisplaySize             = ImVec2((float)g_width, (float)g_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    // DeltaTime
    static auto s_last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_last).count();
    io.DeltaTime = (dt > 0.f && dt < 1.f) ? dt : (1.f / 60.f);
    s_last = now;
}

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
