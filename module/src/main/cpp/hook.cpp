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
#include "Rect.h"
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
    do {
        sleep(1);
        // FIXED: was "libunity.so" — all RVAs in functions.h are offsets within
        // libil2cpp.so (confirmed from dump.cs). libunity.so has a completely
        // different base; every METHOD(rva) call would land at a wrong address.
        g_il2cppBaseMap = KittyMemory::getLibraryBaseMap("libil2cpp.so");
    } while (!g_il2cppBaseMap.isValid());
    KITTY_LOGI("il2cpp base: %p", (void*)(g_il2cppBaseMap.startAddress));
    Pointers();
    Hooks();

    // BUG 1 FIX: hook eglSwapBuffers via libunity.so, not libEGL.so.
    // Unity games resolve their EGL function pointer at startup inside
    // libunity.so. Hooking libEGL.so directly sometimes misses the CODM
    // render thread because it already cached the function pointer before
    // our module loaded. Hooking at the libunity.so call site catches it.
    auto eglhandle = dlopen("libunity.so", RTLD_LAZY);
    auto eglSwapBuffers = eglhandle ? dlsym(eglhandle, "eglSwapBuffers") : nullptr;
    if (eglSwapBuffers) {
        DobbyHook((void*)eglSwapBuffers,(void*)hook_eglSwapBuffers,
                  (void**)&old_eglSwapBuffers);
        LOGI("eglSwapBuffers hooked via libunity.so");
    } else {
        LOGI("eglSwapBuffers not found — CODM likely Vulkan-only on this device, Vulkan path active");
    }

    // BUG 2 FIX: /system/lib/ is 32-bit only. arm64 system libs live in
    // /system/lib64/. On arm64 the old path returns NULL silently.
    // BUG 3 FIX: the original code passed NULL to DobbyHook when sym_input
    // was NULL (second resolver was inside the else-branch of the first NULL
    // check — so consume was never hooked AND if initializeMotionEvent was
    // NULL, DobbyHook(NULL) would crash the thread before eglSwapBuffers
    // even got installed. Both symbols now guarded independently.
#ifdef __aarch64__
    #define LIBINPUT_PATH "/system/lib64/libinput.so"
#else
    #define LIBINPUT_PATH "/system/lib/libinput.so"
#endif

    void *sym_input = DobbySymbolResolver((LIBINPUT_PATH),
        ("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE"));
    if (sym_input) {
        DobbyHook(sym_input,(void*)myInput,(void**)&origInput);
    } else {
        LOGI("initializeMotionEvent symbol not found at %s", LIBINPUT_PATH);
    }

    sym_input = DobbySymbolResolver((LIBINPUT_PATH),
        ("_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE"));
    if (sym_input) {
        DobbyHook(sym_input,(void*)myConsume,(void**)&origConsume);
    } else {
        LOGI("consume symbol not found — touch passthrough disabled, volume-key toggle still works");
    }

    LOGI("Draw Done!");
    return nullptr;
}
