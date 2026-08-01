#pragma once
/**
 * touch_input.h — nativeInjectEvent via DobbySymbolResolver on libunity.so
 *
 * Previous approaches that failed:
 *  - AMotionEvent_getAction (libandroid.so): Unity uses Java MotionEvent, not NDK AInputEvent
 *  - hookJniNativeMethods in postAppSpecialize: Unity classes not loaded yet → silent miss
 *  - JNI function table slot 215 patch: const-cast issues + timing
 *
 * Correct approach:
 *  Called from hack_thread AFTER libunity.so is confirmed loaded.
 *  DobbySymbolResolver finds the exported JNI symbol directly in .dynsym.
 *  Symbol confirmed from Unity crash traces:
 *    Java_com_unity3d_player_UnityPlayer_nativeInjectEvent__Landroid_view_InputEvent_2
 */
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

// ── Thread-safe touch buffer: JNI thread → render thread ─────────────────────
struct TouchEvent { float x, y; int action; };
static std::atomic<uint64_t> g_touchSlot{0};  // packed lock-free slot

static void StoreTouchEvent(float x, float y, int action) {
    // pack: x(16bit) | y(16bit) | action(8bit) | valid(1bit) in 64bits
    uint64_t xi = (uint16_t)std::max(0.f, std::min(x, 65535.f));
    uint64_t yi = (uint16_t)std::max(0.f, std::min(y, 65535.f));
    uint64_t packed = (1ULL<<63) | ((uint64_t)(uint8_t)action << 32) | (yi<<16) | xi;
    g_touchSlot.store(packed, std::memory_order_release);
}
static bool LoadTouchEvent(TouchEvent& out) {
    uint64_t v = g_touchSlot.exchange(0, std::memory_order_acquire);
    if (!(v >> 63)) return false;
    out.x      = (float)(v & 0xFFFF);
    out.y      = (float)((v >> 16) & 0xFFFF);
    out.action = (int)((v >> 32) & 0xFF);
    return true;
}

// ── Cached JNI lookups ────────────────────────────────────────────────────────
static jclass     g_meCls       = nullptr;
static jmethodID  g_meGetAction = nullptr;
static jmethodID  g_meGetX      = nullptr;
static jmethodID  g_meGetY      = nullptr;

static void CacheJniMethods(JNIEnv* env) {
    if (g_meCls) return;
    jclass c = env->FindClass("android/view/MotionEvent");
    if (!c) return;
    g_meCls       = (jclass)env->NewGlobalRef(c); env->DeleteLocalRef(c);
    g_meGetAction = env->GetMethodID(g_meCls, "getAction", "()I");
    g_meGetX      = env->GetMethodID(g_meCls, "getX",      "()F");
    g_meGetY      = env->GetMethodID(g_meCls, "getY",      "()F");
    LOGI("[ENI] touch: MotionEvent JNI methods cached");
}

// ── nativeInjectEvent hook ────────────────────────────────────────────────────
typedef jboolean (*NIEFn)(JNIEnv*, jobject, jobject);
static NIEFn g_orig_nie = nullptr;

static jboolean hook_nativeInjectEvent(JNIEnv* env, jobject obj, jobject event) {
    if (event && ImGui::GetCurrentContext()) {
        CacheJniMethods(env);
        if (g_meCls && env->IsInstanceOf(event, g_meCls)) {
            jint  action = env->CallIntMethod(event, g_meGetAction);
            jfloat   x  = env->CallFloatMethod(event, g_meGetX);
            jfloat   y  = env->CallFloatMethod(event, g_meGetY);
            int   masked = action & AMOTION_EVENT_ACTION_MASK;

            StoreTouchEvent(x, y, masked);

            static int dbg = 0;
            if (dbg < 10) {
                LOGI("[ENI] touch event: action=%d (%.0f,%.0f)", masked, (float)x, (float)y);
                dbg++;
            }

            // Consume event when ImGui owns the touch
            if (ImGui::GetIO().WantCaptureMouse)
                return JNI_TRUE;
        }
    }
    return g_orig_nie ? g_orig_nie(env, obj, event) : JNI_FALSE;
}

// ── InstallMotionHooks — call from hack_thread AFTER libunity.so is loaded ───
static void InstallMotionHooks(int /*unused*/ = 0) {
    // All known nativeInjectEvent symbol names across Unity versions
    static const char* SYMBOLS[] = {
        "Java_com_unity3d_player_UnityPlayer_nativeInjectEvent__Landroid_view_InputEvent_2",
        "Java_com_unity3d_player_UnityPlayer_nativeInjectEvent",
        "Java_com_unity3d_player_ReflectionHelper_nativeInjectEvent__Landroid_view_InputEvent_2",
        nullptr
    };

    void* sym = nullptr;
    // DobbySymbolResolver searches .dynsym — most reliable on stripped .so
    for (int i = 0; SYMBOLS[i] && !sym; i++) {
        sym = DobbySymbolResolver("libunity.so", SYMBOLS[i]);
        if (sym) LOGI("[ENI] touch: nativeInjectEvent found via DobbySymbolResolver[%d]", i);
    }
    // dlsym fallback
    if (!sym) {
        void* h = dlopen("libunity.so", RTLD_LAZY | RTLD_NOLOAD);
        if (h) for (int i = 0; SYMBOLS[i] && !sym; i++) {
            sym = dlsym(h, SYMBOLS[i]);
            if (sym) LOGI("[ENI] touch: nativeInjectEvent found via dlsym[%d]", i);
        }
    }

    if (!sym) {
        LOGE("[ENI] touch: nativeInjectEvent NOT FOUND — check Unity version");
        return;
    }

    int r = DobbyHook(sym, (void*)hook_nativeInjectEvent, (void**)&g_orig_nie);
    LOGI("[ENI] touch: DobbyHook nativeInjectEvent: %s @ %p", r==0?"OK":"FAIL", sym);
}

// ── FlushTouchToImGui — call each frame from hook_eglSwapBuffers ──────────────
// MUST be called BEFORE ImGui::NewFrame()
static void FlushTouchToImGui() {
    TouchEvent e{};
    if (!LoadTouchEvent(e)) return;

    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(e.x, e.y);

    // 0=DOWN, 2=MOVE → button held; 1=UP, 3=CANCEL → button released
    bool pressed = (e.action == AMOTION_EVENT_ACTION_DOWN ||
                    e.action == AMOTION_EVENT_ACTION_MOVE);
    io.AddMouseButtonEvent(0, pressed);
}

// ── CustomAndroidNewFrame — no ANativeWindow needed ───────────────────────────
static void CustomAndroidNewFrame(int w, int h) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize             = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
    static auto s_t = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_t).count();
    io.DeltaTime = (dt > 0.f && dt < 1.f) ? dt : 1.f/60.f;
    s_t = now;
}
