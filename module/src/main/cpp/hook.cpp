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
#include <sys/mman.h>
#include <sys/prctl.h>
#include <link.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>

#define GamePackageName "com.garena.game.codm"

// ══════════════════════════════════════════════════════════════════════════════
//  STRONG BYPASS — 4-layer stealth
//  1. ELF header wipe        — kills magic-byte scan of our .so in memory
//  2. Anonymous remap        — removes file-backed entry from /proc/self/maps
//  3. Anti-ptrace            — blocks attach-based debuggers / scanners
//  4. Fake prctl name        — masks thread name from process inspectors
// ══════════════════════════════════════════════════════════════════════════════

// ── Layer 1 & 2: remap own library as anonymous ───────────────────────────────
// Technique: copy each file-backed segment of ourselves into a new anonymous
// mapping, then munmap the original.  After this our code is still running but
// nothing with a path shows in /proc/self/maps — same approach as reveny's
// "remap hide" in Android-ImGui-Mod-Menu.
struct LibRegion { uintptr_t start, end; };

static int CollectSelfRegions_cb(dl_phdr_info* info, size_t, void* data) {
    // Match the segment that contains our own code
    Dl_info di;
    extern void bypass_remap_self(); // forward
    if (!dladdr((void*)bypass_remap_self, &di)) return 0;
    if (!di.dli_fbase || (uintptr_t)di.dli_fbase != info->dlpi_addr) return 0;

    std::vector<LibRegion>* regions = (std::vector<LibRegion>*)data;
    for (int i = 0; i < (int)info->dlpi_phnum; i++) {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        uintptr_t seg_start = info->dlpi_addr + ph.p_vaddr;
        uintptr_t seg_end   = seg_start + ph.p_memsz;
        // align to page
        seg_start &= ~(uintptr_t)(getpagesize() - 1);
        seg_end    = (seg_end + getpagesize() - 1) & ~(uintptr_t)(getpagesize() - 1);
        regions->push_back({seg_start, seg_end});
    }
    return 1; // stop iteration
}

void bypass_remap_self() {
    std::vector<LibRegion> regions;
    dl_iterate_phdr(CollectSelfRegions_cb, &regions);

    for (auto& r : regions) {
        size_t sz = r.end - r.start;
        // Allocate anonymous backing
        void* anon = mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (anon == MAP_FAILED) continue;

        // Copy content
        memcpy(anon, (void*)r.start, sz);

        // Wipe ELF magic in original (Layer 1: magic-byte scan defeated)
        // Only on the first (lowest) segment which holds the ELF header
        if (r.start == regions[0].start) {
            mprotect((void*)r.start, getpagesize(), PROT_READ | PROT_WRITE);
            memset((void*)r.start, 0, 4); // zero ELF magic "\x7fELF"
        }

        // Unmap original file-backed pages (Layer 2: removed from /proc/maps)
        munmap((void*)r.start, sz);

        // Map our copy at the same address with RX permissions
        void* remapped = mmap((void*)r.start, sz,
                              PROT_READ | PROT_EXEC,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                              -1, 0);
        if (remapped == MAP_FAILED) {
            // If fixed-map fails, copy back to anon (code still runs from anon)
            mprotect(anon, sz, PROT_READ | PROT_EXEC);
        } else {
            memcpy(remapped, anon, sz);
            mprotect(remapped, sz, PROT_READ | PROT_EXEC);
            munmap(anon, sz);
        }
    }
    LOGI("[ENI] bypass: library remapped as anonymous (hidden from /proc/maps)");
}

// ── Layer 3: anti-ptrace ──────────────────────────────────────────────────────
// PR_SET_DUMPABLE 0 prevents any external process from ptrace-attaching to us.
// CODM anti-cheat uses ptrace to inspect memory regions of suspicious threads.
static void bypass_anti_ptrace() {
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    LOGI("[ENI] bypass: PR_SET_DUMPABLE=0 (anti-ptrace active)");
}

// ── Layer 4: thread name spoofing ────────────────────────────────────────────
// Rename hack_thread to something benign so thread inspectors see nothing odd.
static void bypass_spoof_thread_name() {
    prctl(PR_SET_NAME, "FinalizerDaemon", 0, 0, 0);
    LOGI("[ENI] bypass: thread name spoofed to FinalizerDaemon");
}

// ── Master bypass entry — call once at start of hack_thread ──────────────────
static void RunAllBypasses() {
    bypass_spoof_thread_name();
    bypass_anti_ptrace();
    // Remap runs last: after this our .so disappears from maps
    // Small sleep ensures il2cpp is loaded before we remap (avoids map race)
    sleep(2);
    bypass_remap_self();
}

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
    // ── Stealth first, everything else second ─────────────────────────────
    RunAllBypasses();
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
    InstallFeatureHooks();

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
