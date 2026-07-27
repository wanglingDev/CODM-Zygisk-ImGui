/**
 * input_hook.h — JNI RegisterNatives intercept for Unity touch input
 *
 * CORE FIX: use direct io.MousePos / io.MouseDown[0] assignment instead
 * of io.AddMousePosEvent() / io.AddMouseButtonEvent().
 *
 * The Add*() API feeds an input queue processed on ImGui::NewFrame().
 * Touch arrives on a different thread, so WantCaptureMouse reads the
 * PREVIOUS frame's value — always wrong. Direct assignment is visible
 * to ImGui on the same frame it's set, which is what we need here.
 *
 * References: imgui #6498, #3315, ozMod / c4-off community findings.
 *
 * Fixes applied:
 *  1. Direct io.MousePos / io.MouseDown[0] instead of Add*() queue
 *  2. ACTION_UP resets MousePos → (-FLT_MAX,-FLT_MAX) so WantCaptureMouse
 *     clears next frame (imgui #6627)
 *  3. Pointer index from ACTION_POINTER_DOWN/UP encoded in action byte
 *  4. TouchExtraPadding = {8,8} so small buttons are actually hittable
 *  5. ImGuiConfigFlags_NoMouseCursorChange on init (mobile sanity)
 */
#pragma once
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cfloat>
#include <mutex>
#include "imgui.h"

#define INP_TAG "zyCheats"
#define INP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  INP_TAG, __VA_ARGS__)
#define INP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, INP_TAG, __VA_ARGS__)

static constexpr int JNI_REGISTERNATIVES_SLOT = 215;

typedef jboolean (*NativeInjectFn)(JNIEnv*, jobject, jobject);
typedef jint     (*RegisterNativesFn)(JNIEnv*, jclass, const JNINativeMethod*, jint);

static NativeInjectFn    g_orig_nativeInjectEvent = nullptr;
static RegisterNativesFn g_orig_RegisterNatives   = nullptr;
static JavaVM*           g_jvm                    = nullptr;

// Protects direct IO writes from the input thread vs render thread reads.
// A spinlock is enough — the critical section is 3-4 assignments wide.
static std::mutex g_input_mtx;

// Call once after ImGui::CreateContext() to configure touch-friendly settings
static void SetupImGuiTouchConfig() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // Makes every button/checkbox 8px larger in hit-testing — critical on
    // high-density touchscreens where a "miss" is common without it.
    io.TouchExtraPadding = ImVec2(8.f, 8.f);
}

// ── nativeInjectEvent replacement ────────────────────────────────────────────
static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject /*thiz*/, jobject motionEvent) {
    if (!motionEvent || !ImGui::GetCurrentContext())
        goto passthrough;
    {
        jclass cls = env->FindClass("android/view/MotionEvent");
        if (!cls) goto passthrough;

        jmethodID mGetAction = env->GetMethodID(cls, "getAction", "()I");
        jmethodID mGetX      = env->GetMethodID(cls, "getX",      "(I)F");
        jmethodID mGetY      = env->GetMethodID(cls, "getY",      "(I)F");
        env->DeleteLocalRef(cls);
        if (!mGetAction || !mGetX || !mGetY) goto passthrough;

        jint  rawAction = env->CallIntMethod(motionEvent, mGetAction);
        int   act       = rawAction & 0xFF;
        // Pointer index is encoded in the upper byte for POINTER_DOWN/UP
        int   ptrIdx    = (rawAction >> 8) & 0xFF;

        jfloat x = env->CallFloatMethod(motionEvent, mGetX, (jint)ptrIdx);
        jfloat y = env->CallFloatMethod(motionEvent, mGetY, (jint)ptrIdx);

        // Direct IO assignment — visible this frame, not queued for next.
        {
            std::lock_guard<std::mutex> lk(g_input_mtx);
            ImGuiIO& io = ImGui::GetIO();

            switch (act) {
                case 0: // ACTION_DOWN
                case 5: // ACTION_POINTER_DOWN
                    io.MousePos     = ImVec2(x, y);
                    io.MouseDown[0] = true;
                    break;

                case 2: // ACTION_MOVE
                    io.MousePos = ImVec2(x, y);
                    break;

                case 1: // ACTION_UP
                case 6: // ACTION_POINTER_UP
                case 3: // ACTION_CANCEL
                    io.MouseDown[0] = false;
                    // Reset position so WantCaptureMouse clears next frame.
                    // Without this, ImGui keeps hover-state over the window
                    // and WantCaptureMouse stays true — eating all game touches.
                    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
                    break;

                default:
                    break;
            }

            // Block the event from reaching Unity only when ImGui owns it
            if (io.WantCaptureMouse)
                return JNI_TRUE;
        }
    }

passthrough:
    if (g_orig_nativeInjectEvent)
        return g_orig_nativeInjectEvent(env, /*thiz=*/nullptr, motionEvent);
    return JNI_FALSE;
}

// ── RegisterNatives hook ──────────────────────────────────────────────────────
static jint hook_RegisterNatives(JNIEnv* env, jclass clazz,
                                  const JNINativeMethod* methods, jint n) {
    for (jint i = 0; i < n; i++) {
        if (methods[i].name && strcmp(methods[i].name, "nativeInjectEvent") == 0) {
            INP_LOGI("[ENI] RegisterNatives: intercepted nativeInjectEvent");
            g_orig_nativeInjectEvent =
                reinterpret_cast<NativeInjectFn>(methods[i].fnPtr);
            const_cast<JNINativeMethod*>(methods)[i].fnPtr =
                reinterpret_cast<void*>(hook_nativeInjectEvent);
        }
    }
    return g_orig_RegisterNatives(env, clazz, methods, n);
}

// ── Install ───────────────────────────────────────────────────────────────────
static bool InstallInputHook(JavaVM* jvm) {
    if (!jvm) { INP_LOGE("[ENI] null JavaVM"); return false; }
    g_jvm = jvm;

    JNIEnv* env = nullptr;
    if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
        if (jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            INP_LOGE("[ENI] can't get JNIEnv"); return false;
        }

    void** table = reinterpret_cast<void**>(
        const_cast<JNINativeInterface*>(env->functions));

    uintptr_t addr = reinterpret_cast<uintptr_t>(&table[JNI_REGISTERNATIVES_SLOT]);
    uintptr_t page = addr & ~static_cast<uintptr_t>(getpagesize() - 1);

    if (mprotect(reinterpret_cast<void*>(page), getpagesize(),
                 PROT_READ | PROT_WRITE) != 0) {
        INP_LOGE("[ENI] mprotect failed"); return false;
    }
    g_orig_RegisterNatives =
        reinterpret_cast<RegisterNativesFn>(table[JNI_REGISTERNATIVES_SLOT]);
    table[JNI_REGISTERNATIVES_SLOT] =
        reinterpret_cast<void*>(hook_RegisterNatives);
    mprotect(reinterpret_cast<void*>(page), getpagesize(), PROT_READ);

    INP_LOGI("[ENI] JNI table patched — waiting for nativeInjectEvent");
    return true;
}
