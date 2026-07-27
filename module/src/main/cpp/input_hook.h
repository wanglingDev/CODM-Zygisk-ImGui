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

// ── Our nativeInjectEvent replacement ────────────────────────────────────────
static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject thiz, jobject motionEvent) {
    if (motionEvent && ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();

        jclass cls = env->FindClass("android/view/MotionEvent");
        if (cls) {
            jmethodID mGetAction = env->GetMethodID(cls, "getAction", "()I");
            jmethodID mGetX      = env->GetMethodID(cls, "getX",      "()F");
            jmethodID mGetY      = env->GetMethodID(cls, "getY",      "()F");

            jint  action = env->CallIntMethod  (motionEvent, mGetAction);
            jfloat x    = env->CallFloatMethod (motionEvent, mGetX);
            jfloat y    = env->CallFloatMethod (motionEvent, mGetY);
            env->DeleteLocalRef(cls);

            int act = action & 0xFF; // lower byte = action code
            io.AddMousePosEvent(x, y);

            switch (act) {
                case 0: // ACTION_DOWN
                case 5: // ACTION_POINTER_DOWN
                    io.AddMouseButtonEvent(0, true);
                    break;
                case 1: // ACTION_UP
                case 6: // ACTION_POINTER_UP
                case 3: // ACTION_CANCEL
                    io.AddMouseButtonEvent(0, false);
                    break;
                default: break;
            }

            // Block the game from receiving this touch if ImGui wants it
            if (io.WantCaptureMouse) {
                return JNI_TRUE;
            }
        }
    }
    if (g_orig_nativeInjectEvent)
        return g_orig_nativeInjectEvent(env, thiz, motionEvent);
    return JNI_FALSE;
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

    // The JNI function table is a const struct — we need to make the page
    // writable to patch the RegisterNatives slot.
    void** table = const_cast<void**>(
        reinterpret_cast<const void**>(env->functions));

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
