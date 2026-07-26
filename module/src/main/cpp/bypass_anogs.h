#pragma once
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cinttypes>
#include "shadowhook.h"

#ifndef LOGI
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "zyCheats", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "zyCheats", __VA_ARGS__)
#endif
// ═══════════════════════════════════════════════════════════════════
//  bypass_anogs.h — Tencent AnoSDK (libanogs.so) bypass
//  Target: CODM Garena arm64-v8a
//  Analyzed: libanogs.so BuildID b051507f (Jul 2026)
//
//  Kenapa lo kena 10 tahun, kreator lain 7 hari:
//  Bypass sebelumnya hanya hide dari /proc/maps + solist.
//  libanogs.so tetap jalan dan report semua detection ke server.
//  File ini hook exported symbols AnoSDK untuk block reporting.
// ═══════════════════════════════════════════════════════════════════



// ── Toggle: nyalain sebelum masuk match, matiin saat di lobby ─────
// Ini yang dimaksud "toggleable lobby bypass" di komunitas.
// true  = block semua AnoSDK reporting (mode bypass aktif)
// false = pass-through normal (mode lobby aman)
static volatile bool g_anogs_bypass_active = true;

// ─────────────────────────────────────────────────────────────────
//  EXPORTED SYMBOL HOOKS
//  Semua hook via dlsym — tidak butuh RVA hardcode,
//  otomatis resolve ke versi yang ter-install.
// ─────────────────────────────────────────────────────────────────

// ── 1. AnoSDKIoctl — main command dispatcher ─────────────────────
//  Case yang relevan (dari komunitas):
//  Case 16 = opcode_scan / memory scan
//  Case 37 = screenshot / screen capture
//  Return 0 = "command tidak dikenali / failed" → AC ignore
static int (*orig_AnoSDKIoctl)(int cmd, char* buf, void** out,
                                unsigned int outLen, int* outActual) = nullptr;
static int hook_AnoSDKIoctl(int cmd, char* buf, void** out,
                              unsigned int outLen, int* outActual) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKIoctl(cmd, buf, out, outLen, outActual);

    LOGI("[BYPASS] AnoSDKIoctl blocked cmd=%d", cmd);
    // Return 0 = success tapi data-nya kosong, server tidak dapat info
    if (out && outLen > 0) memset(*out, 0, outLen);
    return 0;
}

// ── 2. AnoSDKIoctlOld — legacy dispatcher (juga aktif) ───────────
static int (*orig_AnoSDKIoctlOld)(int cmd, char* buf, void** out,
                                   unsigned int outLen, int* outActual) = nullptr;
static int hook_AnoSDKIoctlOld(int cmd, char* buf, void** out,
                                 unsigned int outLen, int* outActual) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKIoctlOld(cmd, buf, out, outLen, outActual);

    LOGI("[BYPASS] AnoSDKIoctlOld blocked cmd=%d", cmd);
    if (out && outLen > 0) memset(*out, 0, outLen);
    return 0;
}

// ── 3. AnoSDKGetReportData — exfiltrate cheat evidence ───────────
//  Ini yang kirim "bukti" ke server. Return null = tidak ada data.
static int (*orig_AnoSDKGetReportData)(void** data, unsigned int* len) = nullptr;
static int hook_AnoSDKGetReportData(void** data, unsigned int* len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKGetReportData(data, len);
    if (data) *data = nullptr;
    if (len)  *len  = 0;
    return 0;
}

static int (*orig_AnoSDKGetReportData2)(void** data, unsigned int* len) = nullptr;
static int hook_AnoSDKGetReportData2(void** data, unsigned int* len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKGetReportData2(data, len);
    if (data) *data = nullptr;
    if (len)  *len  = 0;
    return 0;
}

static int (*orig_AnoSDKGetReportData3)(void** data, unsigned int* len) = nullptr;
static int hook_AnoSDKGetReportData3(void** data, unsigned int* len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKGetReportData3(data, len);
    if (data) *data = nullptr;
    if (len)  *len  = 0;
    return 0;
}

static int (*orig_AnoSDKGetReportData4)(void** data, unsigned int* len) = nullptr;
static int hook_AnoSDKGetReportData4(void** data, unsigned int* len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKGetReportData4(data, len);
    if (data) *data = nullptr;
    if (len)  *len  = 0;
    return 0;
}

// ── 4. AnoSDKDelReportData — delete sent report (always allow) ───
//  Ini cleanup setelah data dikirim. Boleh jalan normal.
static int (*orig_AnoSDKDelReportData)(void* data) = nullptr;
static int hook_AnoSDKDelReportData(void* data) {
    return orig_AnoSDKDelReportData ? orig_AnoSDKDelReportData(data) : 0;
}

// ── 5. AnoSDKOnRecvData — SERVER → CLIENT commands ───────────────
//  INI YANG EKSEKUSI BAN DI DEVICE.
//  Server kirim "ban packet" → AnoSDKOnRecvData proses → kick + ban.
//  Hook ini = server tidak bisa eksekusi ban di device.
static int (*orig_AnoSDKOnRecvData)(void* data, unsigned int len) = nullptr;
static int hook_AnoSDKOnRecvData(void* data, unsigned int len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKOnRecvData(data, len);

    // Bisa log packet type di sini untuk analisis:
    // if (data && len >= 4) LOGI("[BYPASS] OnRecvData type=0x%x", *(uint32_t*)data);
    LOGI("[BYPASS] AnoSDKOnRecvData blocked (len=%u) — server ban packet dropped", len);
    return 0;
}

// ── 6. AnoSDKOnRecvSignature — signature verification ────────────
//  Block server signature check — ini bisa trigger ban juga
static int (*orig_AnoSDKOnRecvSignature)(void* data, unsigned int len) = nullptr;
static int hook_AnoSDKOnRecvSignature(void* data, unsigned int len) {
    if (!g_anogs_bypass_active)
        return orig_AnoSDKOnRecvSignature(data, len);
    LOGI("[BYPASS] AnoSDKOnRecvSignature blocked");
    return 0;
}

// ── 7. AnoSDKForExport — telemetry data export ───────────────────
//  Dari source community: memset buffer to 0 to block sending
static void (*orig_AnoSDKForExport)(void) = nullptr;
static void hook_AnoSDKForExport(void) {
    if (!g_anogs_bypass_active) {
        if (orig_AnoSDKForExport) orig_AnoSDKForExport();
    }
    // Intentionally do nothing — block telemetry export
    LOGI("[BYPASS] AnoSDKForExport blocked");
}

// ─────────────────────────────────────────────────────────────────
//  INSTALL ALL HOOKS
// ─────────────────────────────────────────────────────────────────
static bool g_anogs_hooks_installed = false;

static void InstallAnogsHooks() {
    if (g_anogs_hooks_installed) return;

    // Wait for libanogs.so to be loaded
    void* lib = nullptr;
    for (int i = 0; i < 60 && !lib; i++) {
        lib = dlopen("libanogs.so", RTLD_LAZY | RTLD_NOLOAD);
        if (!lib) sleep(1);
    }
    if (!lib) {
        LOGE("[BYPASS] libanogs.so not found after 60s");
        return;
    }

    LOGI("[BYPASS] libanogs.so found, installing hooks...");

    // Macro: hook by symbol name via ShadowHook
    #define _AH(sym, hook, orig) do { \
        void* _f = dlsym(lib, sym "@@ANO"); \
        if (!_f) _f = dlsym(lib, sym); \
        if (_f) { \
            void* _s = shadowhook_hook_func_addr(_f, (void*)(hook), (void**)&(orig)); \
            LOGI("[BYPASS] " sym ": %s", _s ? "OK" : shadowhook_to_errmsg(shadowhook_get_errno())); \
        } else { \
            LOGE("[BYPASS] " sym ": symbol not found"); \
        } \
    } while(0)

    _AH("AnoSDKIoctl",           hook_AnoSDKIoctl,           orig_AnoSDKIoctl);
    _AH("AnoSDKIoctlOld",        hook_AnoSDKIoctlOld,        orig_AnoSDKIoctlOld);
    _AH("AnoSDKGetReportData",   hook_AnoSDKGetReportData,   orig_AnoSDKGetReportData);
    _AH("AnoSDKGetReportData2",  hook_AnoSDKGetReportData2,  orig_AnoSDKGetReportData2);
    _AH("AnoSDKGetReportData3",  hook_AnoSDKGetReportData3,  orig_AnoSDKGetReportData3);
    _AH("AnoSDKGetReportData4",  hook_AnoSDKGetReportData4,  orig_AnoSDKGetReportData4);
    _AH("AnoSDKDelReportData",   hook_AnoSDKDelReportData,   orig_AnoSDKDelReportData);
    _AH("AnoSDKOnRecvData",      hook_AnoSDKOnRecvData,      orig_AnoSDKOnRecvData);
    _AH("AnoSDKOnRecvSignature", hook_AnoSDKOnRecvSignature, orig_AnoSDKOnRecvSignature);
    _AH("AnoSDKForExport",       hook_AnoSDKForExport,       orig_AnoSDKForExport);
    #undef _AH

    dlclose(lib);
    g_anogs_hooks_installed = true;
    LOGI("[BYPASS] AnoSDK bypass installed — g_anogs_bypass_active=%d",
         (int)g_anogs_bypass_active);
}

// ── Toggle API — panggil dari menu ───────────────────────────────
static inline void AnogsSetBypass(bool active) {
    g_anogs_bypass_active = active;
    LOGI("[BYPASS] anogs bypass -> %s", active ? "ON" : "OFF");
}
static inline bool AnogsGetBypass() { return g_anogs_bypass_active; }
