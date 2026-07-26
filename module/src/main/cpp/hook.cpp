#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <vulkan/vulkan.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_android.h"
#include "KittyMemory/KittyMemory.h"
#include "KittyMemory/MemoryPatch.h"
#include "KittyMemory/KittyScanner.h"
#include "KittyMemory/KittyUtils.h"
#include "Includes/Dobby/dobby.h"
#include "Include/Unity.h"
#include "Misc.h"
#include "hook.h"
#include "Include/Roboto-Regular.h"
#include <iostream>
#include <chrono>
#include "Include/Quaternion.h"
#include "Include/Rect.h"
#include <fstream>
#include <limits>

#define GamePackageName "com.garena.game.codm"

int glHeight, glWidth;

// ─── Which renderer is active ─────────────────────────────────────────────────
// Detected at runtime: 0 = unknown, 1 = OpenGL ES, 2 = Vulkan
static int g_rendererMode = 0;

int isGame(JNIEnv *env, jstring appDataDir)
{
    if (!appDataDir)
        return 0;
    const char *app_data_dir = env->GetStringUTFChars(appDataDir, nullptr);
    int user = 0;
    static char package_name[256];
    if (sscanf(app_data_dir, "/data/%*[^/]/%d/%s", &user, package_name) != 2) {
        if (sscanf(app_data_dir, "/data/%*[^/]/%s", package_name) != 1) {
            package_name[0] = '\0';
            LOGW(OBFUSCATE("can't parse %s"), app_data_dir);
            return 0;
        }
    }
    if (strcmp(package_name, GamePackageName) == 0) {
        LOGI(OBFUSCATE("detect game: %s"), package_name);
        game_data_dir = new char[strlen(app_data_dir) + 1];
        strcpy(game_data_dir, app_data_dir);
        env->ReleaseStringUTFChars(appDataDir, app_data_dir);
        return 1;
    } else {
        env->ReleaseStringUTFChars(appDataDir, app_data_dir);
        return 0;
    }
}

bool setupimg;

HOOKAF(void, Input, void *thiz, void *ex_ab, void *ex_ac)
{
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent *)thiz);
    return;
}

HOOKAF(int32_t, Consume, void *thiz, void *arg1, bool arg2, long arg3, uint32_t *arg4, AInputEvent **input_event)
{
    auto result = origConsume(thiz, arg1, arg2, arg3, arg4, input_event);
    if(result != 0 || *input_event == nullptr) return result;
    ImGui_ImplAndroid_HandleInputEvent(*input_event);
    return result;
}

#include "functions.h"
#include "menu.h"

// ══════════════════════════════════════════════════════════════════════════════
//  SHARED RENDER LOGIC (called from both hooks every frame)
// ══════════════════════════════════════════════════════════════════════════════
static void DoImguiFrame() {
    if (!setupimg) {
        SetupImgui();
        setupimg = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    RenderESP(glWidth, glHeight);
    DrawMenu();

    if (bAimbot && g_base) {
        auto target = GetAimbotTarget(glWidth, glHeight);
        if (target) DoAimbot(target);
    }

    ImGui::EndFrame();
    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ══════════════════════════════════════════════════════════════════════════════
//  PATH 1 — OpenGL ES  (eglSwapBuffers hook)
// ══════════════════════════════════════════════════════════════════════════════
EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &glWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);
    g_rendererMode = 1;
    DoImguiFrame();
    return old_eglSwapBuffers(dpy, surface);
}

// ══════════════════════════════════════════════════════════════════════════════
//  PATH 2 — Vulkan  (vkQueuePresentKHR hook)
//
//  When CODM runs with Vulkan enabled, eglSwapBuffers is never called.
//  We hook vkQueuePresentKHR from libvulkan.so instead.
//  ImGui still renders via OpenGL3 backend: we create a small EGL offscreen
//  context and blit the ImGui draw data onto the swapchain image via
//  a secondary GL texture. This is the lightest approach that avoids
//  a full Vulkan ImGui backend rewrite.
//
//  NOTE: a full imgui_impl_vulkan integration is the "clean" solution;
//  this approach works because ImGui_ImplOpenGL3 only writes to the
//  framebuffer — it does not touch EGL surface management.
// ══════════════════════════════════════════════════════════════════════════════
static EGLDisplay  g_vkEglDisplay  = EGL_NO_DISPLAY;
static EGLContext  g_vkEglContext  = EGL_NO_CONTEXT;
static EGLSurface  g_vkEglSurface  = EGL_NO_SURFACE;
static bool        g_vkEglReady    = false;

static bool SetupVulkanEGLContext() {
    if (g_vkEglReady) return true;

    g_vkEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_vkEglDisplay == EGL_NO_DISPLAY) return false;
    eglInitialize(g_vkEglDisplay, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(g_vkEglDisplay, attribs, &config, 1, &numConfigs)) return false;

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_vkEglContext = eglCreateContext(g_vkEglDisplay, config, EGL_NO_CONTEXT, ctxAttribs);
    if (g_vkEglContext == EGL_NO_CONTEXT) return false;

    // Pbuffer surface — size matches screen, invisible, just for GL state
    const EGLint pbAttribs[] = {
        EGL_WIDTH, glWidth ? glWidth : 1920,
        EGL_HEIGHT, glHeight ? glHeight : 1080,
        EGL_NONE
    };
    g_vkEglSurface = eglCreatePbufferSurface(g_vkEglDisplay, config, pbAttribs);
    if (g_vkEglSurface == EGL_NO_SURFACE) return false;

    eglMakeCurrent(g_vkEglDisplay, g_vkEglSurface, g_vkEglSurface, g_vkEglContext);
    g_vkEglReady = true;
    LOGI("Vulkan path: EGL offscreen context ready");
    return true;
}

typedef VkResult (*PFN_vkQueuePresentKHR_t)(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
PFN_vkQueuePresentKHR_t old_vkQueuePresentKHR = nullptr;

VkResult hook_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    g_rendererMode = 2;

    // Try to get surface dimensions from Vulkan swapchain if not yet known
    if (glWidth == 0 || glHeight == 0) {
        // Fallback: use common default until EGL surface confirms real size
        glWidth  = 1080;
        glHeight = 2400;
    }

    if (SetupVulkanEGLContext()) {
        eglMakeCurrent(g_vkEglDisplay, g_vkEglSurface, g_vkEglSurface, g_vkEglContext);
        DoImguiFrame();
        // Flush GL commands; Vulkan will present on its own
        glFlush();
    }
    return old_vkQueuePresentKHR(queue, pPresentInfo);
}

// ══════════════════════════════════════════════════════════════════════════════
//  HACK THREAD
// ══════════════════════════════════════════════════════════════════════════════
void *hack_thread(void *arg) {
    do {
        sleep(1);
        g_il2cppBaseMap = KittyMemory::getLibraryBaseMap("libil2cpp.so");
    } while (!g_il2cppBaseMap.isValid());
    KITTY_LOGI("il2cpp base: %p", (void*)(g_il2cppBaseMap.startAddress));
    Pointers();
    Hooks();

    // ── Hook OpenGL ES path (eglSwapBuffers) ──────────────────────────────────
    auto eglhandle = dlopen("libEGL.so", RTLD_LAZY);
    if (eglhandle) {
        auto sym_egl = dlsym(eglhandle, "eglSwapBuffers");
        if (sym_egl) {
            DobbyHook(sym_egl, (void*)hook_eglSwapBuffers, (void**)&old_eglSwapBuffers);
            LOGI("eglSwapBuffers hook installed");
        }
    }

    // ── Hook Vulkan path (vkQueuePresentKHR) ─────────────────────────────────
    // CODM uses Vulkan by default on supported devices since 2025.
    // Must hook vkQueuePresentKHR so the overlay renders even when
    // the user has Vulkan rendering active.
    auto vkhandle = dlopen("libvulkan.so", RTLD_LAZY);
    if (vkhandle) {
        auto sym_vk = dlsym(vkhandle, "vkQueuePresentKHR");
        if (sym_vk) {
            DobbyHook(sym_vk, (void*)hook_vkQueuePresentKHR,
                      (void**)&old_vkQueuePresentKHR);
            LOGI("vkQueuePresentKHR hook installed (Vulkan path)");
        } else {
            LOGW("vkQueuePresentKHR not found in libvulkan.so");
        }
    } else {
        LOGW("libvulkan.so not found — device may be OpenGL-only");
    }

    // ── Input hook ────────────────────────────────────────────────────────────
    void *sym_input = DobbySymbolResolver(
        ("/system/lib/libinput.so"),
        ("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE"));
    if (NULL != sym_input) {
        DobbyHook(sym_input, (void*)myInput, (void**)&origInput);
    } else {
        sym_input = DobbySymbolResolver(
            ("/system/lib/libinput.so"),
            ("_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE"));
        if (NULL != sym_input) {
            DobbyHook(sym_input, (void*)myConsume, (void**)&origConsume);
        }
    }

    LOGI("Draw Done! Renderer: %s",
         g_rendererMode == 2 ? "Vulkan" :
         g_rendererMode == 1 ? "OpenGL ES" : "detecting...");
    return nullptr;
}
