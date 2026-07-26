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
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_android.h"
#include "KittyMemory/KittyMemory.h"
#include "KittyMemory/MemoryPatch.h"
#include "KittyMemory/KittyScanner.h"
// ── 13-Tier ACE bypass (audit-verified, msantiagodev/ACE-ANTICHEAT 2026) ──────
#include "bypass.h"
#include "KittyMemory/KittyUtils.h"
// ── Dobby — static inline hook (replaces ShadowHook + ByteHook) ──
#include "dobby.h"
#include "Include/Unity.h"
#include "Misc.h"
#include "hook.h"
#include "Include/Roboto-Regular.h"
#include <iostream>
#include <chrono>
#include "Include/Quaternion.h"
#include "Include/Rect.h"
#include <limits>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <link.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>

#define GamePackageName "com.garena.game.codm"

// ══════════════════════════════════════════════════════════════════
//  BYPASS — 5 layers
//  1. ELF header wipe        — kills magic-byte scan
//  2. Anonymous remap        — hides from /proc/self/maps
//  3. Solist removal (manual) — hides from linker soinfo chain
//  4. Anti-ptrace            — PR_SET_DUMPABLE=0
//  5. Thread name spoof      — "AsyncTask #2"
// ══════════════════════════════════════════════════════════════════

// ── Layer 1 & 2: remap + ELF wipe ────────────────────────────────
struct LibRegion { uintptr_t start, end; };

static int CollectSelfRegions_cb(dl_phdr_info* info, size_t, void* data) {
    Dl_info di;
    extern void bypass_remap_self();
    if (!dladdr((void*)bypass_remap_self, &di)) return 0;
    if (!di.dli_fbase || (uintptr_t)di.dli_fbase != info->dlpi_addr) return 0;

    auto* regions = (std::vector<LibRegion>*)data;
    for (int i = 0; i < (int)info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        uintptr_t s = (info->dlpi_addr + ph.p_vaddr) & ~(uintptr_t)(getpagesize()-1);
        uintptr_t e = (info->dlpi_addr + ph.p_vaddr + ph.p_memsz + getpagesize()-1)
                      & ~(uintptr_t)(getpagesize()-1);
        regions->push_back({s, e});
    }
    return 1;
}


// ── Old inline bypass removed: bypass.h (13-Tier) handles this now ──────

// ══════════════════════════════════════════════════════════════════
//  DOBBY — unified hook macro (replaces ShadowHook + ByteHook)
// ══════════════════════════════════════════════════════════════════
#define DoHook(addr, fn, orig) \
    DobbyHook((void*)(addr), (void*)(fn), (void**)(orig))

// Forward-declare for EGL hook
extern EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

static void InstallEGLHook() {
    // dlopen libEGL.so, dlsym eglSwapBuffers, Dobby inline hook
    void* libegl = dlopen("libEGL.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!libegl) libegl = dlopen("libEGL.so", RTLD_LAZY);
    if (!libegl) {
        LOGE("[ENI] libEGL.so dlopen failed");
        return;
    }
    void* sym = dlsym(libegl, "eglSwapBuffers");
    if (!sym) {
        LOGE("[ENI] eglSwapBuffers sym not found");
        dlclose(libegl);
        return;
    }
    int r = DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    LOGI("[ENI] eglSwapBuffers Dobby hook: %s (sym=%p)", r == 0 ? "OK" : "FAILED", sym);
    // Register for prologue disguise (hides Dobby stub from ACE library scanner)
    // NOTE: RegisterHookSite / RunPrologueDisguise intentionally disabled.
    // ApplyPrologueDisguise() NOP-s Dobby's LDR X17,#8 stub — this breaks
    // the BR X17 that follows it → crash on first eglSwapBuffers call.
    // T1[6] (library_integrity) is already killed by RunFullBypass(),
    // so the prologue scanner never fires anyway.
    // if (r == 0) RegisterHookSite((uintptr_t)sym, *(uint32_t*)sym);
    // keep libegl open so handle stays valid
}

// ══════════════════════════════════════════════════════════════════
//  GAME LOGIC
// ══════════════════════════════════════════════════════════════════
int isGame(JNIEnv *env, jstring appDataDir) {
    if (!appDataDir) return 0;
    const char* app_data_dir = env->GetStringUTFChars(appDataDir, nullptr);
    int user = 0;
    static char package_name[256];
    if (sscanf(app_data_dir, "/data/%*[^/]/%d/%s", &user, package_name) != 2)
        if (sscanf(app_data_dir, "/data/%*[^/]/%s", package_name) != 1)
            { package_name[0] = '\0'; env->ReleaseStringUTFChars(appDataDir, app_data_dir); return 0; }

    if (strcmp(package_name, GamePackageName) == 0) {
        LOGI(OBFUSCATE("detect game: %s"), package_name);
        game_data_dir = new char[strlen(app_data_dir)+1];
        strcpy(game_data_dir, app_data_dir);
        env->ReleaseStringUTFChars(appDataDir, app_data_dir);
        return 1;
    }
    env->ReleaseStringUTFChars(appDataDir, app_data_dir);
    return 0;
}

// ── Input hooks (Dobby inline) ────────────────────────────────────
HOOKAF(void, Input, void *thiz, void *ex_ab, void *ex_ac) {
    origInput(thiz, ex_ab, ex_ac);
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent*)thiz);
}

HOOKAF(int32_t, Consume, void *thiz, void *arg1, bool arg2, long arg3,
       uint32_t *arg4, AInputEvent **input_event) {
    auto result = origConsume(thiz, arg1, arg2, arg3, arg4, input_event);
    if (result != 0 || *input_event == nullptr) return result;
    ImGui_ImplAndroid_HandleInputEvent(*input_event);
    return result;
}

#include "functions.h"
#include "menu.h"

// ══════════════════════════════════════════════════════════════════
//  HACK THREAD
// ══════════════════════════════════════════════════════════════════
void *hack_thread(void *arg) {
    // ── 13-Tier ACE bypass (bypass.h) — MUST run before any DobbyHook() ──────
    RunFullBypass();

    LOGI("[ENI] hack_thread: scanning for il2cpp...");

    static const char* CANDIDATES[] = {
        "libunity.so", "libil2cpp.so", "libGameAssembly.so", nullptr
    };

    int iters = 0;
    while (!g_il2cppBaseMap.isValid() && iters < 90) {
        sleep(1); ++iters;
        for (int i = 0; CANDIDATES[i]; i++) {
            g_il2cppBaseMap = KittyMemory::getLibraryBaseMap(CANDIDATES[i]);
            if (g_il2cppBaseMap.isValid()) {
                LOGI("[ENI] il2cpp found as '%s'", CANDIDATES[i]);
                break;
            }
        }
        if (iters % 10 == 0) {
            LOGI("[ENI] still searching... (%ds)", iters);
            FILE* maps = fopen("/proc/self/maps", "r");
            if (maps) {
                char line[512], last[512] = "";
                while (fgets(line, sizeof(line), maps)) {
                    if (!strstr(line, ".so")) continue;
                    if (strstr(line,"/system/")||strstr(line,"/vendor/")||strstr(line,"/apex/")) continue;
                    char* sl = strrchr(line, '/');
                    if (!sl) continue; sl++;
                    char* nl = strchr(sl, '\n'); if (nl) *nl = 0;
                    if (strcmp(sl, last)) { LOGI("[ENI]   lib: %s", sl); strncpy(last,sl,511); }
                }
                fclose(maps);
            }
        }
    }

    if (!g_il2cppBaseMap.isValid()) {
        LOGE("[ENI] FATAL: il2cpp not found after 90s");
        return nullptr;
    }

    LOGI("[ENI] il2cpp base: 0x%" PRIxPTR, (uintptr_t)g_il2cppBaseMap.startAddress);
    Pointers();
    Hooks();
    InstallFeatureHooks();

    // ── eglSwapBuffers via Dobby inline hook ──────────────────────
    InstallEGLHook();

    // RunPrologueDisguise() intentionally not called — see InstallEGLHook comment.

    // ── Input hooks via Dobby ─────────────────────────────────────
#ifdef __aarch64__
    #define LIBINPUT "/system/lib64/libinput.so"
#else
    #define LIBINPUT "/system/lib/libinput.so"
#endif

    void* libinput = dlopen(LIBINPUT, RTLD_LAZY);
    if (libinput) {
        void* sym = dlsym(libinput,
            "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE");
        if (sym) {
            int r = DoHook(sym, myInput, &origInput);
            LOGI("[ENI] initializeMotionEvent: %s", r==0?"OK":"FAILED");
        }

        sym = dlsym(libinput,
            "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE");
        if (sym) {
            int r = DoHook(sym, myConsume, &origConsume);
            LOGI("[ENI] consume hook: %s", r==0?"OK":"FAILED");
        }
        // keep open — Dobby trampoline references the original code
    } else {
        LOGI("[ENI] libinput dlopen failed (non-fatal)");
    }

    LOGI("[ENI] hack_thread: setup complete!");
    return nullptr;
}
