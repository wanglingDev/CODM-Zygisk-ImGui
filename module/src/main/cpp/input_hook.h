/**
 * input_hook.h — JNI RegisterNatives intercept for Unity touch input
 *
 * Strategy: hook env->functions->RegisterNatives at the JNI function-table
 * level. When Unity calls RegisterNatives("nativeInjectEvent"), we replace
 * the fnPtr with our own. Then in our hook we read X/Y/action from the Java
 * MotionEvent object and feed them to ImGui.
 *
 * FIXES applied vs original:
 *  1. ACTION_UP/CANCEL: reset MousePos to (-FLT_MAX,-FLT_MAX) after button-up
 *     so ImGui stops claiming WantCaptureMouse on the next frame (imgui #6627).
 *  2. ACTION_POINTER_DOWN/UP: use getX(pointerIndex)/getY(pointerIndex)
 *     instead of getX(0)/getY(0) — fixes wrong coords on multi-touch.
 *  3. Window-bounds guard: only forward touch to ImGui if the touch position
 *     falls inside the ImGui window rect, OR if ImGui already has mouse down.
 *     This prevents any game touch outside the menu from being silently eaten.
 */
#pragma once
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <cfloat>
#include "imgui.h"
#include "imgui_internal.h"   // ImGui::FindWindowByName, ImGuiWindow

#define INP_TAG "zyCheats"
#define INP_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  INP_TAG, __VA_ARGS__)
#define INP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, INP_TAG, __VA_ARGS__)

// JNI spec: RegisterNatives is slot 215 in the function table
static constexpr int JNI_REGISTERNATIVES_SLOT = 215;

typedef jboolean (*NativeInjectFn)(JNIEnv*, jobject, jobject);
typedef jint     (*RegisterNativesFn)(JNIEnv*, jclass, const JNINativeMethod*, jint);

static NativeInjectFn    g_orig_nativeInjectEvent = nullptr;
static RegisterNativesFn g_orig_RegisterNatives   = nullptr;
static JavaVM*           g_jvm                    = nullptr;

// ── Helper: is touch point (x,y) inside ANY visible ImGui window? ─────────────
// Only touches that land on a window get forwarded to ImGui.
// Everything else goes straight to the game — no more phantom captures.
static bool TouchHitsImGuiWindow(float x, float y) {
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return false;
    // Walk the window display list (front-to-back)
    for (int i = ctx->Windows.Size - 1; i >= 0; i--) {
        ImGuiWindow* w = ctx->Windows[i];
        if (!w || (w->Flags & ImGuiWindowFlags_NoMouseInputs)) continue;
        if (w->Hidden || w->Collapsed) continue;
        // Rect.Min / Rect.Max are in screen coords
        if (x >= w->Rect().Min.x && x <= w->Rect().Max.x &&
            y >= w->Rect().Min.y && y <= w->Rect().Max.y)
            return true;
    }
    return false;
}

// ── Our nativeInjectEvent replacement ────────────────────────────────────────
static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject thiz, jobject motionEvent) {
    if (!motionEvent || !ImGui::GetCurrentContext())
        goto passthrough;

    {
        ImGuiIO& io = ImGui::GetIO();

        jclass cls = env->FindClass("android/view/MotionEvent");
        if (!cls) goto passthrough;

        jmethodID mGetAction       = env->GetMethodID(cls, "getAction",       "()I");
        jmethodID mGetX            = env->GetMethodID(cls, "getX",            "(I)F");
        jmethodID mGetY            = env->GetMethodID(cls, "getY",            "(I)F");
        jmethodID mGetPointerCount = env->GetMethodID(cls, "getPointerCount", "()I");
        env->DeleteLocalRef(cls);

        if (!mGetAction || !mGetX || !mGetY) goto passthrough;

        jint  rawAction = env->CallIntMethod(motionEvent, mGetAction);
        int   act       = rawAction & 0xFF;
        // Pointer index encoded in upper byte for POINTER_DOWN/UP
        int   ptrIdx    = (rawAction >> 8) & 0xFF;

        jfloat x = env->CallFloatMethod(motionEvent, mGetX, (jint)ptrIdx);
        jfloat y = env->CallFloatMethod(motionEvent, mGetY, (jint)ptrIdx);

        // Decide whether ImGui should see this touch at all.
        // If mouse button is already held (drag), keep sending to ImGui.
        // Otherwise, only forward if the touch lands inside a window.
        bool imguiHeld   = io.MouseDown[0];
        bool hitsWindow  = TouchHitsImGuiWindow(x, y);
        bool sendToImGui = imguiHeld || hitsWindow;

        if (sendToImGui) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
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
                    // FIX #1: reset MousePos so ImGui stops claiming
                    // WantCaptureMouse=true on the next frame (imgui #6627).
                    // Without this, the next game-touch was eaten by ImGui
                    // because it still thought the cursor was over the window.
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    break;

                default:
                    break; // ACTION_MOVE — pos already updated above
            }

            // Block game from receiving this touch only while ImGui wants it
            if (io.WantCaptureMouse)
                return JNI_TRUE;
        }
    }

passthrough:
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
