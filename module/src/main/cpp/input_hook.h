/**
 * input_hook.h — JNI RegisterNatives intercept for Unity touch input
 *
 * Strategy: hook env->functions->RegisterNatives at the JNI function-table
 * level. When Unity calls RegisterNatives("nativeInjectEvent"), we replace
 * the fnPtr with our own. Then in our hook we read X/Y/action from the Java
 * MotionEvent object and feed them to ImGui.
 *
 * This works on all Android versions (no APEX path hunting needed) because
 * the JNI function table is a plain struct pointer shared by every JNIEnv in
 * the process.
 */
#pragma once
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include "imgui.h"

#define INP_TAG "zyCheats"
#define INP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  INP_TAG, __VA_ARGS__)
#define INP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, INP_TAG, __VA_ARGS__)

// JNI spec: RegisterNatives is slot 215 in the function table
// (https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/functions.html)
static constexpr int JNI_REGISTERNATIVES_SLOT = 215;

// Saved originals
typedef jboolean (*NativeInjectFn)(JNIEnv*, jobject, jobject);
typedef jint     (*RegisterNativesFn)(JNIEnv*, jclass, const JNINativeMethod*, jint);

static NativeInjectFn    g_orig_nativeInjectEvent = nullptr;
static RegisterNativesFn g_orig_RegisterNatives   = nullptr;
static JavaVM*           g_jvm                    = nullptr;

// ── Atomic staging (JNI thread → render thread) ──────────────────────────────
#include <atomic>
static std::atomic<float> g_t_x{0.f};
static std::atomic<float> g_t_y{0.f};
static std::atomic<int>   g_t_action{-1};
static std::atomic<int>   g_t_gen{0};
static std::atomic<int>   g_t_flushed{0};

// ── Our nativeInjectEvent replacement ────────────────────────────────────────
static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject thiz, jobject motionEvent) {
    if (motionEvent) {
        jclass cls = env->FindClass("android/view/MotionEvent");
        if (cls) {
            jmethodID mGetAction = env->GetMethodID(cls, "getAction", "()I");
            jmethodID mGetX      = env->GetMethodID(cls, "getX",      "()F");
            jmethodID mGetY      = env->GetMethodID(cls, "getY",      "()F");

            jint   action = env->CallIntMethod  (motionEvent, mGetAction);
            jfloat x      = env->CallFloatMethod(motionEvent, mGetX);
            jfloat y      = env->CallFloatMethod(motionEvent, mGetY);
            env->DeleteLocalRef(cls);

            int act = action & 0xFF;
            g_t_x.store(x,   std::memory_order_relaxed);
            g_t_y.store(y,   std::memory_order_relaxed);
            g_t_action.store(act, std::memory_order_relaxed);
            g_t_gen.fetch_add(1,  std::memory_order_release);

            static int log_cnt = 0;
            if (log_cnt < 20) {
                INP_LOGI("[ENI] nativeInjectEvent: act=%d (%.0f,%.0f)", act, x, y);
                log_cnt++;
            }

            // Consume event when ImGui wants it (menu visible + click on menu)
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) {
                return JNI_TRUE;
            }
        }
    }
    if (g_orig_nativeInjectEvent)
        return g_orig_nativeInjectEvent(env, thiz, motionEvent);
    return JNI_FALSE;
}

// ── FlushTouchToImGui: called from render thread (hook_eglSwapBuffers) ───────
inline void FlushTouchFromJNI() {
    int gen = g_t_gen.load(std::memory_order_acquire);
    if (gen == g_t_flushed.load(std::memory_order_relaxed)) return;
    g_t_flushed.store(gen, std::memory_order_relaxed);

    float x   = g_t_x.load(std::memory_order_relaxed);
    float y   = g_t_y.load(std::memory_order_relaxed);
    int   act = g_t_action.load(std::memory_order_relaxed);

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(x, y);
    bool down = (act == 0 || act == 2 || act == 5); // DOWN/MOVE/POINTER_DOWN
    io.AddMouseButtonEvent(0, down);
}

// ── RegisterNatives hook: intercepts Unity's nativeInjectEvent registration ──
static jint hook_RegisterNatives(JNIEnv* env, jclass clazz,
                                  const JNINativeMethod* methods, jint n) {
    for (jint i = 0; i < n; i++) {
        if (methods[i].name && strcmp(methods[i].name, "nativeInjectEvent") == 0) {
            INP_LOGI("[ENI] RegisterNatives: intercepted nativeInjectEvent!");
            g_orig_nativeInjectEvent =
                reinterpret_cast<NativeInjectFn>(methods[i].fnPtr);
            // Replace the function pointer in place before Unity registers it
            const_cast<JNINativeMethod*>(methods)[i].fnPtr =
                reinterpret_cast<void*>(hook_nativeInjectEvent);
        }
    }
    return g_orig_RegisterNatives(env, clazz, methods, n);
}

// ── Install: patch the JNI function table ─────────────────────────────────────
static bool InstallInputHook(JavaVM* jvm) {
    if (!jvm) { INP_LOGE("[ENI] InstallInputHook: null JavaVM"); return false; }
    g_jvm = jvm;

    JNIEnv* env = nullptr;
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        if (jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            INP_LOGE("[ENI] InstallInputHook: can't get JNIEnv"); return false;
        }
    }

    // env->functions is const JNINativeInterface* — strip const first, then reinterpret.
    // Clang rejects reinterpret_cast<const void**> because it discards qualifiers;
    // the correct sequence is const_cast first, reinterpret_cast second.
    void** table = reinterpret_cast<void**>(
        const_cast<JNINativeInterface*>(env->functions));

    uintptr_t slot_addr = reinterpret_cast<uintptr_t>(&table[JNI_REGISTERNATIVES_SLOT]);
    uintptr_t page      = slot_addr & ~static_cast<uintptr_t>(getpagesize() - 1);

    if (mprotect(reinterpret_cast<void*>(page), getpagesize(),
                 PROT_READ | PROT_WRITE) != 0) {
        INP_LOGE("[ENI] InstallInputHook: mprotect failed"); return false;
    }

    g_orig_RegisterNatives =
        reinterpret_cast<RegisterNativesFn>(table[JNI_REGISTERNATIVES_SLOT]);
    table[JNI_REGISTERNATIVES_SLOT] =
        reinterpret_cast<void*>(hook_RegisterNatives);

    mprotect(reinterpret_cast<void*>(page), getpagesize(), PROT_READ);

    INP_LOGI("[ENI] JNI RegisterNatives slot patched — waiting for nativeInjectEvent");
    return true;
}
