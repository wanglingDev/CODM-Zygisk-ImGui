#pragma once
// ================================================================
// CODM Garena - functions.h
// Fresh Dump: 25 July 2026
// Method: Direct RVA (libil2cpp.so base)
// ================================================================

#include <cfloat>  // FLT_MAX

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

// Field offsets
#define OFF_Pawn_m_HeadBone           0x308
#define OFF_Pawn_m_IsAlive            0x548
#define OFF_Pawn_m_PlayerInfo         0x5C0
#define OFF_Pawn_m_Mesh               0x628
#define OFF_AttackTarget_m_Info       0x18
#define OFF_AttackInfo_m_Health       0x34
#define OFF_AttackInfo_m_MaxHealth    0x38
#define OFF_PlayerInfo_m_NickName     0x158

// ================================================================
// FEATURES TOGGLES
// ================================================================
bool bESP_Box      = false;
bool bESP_Line     = false;
bool bESP_Health   = false;
bool bESP_Name     = false;
bool bESP_Distance = false;
bool bAimbot       = false;
bool bAimFOV       = true;
bool bAimHead      = true;
bool bTrigger      = false;
float fAimFOVSize  = 150.0f;
float fAimSmooth   = 1.0f;
int  iAimBone      = 0; // 0=head, 1=neck, 2=body

// ================================================================
// BASE ADDRESS
// ================================================================
uintptr_t g_base = 0;
#define METHOD(rva) (g_base + (rva))

// ================================================================
// TYPES
// Vector3, Vector2, Quaternion already defined in Include/ headers
// included before functions.h in hook.cpp — DO NOT redefine here.
// ================================================================
struct Il2CppString {
    void* klass;
    void* monitor;
    int   length;
    // IL2CPP string data is UTF-16 (uint16_t = 2 bytes)
    uint16_t chars[1];
    std::string toString() {
        std::string s;
        s.reserve(length);
        for (int i = 0; i < length; i++) {
            uint16_t c = chars[i];
            if (c < 0x80) {
                s += (char)c;
            } else if (c < 0x800) {
                s += (char)(0xC0 | (c >> 6));
                s += (char)(0x80 | (c & 0x3F));
            } else {
                s += (char)(0xE0 | (c >> 12));
                s += (char)(0x80 | ((c >> 6) & 0x3F));
                s += (char)(0x80 | (c & 0x3F));
            }
        }
        return s;
    }
};

template<typename T>
struct Il2CppList {
    void* klass;
    void* monitor;
    void* items_ptr;
    int   size;
    int   version;
    T* getItems() {
        if (!items_ptr) return nullptr;
        return (T*)((uintptr_t)items_ptr + 0x20);
    }
    int getSize() { return size; }
};

struct Transform;
struct Camera;

// ================================================================
// CORE FUNCTIONS
// ================================================================
Vector3 Transform_GetPos(uintptr_t transform) {
    if (!transform) return {0,0,0};
    auto fn = (Vector3(*)(uintptr_t))(METHOD(RVA_Transform_get_position));
    return fn(transform);
}

Camera* Camera_GetMain() {
    auto fn = (Camera*(*)())(METHOD(RVA_Camera_get_main));
    return fn();
}

Vector3 Camera_WorldToScreen(Camera* cam, Vector3 pos) {
    if (!cam) return {0,0,0};
    auto fn = (Vector3(*)(Camera*, Vector3))(METHOD(RVA_Camera_WorldToScreen));
    return fn(cam, pos);
}

Vector3 WorldToScreen(Vector3 worldPos) {
    auto cam = Camera_GetMain();
    if (!cam) return {0,0,0};
    return Camera_WorldToScreen(cam, worldPos);
}

uintptr_t GetLocalPawn() {
    auto fn = (uintptr_t(*)())(METHOD(RVA_BRGamePlay_get_LocalPawn));
    return fn();
}

uintptr_t GetBaseGame() {
    auto fn = (uintptr_t(*)())(METHOD(RVA_GamePlay_get_Game));
    return fn();
}

Il2CppList<uintptr_t>* GetEnemyPawns() {
    auto game = GetBaseGame();
    if (!game) return nullptr;
    auto fn = (Il2CppList<uintptr_t>*(*)(uintptr_t))(METHOD(RVA_BaseGame_GetEnemyPawns));
    return fn(game);
}

bool IsPawnAlive(uintptr_t pawn) {
    if (!pawn) return false;
    return *(bool*)(pawn + OFF_Pawn_m_IsAlive);
}

uintptr_t GetPawnMesh(uintptr_t pawn) {
    return pawn ? *(uintptr_t*)(pawn + OFF_Pawn_m_Mesh) : 0;
}

uintptr_t GetPawnHeadBone(uintptr_t pawn) {
    return pawn ? *(uintptr_t*)(pawn + OFF_Pawn_m_HeadBone) : 0;
}

float GetPawnHealth(uintptr_t pawn) {
    if (!pawn) return 0;
    auto info = *(uintptr_t*)(pawn + OFF_AttackTarget_m_Info);
    if (!info) return 0;
    return *(float*)(info + OFF_AttackInfo_m_Health);
}

float GetPawnMaxHealth(uintptr_t pawn) {
    if (!pawn) return 100;
    auto info = *(uintptr_t*)(pawn + OFF_AttackTarget_m_Info);
    if (!info) return 100;
    float v = *(float*)(info + OFF_AttackInfo_m_MaxHealth);
    return v > 0 ? v : 100;
}

std::string GetPawnName(uintptr_t pawn) {
    if (!pawn) return "?";
    auto pi = *(uintptr_t*)(pawn + OFF_Pawn_m_PlayerInfo);
    if (!pi) return "?";
    auto ns = *(Il2CppString**)(pi + OFF_PlayerInfo_m_NickName);
    if (!ns) return "?";
    return ns->toString();
}

void SetPawnAimRotation(uintptr_t pawn, Quaternion rot) {
    if (!pawn) return;
    auto fn = (void(*)(uintptr_t, Quaternion))(METHOD(RVA_Pawn_SetAimRotation));
    fn(pawn, rot);
}

// ================================================================
// AIMBOT
// ================================================================
bool IsInsideFOV(float sx, float sy, int screenW, int screenH) {
    if (!bAimFOV) return true;
    float cx = screenW / 2.0f, cy = screenH / 2.0f;
    float r = fAimFOVSize;
    return (sx-cx)*(sx-cx) + (sy-cy)*(sy-cy) <= r*r;
}

uintptr_t GetAimbotTarget(int screenW, int screenH) {
    uintptr_t result = 0;
    float minDist = FLT_MAX;
    auto local = GetLocalPawn();
    if (!local) return 0;
    auto enemies = GetEnemyPawns();
    if (!enemies) return 0;
    auto items = enemies->getItems();
    if (!items) return 0;
    float cx = screenW / 2.0f, cy = screenH / 2.0f;
    for (int i = 0; i < enemies->getSize(); i++) {
        auto p = items[i];
        if (!p || p == local || !IsPawnAlive(p)) continue;
        auto head = GetPawnHeadBone(p);
        if (!head) continue;
        auto sc = WorldToScreen(Transform_GetPos(head));
        if (sc.z <= 0) continue;
        if (!IsInsideFOV(sc.x, sc.y, screenW, screenH)) continue;
        float d = sqrtf((sc.x-cx)*(sc.x-cx)+(sc.y-cy)*(sc.y-cy));
        if (d < minDist) { result = p; minDist = d; }
    }
    return result;
}

Quaternion LookAt(Vector3 from, Vector3 to) {
    float dx = to.x-from.x, dy = to.y-from.y, dz = to.z-from.z;
    float yaw   = atan2f(dx, dz);
    float pitch = -atan2f(dy, sqrtf(dx*dx+dz*dz));
    float cy = cosf(yaw*0.5f), sy = sinf(yaw*0.5f);
    float cp = cosf(pitch*0.5f), sp = sinf(pitch*0.5f);
    return {sy*cp, cy*sp, -sy*sp, cy*cp};
}

Quaternion QuatSlerp(Quaternion a, Quaternion b, float t) {
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    if (dot < 0) { b.x=-b.x; b.y=-b.y; b.z=-b.z; b.w=-b.w; dot=-dot; }
    if (dot > 0.9995f) {
        return { a.x + t*(b.x-a.x), a.y + t*(b.y-a.y),
                 a.z + t*(b.z-a.z), a.w + t*(b.w-a.w) };
    }
    float theta0 = acosf(dot);
    float theta  = theta0 * t;
    float s0 = cosf(theta) - dot * sinf(theta) / sinf(theta0);
    float s1 = sinf(theta) / sinf(theta0);
    return { s0*a.x + s1*b.x, s0*a.y + s1*b.y,
             s0*a.z + s1*b.z, s0*a.w + s1*b.w };
}

void DoAimbot(uintptr_t target) {
    if (!target) return;
    auto local = GetLocalPawn();
    if (!local) return;
    auto myMesh = GetPawnMesh(local);
    if (!myMesh) return;
    Vector3 myPos = Transform_GetPos(myMesh);
    uintptr_t bone = bAimHead ? GetPawnHeadBone(target) : GetPawnMesh(target);
    if (!bone) return;
    Vector3 targetPos = Transform_GetPos(bone);
    Quaternion desired = LookAt(myPos, targetPos);

    float t = 1.0f / fAimSmooth;
    if (t >= 1.0f) {
        SetPawnAimRotation(local, desired);
    } else {
        static Quaternion current = {0,0,0,1};
        current = QuatSlerp(current, desired, t);
        SetPawnAimRotation(local, current);
    }
}

// ================================================================
// POINTERS & HOOKS
// ================================================================
void Pointers() {
    g_base = g_il2cppBaseMap.startAddress;
    LOGI("CODM il2cpp base: 0x%lx", g_base);
}

void Hooks() {
    LOGI("Hooks initialized (direct RVA mode)");
}
