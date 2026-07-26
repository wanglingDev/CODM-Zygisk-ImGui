#pragma once
// ═══════════════════════════════════════════════════════════════════
//  skin_changer.h — CODM Garena Weapon + Operator Skin Changer
//  Target: libunity.so (IL2CPP) arm64-v8a
//  Dump: 25 July 2026
//
//  Method: hook WeaponSkinHelper.DoChange + CharacterSkinHelper
//  Skin ID dioverride ke ID yang lo mau sebelum dikirim ke server
//  Server-side masih punya skin lo yang asli, jadi visual-only
//  (tidak ada unlock permanen, tidak pengaruh ke inventory)
// ═══════════════════════════════════════════════════════════════════

#include <cstring>
#include <cinttypes>
#include "Misc.h"

// ── Skin toggle ──────────────────────────────────────────────────
bool bSkinChanger   = false;   // master toggle
bool bWeaponSkin    = false;   // override weapon skin
bool bOperatorSkin  = false;   // override operator/character skin
bool bBlueprintSkin = false;   // force blueprint variant

// Weapon skin ID (dari ingame skin IDs — lihat skin_ids.h)
// Default: 0 = default/no override
int  iWeaponSkinID   = 0;
int  iOperatorSkinID = 0;

// ── RVA dari dump.cs 25/07/2026 ──────────────────────────────────
// Verify dengan Remote Spy atau logcat hook sebelum enable
// TODO: cari via dump.cs search — gue sisain placeholder dulu
// grep "WeaponSkinHelper\|DoChange\|SetSkinID\|ApplySkin" dump.cs

// Placeholder RVAs — HARUS diverifikasi dulu sebelum uncomment!
// #define RVA_WeaponSkinHelper_DoChange     0xABC53FC
// #define RVA_CharSkinHelper_Apply          0xBBD1234
// #define RVA_BREquipmentSkinMgr_SetSkin    0xCC45678

// ── Hook implementations ──────────────────────────────────────────

// WeaponSkinHelper.DoChange(weaponID, skinID) — intercept skinID
static void (*orig_WeaponSkinDoChange)(void* thiz, int weaponID,
                                        int skinID, bool force) = nullptr;
static void hook_WeaponSkinDoChange(void* thiz, int weaponID,
                                     int skinID, bool force) {
    if (bSkinChanger && bWeaponSkin && iWeaponSkinID != 0) {
        LOGI("[SKIN] WeaponSkin override: weapon=%d skin %d->%d",
             weaponID, skinID, iWeaponSkinID);
        skinID = iWeaponSkinID;
    }
    orig_WeaponSkinDoChange(thiz, weaponID, skinID, force);
}

// Operator skin apply
static void (*orig_CharSkinApply)(void* thiz, int charID,
                                   int skinID) = nullptr;
static void hook_CharSkinApply(void* thiz, int charID, int skinID) {
    if (bSkinChanger && bOperatorSkin && iOperatorSkinID != 0) {
        LOGI("[SKIN] OperatorSkin override: char=%d skin %d->%d",
             charID, skinID, iOperatorSkinID);
        skinID = iOperatorSkinID;
    }
    orig_CharSkinApply(thiz, charID, skinID);
}

// ─────────────────────────────────────────────────────────────────
//  Install Skin Hooks
//  Dipanggil setelah g_base valid (dari Pointers())
// ─────────────────────────────────────────────────────────────────
static bool g_skin_hooks_installed = false;

static void InstallSkinHooks() {
    if (g_skin_hooks_installed) return;
    if (!g_base) { LOGE("[SKIN] g_base not set"); return; }

    // Uncomment setelah RVA diverifikasi:
    /*
    #define _SH(rva, fn, orig) do { \
        void* _addr = (void*)(g_base + (rva)); \
        void* _s = shadowhook_hook_func_addr(_addr, (void*)(fn), (void**)&(orig)); \
        LOGI("[SKIN] hook @ 0x%lx: %s", (long)(rva), _s ? "OK" : "FAIL"); \
    } while(0)

    _SH(RVA_WeaponSkinHelper_DoChange, hook_WeaponSkinDoChange, orig_WeaponSkinDoChange);
    _SH(RVA_CharSkinHelper_Apply,      hook_CharSkinApply,      orig_CharSkinApply);
    #undef _SH
    */

    // Sementara ini: log RVA lookup untuk bantu verifikasi
    LOGI("[SKIN] InstallSkinHooks called — RVAs pending verification");
    LOGI("[SKIN] Search dump.cs untuk: WeaponSkinHelper, DoChange, SetSkinID");
    LOGI("[SKIN] Lalu Remote Spy di device untuk confirm RVA live");

    g_skin_hooks_installed = true;
}

// ─────────────────────────────────────────────────────────────────
//  Menu integration — tambah ke tab WPN di menu.h
// ─────────────────────────────────────────────────────────────────
/*
// Tambahkan ke dalam tab WPN di DrawMenu():

ImGui::Spacing();
SectionLabel("[ SKIN CHANGER ]");

NeonToggle("Skin Changer", &bSkinChanger, {1.f,0.f,1.f,1.f});

if (bSkinChanger) {
    ImGui::Spacing();
    NeonToggle("Weapon Skin",   &bWeaponSkin,   {0.f,1.f,1.f,1.f});
    if (bWeaponSkin) {
        ImGui::InputInt("Weapon Skin ID", &iWeaponSkinID);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.6f,0.7f,1.f));
        ImGui::TextWrapped("Skin ID dari in-game. 0 = no override.");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    NeonToggle("Operator Skin", &bOperatorSkin, {1.f,0.f,1.f,1.f});
    if (bOperatorSkin) {
        ImGui::InputInt("Operator Skin ID", &iOperatorSkinID);
    }
}
*/
