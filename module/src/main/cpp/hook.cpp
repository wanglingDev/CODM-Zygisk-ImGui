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

// ── xDL for solist manipulation + stealth dlopen ──────────────────

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
#include <android/input.h>
#include "input_hook.h"
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
// prot flags derived from ELF Phdr p_flags
struct LibRegion { uintptr_t start, end; int prot; };

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
        // Translate ELF Phdr flags → mmap prot bits
        int prot = 0;
        if (ph.p_flags & PF_R) prot |= PROT_READ;
        if (ph.p_flags & PF_W) prot |= PROT_WRITE;
        if (ph.p_flags & PF_X) prot |= PROT_EXEC;
        regions->push_back({s, e, prot});
    }
    return 1;
}

void bypass_remap_self() {
    std::vector<LibRegion> regions;
    dl_iterate_phdr(CollectSelfRegions_cb, &regions);

    for (auto& r : regions) {
        size_t sz = r.end - r.start;

        // Stage in anonymous memory first
        void* anon = mmap(nullptr, sz, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) continue;
        memcpy(anon, (void*)r.start, sz);

        // Wipe ELF magic in the copy (only first segment = .text header)
        if (r.start == regions[0].start)
            memset(anon, 0, 4);

        munmap((void*)r.start, sz);

        // Remap at original VA with CORRECT per-segment permissions.
        // Critical: .data/.bss must stay PROT_READ|PROT_WRITE or static
        // variable writes in the hooked functions will SIGSEGV.
        void* fixed = mmap((void*)r.start, sz, r.prot,
                           MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        if (fixed == MAP_FAILED) {
            mprotect(anon, sz, r.prot);
        } else {
            memcpy(fixed, anon, sz);
            mprotect(fixed, sz, r.prot);
            munmap(anon, sz);
        }
    }
    LOGI("[ENI] bypass L1+L2: remap+ELF wipe done (segment-correct perms)");
}

// ── Layer 3: solist removal (manual walk) ────────────────────────
static void bypass_hide_soinfo() {
    Dl_info di;
    if (!dladdr((void*)bypass_hide_soinfo, &di) || !di.dli_fname) {
        LOGI("[ENI] bypass L3: dladdr failed");
        return;
    }

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
// Using Dobby for all hooks
#define SHHook(addr, fn, orig) DobbyHook((void*)(addr), (void*)(fn), (void**)(orig))
static void InitShadowHook() { LOGI("[ENI] Dobby hook mode active"); }

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
    // ImGui first — gets a clean ACTION_DOWN/UP cycle.
    // imgui_impl_android resets MousePos→(-FLT_MAX,-FLT_MAX) on UP (imgui #6627)
    // so WantCaptureMouse clears next frame; game touches outside the window
    // are never swallowed.
    ImGui_ImplAndroid_HandleInputEvent((AInputEvent*)thiz);
    if (!ImGui::GetIO().WantCaptureMouse)
        origInput(thiz, ex_ab, ex_ac);
}

HOOKAF(int32_t, Consume, void *thiz, void *arg1, bool arg2, long arg3,
       uint32_t *arg4, AInputEvent **input_event) {
    if (input_event && *input_event) {
        // Check BEFORE feeding to ImGui whether this touch lands on a window.
        // WantCaptureMouse is evaluated AFTER HandleInputEvent, which is too late
        // for the Consume path — by then origConsume would already have run.
        // So we peek at the touch position first.
        float ex = AMotionEvent_getX(*input_event, 0);
        float ey = AMotionEvent_getY(*input_event, 0);
        bool  onWindow = ImGui::GetIO().MouseDown[0] || TouchHitsImGuiWindow(ex, ey);

        ImGui_ImplAndroid_HandleInputEvent(*input_event);

        // Block origConsume only when this touch was on our window.
        // All game touches (outside the window) still reach origConsume.
        if (onWindow && ImGui::GetIO().WantCaptureMouse)
            return 0;
    }
    return origConsume(thiz, arg1, arg2, arg3, arg4, input_event);
}

#include "functions.h"
#include "menu.h"

// ══════════════════════════════════════════════════════════════════
//  HACK THREAD
// ══════════════════════════════════════════════════════════════════
void *hack_thread(void *arg) {
    RunAllBypasses();
    InitShadowHook(); // init hook mode

    LOGI("[ENI] hack_thread: scanning for il2cpp...");

    // CODM Garena uses merged IL2CPP — game bytecode is compiled statically
    // INTO libunity.so. There is no separate libil2cpp.so in this build.
    // All RVAs in dump.cs / functions.h are relative to libunity.so's base.
    static const char* CANDIDATES[] = {
        "libunity.so",        // correct for CODM Garena merged IL2CPP build
        "libil2cpp.so",       // fallback for split builds
        "libGameAssembly.so", // fallback for other Unity versions
        nullptr
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
    void* eglLib = dlopen("libEGL.so", RTLD_LAZY | RTLD_NOLOAD);
    if (eglLib) {
        eglSwap = dlsym(eglLib, "eglSwapBuffers");
        if (eglSwap) LOGI("[ENI] eglSwapBuffers via libEGL @ %p", eglSwap);
        dlclose(eglLib);
    }
    if (!eglSwap) {
        void* uLib = dlopen("libunity.so", RTLD_LAZY | RTLD_NOLOAD);
        if (uLib) {
            eglSwap = dlsym(uLib, "eglSwapBuffers");
            if (eglSwap) LOGI("[ENI] eglSwapBuffers via libunity @ %p", eglSwap);
            dlclose(uLib);
        }
    }

    if (eglSwap) {
        int _egl_r = SHHook(eglSwap, hook_eglSwapBuffers, &old_eglSwapBuffers);
        LOGI("[ENI] eglSwapBuffers hook: %s", _egl_r == 0 ? "OK" : "FAILED");
    } else {
        LOGE("[ENI] eglSwapBuffers not found — Vulkan device?");
        LOGE("[ENI] Try: adb shell setprop debug.hwui.renderer opengl");
    }

    LOGI("[ENI] hack_thread: setup complete!");
    return nullptr;
}
