#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
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
#define GamePackageName "com.garena.game.codm" // define the game package name here please

int glHeight, glWidth;

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

void *hack_thread(void *arg) {
    LOGI("[ENI] hack_thread: started, scanning for il2cpp library...");

    // Garena CODM merges IL2CPP directly into libunity.so — confirmed by
    // logcat lib dump: libil2cpp.so never appears in /proc/self/maps.
    // libunity.so is always the correct base for RVAs from this dump.cs.
    static const char* IL2CPP_CANDIDATES[] = {
        "libunity.so",      // ← CODM Garena: IL2CPP merged here (confirmed)
        "libil2cpp.so",     // standard Unity IL2CPP build
        "libGameAssembly.so",
        "libCODM.so",
        "libcodm.so",
        nullptr
    };

    int wait_iters = 0;
    while (!g_il2cppBaseMap.isValid() && wait_iters < 90) {
        sleep(1);
        ++wait_iters;

        // Try each candidate name
        for (int ci = 0; IL2CPP_CANDIDATES[ci]; ci++) {
            g_il2cppBaseMap = KittyMemory::getLibraryBaseMap(IL2CPP_CANDIDATES[ci]);
            if (g_il2cppBaseMap.isValid()) {
                LOGI("[ENI] il2cpp library found as '%s'", IL2CPP_CANDIDATES[ci]);
                break;
            }
        }

        // Every 10 s, dump all game .so files to logcat for diagnosis
        if (wait_iters % 10 == 0) {
            LOGI("[ENI] still searching... (%ds) — dumping loaded libs:", wait_iters);
            FILE* maps = fopen("/proc/self/maps", "r");
            if (maps) {
                char line[512];
                char last_lib[512] = "";
                while (fgets(line, sizeof(line), maps)) {
                    // only print executable .so segments, skip system libs
                    if (!strstr(line, ".so")) continue;
                    if (strstr(line, "/system/") || strstr(line, "/vendor/") ||
                        strstr(line, "/apex/")   || strstr(line, "bionic")) continue;
                    char* slash = strrchr(line, '/');
                    if (!slash) continue;
                    // deduplicate consecutive same-lib lines
                    slash++;
                    char* nl = strchr(slash, '\n');
                    if (nl) *nl = 0;
                    if (strcmp(slash, last_lib) != 0) {
                        LOGI("[ENI]   lib: %s", slash);
                        strncpy(last_lib, slash, sizeof(last_lib)-1);
                    }
                }
                fclose(maps);
            }
        }
    }

    if (!g_il2cppBaseMap.isValid()) {
        LOGE("[ENI] FATAL: il2cpp library not found after 90 s — check logcat lib dump above");
        return nullptr;
    }

    LOGI("[ENI] il2cpp base: %p", (void*)g_il2cppBaseMap.startAddress);
    Pointers();
    Hooks();

    // ── eglSwapBuffers ────────────────────────────────────────────────────────
    void* eglSwapBuffers = nullptr;

    auto eglhandle = dlopen("libEGL.so", RTLD_LAZY);
    if (eglhandle) {
        eglSwapBuffers = dlsym(eglhandle, "eglSwapBuffers");
        if (eglSwapBuffers)
            LOGI("[ENI] eglSwapBuffers found in libEGL.so @ %p", eglSwapBuffers);
        else
            LOGI("[ENI] libEGL.so opened but eglSwapBuffers symbol missing");
    } else {
        LOGE("[ENI] dlopen(libEGL.so) failed: %s", dlerror());
    }

    if (!eglSwapBuffers) {
        auto unityhandle = dlopen("libunity.so", RTLD_LAZY);
        if (unityhandle) {
            eglSwapBuffers = dlsym(unityhandle, "eglSwapBuffers");
            if (eglSwapBuffers)
                LOGI("[ENI] eglSwapBuffers found in libunity.so (fallback) @ %p", eglSwapBuffers);
        }
    }

    if (eglSwapBuffers) {
        int dret = DobbyHook((void*)eglSwapBuffers, (void*)hook_eglSwapBuffers,
                             (void**)&old_eglSwapBuffers);
        if (dret == 0)
            LOGI("[ENI] eglSwapBuffers hooked successfully!");
        else
            LOGE("[ENI] DobbyHook(eglSwapBuffers) FAILED, ret=%d", dret);
    } else {
        LOGE("[ENI] eglSwapBuffers NOT FOUND — game may use Vulkan. Menu will not render.");
        LOGE("[ENI] Run: adb shell setprop debug.hwui.renderer opengl  and restart game");
    }

    // ── Input hooks ──────────────────────────────────────────────────────────
#ifdef __aarch64__
    #define LIBINPUT_PATH "/system/lib64/libinput.so"
#else
    #define LIBINPUT_PATH "/system/lib/libinput.so"
#endif

    void *sym_input = DobbySymbolResolver(LIBINPUT_PATH,
        "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
    if (sym_input) {
        int r = DobbyHook(sym_input, (void*)myInput, (void**)&origInput);
        LOGI("[ENI] initializeMotionEvent hook: %s", r==0?"OK":"FAILED");
    } else {
        LOGI("[ENI] initializeMotionEvent not found in %s (non-fatal)", LIBINPUT_PATH);
    }

    sym_input = DobbySymbolResolver(LIBINPUT_PATH,
        "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE");
    if (sym_input) {
        int r = DobbyHook(sym_input, (void*)myConsume, (void**)&origConsume);
        LOGI("[ENI] consume hook: %s", r==0?"OK":"FAILED");
    } else {
        LOGI("[ENI] consume not found in %s (non-fatal)", LIBINPUT_PATH);
    }

    LOGI("[ENI] hack_thread: setup complete!");
    return nullptr;
}
