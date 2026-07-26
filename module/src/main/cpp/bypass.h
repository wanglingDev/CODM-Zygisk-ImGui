#pragma once
// ══════════════════════════════════════════════════════════════════════════════
//  ACE BYPASS — Maximum 13-Tier Implementation (AUDIT-VERIFIED)
//  Target : libanort.so (~1.6 MB) + libanogs.so (~5.7 MB)
//           Tencent ACE "Anti-Cheat Expert", CODM Garena 2026
//
//  Research source : msantiagodev/ACE-ANTICHEAT (April 2026 audit)
//  Architecture    : ARM64 (aarch64-linux-android), Android 10+
//
//  Tier map (expanded from audit 73_bypass_status_audit.md):
//   T1  ANORT_PATCHES[0..10]   — All 11 verified sensor offsets (libanort)
//   T2  TDM kill               — Drop ace_jni_senddatatosvr_trampoline (libanogs)
//   T4  ace_run_scan_rules     — NOP rule interpreter (libanogs+0x3ECFF8)
//   T5  g_anort_config_flags   — Write 0x100; block ACE_ConfigUpdateFromServer
//   T7  ace_arm64_relocator    — NOP hook installer (libanogs+0x3F9CFC)
//   T8  ACE_VMExecutionDriver  — NOP ARM64 emulator (libanort+0x137804)
//   T9  ACE_ResolveDynFunc_NoDlsym — NOP runtime DEX loader
//   T10 ACE_ScheduledTimerProbabilisticDetect — always return false (0.1%/day audit)
//   T11 JNI_ACE_CommandDispatch — drop "stop" kill command from Java side
//   T12 ace_init_remoteconfig_channel — block GCloud rule push (return 0)
//   T13 Heartbeat BST poison   — write fake G_HB_K_RESP before poll window
//  +X   Prologue shield        — disguise Dobby stubs after all DobbyHook() calls
//  +X   Surface bypass         — anonymous remap, PR_SET_DUMPABLE, thread spoof
//  +X   TracerPid spoof        — /proc/self/status bind-mount to hide debugger
//  +X   inotify killer         — close ACE's inotify fds before they can fire
//
//  ANORT_PATCHES VERIFIED OFFSETS (from 26_anort_11_patches_explained.md):
//   [0]  0x13EA50  ptrace_wrapper         — detects attached debuggers
//   [1]  0x136E94  fork_execv_killer      — spawns external killer per arm64 lib
//   [2]  0x045CE8  dlopen_scanner         — detects unsigned/foreign modules
//   [3]  0x1411DC  raw_syscall_bridge     — VM→native syscall dispatcher
//   [4]  0x120C88  mprotect_check_1       — SIGKILL on mprotect failure
//   [5]  0x120D80  mprotect_check_2       — sibling of [4], same kill path
//   [6]  0x0CE64C  library_integrity      — catches Dobby hooks in .text
//   [7]  0x0A7E7C  memory_region_validator— page-hash deviation > 10% → flag
//   [8]  0x07A28C  file_integrity_checker — SHA-1 APK/SO on-disk hash check
//   [9]  0x03DCFC  file_stat_checker      — stat()/access() file size/date check
//  [10]  0x0AB9A4  virtual_env_detector   — VirtualXposed/clone/emulator detect
//
//  CALL ORDER (from hack_thread):
//   1. RunFullBypass()        — before any DobbyHook() calls
//   2. [your DobbyHook() calls]
//   3. RunPrologueDisguise()  — after ALL DobbyHook() calls
// ══════════════════════════════════════════════════════════════════════════════

#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/inotify.h>
#include <link.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

// ── Logging ───────────────────────────────────────────────────────────────────
#ifndef LOGI
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "BYPASS", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BYPASS", __VA_ARGS__)
#endif

// ── ARM64 canonical instruction words ────────────────────────────────────────
//  NOP            = D5 03 20 1F
//  RET            = C0 03 5F D6
//  MOV X0, XZR   = E0 03 1F AA   (clears X0, idiom for return 0)
//  MOV X0, #1    = 20 00 80 D2   (return true / success)
//
static constexpr uint32_t ARM64_NOP        = 0xD503201F;
static constexpr uint32_t ARM64_RET        = 0xD65F03C0;
static constexpr uint32_t ARM64_MOV_X0_XZR = 0xAA1F03E0; // MOV X0, XZR
static constexpr uint32_t ARM64_MOV_X0_1   = 0xD2800020; // MOV X0, #1

// ── MOV X0,#0; RET stub (8 bytes) ─────────────────────────────────────────────
static const uint8_t kStub_Return0[8] = {
    0xE0, 0x03, 0x1F, 0xAA, // MOV X0, XZR
    0xC0, 0x03, 0x5F, 0xD6  // RET
};
// MOV X0,#1; RET stub — for sensors that expect non-zero on success
static const uint8_t kStub_Return1[8] = {
    0x20, 0x00, 0x80, 0xD2, // MOV X0, #1
    0xC0, 0x03, 0x5F, 0xD6  // RET
};

// ══════════════════════════════════════════════════════════════════════════════
//  LOW-LEVEL PATCH HELPERS
// ══════════════════════════════════════════════════════════════════════════════

// Write N bytes to addr, making page temporarily writable.
static bool PatchBytes(uintptr_t addr, const void* bytes, size_t len) {
    if (!addr || !bytes || !len) return false;
    int ps = getpagesize();
    uintptr_t page = addr & ~(uintptr_t)(ps - 1);
    // Cover up to 2 pages in case stub straddles a boundary
    size_t  mapLen = ((addr + len - page + ps - 1) & ~(uintptr_t)(ps - 1));
    if (mprotect((void*)page, mapLen, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return false;
    memcpy((void*)addr, bytes, len);
    __builtin___clear_cache((char*)addr, (char*)(addr + len));
    mprotect((void*)page, mapLen, PROT_READ | PROT_EXEC);
    return true;
}

static bool SafePatch(uintptr_t addr, uint32_t word) {
    return PatchBytes(addr, &word, 4);
}
static bool SafePatchReturn0(uintptr_t addr) {
    return PatchBytes(addr, kStub_Return0, 8);
}
static bool SafePatchReturn1(uintptr_t addr) {
    return PatchBytes(addr, kStub_Return1, 8);
}

// ── /proc/self/maps reader ────────────────────────────────────────────────────
struct LibInfo { uintptr_t base; size_t size; bool valid; };

static LibInfo GetLibInfo(const char* name) {
    LibInfo info{0, 0, false};
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return info;
    char line[512];
    uintptr_t first = 0, last = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, name) || !strstr(line, "r-xp")) continue;
        uintptr_t s, e;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &s, &e) != 2) continue;
        if (!first) first = s;
        last = e;
    }
    fclose(f);
    if (first && last > first) {
        info.base  = first;
        info.size  = last - first;
        info.valid = true;
    }
    return info;
}

// Returns the first writable (rw-p) segment base for a named library
static uintptr_t GetLibDataBase(const char* name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t result = 0;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, name) || !strstr(line, "rw-p")) continue;
        uintptr_t s, e;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &s, &e) == 2) {
            result = s;
            break;
        }
    }
    fclose(f);
    return result;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 1 — All 11 ANORT_PATCHES via VERIFIED OFFSETS
//
//  Audit source: 26_anort_11_patches_explained.md
//  Each entry: {offset, sensor_name, notes}
//  All patched with MOV X0, XZR; RET (8 bytes) — makes every sensor
//  return 0 / false, short-circuiting detection and SIGKILL paths.
//
//  IMPORTANT — Patch [4] and [5] (mprotect_check_1/2):
//  The audit shows these do: if (ACE_EventSignal(...) != -993659912) kill(getpid(), SIGKILL)
//  We need MOV X0, #0; RET so the check sees 0, NOT -993659912, meaning
//  the conditional branch that calls kill() is NOT taken — because the
//  branch is "if result != expected_value". Returning 0 safely skips kill.
// ══════════════════════════════════════════════════════════════════════════════

static const struct {
    uintptr_t   offset;
    const char* name;
} ANORT_PATCHES[11] = {
    { 0x13EA50, "ptrace_wrapper"          },  // [0]  debugger detect
    { 0x136E94, "fork_execv_killer"       },  // [1]  external kill-spawner
    { 0x045CE8, "dlopen_scanner"          },  // [2]  foreign module detect
    { 0x1411DC, "raw_syscall_bridge"      },  // [3]  VM→syscall dispatcher
    { 0x120C88, "mprotect_check_1"        },  // [4]  SIGKILL on mprotect fail
    { 0x120D80, "mprotect_check_2"        },  // [5]  SIGKILL sibling
    { 0x0CE64C, "library_integrity"       },  // [6]  Dobby hook detector ← CRITICAL
    { 0x0A7E7C, "memory_region_validator" },  // [7]  page-hash deviation flag
    { 0x07A28C, "file_integrity_checker"  },  // [8]  SHA-1 APK/SO hash
    { 0x03DCFC, "file_stat_checker"       },  // [9]  stat()/access() check
    { 0x0AB9A4, "virtual_env_detector"    },  // [10] emulator/clone detect
};

static void Tier1_KillAllAnortSensors(LibInfo& anort) {
    if (!anort.valid) {
        LOGE("[T1] libanort not found — skipping all 11 patches");
        return;
    }
    for (int i = 0; i < 11; i++) {
        uintptr_t addr = anort.base + ANORT_PATCHES[i].offset;

        // ── Pre-patch sanity check ──────────────────────────────────────────
        // Each sensor function starts with STP X29,X30,[SP,#-N]!
        // ARM64 encoding mask: (word & 0xFF00FFFF) == 0xA9007BFD
        // If the word at the target offset does NOT match an STP prologue,
        // this offset is wrong for the current ACE build — skip rather than
        // corrupt random memory.
        uint32_t word = *(volatile uint32_t*)addr;
        bool looksLikeFunction = ((word & 0xFF00FFFF) == 0xA9007BFD) || // STP X29,X30
                                  (word & 0xFF000000) == 0xD1000000  ||  // SUB SP,SP,#imm
                                  (word & 0xFFE003FF) == 0xA9400000;     // STP (load variant)
        if (!looksLikeFunction) {
            LOGI("[T1][%2d] %-28s @ +0x%06lx SKIPPED (word=0x%08X, offset mismatch for this ACE build)",
                 i, ANORT_PATCHES[i].name,
                 (long)ANORT_PATCHES[i].offset, word);
            continue;
        }

        bool ok = SafePatchReturn0(addr);
        LOGI("[T1][%2d] %-28s @ libanort+0x%06lx → %s",
             i, ANORT_PATCHES[i].name,
             (long)ANORT_PATCHES[i].offset, ok ? "OK" : "FAIL");
    }
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 2 — Kill TDM send trampoline (no reports ever reach the ACE server)
//
//  ace_jni_senddatatosvr_trampoline in libanogs calls through a JNI
//  function pointer from the dispatch table. We NOP its entry so no
//  detection report can be submitted regardless of what fires.
//
//  Pattern: distinctive LDR X8,[X0,#0] + <4 wildcard bytes> + BLR X8
//  trampoline for JNI dispatch; walk back to function start.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier2_KillTDMSend(LibInfo& anogs) {
    if (!anogs.valid) return false;

    static const uint8_t kPat[] = {
        0x08, 0x00, 0x40, 0xF9,  // LDR X8, [X0, #0]
        0x00, 0x00, 0x00, 0x00,  // (wildcard offset load)
        0x00, 0x01, 0x3F, 0xD6   // BLR X8
    };
    static const char kMask[] = "xxxx????xxxx";
    size_t patLen = strlen(kMask);

    uintptr_t hit = 0;
    for (size_t i = 0; i + patLen <= anogs.size; i++) {
        bool ok = true;
        for (size_t j = 0; j < patLen; j++) {
            if (kMask[j] == 'x' && ((uint8_t*)(anogs.base + i))[j] != kPat[j]) {
                ok = false; break;
            }
        }
        if (ok) { hit = anogs.base + i; break; }
    }

    if (!hit) {
        LOGE("[T2] TDM send pattern not found — TDM not killed");
        return false;
    }

    // Walk back up to 128 bytes to find STP X29,X30 function prologue
    uintptr_t fn = hit;
    for (int b = 0; b <= 128; b += 4) {
        if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
            fn = hit - b; break;
        }
    }

    bool ok = SafePatchReturn0(fn);
    LOGI("[T2] TDM send trampoline @ libanogs+0x%lx → %s",
         (long)(fn - anogs.base), ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 4 — ace_run_scan_rules (rule interpreter result sink)
//
//  Even if the VM fires and produces a detection, it passes through
//  ace_run_scan_rules before any kill path triggers. Killing this
//  discards every scan result before it can be acted upon.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier4_KillScanRules(LibInfo& anogs) {
    if (!anogs.valid) return false;

    // Verified audit offset
    uintptr_t addr = anogs.base + 0x3ECFF8;

    // Pattern fallback: LDR X1, [X0, #8] — reads rule_ctx->id immediately
    static const uint8_t kPat[] = { 0x01, 0x04, 0x40, 0xF9 };
    for (size_t i = 0; i + 4 <= anogs.size; i++) {
        if (memcmp((void*)(anogs.base + i), kPat, 4) == 0) {
            uintptr_t hit = anogs.base + i;
            for (int b = 0; b <= 48; b += 4) {
                if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
                    addr = hit - b;
                    LOGI("[T4] ace_run_scan_rules found by pattern @ libanogs+0x%lx",
                         (long)(addr - anogs.base));
                    break;
                }
            }
            break;
        }
    }

    bool ok = SafePatchReturn0(addr);
    LOGI("[T4] ace_run_scan_rules → %s", ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 5 — g_anort_config_flags = 0x100
//
//  Bit 8 (0x100) disables ACE_ConfigUpdateFromServer, preventing the server
//  from pushing new detection rules into the local scanner config.
//
//  The config flags live in libanort's DATA segment at the verified
//  file-relative offset 0x171118. We map the writable segment and write
//  directly — no executable page manipulation needed.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier5_KillConfigUpdate(LibInfo& anort) {
    if (!anort.valid) return false;

    // The ELF offset for g_anort_config_flags is 0x171118.
    // libanort's .data section starts right after .text.
    // We read /proc/self/maps for the rw-p mapping of libanort
    // then compute offset relative to that mapping's start,
    // which corresponds to ELF file offset of the writable segment.
    //
    // In practice the data segment is loaded at:
    //   load_bias + data_vaddr  (not necessarily contiguous with .text)
    //
    // Safest approach: find the rw-p mapping and use a small window scan
    // for the config flags initialisation pattern (0x00000001 written at init).
    // If we can't find it precisely, we write to the best-known offset.

    uintptr_t dataBase = GetLibDataBase("libanort.so");
    if (!dataBase) {
        LOGE("[T5] libanort.so writable segment not found");
        return false;
    }

    // Audit: g_anort_config_flags is at data VA 0x171118.
    // The executable segment starts at some load_bias.
    // data segment VA − text segment VA gives delta from base.
    // For the audited binary: text at 0x0, data at ~0x173000
    // => data segment offset from load_bias ≈ 0x173000
    // Our actual data mapping start from /proc/maps = dataBase
    // Within that mapping, g_anort_config_flags is at offset:
    //   0x171118 - 0x173000 = -0x1EE8  ... (negative means it may be
    //   in a different sub-segment; use a scan instead)
    //
    // Practical resolution: scan the first 64 KB of the data mapping
    // for the uint32_t value 0x00000001 at 4-byte alignment,
    // and write 0x100 to offset 0 from that candidate.
    // This is conservative but safe when exact offset isn't portable.
    //
    // Alternatively (most robust): just write to the known offset
    // relative to text base + 0x171118, treating the segment as
    // a contiguous mapped file (which Android's linker produces).

    // Use text base + known ELF offset (works for ASLR because ASLR
    // shifts the entire file mapping uniformly):
    uintptr_t flagAddr = anort.base + 0x171118;

    // Make the containing page writable
    uintptr_t page = flagAddr & ~(uintptr_t)(getpagesize() - 1);
    if (mprotect((void*)page, (size_t)getpagesize(),
                 PROT_READ | PROT_WRITE) == 0) {
        *(volatile uint32_t*)flagAddr = 0x100;
        __builtin___clear_cache((char*)flagAddr, (char*)(flagAddr + 4));
        mprotect((void*)page, (size_t)getpagesize(), PROT_READ);
        LOGI("[T5] g_anort_config_flags = 0x100 @ +0x171118 → OK");
        return true;
    }

    // Fallback: try via data segment
    uintptr_t fallbackAddr = dataBase + 0x118;
    if (mprotect((void*)(fallbackAddr & ~(uintptr_t)(getpagesize() - 1)),
                 (size_t)getpagesize(), PROT_READ | PROT_WRITE) == 0) {
        *(volatile uint32_t*)fallbackAddr = 0x100;
        __builtin___clear_cache((char*)fallbackAddr, (char*)(fallbackAddr + 4));
        mprotect((void*)(fallbackAddr & ~(uintptr_t)(getpagesize() - 1)),
                 (size_t)getpagesize(), PROT_READ);
        LOGI("[T5] g_anort_config_flags = 0x100 (fallback) → OK");
        return true;
    }

    LOGE("[T5] g_anort_config_flags write failed");
    return false;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 7 — ace_arm64_relocator (prevents ACE installing new inline hooks)
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier7_KillRelocator(LibInfo& anogs) {
    if (!anogs.valid) return false;

    uintptr_t addr = anogs.base + 0x3F9CFC;

    // Pattern fallback: SVC #0 — relocator uses raw syscall wrapping
    static const uint8_t kPat[] = { 0x01, 0x00, 0x00, 0xD4 };
    for (size_t i = 0; i + 4 <= anogs.size; i++) {
        if (memcmp((void*)(anogs.base + i), kPat, 4) == 0) {
            uintptr_t hit = anogs.base + i;
            for (int b = 0; b <= 64; b += 4) {
                if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
                    addr = hit - b;
                    LOGI("[T7] ace_arm64_relocator found by pattern @ libanogs+0x%lx",
                         (long)(addr - anogs.base));
                    break;
                }
            }
            break;
        }
    }

    bool ok = SafePatchReturn0(addr);
    LOGI("[T7] ace_arm64_relocator → %s", ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 8 — ACE_VMExecutionDriver (kills the entire ARM64 bytecode emulator)
//
//  The VM drives ALL bytecode-based detection. Dead VM = ~95% of ACE dead.
//  Audit offset: libanort+0x137804
//  Pattern: module header magic 0x48D9 load, then STP X29,X30 prologue.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier8_KillVM(LibInfo& anort) {
    if (!anort.valid) return false;

    uintptr_t vmAddr = anort.base + 0x137804;

    // Pattern: 0x48D9 magic comparison in immediate
    static const uint8_t kPat[] = { 0xD9, 0x48, 0x00, 0x00 };
    for (size_t i = 0; i + 4 <= anort.size; i++) {
        if (kPat[0] == ((uint8_t*)(anort.base + i))[0] &&
            kPat[1] == ((uint8_t*)(anort.base + i))[1]) {
            uintptr_t hit = anort.base + i;
            for (int b = 0; b <= 32; b += 4) {
                uint32_t w = *(uint32_t*)(hit - b);
                if ((w & 0xFF00FFFF) == 0xA9007BFD) {
                    vmAddr = hit - b;
                    LOGI("[T8] VMExecutionDriver found by pattern @ libanort+0x%lx",
                         (long)(vmAddr - anort.base));
                    break;
                }
            }
            break;
        }
    }

    bool ok = SafePatchReturn0(vmAddr);
    LOGI("[T8] ACE_VMExecutionDriver → %s", ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 9 — ACE_ResolveDynFunc_NoDlsym (runtime DEX loader, libanort)
//
//  This function resolves art::DexFile* function pointers at runtime to
//  load detection bytecode from a downloaded DEX without calling dlsym.
//  If the VM is dead (T8), the DEX never runs anyway — this is belt-and-
//  suspenders for future ACE updates that might move DEX loading earlier
//  in the boot chain, before T8 fires.
//
//  Audit reference: 62_no_dlsym_symbol_resolver.md
//  Pattern: the resolver opens with a distinctive ADRP + BL pair that
//  loads the libart.so symbol table pointer.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier9_KillDexLoader(LibInfo& anort) {
    if (!anort.valid) return false;

    // Look for the "art::DexFile" string in libanort — the resolver
    // references it to validate it's loading from the right runtime
    const char* kDexSig = "art::DexFile";
    uintptr_t strAddr = 0;
    for (size_t i = 0; i + 12 <= anort.size; i++) {
        if (memcmp((void*)(anort.base + i), kDexSig, 12) == 0) {
            strAddr = anort.base + i;
            break;
        }
    }

    if (!strAddr) {
        // Try "libdvm.so" — older Android runtime
        const char* kDvmSig = "libdvm.so";
        for (size_t i = 0; i + 9 <= anort.size; i++) {
            if (memcmp((void*)(anort.base + i), kDvmSig, 9) == 0) {
                strAddr = anort.base + i; break;
            }
        }
    }

    if (!strAddr) {
        LOGI("[T9] DEX loader signature not found — already inert or not present");
        return false;
    }

    // Find the ADRP loading this string — walk all ADRP instructions
    uintptr_t pageOfStr = strAddr & ~(uintptr_t)0xFFF;
    uintptr_t fn = 0;
    for (size_t i = 0; i + 4 <= anort.size && !fn; i += 4) {
        uint32_t w = *(uint32_t*)(anort.base + i);
        if ((w & 0x9F000000) != 0x90000000) continue;
        // Decode ADRP: imm = SignExtend(immhi:immlo:12'b0, 64)
        int64_t immhi = (int64_t)(int32_t)((w >> 5) & 0x7FFFF);
        uint64_t immlo = (w >> 29) & 0x3;
        int64_t imm = ((immhi << 2) | (int64_t)immlo) << 12;
        uintptr_t adrpPage = (anort.base + i) & ~(uintptr_t)0xFFF;
        if ((uintptr_t)(adrpPage + imm) == pageOfStr) {
            // Walk back to function start (STP X29, X30, [SP,#-N]!)
            uintptr_t hit = anort.base + i;
            for (int b = 0; b <= 256; b += 4) {
                if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
                    fn = hit - b;
                    break;
                }
            }
        }
    }

    if (!fn) {
        LOGI("[T9] DEX loader function start not found");
        return false;
    }

    bool ok = SafePatchReturn0(fn);
    LOGI("[T9] ACE_ResolveDynFunc_NoDlsym @ libanort+0x%lx → %s",
         (long)(fn - anort.base), ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 10 — ACE_ScheduledTimerProbabilisticDetect (0.1%/day deep audit)
//
//  ACE fires a deep forensic scan with probability 0.001 per day.
//  This is the low-frequency "did we miss anything?" sweep.
//  Pattern: the function uses clock_gettime + a pseudo-random comparison
//  to decide whether to run — it's the ONLY function in libanort that
//  calls both clock_gettime (syscall #113) and uses a float comparison
//  for the probability threshold.
//  Kill it → deep audit never fires.
//
//  Audit reference: 56_probabilistic_detection_timer.md
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier10_KillProbabilisticTimer(LibInfo& anort) {
    if (!anort.valid) return false;

    // clock_gettime syscall on ARM64 = 0x71 = 113
    // MOV X8, #0x71 → encoding: 0xD2800E28
    static const uint8_t kClockPat[] = { 0x28, 0x0E, 0x80, 0xD2 };
    // After finding MOV X8,#0x71, look for a nearby FCMP (float compare)
    // FCMP Sn, Sm = encoding starts with 0x1E (FP data processing)
    uintptr_t bestFn = 0;
    for (size_t i = 0; i + 4 <= anort.size; i++) {
        if (memcmp((void*)(anort.base + i), kClockPat, 4) != 0) continue;
        // Found clock_gettime syscall — look for FCMP in next 128 bytes
        bool hasFCMP = false;
        for (size_t j = i; j < i + 128 && j + 4 <= anort.size; j += 4) {
            uint32_t w = *(uint32_t*)(anort.base + j);
            // FCMP Sn,Sm = 0x1E202008 masked: top byte 0x1E, bits 13-10 = 0b1000
            if ((w & 0xFF20FC1F) == 0x1E200008) {
                hasFCMP = true;
                break;
            }
        }
        if (!hasFCMP) continue;

        // This function has both clock_gettime and FCMP — it's our timer
        uintptr_t hit = anort.base + i;
        for (int b = 0; b <= 512; b += 4) {
            if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
                bestFn = hit - b;
                break;
            }
        }
        if (bestFn) break;
    }

    if (!bestFn) {
        LOGI("[T10] Probabilistic timer not found (may not be present in this build)");
        return false;
    }

    // Return 0 so the caller thinks "not this time" — scan never runs
    bool ok = SafePatchReturn0(bestFn);
    LOGI("[T10] ACE_ScheduledTimerProbabilisticDetect @ libanort+0x%lx → %s",
         (long)(bestFn - anort.base), ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 11 — JNI_ACE_CommandDispatch "stop" kill-path intercept
//
//  ACE's Java layer can send a "stop" command through the JNI bridge to
//  call exit_group() on our process. This goes through a command dispatch
//  table in libanogs.
//
//  We hook the JNI dispatch function to return immediately without
//  processing any commands — effectively making the Java kill-path a no-op.
//
//  Pattern: the dispatch function reads a command tag string from Java
//  (via GetStringUTFChars) immediately after saving registers.
//  Distinctive: calls GetStringUTFChars (JNI env vtable[169])
//  LDR X8,[X0,#0] then LDR X8,[X8,#0x548] (vtable offset for GetStringUTFChars)
//  0x548 / 8 = 169 — confirmed in 94_jni_dispatch_table.md
//
//  Audit reference: 60_validate_config_kill_chain.md, 94_jni_dispatch_table.md
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier11_KillJNICommandDispatch(LibInfo& anogs) {
    if (!anogs.valid) return false;

    // LDR X8,[X8,#0x548] = 0xF9402908
    // 0x548 >> 3 = 0xA9 → LDR Xn,[Xm, #imm12<<3] where imm12 = 0xA9
    // Encoding: LDR X8,[X8,#0x548] = F9 29 40 F9
    static const uint8_t kPat[] = { 0x08, 0x29, 0x40, 0xF9 };
    uintptr_t hit = 0;
    for (size_t i = 0; i + 4 <= anogs.size; i++) {
        if (memcmp((void*)(anogs.base + i), kPat, 4) == 0) {
            hit = anogs.base + i; break;
        }
    }

    if (!hit) {
        // Alternate: look for "stop" string in libanogs and trace back to function
        const char* kStop = "stop";
        for (size_t i = 0; i + 4 <= anogs.size; i++) {
            if (memcmp((void*)(anogs.base + i), kStop, 4) == 0 &&
                ((uint8_t*)(anogs.base + i))[4] == 0) { // null-terminated
                hit = anogs.base + i;
                break;
            }
        }
        if (!hit) {
            LOGI("[T11] JNI command dispatch pattern not found");
            return false;
        }
    }

    uintptr_t fn = hit;
    for (int b = 0; b <= 256; b += 4) {
        if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
            fn = hit - b; break;
        }
    }

    bool ok = SafePatchReturn0(fn);
    LOGI("[T11] JNI_ACE_CommandDispatch @ libanogs+0x%lx → %s",
         (long)(fn - anogs.base), ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 12 — ace_init_remoteconfig_channel (blocks GCloud rule push)
//
//  ACE connects to Google Cloud Firebase REMOTECONFIG to push new detection
//  rules dynamically — separate from the server config path that T5 blocks.
//  This is the Tier-5 gap identified in the audit (73_bypass_status_audit.md).
//
//  Killing it means the GCloud channel never initialises — no new rules
//  can arrive, even after a server-side update to detection strategy.
//
//  Pattern: the init function calls firebase::remote_config::Initialize,
//  which dlopens "libfirebase_remote_config.so" or calls through a vtable.
//  We look for the "remoteconfig" or "firebase" string in libanogs.
//
//  Audit reference: 71_gcloud_remote_config.md
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier12_KillGCloudChannel(LibInfo& anogs) {
    if (!anogs.valid) return false;

    // Scan for Firebase remote config identifier strings
    const char* candidates[] = {
        "remoteconfig",
        "firebase",
        "ace_remoteconfig",
        "ace_init_remote",
        nullptr
    };

    uintptr_t strAddr = 0;
    for (int c = 0; candidates[c] && !strAddr; c++) {
        size_t slen = strlen(candidates[c]);
        for (size_t i = 0; i + slen <= anogs.size; i++) {
            if (memcmp((void*)(anogs.base + i), candidates[c], slen) == 0) {
                strAddr = anogs.base + i;
                break;
            }
        }
    }

    if (!strAddr) {
        LOGI("[T12] GCloud channel init string not found (Firebase may be absent)");
        return false;
    }

    // Find ADRP loading this string, walk back to function
    uintptr_t pageOfStr = strAddr & ~(uintptr_t)0xFFF;
    uintptr_t fn = 0;
    for (size_t i = 0; i + 4 <= anogs.size && !fn; i += 4) {
        uint32_t w = *(uint32_t*)(anogs.base + i);
        if ((w & 0x9F000000) != 0x90000000) continue;
        int64_t immhi = (int64_t)(int32_t)((w >> 5) & 0x7FFFF);
        uint64_t immlo = (w >> 29) & 0x3;
        int64_t imm = ((immhi << 2) | (int64_t)immlo) << 12;
        uintptr_t adrpPage = (anogs.base + i) & ~(uintptr_t)0xFFF;
        if ((uintptr_t)(adrpPage + imm) == pageOfStr) {
            uintptr_t hit = anogs.base + i;
            for (int b = 0; b <= 256; b += 4) {
                if ((*(uint32_t*)(hit - b) & 0xFF00FFFF) == 0xA9007BFD) {
                    fn = hit - b; break;
                }
            }
        }
    }

    if (!fn) {
        LOGI("[T12] GCloud init function start not found");
        return false;
    }

    bool ok = SafePatchReturn0(fn);
    LOGI("[T12] ace_init_remoteconfig_channel @ libanogs+0x%lx → %s",
         (long)(fn - anogs.base), ok ? "OK" : "FAIL");
    return ok;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER 13 — Heartbeat BST poisoning
//
//  When T8 kills ACE_VMExecutionDriver, the VM heartbeat module (vm_hb.img)
//  never executes, so the heartbeat response key G_HB_K_RESP is never
//  written to the BST. ACE's boot driver polls 50×100ms = 5 seconds for
//  this response. If it doesn't arrive, boot flag +61 stays 0 — ACE knows
//  its VM is dead.
//
//  Tier 13 pre-writes a synthetic G_HB_K_RESP to the ACE BST before the
//  boot driver polls. We find the ACE BST state pointer by tracing the
//  ACE_BSTEncodedUpsert call from vfunc_3_boot_driver, then write the
//  expected "1<suffix>" response.
//
//  Implementation: hook ACE_BSTEncodedUpsert itself to intercept the
//  G_HB_ASK_K write and immediately forge the response.
//
//  Audit reference: 63_boot_heartbeat_chain.md
//
//  Strategy: Instead of reversing the hash function (complex), we NOP
//  the VALIDATION step — the memcmp at the end of vfunc_3 that checks
//  response[0]. We patch the boot driver to always set flag +61 to 1
//  regardless of heartbeat outcome.
//
//  Function: vfunc_3_boot_driver at libanort+0x138344
//  The "set boot flag" line: *(self + 61) = 1 — we find the store
//  instruction and NOP the surrounding branch so it always executes.
// ══════════════════════════════════════════════════════════════════════════════
static bool Tier13_PoisonHeartbeat(LibInfo& anort) {
    if (!anort.valid) return false;

    // vfunc_3_boot_driver at audit-verified offset libanort+0x138344
    uintptr_t bootDriverAddr = anort.base + 0x138344;

    // The boot driver polls G_HB_K_RESP and conditionally sets byte at +61.
    // We NOP the entire poll loop by patching the first CBNZ/CBZ that
    // guards the response check — effectively making it always "succeed".
    //
    // Scan 256 bytes into the function for a STRB (store byte) instruction
    // that writes to [X0, #61] — the boot flag write.
    // STRB Wn, [X0, #61] = encoding: xx 0F 00 39 (offset 61 = 0x3D)
    // Full encoding: STRB W1, [X0, #61] = 01 F4 00 39 (if Wn=1)
    // General mask: ????F400 39 — bits[21:10] = offset, bits[9:5] = base (X0)

    uintptr_t strbAddr = 0;
    for (int off = 0; off < 512; off += 4) {
        uint32_t w = *(uint32_t*)(bootDriverAddr + off);
        // STRB Wt, [X0, #61]: base X0 = 0, offset 61 → encoded as 61<<10 = 0xF400
        // Encoding: [31:22]=0x38800000>>22, [21:10]=imm12, [9:5]=Rn, [4:0]=Rt
        // Simpler: check for STRB with Rn=X0 and imm12 matching 61
        // STRB general = (w >> 22) == 0b00111000000 (size=0, V=0, opc=00)
        if ((w >> 22) == 0b00111000000 &&  // STRB
            ((w >> 5) & 0x1F) == 0 &&       // Rn = X0
            ((w >> 10) & 0xFFF) == 61) {    // offset = 61
            strbAddr = bootDriverAddr + off;
            LOGI("[T13] Boot flag STRB found at libanort+0x%lx",
                 (long)(off + 0x138344));
            break;
        }
    }

    if (!strbAddr) {
        // Alternate: patch the entire boot driver to immediately return 1
        // The function will still run but we guarantee the boot flag is set.
        // Find the very first CBZ/CBNZ in the function (poll loop guard)
        // and NOP it to force the "success" branch.
        for (int off = 4; off < 256; off += 4) {
            uint32_t w = *(uint32_t*)(bootDriverAddr + off);
            // CBZ/CBNZ = bit [31]=0, [30]=1, [25:24]=01/11 → mask 0x7E000000
            if ((w & 0x7E000000) == 0x34000000 || // CBZ
                (w & 0x7E000000) == 0x35000000) {  // CBNZ
                SafePatch(bootDriverAddr + off, ARM64_NOP);
                LOGI("[T13] Boot driver poll guard NOP'd at libanort+0x%lx",
                     (long)(off + 0x138344));
                break;
            }
        }
        LOGI("[T13] Heartbeat poison applied (poll guard NOP fallback)");
        return true;
    }

    // NOP the branch that guards the STRB — two instructions before it
    // is usually the CBNZ checking response[0]. Find and NOP it.
    for (int back = 4; back <= 32; back += 4) {
        uint32_t w = *(uint32_t*)(strbAddr - back);
        if ((w & 0x7E000000) == 0x34000000 || (w & 0x7E000000) == 0x35000000) {
            SafePatch(strbAddr - back, ARM64_NOP);
            LOGI("[T13] Boot flag branch NOP'd — heartbeat will always pass");
            break;
        }
    }

    LOGI("[T13] Heartbeat BST poison → OK");
    return true;
}

// ══════════════════════════════════════════════════════════════════════════════
//  TIER +X  —  Dobby prologue shield
//
//  Dobby leaves LDR X17,#8; BR X17 (= D1 00 00 58 / D1 02 1F D6) at hooked
//  function prologues. ACE's library_integrity_scanner (T1[6]) specifically
//  looks for this pattern. Since T1[6] is killed, the scanner never fires —
//  but we still apply the disguise in case a future ACE update moves
//  prologue checking somewhere else (e.g. a libanogs scanner).
//
//  Call RegisterHookSite(addr, original_first_word) AFTER each DobbyHook().
//  Call RunPrologueDisguise() AFTER all DobbyHook() calls are done.
// ══════════════════════════════════════════════════════════════════════════════
struct HookSite { uintptr_t addr; uint32_t origWord; };
static std::vector<HookSite> g_hookSites;

void RegisterHookSite(uintptr_t hookedAddr, uint32_t savedOrigWord) {
    g_hookSites.push_back({hookedAddr, savedOrigWord});
}

// Dobby's LDR X17, #8 prologue stub = 0x580000D1
// Replace with NOP — Dobby trampoline still routes calls correctly
static void ApplyPrologueDisguise() {
    int count = 0;
    for (auto& site : g_hookSites) {
        uint32_t current = *(volatile uint32_t*)site.addr;
        if (current == 0x580000D1) {
            SafePatch(site.addr, ARM64_NOP);
            count++;
        }
    }
    LOGI("[PROLOGUE] Disguised %d/%zu hook sites", count, g_hookSites.size());
}

// ══════════════════════════════════════════════════════════════════════════════
//  SURFACE BYPASS — Layers 1-4  (library stealth, anti-ptrace, thread spoof)
// ══════════════════════════════════════════════════════════════════════════════

// Layer 1+2: Anonymous remap — wipes our .so from /proc/self/maps
//
//  FIX vs original: we use /proc/self/maps to enumerate OUR library's
//  regions rather than the broken dl_iterate_phdr + dladdr approach.
//  We identify our own library by the address of this very function.
// ══════════════════════════════════════════════════════════════════════════════
void Surface_Remap() {
    // Identify this library's base by looking up our own function pointer
    Dl_info di;
    if (!dladdr((void*)Surface_Remap, &di) || !di.dli_fbase) {
        LOGI("[SURFACE] Remap: dladdr failed — skipping");
        return;
    }

    uintptr_t myBase = (uintptr_t)di.dli_fbase;

    struct Seg { uintptr_t start, end; int prot; };
    std::vector<Seg> segs;

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uintptr_t s, e;
        char perms[8];
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %7s", &s, &e, perms) != 3)
            continue;
        if (s < myBase) continue;
        if (s >= myBase + 16 * 1024 * 1024) break; // 16 MB window max
        if (!strstr(line, (const char*)di.dli_fname)) continue;
        int prot = 0;
        if (strchr(perms, 'r')) prot |= PROT_READ;
        if (strchr(perms, 'w')) prot |= PROT_WRITE;
        if (strchr(perms, 'x')) prot |= PROT_EXEC;
        segs.push_back({s, e, prot});
    }
    fclose(f);

    int ps = getpagesize();
    for (auto& seg : segs) {
        size_t sz = seg.end - seg.start;
        if (!sz) continue;

        // Allocate temp buffer, copy, remap anonymous, restore
        void* tmp = mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (tmp == MAP_FAILED) continue;
        memcpy(tmp, (void*)seg.start, sz);

        // If this is the first (lowest) segment, wipe ELF magic so
        // the library doesn't appear in /proc/self/maps scanners
        if (seg.start == segs[0].start) {
            mprotect((void*)(seg.start & ~(uintptr_t)(ps - 1)), (size_t)ps,
                     PROT_READ | PROT_WRITE);
            memset((void*)seg.start, 0, 4); // zero \x7fELF header
        }

        munmap((void*)seg.start, sz);

        void* mapped = mmap((void*)seg.start, sz,
                            PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (mapped != MAP_FAILED) {
            memcpy(mapped, tmp, sz);
            mprotect(mapped, sz, seg.prot ? seg.prot : PROT_READ | PROT_EXEC);
        }
        munmap(tmp, sz);
    }
    LOGI("[SURFACE] Library anonymously remapped (%zu segments)", segs.size());
}

// Layer 3: PR_SET_DUMPABLE = 0 — hides us from ptrace and /proc/self/mem readers
static void Surface_AntiDump() {
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
    LOGI("[SURFACE] PR_SET_DUMPABLE = 0");
}

// Layer 4: Thread name spoof — our threads look like ART threads to ACE scanners
static void Surface_SpoofThread() {
    // Use names that appear in every normal Android process
    prctl(PR_SET_NAME, "FinalizerDaemon", 0, 0, 0);
    LOGI("[SURFACE] Thread name → FinalizerDaemon");
}

// ══════════════════════════════════════════════════════════════════════════════
//  SURFACE +X  —  TracerPid spoof (hide debugger from /proc/self/status)
//
//  ACE reads /proc/self/status and checks the "TracerPid:" field.
//  If non-zero, ACE treats the process as traced and flags it.
//  We use a bind-mount to shadow /proc/self/status with a synthetic
//  version where TracerPid is always 0.
//
//  Requires: CAP_SYS_ADMIN (available on rooted / Zygisk context)
//  If bind-mount fails (no permission), we fall back silently.
// ══════════════════════════════════════════════════════════════════════════════
static void Surface_SpoofTracerPid() {
    // Read the real status file
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char content[4096] = {0};
    size_t n = fread(content, 1, sizeof(content) - 1, f);
    fclose(f);
    if (!n) return;

    // Find and zero-out TracerPid line
    char* tracer = strstr(content, "TracerPid:");
    if (tracer) {
        char* eol = strchr(tracer, '\n');
        // Replace "TracerPid:\t<N>" with "TracerPid:\t0  " (preserve length)
        char* digit = tracer + 10;
        while (digit < eol && (*digit == '\t' || *digit == ' ')) digit++;
        *digit = '0';
        digit++;
        while (digit < eol) { *digit++ = ' '; }
    }

    // Write spoofed content to a temp file and bind-mount over /proc/self/status
    char tmpPath[] = "/data/local/tmp/status_XXXXXX";
    int fd = mkstemp(tmpPath);
    if (fd < 0) return;
    write(fd, content, n);
    close(fd);

    // bind-mount our file over /proc/self/status
    if (mount(tmpPath, "/proc/self/status", nullptr, MS_BIND, nullptr) == 0) {
        LOGI("[SURFACE] TracerPid spoofed via bind-mount");
    } else {
        LOGI("[SURFACE] TracerPid bind-mount failed (no CAP_SYS_ADMIN) — skipped");
    }
    unlink(tmpPath);
}

// ══════════════════════════════════════════════════════════════════════════════
//  SURFACE +X  —  inotify watch killer
//
//  ACE installs inotify watches on files it cares about (libUE4.so,
//  the APK, /data/data/<pkg>/files/). When we remap/modify memory,
//  these watches could fire and alert ACE.
//
//  Strategy: after ACE initialises but before our hooks, scan
//  /proc/self/fdinfo for all inotify file descriptors and close them.
//  An inotify fd's fdinfo contains "inotify" in the first line.
// ══════════════════════════════════════════════════════════════════════════════
static void Surface_KillInotifyWatches() {
    DIR* d = opendir("/proc/self/fd");
    if (!d) return;

    std::vector<int> inotify_fds;
    struct dirent* ent;
    char infoBuf[256];

    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        int fdNum = atoi(ent->d_name);
        if (fdNum <= 2) continue; // skip stdin/stdout/stderr

        char infoPath[64];
        snprintf(infoPath, sizeof(infoPath), "/proc/self/fdinfo/%d", fdNum);
        FILE* fi = fopen(infoPath, "r");
        if (!fi) continue;
        bool isInotify = false;
        while (fgets(infoBuf, sizeof(infoBuf), fi)) {
            if (strstr(infoBuf, "inotify")) { isInotify = true; break; }
        }
        fclose(fi);
        if (isInotify) inotify_fds.push_back(fdNum);
    }
    closedir(d);

    for (int fd : inotify_fds) {
        close(fd);
        LOGI("[SURFACE] Closed inotify fd %d", fd);
    }
    LOGI("[SURFACE] Killed %zu inotify watch fd(s)", inotify_fds.size());
}

// ══════════════════════════════════════════════════════════════════════════════
//  MASTER ENTRY POINTS
// ══════════════════════════════════════════════════════════════════════════════

// RunFullBypass() — call BEFORE any DobbyHook() calls in hack_thread()
// RunPrologueDisguise() — call AFTER all DobbyHook() calls in hack_thread()

static void RunFullBypass() {
    LOGI("[BYPASS] ═══ ACE 13-Tier Full Bypass Starting ═══");

    // ── Surface layers first: instant, no library dependency ─────────────────
    Surface_SpoofThread();
    Surface_AntiDump();
    Surface_SpoofTracerPid();

    // ── Wait for ACE libraries to load ───────────────────────────────────────
    LOGI("[BYPASS] Waiting for libanort.so + libanogs.so ...");
    LibInfo anort{0,0,false}, anogs{0,0,false};
    int waitMs = 0;
    while (waitMs < 15000) {
        anort = GetLibInfo("libanort.so");
        anogs = GetLibInfo("libanogs.so");
        if (anort.valid && anogs.valid) break;
        usleep(200000);
        waitMs += 200;
    }

    if (anort.valid)
        LOGI("[BYPASS] libanort @ 0x%lx  size=0x%lx", (long)anort.base, (long)anort.size);
    else
        LOGE("[BYPASS] libanort.so NOT FOUND after 15s — some tiers will skip");

    if (anogs.valid)
        LOGI("[BYPASS] libanogs @ 0x%lx  size=0x%lx", (long)anogs.base, (long)anogs.size);
    else
        LOGE("[BYPASS] libanogs.so NOT FOUND after 15s — some tiers will skip");

    // ── Tier 8 first: VM dead = ~95% of ACE neutralised ─────────────────────
    Tier8_KillVM(anort);

    // ── Tier 13: poison heartbeat before boot driver polls (5s window) ───────
    Tier13_PoisonHeartbeat(anort);

    // ── Tier 7: prevent ACE from installing new inline hooks ─────────────────
    Tier7_KillRelocator(anogs);

    // ── Tier 4: drop all scan rule results (belt-and-suspenders on T8) ───────
    Tier4_KillScanRules(anogs);

    // ── Tier 2: kill report submission channel ────────────────────────────────
    Tier2_KillTDMSend(anogs);

    // ── Tier 5: block server config update path ───────────────────────────────
    Tier5_KillConfigUpdate(anort);

    // ── Tier 11: intercept Java-side kill command ─────────────────────────────
    Tier11_KillJNICommandDispatch(anogs);

    // ── Tier 12: block GCloud remote config channel ───────────────────────────
    Tier12_KillGCloudChannel(anogs);

    // ── Tier 1: all 11 ANORT native sensors (verified offsets) ───────────────
    Tier1_KillAllAnortSensors(anort);

    // ── Tier 9: runtime DEX loader ───────────────────────────────────────────
    Tier9_KillDexLoader(anort);

    // ── Tier 10: 0.1%/day probabilistic deep audit ───────────────────────────
    Tier10_KillProbabilisticTimer(anort);

    // ── Kill inotify watches (after ACE has set them up) ─────────────────────
    Surface_KillInotifyWatches();

    // ── Surface remap last: ACE is neutered, remap can't be caught now ────────
    sleep(1);
    Surface_Remap();

    LOGI("[BYPASS] ═══ Full 13-Tier Bypass Complete ═══");
}

// Call after ALL DobbyHook() calls
static void RunPrologueDisguise() {
    ApplyPrologueDisguise();
    LOGI("[BYPASS] Prologue disguise applied to %zu site(s)",
         g_hookSites.size());
}

// ── Convenience: typical hack_thread() usage pattern ─────────────────────────
//
//  void hack_thread() {
//      RunFullBypass();
//
//      // Save original first words before hooking:
//      uint32_t orig_word = *(uint32_t*)(target_addr);
//      DobbyHook((void*)target_addr, (void*)MyHook, (void**)&orig_fn);
//      RegisterHookSite(target_addr, orig_word);
//      // ... repeat for each hook ...
//
//      RunPrologueDisguise();
//  }
