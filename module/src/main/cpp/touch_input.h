/**
 * touch_input.h — Unity nativeInjectEvent hook (confirmed working method)
 *
 * Why AMotionEvent_getAction from libandroid.so never worked:
 *   Unity on Android processes touch via Java android.view.MotionEvent,
 *   dispatched through com.unity3d.player.UnityPlayer.onTouchEvent() →
 *   nativeInjectEvent(). The NDK AInputEvent C API (AMotionEvent_getAction etc.)
 *   is NOT called by Unity — only NativeActivity games use it.
 *
 * Correct approach (confirmed from Unity crash traces + working mods):
 *   Hook Java_com_unity3d_player_UnityPlayer_nativeInjectEvent__Landroid_view_InputEvent_2
 *   directly in libunity.so using DobbySymbolResolver. Every touch Unity receives
 *   passes through this JNI bridge exactly once. Read X/Y/action via JNI MotionEvent
 *   methods, buffer atomically, flush to ImGui IO each frame from eglSwapBuffers.
 */
#pragma once
#include <jni.h>
#include <android/input.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include "imgui.h"
#include "dobby.h"
#include "hook.h"

extern int g_width, g_height;

// ── Lock-free single-slot touch buffer ───────────────────────────────────────
// Packs: valid(1) | down(1) | y(15) | x(15) = 32 bits
// 15-bit coords → max 32767 px, enough for any phone screen
static std::atomic<uint32_t> g_touchBuf{0};

static inline uint32_t PackTouch(float x, float y, bool down, bool valid) {
    uint16_t ix = (uint16_t)std::max(0.f, std::min(x, 32767.f));
    uint16_t iy = (uint16_t)std::max(0.f, std::min(y, 32767.f));
    return ((uint32_t)(valid?1:0) << 31) |
           ((uint32_t)(down ?1:0) << 30) |
           ((uint32_t)(iy & 0x7FFF) << 15) |
           (uint32_t)(ix & 0x7FFF);
}
struct TS { float x,y; bool down,valid; };
static inline TS UnpackTouch(uint32_t v) {
    return { (float)(v & 0x7FFF), (float)((v>>15)&0x7FFF),
             (bool)((v>>30)&1),   (bool)((v>>31)&1) };
}

// ── Cached JNI method IDs (set once on first call) ───────────────────────────
static jmethodID g_mid_getAction = nullptr;
static jmethodID g_mid_getX      = nullptr;
static jmethodID g_mid_getY      = nullptr;
static jclass    g_motionCls     = nullptr;

static void CacheMotionMethods(JNIEnv* env) {
    if (g_mid_getAction) return;
    jclass cls = env->FindClass("android/view/MotionEvent");
    if (!cls) return;
    g_motionCls     = (jclass)env->NewGlobalRef(cls);
    g_mid_getAction = env->GetMethodID(g_motionCls, "getAction", "()I");
    g_mid_getX      = env->GetMethodID(g_motionCls, "getX",      "()F");
    g_mid_getY      = env->GetMethodID(g_motionCls, "getY",      "()F");
    env->DeleteLocalRef(cls);
    LOGI("[ENI] MotionEvent JNI methods cached");
}

// ── nativeInjectEvent hook ────────────────────────────────────────────────────
typedef jboolean (*NativeInjectFn)(JNIEnv*, jobject, jobject);
static NativeInjectFn orig_nativeInjectEvent = nullptr;

static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject thiz, jobject inputEvent) {
    if (inputEvent && ImGui::GetCurrentContext()) {
        CacheMotionMethods(env);

        if (g_motionCls && g_mid_getAction &&
            env->IsInstanceOf(inputEvent, g_motionCls)) {

            jint  action = env->CallIntMethod(inputEvent, g_mid_getAction);
            jfloat x     = env->CallFloatMethod(inputEvent, g_mid_getX);
            jfloat y     = env->CallFloatMethod(inputEvent, g_mid_getY);
            int    act   = action & 0xFF;

            bool down = (act == AMOTION_EVENT_ACTION_DOWN ||
                         act == AMOTION_EVENT_ACTION_MOVE ||
                         act == 5); // POINTER_DOWN

            // Buffer the event (render thread reads it via FlushTouchToImGui)
            g_touchBuf.store(PackTouch(x, y, down, true),
                             std::memory_order_release);

            // If ImGui is consuming touch, don't pass to game
            if (ImGui::GetIO().WantCaptureMouse) {
                return JNI_TRUE;
            }
        }
    }
    return orig_nativeInjectEvent ? orig_nativeInjectEvent(env, thiz, inputEvent)
                                  : JNI_FALSE;
}

// ── InstallMotionHooks — call once from hack_thread ───────────────────────────
static void InstallMotionHooks() {
    // Confirmed symbol names from Unity crash traces (JNI mangled + short form)
    static const char* SYMBOLS[] = {
        "Java_com_unity3d_player_UnityPlayer_nativeInjectEvent__Landroid_view_InputEvent_2",
        "Java_com_unity3d_player_UnityPlayer_nativeInjectEvent",
        "Java_com_unity3d_player_ReflectionHelper_nativeInjectEvent__Landroid_view_InputEvent_2",
        nullptr
    };

    void* sym = nullptr;

    // Try DobbySymbolResolver first (searches .dynsym)
    for (int i = 0; SYMBOLS[i] && !sym; i++) {
        sym = DobbySymbolResolver("libunity.so", SYMBOLS[i]);
        if (sym) LOGI("[ENI] Found nativeInjectEvent via DobbySymbolResolver[%d]", i);
    }

    // Fallback: dlsym (requires library to be open in this namespace)
    if (!sym) {
        void* unity = dlopen("libunity.so", RTLD_LAZY | RTLD_NOLOAD);
        if (unity) {
            for (int i = 0; SYMBOLS[i] && !sym; i++) {
                sym = dlsym(unity, SYMBOLS[i]);
                if (sym) LOGI("[ENI] Found nativeInjectEvent via dlsym[%d]", i);
            }
        }
    }

    if (!sym) {
        LOGE("[ENI] nativeInjectEvent NOT FOUND in libunity.so — touch will not work");
        return;
    }

    int r = DobbyHook(sym, (void*)hook_nativeInjectEvent,
                      (void**)&orig_nativeInjectEvent);
    LOGI("[ENI] nativeInjectEvent hook: %s @ %p", r==0?"OK":"FAIL", sym);
}

// ── FlushTouchToImGui — call every frame from hook_eglSwapBuffers ─────────────
// BEFORE ImGui::NewFrame()
static void FlushTouchToImGui() {
    uint32_t packed = g_touchBuf.exchange(0, std::memory_order_acquire);
    if (!packed) return;

    TS s = UnpackTouch(packed);
    if (!s.valid) return;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(s.x, s.y);
    io.AddMouseButtonEvent(0, s.down);
}

// ── CustomAndroidNewFrame — replaces ImGui_ImplAndroid_NewFrame() ─────────────
static void CustomAndroidNewFrame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float)g_width, (float)g_height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    static auto s_last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_last).count();
    io.DeltaTime = (dt > 0.f && dt < 1.f) ? dt : (1.f / 60.f);
    s_last = now;
}
