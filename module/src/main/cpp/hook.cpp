#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cinttypes>
#include <string>
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
#include <linux/input.h>

#define GamePackageName "com.garena.game.codm"

// ── Global EGL surface dimensions (read by touch_input.h) ────────
int g_width = 1080, g_height = 2400;
int g_companion_sock = -1; // defined here, extern in main.cpp

// ── Touch input: dual-layer system ───────────────────────────────
#include "touch_input.h"

// ══════════════════════════════════════════════════════════════════
//  BYPASS
// ══════════════════════════════════════════════════════════════════
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
        void* anon = mmap(nullptr, sz, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) continue;
        memcpy(anon, (void*)r.start, sz);
        if (r.start == regions[0].start) {
            mprotect((void*)r.start, getpagesize(), PROT_READ|PROT_WRITE);
            memset((void*)r.start, 0, 4);
        }
        munmap((void*)r.start, sz);
        void* fixed = mmap((void*)r.start, sz, PROT_READ|PROT_EXEC,
                           MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        if (fixed == MAP_FAILED) {
            mprotect(anon, sz, PROT_READ|PROT_EXEC);
        } else {
            memcpy(fixed, anon, sz);
            mprotect(fixed, sz, PROT_READ|PROT_EXEC);
            munmap(anon, sz);
        }
    }
    LOGI("[ENI] bypass L1+L2 done");
}

static void bypass_hide_soinfo() {
    Dl_info di;
    if (!dladdr((void*)bypass_hide_soinfo, &di) || !di.dli_fname) return;
    struct soinfo_partial { char pad[0x10]; soinfo_partial* next; };
    void* linker = dlopen("/apex/com.android.runtime/bin/linker64", RTLD_LAZY|RTLD_NOLOAD);
    if (!linker) linker = dlopen("/system/bin/linker64", RTLD_LAZY|RTLD_NOLOAD);
    if (!linker) return;
    soinfo_partial** head = (soinfo_partial**)dlsym(linker, "__dl__ZL6solist");
    if (!head || !*head) head = (soinfo_partial**)dlsym(linker, "__dl_g_soinfo_handles_map");
    if (!head) { dlclose(linker); return; }
    uintptr_t our_base = (uintptr_t)di.dli_fbase;
    soinfo_partial* prev = nullptr;
    soinfo_partial* cur  = *head;
    while (cur) {
        if (*(uintptr_t*)((uintptr_t)cur + 0x18) == our_base) {
            if (prev) prev->next = cur->next; else *head = cur->next;
            LOGI("[ENI] bypass L3: soinfo removed");
            break;
        }
        prev = cur; cur = cur->next;
    }
    dlclose(linker);
}

static void RunAllBypasses() {
    prctl(PR_SET_NAME, "AsyncTask #2", 0, 0, 0);
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    LOGI("[ENI] bypass L4+L5 done");
    sleep(2);
    bypass_remap_self();
    bypass_hide_soinfo();
}

// ══════════════════════════════════════════════════════════════════
//  EGL + INPUT HOOKS
// ══════════════════════════════════════════════════════════════════
extern EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface);
static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;

// Input handled via AMotionEvent_getX/Y/Action hooks in touch_input.h

static void InstallEGLHook() {
    void* libegl = dlopen("libEGL.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!libegl) libegl = dlopen("libEGL.so", RTLD_LAZY);
    if (!libegl) { LOGE("[ENI] libEGL dlopen failed"); return; }
    void* sym = dlsym(libegl, "eglSwapBuffers");
    if (!sym) { LOGE("[ENI] eglSwapBuffers not found"); return; }
    int r = DobbyHook(sym, (void*)hook_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    LOGI("[ENI] eglSwapBuffers hook: %s (addr=%p)", r==0?"OK":"FAIL", sym);
}



#include "functions.h"
#include "menu.h"

// ══════════════════════════════════════════════════════════════════
//  HACK THREAD
// ══════════════════════════════════════════════════════════════════
int isGame(JNIEnv *env, jstring appDataDir) {
    if (!appDataDir) return 0;
    const char* d = env->GetStringUTFChars(appDataDir, nullptr);
    int user = 0; static char pkg[256];
    if (sscanf(d, "/data/%*[^/]/%d/%s", &user, pkg) != 2)
        if (sscanf(d, "/data/%*[^/]/%s", pkg) != 1)
            { env->ReleaseStringUTFChars(appDataDir, d); return 0; }
    if (strcmp(pkg, GamePackageName) == 0) {
        game_data_dir = new char[strlen(d)+1]; strcpy(game_data_dir, d);
        env->ReleaseStringUTFChars(appDataDir, d); return 1;
    }
    env->ReleaseStringUTFChars(appDataDir, d); return 0;
}

void *hack_thread(void *arg) {
    RunAllBypasses();
    LOGI("[ENI] hack_thread: scanning for il2cpp...");

    static const char* CANDIDATES[] = { "libunity.so", "libil2cpp.so", "libGameAssembly.so", nullptr };
    int iters = 0;
    while (!g_il2cppBaseMap.isValid() && iters < 90) {
        sleep(1); ++iters;
        for (int i = 0; CANDIDATES[i]; i++) {
            g_il2cppBaseMap = KittyMemory::getLibraryBaseMap(CANDIDATES[i]);
            if (g_il2cppBaseMap.isValid()) { LOGI("[ENI] il2cpp: %s", CANDIDATES[i]); break; }
        }
    }

    if (!g_il2cppBaseMap.isValid()) { LOGE("[ENI] FATAL: il2cpp not found"); return nullptr; }
    LOGI("[ENI] il2cpp base: 0x%" PRIxPTR, (uintptr_t)g_il2cppBaseMap.startAddress);

    Pointers();
    Hooks();
    InstallFeatureHooks();

    // Get touch fd from companion
    InstallMotionHooks(g_companion_sock);

    InstallEGLHook();       // eglSwapBuffers → renders ImGui

    LOGI("[ENI] hack_thread: setup complete!");
    return nullptr;
}
