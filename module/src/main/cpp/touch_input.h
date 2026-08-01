#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT — AMotionEvent_getX/Y/Action hook (libandroid.so)
//
//  Why this works:
//  • libandroid.so always present, never moved to APEX
//  • Unity (and every Android game) calls AMotionEvent_get* for input
//  • Coordinates already in screen space — no scaling needed
//  • AddMouseSourceEvent(TouchScreen) required for ImGui to register taps
//  • Thread-safe: atomic staging, flushed from render thread
// ════════════════════════════════════════════════════════════════
#include <android/input.h>
#include <dlfcn.h>
#include <atomic>
#include "imgui.h"
#include "dobby.h"
#include "hook.h"

extern int g_width, g_height;

// ── Staging area (written by input thread, read by render thread) ─
static std::atomic<float> g_t_x{0.f};
static std::atomic<float> g_t_y{0.f};
static std::atomic<bool>  g_t_down{false};
static std::atomic<int>   g_t_gen{0};      // increments each event
static std::atomic<int>   g_t_flushed{0};  // last gen we flushed

// ── Original function pointers ────────────────────────────────────
static float    (*orig_getX)(const AInputEvent*, size_t) = nullptr;
static float    (*orig_getY)(const AInputEvent*, size_t) = nullptr;
static int32_t  (*orig_getAction)(const AInputEvent*)    = nullptr;

// ── Hooks ─────────────────────────────────────────────────────────
static float hook_getX(const AInputEvent* ev, size_t idx) {
    float x = orig_getX(ev, idx);
    if (idx == 0) g_t_x.store(x, std::memory_order_relaxed);
    return x;
}

static float hook_getY(const AInputEvent* ev, size_t idx) {
    float y = orig_getY(ev, idx);
    if (idx == 0) g_t_y.store(y, std::memory_order_relaxed);
    return y;
}

static int32_t hook_getAction(const AInputEvent* ev) {
    int32_t action = orig_getAction(ev);
    int masked = action & AMOTION_EVENT_ACTION_MASK;

    // Pull X/Y from the SAME event — eliminates race condition with hook_getX/Y
    if (orig_getX) g_t_x.store(orig_getX(ev, 0), std::memory_order_relaxed);
    if (orig_getY) g_t_y.store(orig_getY(ev, 0), std::memory_order_relaxed);

    bool down = (masked == AMOTION_EVENT_ACTION_DOWN ||
                 masked == AMOTION_EVENT_ACTION_MOVE);
    g_t_down.store(down, std::memory_order_relaxed);

    // Increment gen AFTER all data is written
    g_t_gen.fetch_add(1, std::memory_order_release);
    return action;
}

// ── Install hooks ─────────────────────────────────────────────────
static bool g_motion_hooked = false;

static void InstallMotionHooks() {
    void* lib = dlopen("libandroid.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!lib) lib = dlopen("libandroid.so", RTLD_LAZY);
    if (!lib) { LOGE("[ENI] libandroid.so not found!"); return; }

    struct { const char* name; void* hook; void** orig; } hooks[] = {
        {"AMotionEvent_getX",      (void*)hook_getX,      (void**)&orig_getX},
        {"AMotionEvent_getY",      (void*)hook_getY,      (void**)&orig_getY},
        {"AMotionEvent_getAction", (void*)hook_getAction, (void**)&orig_getAction},
    };

    int ok = 0;
    for (auto& h : hooks) {
        void* sym = dlsym(lib, h.name);
        if (!sym) { LOGE("[ENI] %s not found", h.name); continue; }
        int r = DobbyHook(sym, h.hook, h.orig);
        LOGI("[ENI] hook %s: %s", h.name, r==0?"OK":"FAIL");
        if (r == 0) ok++;
    }
    g_motion_hooked = (ok > 0);
    LOGI("[ENI] AMotionEvent hooks: %d/3 OK", ok);
}

// ── Flush pending touch to ImGui IO (call from render thread) ─────
static int g_flush_log_count = 0;

static inline void FlushTouchToImGui() {
    int gen = g_t_gen.load(std::memory_order_acquire);
    if (gen == g_t_flushed.load(std::memory_order_relaxed)) return;
    g_t_flushed.store(gen, std::memory_order_relaxed);

    float x    = g_t_x.load(std::memory_order_relaxed);
    float y    = g_t_y.load(std::memory_order_relaxed);
    bool down  = g_t_down.load(std::memory_order_relaxed);

    // Log first 20 touch events so we can verify coords
    if (g_flush_log_count < 20) {
        LOGI("[ENI] touch flush: (%.0f, %.0f) down=%d gen=%d", x, y, (int)down, gen);
        g_flush_log_count++;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(0, down);
}

// ── Custom NewFrame: sets DisplaySize + DeltaTime without ANativeWindow ──
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
