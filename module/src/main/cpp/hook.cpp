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
// ── ShadowHook 2.0.1 — island-trampoline inline hook ──────────────
#include "shadowhook.h"
// ── ByteHook 1.1.1 — PLT/GOT hook (zero byte-patch at hook site) ──
#include "bytehook.h"
// ── xDL 2.4.0 — enhanced dlopen/dlsym + solist hiding ─────────────
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
//  3. Solist removal (xDL)   — hides from linker soinfo chain
//  4. Anti-ptrace            — PR_SET_DUMPABLE=0
//  5. Thread name spoof      — "AsyncTask #2" (more realistic than
//                              FinalizerDaemon for a non-GC thread)
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
static void bypass_hide_soinfo() {
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

    xdl_info_t xinfo;
    if (xdl_info(xhandle, XDL_DI_DLINFO, &xinfo) == 0) {
        LOGI("[ENI] bypass L3: xdl confirmed lib @ %p (%s)",
             xinfo.dli_fbase, xinfo.dli_fname ? xinfo.dli_fname : "anonymous");
    }
    xdl_close(xhandle);

    // Manual solist walk — unlink our soinfo from the linker chain.
    // Works Android 5–16; solist head is at __dl__ZL6solist in linker.
    struct soinfo_partial {
        char             padding[0x10];
        soinfo_partial*  next;
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

    soinfo_partial** solist_head =
        (soinfo_partial**)dlsym(linker, "__dl__ZL6solist");
    if (!solist_head || !*solist_head)
        solist_head = (soinfo_partial**)dlsym(linker, "__dl_g_soinfo_handles_map");

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
        LOGI("[ENI] bypass L3: soinfo not found (may already be gone after remap)");

    dlclose(linker);
}

// ── Layer 4: anti-ptrace ──────────────────────────────────────────
static void bypass_anti_ptrace() {
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    LOGI("[ENI] bypass L4: PR_SET_DUMPABLE=0");
}

// ── Layer 5: thread name spoof ────────────────────────────────────
static void bypass_spoof_thread_name() {
    // "AsyncTask #2" — plausible non-GC worker thread name.
    // "FinalizerDaemon" would flag itself because ACE knows that name
    // belongs to the JVM GC thread and can cross-check thread binding.
    prctl(PR_SET_NAME, "AsyncTask #2", 0, 0, 0);
    LOGI("[ENI] bypass L5: thread name → 'AsyncTask #2'");
}

// ── Master bypass ─────────────────────────────────────────────────
static void RunAllBypasses() {
    bypass_spoof_thread_name();   // L5 first — name set before any scan window
    bypass_anti_ptrace();          // L4
    sleep(2);                      // wait for il2cpp to settle before remap
    bypass_remap_self();           // L1+L2
    bypass_hide_soinfo();          // L3 — after remap so our base is confirmed
}

// ══════════════════════════════════════════════════════════════════
//  SHADOWHOOK 2.0.1 — island-trampoline inline hook
//  Used for: hooks where we need the original function pointer
//  (input handlers, game function hooks)
//  NOT used for eglSwapBuffers — ByteHook PLT is stealthier there.
// ══════════════════════════════════════════════════════════════════
static bool g_shadowhook_inited = false;

static void InitShadowHook() {
    if (g_shadowhook_inited) return;
    int r = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("[ENI] shadowhook_init: %d (%s)", r, r == 0 ? "OK" : shadowhook_to_errmsg(r));
    g_shadowhook_inited = (r == 0);
}

// Macro: same call-site as DobbyHook was, now routes to ShadowHook
#define SHHook(addr, fn, orig) \
    shadowhook_hook_func_addr((void*)(addr), (void*)(fn), (void**)(orig))

// ══════════════════════════════════════════════════════════════════
//  BYTEHOOK 1.1.1 — PLT/GOT hook for eglSwapBuffers
//
//  Why PLT over ShadowHook inline here:
//  ShadowHook writes an island trampoline but still patches the first
//  instruction of the target function — ACE's library_integrity_scanner
//  (libanort T1[6]) specifically walks .text looking for modified
//  prologues. ByteHook instead overwrites the GOT *pointer* inside
//  libunity.so's .got.plt — no instruction bytes are touched.
//  ACE doesn't scan GOT entries, so this is a structural blind spot.
//
//  Strategy:
//    bytehook_hook_single("libunity.so", "libEGL.so", "eglSwapBuffers", ...)
//    — hooks the GOT entry in libunity.so that resolves to libEGL's
//      eglSwapBuffers. Calls from libunity → our hook → original.
//    We also hook it in libGameAssembly.so for full coverage.
// ══════════════════════════════════════════════════════════════════
static bool           g_bytehook_inited  = false;
static bytehook_stub_t g_egl_stub_unity  = nullptr;
static bytehook_stub_t g_egl_stub_game   = nullptr;

// hook_eglSwapBuffers and old_eglSwapBuffers are defined in menu.h (included below).
// Forward-declare only so InstallEGLPLTHook can reference them before the include.
extern EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);

static void InitByteHook() {
    if (g_bytehook_inited) return;
    int r = bytehook_init(BYTEHOOK_MODE_AUTOMATIC, false);
    LOGI("[ENI] bytehook_init: %d (%s)", r, r == 0 ? "OK" : "FAILED");
    g_bytehook_inited = (r == 0);
}

// PLT hook installer — called after il2cpp is confirmed loaded
static void InstallEGLPLTHook() {
    if (!g_bytehook_inited) {
        LOGE("[ENI] bytehook not initialised — EGL PLT hook skipped");
        return;
    }

    // Hook eglSwapBuffers in libunity.so's GOT (primary call site)
    g_egl_stub_unity = bytehook_hook_single(
        "libunity.so",           // caller lib — where the GOT entry lives
        "libEGL.so",             // callee lib — where the symbol resolves from
        "eglSwapBuffers",        // symbol name
        (void*)hook_eglSwapBuffers,
        nullptr,                 // hooked callback (not needed)
        nullptr                  // hooked callback arg
    );
    LOGI("[ENI] eglSwapBuffers PLT hook (unity.so): %s",
         g_egl_stub_unity ? "OK" : "FAILED");

    // Also hook in libGameAssembly.so — some CODM builds call EGL directly
    g_egl_stub_game = bytehook_hook_single(
        "libGameAssembly.so",
        "libEGL.so",
        "eglSwapBuffers",
        (void*)hook_eglSwapBuffers,
        nullptr,
        nullptr
    );
    LOGI("[ENI] eglSwapBuffers PLT hook (GameAssembly.so): %s",
         g_egl_stub_game ? "OK" : "FAILED (non-fatal if lib absent)");
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

// ── Input hooks (ShadowHook island-trampoline) ────────────────────
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
    // ── Surface + solist bypass ───────────────────────────────────
    RunAllBypasses();

    // ── Init hook engines ─────────────────────────────────────────
    InitShadowHook();   // island-trampoline for game hooks
    InitByteHook();     // PLT/GOT hook for EGL

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

    // ── eglSwapBuffers — PLT hook via ByteHook ────────────────────
    // No dlsym or address needed: ByteHook resolves the GOT entry
    // inside the caller lib automatically. Zero instruction bytes
    // are modified — ACE's .text scanner sees nothing.
    InstallEGLPLTHook();

    // ── Input hooks via ShadowHook (island trampoline) ────────────
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
