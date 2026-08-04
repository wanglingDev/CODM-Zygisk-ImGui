#pragma once
// ════════════════════════════════════════════════════════════════
//  TOUCH INPUT via Zygisk hookJniNativeMethods
//
//  Proven approach used by ozMod, springmusk026, all working templates:
//  Hook UnityPlayer.nativeInjectEvent → extract MotionEvent X/Y → ImGui
//  Ref: github.com/ocornut/imgui/issues/6498
// ════════════════════════════════════════════════════════════════
#include <jni.h>
#include <atomic>
#include <cstring>
#include <ctime>
#include "imgui.h"
#include "hook.h"

extern int g_width, g_height;

// ── Staging: JNI thread → render thread ──────────────────────────
static std::atomic<float> g_t_x{0.f};
static std::atomic<float> g_t_y{0.f};
static std::atomic<int>   g_t_action{-1};  // 0=DOWN,1=UP,2=MOVE
static std::atomic<int>   g_t_gen{0};
static std::atomic<int>   g_t_flushed{0};

// ── nativeInjectEvent hook ────────────────────────────────────────
// Unity calls this for every touch/key event
static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject /*obj*/, jobject inputEvent) {
    if (!inputEvent || !env) return JNI_FALSE;

    jclass motionClass = env->FindClass("android/view/MotionEvent");
    if (!motionClass || !env->IsInstanceOf(inputEvent, motionClass)) {
        if (motionClass) env->DeleteLocalRef(motionClass);
        return JNI_FALSE;
    }

    jmethodID getAction = env->GetMethodID(motionClass, "getAction",       "()I");
    jmethodID getX      = env->GetMethodID(motionClass, "getX",            "()F");
    jmethodID getY      = env->GetMethodID(motionClass, "getY",            "()F");

    if (!getAction || !getX || !getY) {
        env->DeleteLocalRef(motionClass);
        return JNI_FALSE;
    }

    jint  action = env->CallIntMethod(inputEvent, getAction);
    jfloat x     = env->CallFloatMethod(inputEvent, getX);
    jfloat y     = env->CallFloatMethod(inputEvent, getY);
    env->DeleteLocalRef(motionClass);

    int masked = action & 0xFF; // AMOTION_EVENT_ACTION_MASK

    g_t_x.store(x,      std::memory_order_relaxed);
    g_t_y.store(y,      std::memory_order_relaxed);
    g_t_action.store(masked, std::memory_order_relaxed);
    g_t_gen.fetch_add(1, std::memory_order_release);

    static int log_cnt = 0;
    if (log_cnt < 20) {
        LOGI("[ENI] nativeInjectEvent: action=%d (%.0f,%.0f)", masked, (float)x, (float)y);
        log_cnt++;
    }

    return JNI_FALSE; // return false = don't consume (game still gets event)
}

// InstallNativeInjectHook is defined in main.cpp (needs zygisk::Api scope)
// InstallMotionHooks is a no-op here — hook done via Zygisk in postAppSpecialize
static void InstallMotionHooks(int /*sock*/) {
    LOGI("[ENI] touch: using Zygisk nativeInjectEvent hook");
}

// ── Flush from render thread (hook_eglSwapBuffers) ────────────────
static inline void FlushTouchToImGui() {
    int gen = g_t_gen.load(std::memory_order_acquire);
    if (gen == g_t_flushed.load(std::memory_order_relaxed)) return;
    g_t_flushed.store(gen, std::memory_order_relaxed);

    float  x      = g_t_x.load(std::memory_order_relaxed);
    float  y      = g_t_y.load(std::memory_order_relaxed);
    int    action = g_t_action.load(std::memory_order_relaxed);

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(x, y);

    // 0=DOWN, 2=MOVE → pressed; 1=UP → released
    bool down = (action == 0 || action == 2);
    io.AddMouseButtonEvent(0, down);
}

// ── CustomAndroidNewFrame: no ANativeWindow needed ─────────────────
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
