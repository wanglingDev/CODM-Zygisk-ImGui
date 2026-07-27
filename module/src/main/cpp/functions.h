#pragma once

// Forward declarations — defined in menu.h (included after this file in hook.cpp)
extern bool  bAimFOV;
extern float fAimFOVSize;
extern bool  bAimHead;
extern float fAimSmooth;
extern bool  bSpeedHack;
extern float fSpeedMult;
extern bool  bHighJump;
extern float fJumpMult;
extern bool  bRapidFire;
extern bool  bNoRecoil;
extern bool  bNoSpread;
extern bool  bTriggerbot;
extern bool  bSilentAim;
extern bool  bNoReload;
extern bool  bFastScope;
extern bool  bLongSlide;
extern bool  bNoParachute;
extern bool  bAntiFlash;
extern bool  bFastSwim;
// ================================================================
// CODM Garena - functions.h
// Dump: 25 July 2026 | libunity.so (IL2CPP merged)
// ================================================================

#include <cfloat>
#include <inttypes.h>
#include <cmath>

// ================================================================
// DIRECT RVA FROM DUMP.CS (25/07/2026)
// ================================================================
#define RVA_Camera_get_main           0x4E09834
#define RVA_Camera_WorldToScreen      0x4E09378
#define RVA_Transform_get_position    0x4E63820
#define RVA_BRGamePlay_get_LocalPawn  0x5B671AC
#define RVA_GamePlay_get_Game         0xC3FE434
#define RVA_BaseGame_GetEnemyPawns    0x73B8D9C
#define RVA_BaseGame_GetAllPawns      0x73B2960
#define RVA_Pawn_SetAimRotation       0x4FAE5DC

// — Weapon RVAs (verify against your dump before enabling)
// Recoil
#define RVA_RecoilUpBase              0x5C3A100
#define RVA_RecoilLatBase             0x5C3A160
#define RVA_RecoilUpMax               0x5C3A1C0
#define RVA_RecoilLatMax              0x5C3A220
#define RVA_RecoilUpMod               0x5C3A280
#define RVA_RecoilLatMod              0x5C3A2E0
// Rapid fire / fire rate
#define RVA_get_FireInterval          0x5C37F40   // TODO: verify
#define RVA_get_ReloadTime            0x5C38200   // TODO: verify
#define RVA_get_CurAmmo               0x5C37C80   // TODO: verify
#define RVA_get_MaxAmmo               0x5C37CE0   // TODO: verify
// ADS / scope
#define RVA_get_ADSTime               0x5C39A00   // TODO: verify
#define RVA_get_ScopeTime             0x5C39B20   // TODO: verify
// Movement
#define RVA_get_MaxSpeed              0x4F9B688
#define RVA_get_JumpHeight            0x5006AC4
#define RVA_get_SlideTime             0x4F9DC00   // TODO: verify
#define RVA_get_SwimSpeed             0x4F9E100   // TODO: verify
// BR specific
#define RVA_get_ParachuteOpen         0x5B80300   // TODO: verify
#define RVA_get_FlashDuration         0x5B92400   // TODO: verify
// Triggerbot / auto-fire
#define RVA_Pawn_Fire                 0x4FAF200   // TODO: verify

// Field offsets
#define OFF_Pawn_m_HeadBone           0x308
#define OFF_Pawn_m_IsAlive            0x548
#define OFF_Pawn_m_PlayerInfo         0x5C0
#define OFF_Pawn_m_Mesh               0x628
#define OFF_Pawn_m_Team               0x5E0
#define OFF_AttackTarget_m_Info       0x18
#define OFF_AttackInfo_m_Health       0x34
#define OFF_AttackInfo_m_MaxHealth    0x38
#define OFF_AttackInfo_m_Armor        0x3C
#define OFF_AttackInfo_m_MaxArmor     0x40
#define OFF_PlayerInfo_m_NickName     0x158

// ================================================================
// BASE ADDRESS
// ================================================================
uintptr_t g_base = 0;
#define METHOD(rva) (g_base + (rva))

// ================================================================
// TYPES
// ================================================================
struct Il2CppString {
    void* klass;
    void* monitor;
    int   length;
    uint16_t chars[1];
    std::string toString() {
        std::string s;
        s.reserve(length);
        for (int i = 0; i < length; i++) {
            uint16_t c = chars[i];
            if (c < 0x80)       { s += (char)c; }
            else if (c < 0x800) { s += (char)(0xC0|(c>>6)); s += (char)(0x80|(c&0x3F)); }
            else                { s += (char)(0xE0|(c>>12)); s += (char)(0x80|((c>>6)&0x3F)); s += (char)(0x80|(c&0x3F)); }
        }
        return s;
    }
};

template<typename T>
struct Il2CppList {
    void* klass; void* monitor; void* items_ptr; int size; int version;
    T* getItems()  { return items_ptr ? (T*)((uintptr_t)items_ptr + 0x20) : nullptr; }
    int getSize()  { return size; }
};

struct Transform; struct Camera;

// ================================================================
// CORE GAME FUNCTIONS
// ================================================================
Vector3 Transform_GetPos(uintptr_t transform) {
    if (!transform) return {0,0,0};
    return ((Vector3(*)(uintptr_t))(METHOD(RVA_Transform_get_position)))(transform);
}
Camera* Camera_GetMain() {
    return ((Camera*(*)())(METHOD(RVA_Camera_get_main)))();
}
Vector3 Camera_WorldToScreen(Camera* cam, Vector3 pos) {
    if (!cam) return {0,0,0};
    return ((Vector3(*)(Camera*,Vector3))(METHOD(RVA_Camera_WorldToScreen)))(cam, pos);
}
Vector3 WorldToScreen(Vector3 worldPos) {
    auto cam = Camera_GetMain();
    return cam ? Camera_WorldToScreen(cam, worldPos) : Vector3{0,0,0};
}
uintptr_t GetLocalPawn() {
    return ((uintptr_t(*)())(METHOD(RVA_BRGamePlay_get_LocalPawn)))();
}
uintptr_t GetBaseGame() {
    return ((uintptr_t(*)())(METHOD(RVA_GamePlay_get_Game)))();
}
Il2CppList<uintptr_t>* GetEnemyPawns() {
    auto g = GetBaseGame(); if (!g) return nullptr;
    return ((Il2CppList<uintptr_t>*(*)(uintptr_t))(METHOD(RVA_BaseGame_GetEnemyPawns)))(g);
}
Il2CppList<uintptr_t>* GetAllPawns() {
    auto g = GetBaseGame(); if (!g) return nullptr;
    return ((Il2CppList<uintptr_t>*(*)(uintptr_t))(METHOD(RVA_BaseGame_GetAllPawns)))(g);
}
bool IsPawnAlive(uintptr_t pawn) {
    return pawn ? *(bool*)(pawn + OFF_Pawn_m_IsAlive) : false;
}
uintptr_t GetPawnMesh(uintptr_t pawn)     { return pawn ? *(uintptr_t*)(pawn + OFF_Pawn_m_Mesh)     : 0; }
uintptr_t GetPawnHeadBone(uintptr_t pawn) { return pawn ? *(uintptr_t*)(pawn + OFF_Pawn_m_HeadBone) : 0; }
float GetPawnHealth(uintptr_t pawn) {
    if (!pawn) return 0;
    auto info = *(uintptr_t*)(pawn + OFF_AttackTarget_m_Info);
    return info ? *(float*)(info + OFF_AttackInfo_m_Health) : 0;
}
float GetPawnMaxHealth(uintptr_t pawn) {
    if (!pawn) return 100;
    auto info = *(uintptr_t*)(pawn + OFF_AttackTarget_m_Info);
    if (!info) return 100;
    float v = *(float*)(info + OFF_AttackInfo_m_MaxHealth);
    return v > 0 ? v : 100;
}
float GetPawnArmor(uintptr_t pawn) {
    if (!pawn) return 0;
    auto info = *(uintptr_t*)(pawn + OFF_AttackTarget_m_Info);
    return info ? *(float*)(info + OFF_AttackInfo_m_Armor) : 0;
}
std::string GetPawnName(uintptr_t pawn) {
    if (!pawn) return "?";
    auto pi = *(uintptr_t*)(pawn + OFF_Pawn_m_PlayerInfo);
    if (!pi) return "?";
    auto ns = *(Il2CppString**)(pi + OFF_PlayerInfo_m_NickName);
    return ns ? ns->toString() : "?";
}
void SetPawnAimRotation(uintptr_t pawn, Quaternion rot) {
    if (!pawn) return;
    ((void(*)(uintptr_t,Quaternion))(METHOD(RVA_Pawn_SetAimRotation)))(pawn, rot);
}

// ================================================================
// AIMBOT
// ================================================================
bool IsInsideFOV(float sx, float sy, int sw, int sh) {
    if (!bAimFOV) return true;
    float cx = sw*0.5f, cy = sh*0.5f;
    return (sx-cx)*(sx-cx) + (sy-cy)*(sy-cy) <= fAimFOVSize*fAimFOVSize;
}
uintptr_t GetAimbotTarget(int sw, int sh) {
    uintptr_t result = 0; float minDist = FLT_MAX;
    auto local = GetLocalPawn(); if (!local) return 0;
    auto enemies = GetEnemyPawns(); if (!enemies) return 0;
    auto items = enemies->getItems(); if (!items) return 0;
    float cx = sw*0.5f, cy = sh*0.5f;
    for (int i = 0; i < enemies->getSize(); i++) {
        auto p = items[i];
        if (!p || p==local || !IsPawnAlive(p)) continue;
        auto head = GetPawnHeadBone(p); if (!head) continue;
        auto sc = WorldToScreen(Transform_GetPos(head));
        if (sc.Z <= 0) continue;
        if (!IsInsideFOV(sc.X, sc.Y, sw, sh)) continue;
        float d = sqrtf((sc.X-cx)*(sc.X-cx)+(sc.Y-cy)*(sc.Y-cy));
        if (d < minDist) { result = p; minDist = d; }
    }
    return result;
}
Quaternion LookAt(Vector3 from, Vector3 to) {
    float dx=to.X-from.X, dy=to.Y-from.Y, dz=to.Z-from.Z;
    float yaw=atan2f(dx,dz), pitch=-atan2f(dy,sqrtf(dx*dx+dz*dz));
    float cy=cosf(yaw*0.5f),sy=sinf(yaw*0.5f),cp=cosf(pitch*0.5f),sp=sinf(pitch*0.5f);
    return {sy*cp, cy*sp, -sy*sp, cy*cp};
}
Quaternion QuatSlerp(Quaternion a, Quaternion b, float t) {
    float dot=a.X*b.X+a.Y*b.Y+a.Z*b.Z+a.W*b.W;
    if (dot<0){b.X=-b.X;b.Y=-b.Y;b.Z=-b.Z;b.W=-b.W;dot=-dot;}
    if (dot>0.9995f) return {a.X+t*(b.X-a.X),a.Y+t*(b.Y-a.Y),a.Z+t*(b.Z-a.Z),a.W+t*(b.W-a.W)};
    float th0=acosf(dot),th=th0*t,s0=cosf(th)-dot*sinf(th)/sinf(th0),s1=sinf(th)/sinf(th0);
    return {s0*a.X+s1*b.X,s0*a.Y+s1*b.Y,s0*a.Z+s1*b.Z,s0*a.W+s1*b.W};
}
void DoAimbot(uintptr_t target) {
    if (!target) return;
    auto local = GetLocalPawn(); if (!local) return;
    auto myMesh = GetPawnMesh(local); if (!myMesh) return;
    Vector3 myPos = Transform_GetPos(myMesh);
    uintptr_t bone = bAimHead ? GetPawnHeadBone(target) : GetPawnMesh(target);
    if (!bone) return;
    Quaternion desired = LookAt(myPos, Transform_GetPos(bone));
    float t = 1.0f / fAimSmooth;
    if (t >= 1.0f) {
        SetPawnAimRotation(local, desired);
    } else {
        static Quaternion cur = {0,0,0,1};
        cur = QuatSlerp(cur, desired, t);
        SetPawnAimRotation(local, cur);
    }
}

// ================================================================
// POINTERS & HOOKS
// ================================================================
void Pointers() {
    g_base = (uintptr_t)g_il2cppBaseMap.startAddress;
    LOGI("[ENI] il2cpp base: 0x%" PRIxPTR, g_base);
}
void Hooks() { LOGI("[ENI] Hooks initialized (direct RVA)"); }

// ================================================================
// FEATURE HOOK ORIGINALS
// ================================================================
// — Movement
static float (*orig_get_MaxSpeed)(void*)    = nullptr;
static float (*orig_get_JumpHeight)(void*)  = nullptr;
static float (*orig_get_SlideTime)(void*)   = nullptr;
static float (*orig_get_SwimSpeed)(void*)   = nullptr;
// — Weapon
static float (*orig_get_FireInterval)(void*)= nullptr;
static float (*orig_get_ReloadTime)(void*)  = nullptr;
static int   (*orig_get_CurAmmo)(void*)     = nullptr;
static int   (*orig_get_MaxAmmo)(void*)     = nullptr;
static float (*orig_get_ADSTime)(void*)     = nullptr;
static float (*orig_get_ScopeTime)(void*)   = nullptr;
// — Recoil
static float (*orig_RecoilUpBase )(void*)   = nullptr;
static float (*orig_RecoilLatBase)(void*)   = nullptr;
static float (*orig_RecoilUpMax  )(void*)   = nullptr;
static float (*orig_RecoilLatMax )(void*)   = nullptr;
static float (*orig_RecoilUpMod  )(void*)   = nullptr;
static float (*orig_RecoilLatMod )(void*)   = nullptr;
// — BR
static float (*orig_get_FlashDuration)(void*)= nullptr;
static bool  (*orig_get_ParachuteOpen)(void*)= nullptr;

// ================================================================
// FEATURE HOOK IMPLEMENTATIONS
// ================================================================
// Movement
float hook_get_MaxSpeed(void* p)   { float b=orig_get_MaxSpeed?orig_get_MaxSpeed(p):0.f;   return bSpeedHack  ? b*fSpeedMult  : b; }
float hook_get_JumpHeight(void* p) { float b=orig_get_JumpHeight?orig_get_JumpHeight(p):0.f;return bHighJump  ? b*fJumpMult   : b; }
float hook_get_SlideTime(void* p)  { float b=orig_get_SlideTime?orig_get_SlideTime(p):0.f;  return bLongSlide  ? b*3.5f        : b; }
float hook_get_SwimSpeed(void* p)  { float b=orig_get_SwimSpeed?orig_get_SwimSpeed(p):0.f;  return bFastSwim   ? b*2.5f        : b; }
// Weapon
float hook_get_FireInterval(void* p){ float b=orig_get_FireInterval?orig_get_FireInterval(p):0.f; return bRapidFire ? b*0.05f : b; }
float hook_get_ReloadTime(void* p)  { float b=orig_get_ReloadTime?orig_get_ReloadTime(p):0.f;     return bNoReload  ? 0.01f  : b; }
int   hook_get_CurAmmo(void* p)     { return bNoReload && orig_get_MaxAmmo ? orig_get_MaxAmmo(p) : (orig_get_CurAmmo ? orig_get_CurAmmo(p) : 0); }
float hook_get_ADSTime(void* p)     { float b=orig_get_ADSTime?orig_get_ADSTime(p):0.f;           return bFastScope ? b*0.1f  : b; }
float hook_get_ScopeTime(void* p)   { float b=orig_get_ScopeTime?orig_get_ScopeTime(p):0.f;       return bFastScope ? 0.01f   : b; }
// Recoil
float hook_RecoilUpBase (void* p) { return bNoRecoil ? 0.f : (orig_RecoilUpBase  ? orig_RecoilUpBase(p)  : 0.f); }
float hook_RecoilLatBase(void* p) { return bNoRecoil ? 0.f : (orig_RecoilLatBase ? orig_RecoilLatBase(p) : 0.f); }
float hook_RecoilUpMax  (void* p) { return bNoRecoil ? 0.f : (orig_RecoilUpMax   ? orig_RecoilUpMax(p)   : 0.f); }
float hook_RecoilLatMax (void* p) { return bNoRecoil ? 0.f : (orig_RecoilLatMax  ? orig_RecoilLatMax(p)  : 0.f); }
float hook_RecoilUpMod  (void* p) { return bNoSpread ? 0.f : (orig_RecoilUpMod   ? orig_RecoilUpMod(p)   : 0.f); }
float hook_RecoilLatMod (void* p) { return bNoSpread ? 0.f : (orig_RecoilLatMod  ? orig_RecoilLatMod(p)  : 0.f); }
// BR
float hook_get_FlashDuration(void* p){ return bAntiFlash ? 0.f : (orig_get_FlashDuration ? orig_get_FlashDuration(p) : 0.f); }
bool  hook_get_ParachuteOpen(void* p){ return bNoParachute ? false : (orig_get_ParachuteOpen ? orig_get_ParachuteOpen(p) : false); }

// ================================================================
// INIT PATCHES — install all Dobby hooks
// ================================================================
// Macro: hook RVA, log result
#define _HOOK(rva, fn, orig) \
    r = DobbyHook((void*)METHOD(rva),(void*)(fn),(void**)&(orig)); \
    LOGI("[ENI] hook " #fn " @ 0x%lx → %s (r=%d)", (long)(rva), r==0?"OK":"FAIL",r);

void InitPatches() {
    int r;

    // — Movement (verified RVAs)
    _HOOK(RVA_get_MaxSpeed,   hook_get_MaxSpeed,   orig_get_MaxSpeed);
    _HOOK(RVA_get_JumpHeight, hook_get_JumpHeight, orig_get_JumpHeight);

    // — Movement (TODO: verify RVAs before uncommenting)
    // _HOOK(RVA_get_SlideTime, hook_get_SlideTime, orig_get_SlideTime);
    // _HOOK(RVA_get_SwimSpeed, hook_get_SwimSpeed, orig_get_SwimSpeed);

    // — Recoil (TODO: verify RVAs before uncommenting)
    // _HOOK(RVA_RecoilUpBase,  hook_RecoilUpBase,  orig_RecoilUpBase);
    // _HOOK(RVA_RecoilLatBase, hook_RecoilLatBase, orig_RecoilLatBase);
    // _HOOK(RVA_RecoilUpMax,   hook_RecoilUpMax,   orig_RecoilUpMax);
    // _HOOK(RVA_RecoilLatMax,  hook_RecoilLatMax,  orig_RecoilLatMax);
    // _HOOK(RVA_RecoilUpMod,   hook_RecoilUpMod,   orig_RecoilUpMod);
    // _HOOK(RVA_RecoilLatMod,  hook_RecoilLatMod,  orig_RecoilLatMod);

    // — Weapon (TODO: verify RVAs before uncommenting)
    // _HOOK(RVA_get_FireInterval, hook_get_FireInterval, orig_get_FireInterval);
    // _HOOK(RVA_get_ReloadTime,   hook_get_ReloadTime,   orig_get_ReloadTime);
    // _HOOK(RVA_get_CurAmmo,      hook_get_CurAmmo,      orig_get_CurAmmo);
    // _HOOK(RVA_get_ADSTime,      hook_get_ADSTime,      orig_get_ADSTime);
    // _HOOK(RVA_get_ScopeTime,    hook_get_ScopeTime,    orig_get_ScopeTime);

    // — BR (TODO: verify RVAs before uncommenting)
    // _HOOK(RVA_get_FlashDuration,  hook_get_FlashDuration,  orig_get_FlashDuration);
    // _HOOK(RVA_get_ParachuteOpen,  hook_get_ParachuteOpen,  orig_get_ParachuteOpen);
}
#undef _HOOK

void TickPatches() {
    // Hook-based features check their own booleans inside the hook.
    // Add per-frame MemoryPatch toggles here if needed.
}

// Backward-compat alias — older hook.cpp versions call InstallFeatureHooks()
// instead of InitPatches(). Both do the same thing.
inline void InstallFeatureHooks() { InitPatches(); }
