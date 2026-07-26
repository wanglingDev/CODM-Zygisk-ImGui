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
#include "KittyMemory/KittyUtils.h"
// ── ShadowHook replaces Dobby ──────────────────────────────────────
#include "shadowhook.h"
#include "bypass_anogs.h"
// ── xDL for solist manipulation + stealth dlopen ──────────────────
#include "xdl.h"
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
//  3. Solist removal (xDL)   — hides from linker soinfo chain  ← NEW
//  4. Anti-ptrace            — PR_SET_DUMPABLE=0
//  5. Thread name spoof      — benign-looking thread name
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

void bypass_remap_self() {
    std::vector<LibRegion> regions;
    dl_iterate_phdr(CollectSelfRegions_cb, &regions);

    for (auto& r : regions) {
        size_t sz = r.end - r.start;
        void* anon = mmap(nullptr, sz, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) continue;

        memcpy(anon, (void*)r.start, sz);

        // Wipe ELF magic on first segment
        if (r.start == regions[0].start) {
            mprotect((void*)r.start, getpagesize(), PROT_READ|PROT_WRITE);
            memset((void*)r.start, 0, 4);
        }

        munmap((void*)r.start, sz);

        void* fixed = mmap((void*)r.start, sz,
                           PROT_READ|PROT_EXEC,
                           MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        if (fixed == MAP_FAILED) {
            mprotect(anon, sz, PROT_READ|PROT_EXEC);
        } else {
            memcpy(fixed, anon, sz);
            mprotect(fixed, sz, PROT_READ|PROT_EXEC);
            munmap(anon, sz);
        }
    }
    LOGI("[ENI] bypass L1+L2: remap+ELF wipe done");
}

// ── Layer 3: xDL solist removal ───────────────────────────────────
// xDL exposes xdl_open which bypasses linker namespace and returns
// a handle we can use to walk + unlink our soinfo entry.
// We use a simpler but effective approach: open ourselves via xdl,
// then call xdl_close — xDL's internals will clean up the reference.
// For full soinfo unlinking we walk the solist manually.
static void bypass_hide_soinfo() {
    // Strategy: use xdl_addr on our own function to get our lib path,
    // then force-open with RTLD_NOLOAD to confirm we're loaded,
    // then manipulate the solist next-pointer to unlink us.

    Dl_info di;
    if (!dladdr((void*)bypass_hide_soinfo, &di) || !di.dli_fname) {
        LOGI("[ENI] bypass L3: dladdr failed, skip soinfo hide");
        return;
    }

    // xDL enhanced open — bypasses Android 7+ namespace restrictions
    void* xhandle = xdl_open(di.dli_fname, XDL_DEFAULT);
    if (!xhandle) {
        LOGI("[ENI] bypass L3: xdl_open failed (%s)", di.dli_fname);
        return;
    }

    // Walk /proc/self/maps to confirm we're anonymous after remap
    // then use xdl to verify our symbol is still callable
    xdl_info_t xinfo;
    if (xdl_info(xhandle, XDL_DI_DLINFO, &xinfo) == 0) {
        LOGI("[ENI] bypass L3: xdl confirmed lib @ %p (%s)",
             xinfo.dli_fbase, xinfo.dli_fname ? xinfo.dli_fname : "anonymous");
    }
    xdl_close(xhandle);

    // Manual solist walk — unlink our soinfo from the chain
    // Works on Android 5-16 (solist is always at a fixed linker symbol)
    struct soinfo_partial {
        char      padding[0x10];  // stname + flags
        soinfo_partial* next;
    };

    void* linker = dlopen(
#ifdef __aarch64__
        "/apex/com.android.runtime/bin/linker64",
#else
        "/apex/com.android.runtime/bin/linker",
#endif
        RTLD_LAZY | RTLD_NOLOAD);

    if (!linker) linker = dlopen(
#ifdef __aarch64__
        "/system/bin/linker64",
#else
        "/system/bin/linker",
#endif
        RTLD_LAZY | RTLD_NOLOAD);

    if (!linker) {
        LOGI("[ENI] bypass L3: linker dlopen failed, skip manual solist");
        return;
    }

    // __dl__ZL6solist is the solist head in most AOSP versions
    soinfo_partial** solist_head =
        (soinfo_partial**)dlsym(linker, "__dl__ZL6solist");
    if (!solist_head || !*solist_head) {
        // Try alternate symbol name
        solist_head = (soinfo_partial**)dlsym(linker, "__dl_g_soinfo_handles_map");
    }
    if (!solist_head) {
        LOGI("[ENI] bypass L3: solist symbol not found, relying on remap+xdl");
        dlclose(linker);
        return;
    }

    uintptr_t our_base = (uintptr_t)di.dli_fbase;
    soinfo_partial* prev = nullptr;
    soinfo_partial* cur  = *solist_head;
    int removed = 0;

    while (cur) {
        // Check if this soinfo base matches ours
        // soinfo base is at offset 0x18 on most AOSP (after stname, flags, phdr, phnum)
        // We use a heuristic: check if cur is near our remap address
        uintptr_t candidate = *(uintptr_t*)((uintptr_t)cur + 0x18);
        if (candidate == our_base) {
            if (prev) prev->next = cur->next;
            else       *solist_head = cur->next;
            removed++;
            LOGI("[ENI] bypass L3: soinfo unlinked @ %p", (void*)cur);
            break;
        }
        prev = cur;
        cur  = cur->next;
    }

    if (!removed)
        LOGI("[ENI] bypass L3: soinfo not found in list (may already be gone after remap)");

    dlclose(linker);
}

// ── Layer 4: anti-ptrace ──────────────────────────────────────────
static void bypass_anti_ptrace() {
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    LOGI("[ENI] bypass L4: PR_SET_DUMPABLE=0");
}

// ── Layer 5: thread name spoof ────────────────────────────────────
static void bypass_spoof_thread_name() {
    // "AsyncTask #2" is far more plausible than "FinalizerDaemon"
    // for a thread that isn't bound to the JVM GC
    prctl(PR_SET_NAME, "AsyncTask #2", 0, 0, 0);
    LOGI("[ENI] bypass L5: thread name spoofed to 'AsyncTask #2'");
}

// ── Master bypass ─────────────────────────────────────────────────
static void RunAllBypasses() {
    bypass_spoof_thread_name();   // L5 first — sets name before any scan
    bypass_anti_ptrace();          // L4
    sleep(2);                      // wait for il2cpp to settle before remap
    bypass_remap_self();           // L1+L2
    bypass_hide_soinfo();          // L3 — after remap so our base is correct
}

// ══════════════════════════════════════════════════════════════════
//  SHADOWHOOK — replaces Dobby for all hooks
//  Island trampoline = no detectable byte patches at hook site
// ══════════════════════════════════════════════════════════════════
static bool g_shadowhook_inited = false;

static void InitShadowHook() {
    if (g_shadowhook_inited) return;
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("[ENI] shadowhook_init: %d (%s)", r, r == 0 ? "OK" : shadowhook_to_errmsg(r));
    g_shadowhook_inited = (r == 0);
}

// Wrapper macro: same call-site as DobbyHook was
#define SHHook(addr, fn, orig) \
    shadowhook_hook_func_addr((void*)(addr), (void*)(fn), (void**)(orig))

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

// ── Input hooks ───────────────────────────────────────────────────
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
    RunAllBypasses();
    InitShadowHook();   // init shadowhook after bypass

    pthread_t anogs_tid;
pthread_create(&anogs_tid, nullptr, [](void*) -> void* {
    InstallAnogsHooks();
    return nullptr;
}, nullptr);
pthread_detach(anogs_tid);

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

    // ── eglSwapBuffers — hook via ShadowHook ────────────────────
    void* eglSwap = nullptr;
    void* eglLib  = xdl_open("libEGL.so", XDL_DEFAULT);
    if (eglLib) {
        eglSwap = xdl_sym(eglLib, "eglSwapBuffers", nullptr);
        if (eglSwap) LOGI("[ENI] eglSwapBuffers via xdl @ %p", eglSwap);
        xdl_close(eglLib);
    }
    if (!eglSwap) {
        void* uLib = xdl_open("libunity.so", XDL_DEFAULT);
        if (uLib) {
            eglSwap = xdl_sym(uLib, "eglSwapBuffers", nullptr);
            if (eglSwap) LOGI("[ENI] eglSwapBuffers via xdl(unity) @ %p", eglSwap);
            xdl_close(uLib);
        }
    }

    if (eglSwap) {
        void* stub = SHHook(eglSwap, hook_eglSwapBuffers, &old_eglSwapBuffers);
        LOGI("[ENI] eglSwapBuffers ShadowHook: %s", stub ? "OK" : "FAILED");
    } else {
        LOGE("[ENI] eglSwapBuffers not found — Vulkan device?");
        LOGE("[ENI] Try: adb shell setprop debug.hwui.renderer opengl");
    }

    // ── Input hooks via ShadowHook ───────────────────────────────
#ifdef __aarch64__
    #define LIBINPUT "/system/lib64/libinput.so"
#else
    #define LIBINPUT "/system/lib/libinput.so"
#endif

    void* libinput = xdl_open(LIBINPUT, XDL_DEFAULT);
    if (libinput) {
        void* sym = xdl_sym(libinput,
            "_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE",
            nullptr);
        if (sym) {
            void* s = SHHook(sym, myInput, &origInput);
            LOGI("[ENI] initializeMotionEvent: %s", s?"OK":"FAILED");
        }

        sym = xdl_sym(libinput,
            "_ZN7android13InputConsumer7consumeEPNS_26InputEventFactoryInterfaceEblPjPPNS_10InputEventE",
            nullptr);
        if (sym) {
            void* s = SHHook(sym, myConsume, &origConsume);
            LOGI("[ENI] consume hook: %s", s?"OK":"FAILED");
        }
        xdl_close(libinput);
    } else {
        LOGI("[ENI] libinput xdl_open failed (non-fatal)");
    }

    LOGI("[ENI] hack_thread: setup complete!");
    return nullptr;
}
