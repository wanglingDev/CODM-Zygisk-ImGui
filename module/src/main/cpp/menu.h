#pragma once
#include <cinttypes>
#include <cmath>
#include <ctime>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_android.h"
#include "functions.h"

// ════════════════════════════════════════════════════════════════
//  FEATURE TOGGLES
// ════════════════════════════════════════════════════════════════
bool  bAimbot       = false;
bool  bAimFOV       = true;
bool  bAimHead      = true;
bool  bSilentAim    = false;
bool  bTriggerbot   = false;
float fAimFOVSize   = 150.f;
float fAimSmooth    = 1.f;
int   iAimBone      = 0;

bool  bESP_Box      = false;
bool  bESP_Line     = false;
bool  bESP_Health   = false;
bool  bESP_Name     = false;
bool  bESP_Distance = false;
bool  bESP_Skeleton = false;
bool  bBoxFill      = false;
bool  bCornerBox    = false;
bool  bSnapLine     = false;
float espColorBox[4]  = { 1.f, 0.18f, 0.18f, 1.f };
float espColorLine[4] = { 0.18f, 1.f,  0.4f,  0.9f };
float espColorName[4] = { 1.f,  1.f,  1.f,   1.f  };

bool  bNoRecoil     = false;
bool  bNoSpread     = false;
bool  bRapidFire    = false;

bool  bSpeedHack    = false;
float fSpeedMult    = 1.5f;
bool  bHighJump     = false;
float fJumpMult     = 2.f;

bool  bShowFPS      = true;
bool  bAntiDetect   = true;

// ════════════════════════════════════════════════════════════════
//  RECOIL HOOKS
// ════════════════════════════════════════════════════════════════
static float (*orig_RecoilUpBase )(void*) = nullptr;
static float (*orig_RecoilLatBase)(void*) = nullptr;
static float (*orig_RecoilUpMax  )(void*) = nullptr;
static float (*orig_RecoilLatMax )(void*) = nullptr;
static float (*orig_RecoilUpMod  )(void*) = nullptr;
static float (*orig_RecoilLatMod )(void*) = nullptr;

float hook_RecoilUpBase (void* p){ return bNoRecoil ? 0.f : orig_RecoilUpBase(p);  }
float hook_RecoilLatBase(void* p){ return bNoRecoil ? 0.f : orig_RecoilLatBase(p); }
float hook_RecoilUpMax  (void* p){ return bNoRecoil ? 0.f : orig_RecoilUpMax(p);   }
float hook_RecoilLatMax (void* p){ return bNoRecoil ? 0.f : orig_RecoilLatMax(p);  }
float hook_RecoilUpMod  (void* p){ return bNoSpread ? 0.f : orig_RecoilUpMod(p);   }
float hook_RecoilLatMod (void* p){ return bNoSpread ? 0.f : orig_RecoilLatMod(p);  }

void InstallFeatureHooks() {
    #define _H(rva, fn, orig) \
        DobbyHook((void*)((uintptr_t)g_il2cppBaseMap.startAddress + (rva)), (void*)(fn), (void**)&(orig))
    // _H(0x5C3A100, hook_RecoilUpBase,  orig_RecoilUpBase );
    // _H(0x5C3A160, hook_RecoilLatBase, orig_RecoilLatBase);
    // _H(0x5C3A1C0, hook_RecoilUpMax,   orig_RecoilUpMax  );
    // _H(0x5C3A220, hook_RecoilLatMax,  orig_RecoilLatMax );
    // _H(0x5C3A280, hook_RecoilUpMod,   orig_RecoilUpMod  );
    // _H(0x5C3A2E0, hook_RecoilLatMod,  orig_RecoilLatMod );
    #undef _H
    LOGI("[ENI] InstallFeatureHooks: done");
}

// ════════════════════════════════════════════════════════════════
//  EGL
// ════════════════════════════════════════════════════════════════
static EGLBoolean (*old_eglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
static bool  g_imgui_init = false;
static int   g_width = 0, g_height = 0;

// ════════════════════════════════════════════════════════════════
//  THEME
// ════════════════════════════════════════════════════════════════
static void ApplyCyberpunkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10.f; s.ChildRounding = 7.f;
    s.FrameRounding  = 5.f;  s.GrabRounding  = 5.f;
    s.TabRounding    = 5.f;  s.ScrollbarRounding = 5.f;
    s.WindowBorderSize = 1.f; s.FrameBorderSize = 0.f;
    s.ItemSpacing  = {8.f, 6.f}; s.FramePadding = {8.f, 4.f};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = {0.04f,0.03f,0.09f,0.94f};
    c[ImGuiCol_ChildBg]           = {0.06f,0.04f,0.12f,0.85f};
    c[ImGuiCol_Border]            = {0.00f,0.90f,0.90f,0.35f};
    c[ImGuiCol_Header]            = {0.55f,0.00f,0.75f,0.40f};
    c[ImGuiCol_HeaderHovered]     = {0.65f,0.00f,0.90f,0.55f};
    c[ImGuiCol_HeaderActive]      = {0.75f,0.00f,1.00f,0.70f};
    c[ImGuiCol_CheckMark]         = {0.00f,1.00f,1.00f,1.00f};
    c[ImGuiCol_Button]            = {0.00f,0.60f,0.70f,0.35f};
    c[ImGuiCol_ButtonHovered]     = {0.00f,0.80f,0.90f,0.55f};
    c[ImGuiCol_ButtonActive]      = {0.00f,1.00f,1.00f,0.70f};
    c[ImGuiCol_SliderGrab]        = {0.00f,1.00f,1.00f,0.80f};
    c[ImGuiCol_SliderGrabActive]  = {0.55f,0.00f,1.00f,1.00f};
    c[ImGuiCol_FrameBg]           = {0.08f,0.06f,0.16f,0.80f};
    c[ImGuiCol_FrameBgHovered]    = {0.10f,0.08f,0.22f,0.85f};
    c[ImGuiCol_FrameBgActive]     = {0.14f,0.10f,0.28f,0.90f};
    c[ImGuiCol_TitleBg]           = {0.02f,0.01f,0.07f,1.00f};
    c[ImGuiCol_TitleBgActive]     = {0.04f,0.02f,0.12f,1.00f};
    c[ImGuiCol_Tab]               = {0.03f,0.02f,0.09f,0.90f};
    c[ImGuiCol_TabHovered]        = {0.00f,0.70f,0.80f,0.60f};
    c[ImGuiCol_TabActive]         = {0.35f,0.00f,0.65f,0.80f};
    c[ImGuiCol_TabUnfocusedActive]= {0.20f,0.00f,0.45f,0.75f};
    c[ImGuiCol_Text]              = {0.88f,0.95f,1.00f,1.00f};
    c[ImGuiCol_TextDisabled]      = {0.35f,0.40f,0.50f,1.00f};
    c[ImGuiCol_ScrollbarBg]       = {0.02f,0.01f,0.06f,0.60f};
    c[ImGuiCol_ScrollbarGrab]     = {0.00f,0.60f,0.70f,0.60f};
    c[ImGuiCol_Separator]         = {0.00f,0.70f,0.80f,0.25f};
    c[ImGuiCol_ResizeGrip]        = {0.00f,0.80f,1.00f,0.20f};
    c[ImGuiCol_ResizeGripHovered] = {0.00f,0.90f,1.00f,0.40f};
    c[ImGuiCol_ResizeGripActive]  = {0.55f,0.00f,1.00f,0.70f};
}

// ════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════
static void SectionLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f,1.f,1.f,0.7f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator(); ImGui::Spacing();
}

#include "bypass_anogs.h"
#include "skin_changer.h"

static void NeonToggle(const char* label, bool* v,
                       ImVec4 onColor = {0.f,1.f,1.f,1.f}) {
    if (*v) ImGui::PushStyleColor(ImGuiCol_CheckMark, onColor);
    ImGui::Checkbox(label, v);
    if (*v) ImGui::PopStyleColor();
}
static bool NeonToggle_retval(const char* label, bool* v,
                              ImVec4 onColor = {0.f,1.f,1.f,1.f}) {
    if (*v) ImGui::PushStyleColor(ImGuiCol_CheckMark, onColor);
    bool ch = ImGui::Checkbox(label, v);
    if (*v) ImGui::PopStyleColor();
    return ch;
}

// ════════════════════════════════════════════════════════════════
//  FLOATING ICON + MENU
// ════════════════════════════════════════════════════════════════
static void DrawMenu() {
    static bool    showMenu   = false;          // menu starts closed
    static ImVec2  iconPos    = {50.f, 200.f};  // icon position (draggable)
    static bool    wasDragging= false;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x, sh = io.DisplaySize.y;

    const float ICON_R  = 28.f;   // icon radius
    const float ICON_D  = ICON_R * 2.f;

    // ── FLOATING ICON WINDOW ─────────────────────────────────────
    // Transparent, no chrome, just our draw area
    ImGui::SetNextWindowPos(iconPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ICON_D, ICON_D});
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f,0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {ICON_D,ICON_D});
    ImGui::Begin("##gaeris_icon", nullptr,
        ImGuiWindowFlags_NoDecoration  |
        ImGuiWindowFlags_NoMove        |
        ImGuiWindowFlags_NoScrollbar   |
        ImGuiWindowFlags_NoNav         |
        ImGuiWindowFlags_NoSavedSettings|
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* idl = ImGui::GetWindowDrawList();
    ImVec2 center = {iconPos.x + ICON_R, iconPos.y + ICON_R};

    // Pulse glow ring (animated using time)
    float t      = (float)ImGui::GetTime();
    float pulse  = 0.45f + 0.25f * sinf(t * 3.0f);
    ImU32 ring1  = showMenu
                 ? IM_COL32(160,  0, 255, (int)(pulse*220))
                 : IM_COL32(  0, 220, 230, (int)(pulse*180));
    ImU32 ring2  = showMenu
                 ? IM_COL32(200, 50, 255, 90)
                 : IM_COL32(  0, 255, 255, 70);

    idl->AddCircle(center, ICON_R + 5.f, ring2,  64, 2.5f);  // outer glow
    idl->AddCircle(center, ICON_R,       ring1,  64, 1.8f);  // inner ring
    idl->AddCircleFilled(center, ICON_R - 2.f,
                         showMenu ? IM_COL32(15,3,30,235)
                                  : IM_COL32(4,4,18,230));   // bg fill

    // "G" letter centred
    const char* glyph = "G";
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(20.f, FLT_MAX, 0.f, glyph);
    ImU32 tc = showMenu ? IM_COL32(190,60,255,255)
                        : IM_COL32(0,255,255,255);
    idl->AddText(ImGui::GetFont(), 20.f,
                 {center.x - ts.x * 0.5f, center.y - ts.y * 0.5f},
                 tc, glyph);

    // Invisible button — handles tap + drag
    ImGui::InvisibleButton("##icon_btn", {ICON_D, ICON_D});

    bool isActive  = ImGui::IsItemActive();
    bool isClicked = ImGui::IsItemDeactivated()  // released
                  && !wasDragging;               // without drag

    if (isActive && ImGui::IsMouseDragging(0, 6.f)) {
        wasDragging = true;
        iconPos.x  += io.MouseDelta.x;
        iconPos.y  += io.MouseDelta.y;
        // clamp to screen
        iconPos.x = ImClamp(iconPos.x, 0.f, sw - ICON_D);
        iconPos.y = ImClamp(iconPos.y, 0.f, sh - ICON_D);
    }
    if (!isActive) wasDragging = false;
    if (isClicked) showMenu = !showMenu;

    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!showMenu) {
        // FOV circle when menu hidden — always useful to see
        if (bAimbot && bAimFOV) {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            dl->AddCircle({sw*0.5f, sh*0.5f}, fAimFOVSize,
                          IM_COL32(0,255,255,70), 64, 1.2f);
        }
        return;
    }

    // ── FULL MENU PANEL ──────────────────────────────────────────
    // Positioned so it doesn't overlap the icon
    float panelW = sw * 0.58f;
    float panelH = sh * 0.80f;
    float panelX = (iconPos.x > sw * 0.5f)
                 ? iconPos.x - panelW - 12.f
                 : iconPos.x + ICON_D  + 12.f;
    float panelY = ImClamp(iconPos.y - 20.f, 0.f, sh - panelH);

    ImGui::SetNextWindowPos({panelX, panelY}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({panelW, panelH}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGui::Begin("  ★ GAERIS  v1.0", &showMenu,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoCollapse  |
        ImGuiWindowFlags_NoResize    |
        ImGuiWindowFlags_NoMove);

    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.f,1.f,1.f,0.5f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("MainTabs")) {

        if (ImGui::BeginTabItem(" AIM ")) {
            ImGui::Spacing();
            SectionLabel("[ AIMBOT ]");
            NeonToggle("Aimbot",    &bAimbot,    {0.f,1.f,1.f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("Silent Aim",&bSilentAim, {1.f,0.f,1.f,1.f});
            NeonToggle("FOV Circle",&bAimFOV);
            ImGui::SameLine(0,20);
            NeonToggle("Triggerbot",&bTriggerbot,{1.f,0.5f,0.f,1.f});
            ImGui::Spacing();
            ImGui::SliderFloat("FOV Size",&fAimFOVSize,20.f,400.f,"%.0f px");
            ImGui::SliderFloat("Smooth",  &fAimSmooth, 1.f, 20.f,"%.1f");
            ImGui::Spacing();
            SectionLabel("[ BONE ]");
            const char* bones[]={"Head","Neck","Chest"};
            ImGui::Combo("Target Bone",&iAimBone,bones,3);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(" ESP ")) {
            ImGui::Spacing();
            SectionLabel("[ VISUALS ]");
            ImGui::Columns(2,nullptr,false);
            NeonToggle("Box ESP",   &bESP_Box);
            NeonToggle("Corner Box",&bCornerBox);
            NeonToggle("Fill Box",  &bBoxFill);
            NeonToggle("Skeleton",  &bESP_Skeleton);
            ImGui::NextColumn();
            NeonToggle("Health Bar",&bESP_Health);
            NeonToggle("Name",      &bESP_Name);
            NeonToggle("Distance",  &bESP_Distance);
            NeonToggle("Snap Line", &bSnapLine);
            ImGui::Columns(1);
            ImGui::Spacing();
            SectionLabel("[ COLORS ]");
            ImGui::ColorEdit4("Box Color", espColorBox, ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit4("Line Color",espColorLine,ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit4("Name Color",espColorName,ImGuiColorEditFlags_NoInputs);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(" WPN ")) {
            ImGui::Spacing();
            SectionLabel("[ CONTROL ]");
            NeonToggle("No Recoil", &bNoRecoil, {0.f,1.f,0.5f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("No Spread", &bNoSpread, {0.f,0.8f,0.4f,1.f});
            NeonToggle("Rapid Fire",&bRapidFire,{1.f,0.4f,0.f,1.f});
            ImGui::Spacing();
            SectionLabel("[ SKIN CHANGER ]");
            NeonToggle("Skin Changer",&bSkinChanger,{1.f,0.f,1.f,1.f});
            if (bSkinChanger) {
                NeonToggle("Weapon Skin",&bWeaponSkin,{0.f,1.f,1.f,1.f});
                if (bWeaponSkin) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*0.6f);
                    ImGui::InputInt("Weapon ID##ws",&iWeaponSkinID);
                    if (iWeaponSkinID<0) iWeaponSkinID=0;
                }
                ImGui::Spacing();
                NeonToggle("Operator Skin",&bOperatorSkin,{1.f,0.f,1.f,1.f});
                if (bOperatorSkin) {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*0.6f);
                    ImGui::InputInt("Operator ID##os",&iOperatorSkinID);
                    if (iOperatorSkinID<0) iOperatorSkinID=0;
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(" MOV ")) {
            ImGui::Spacing();
            SectionLabel("[ MOVEMENT ]");
            NeonToggle("Speed Hack",&bSpeedHack,{0.f,1.f,1.f,1.f});
            if (bSpeedHack)
                ImGui::SliderFloat("Speed Mult",&fSpeedMult,1.f,5.f,"%.1fx");
            ImGui::Spacing();
            NeonToggle("High Jump", &bHighJump, {1.f,0.f,1.f,1.f});
            if (bHighJump)
                ImGui::SliderFloat("Jump Mult",&fJumpMult,1.f,10.f,"%.1fx");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(" MISC ")) {
            ImGui::Spacing();
            SectionLabel("[ BYPASS ]");
            bool bypassOn = AnogsGetBypass();
            if (NeonToggle_retval("AnoSDK Bypass",&bypassOn,{0.f,1.f,0.5f,1.f}))
                AnogsSetBypass(bypassOn);
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.5f,0.6f,0.7f,1.f));
            ImGui::TextWrapped(bypassOn
                ? "ON: AnoSDK reporting blocked."
                : "OFF: AnoSDK jalan normal.");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            SectionLabel("[ MISC ]");
            NeonToggle("Show FPS",&bShowFPS);
            if (bShowFPS) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.f,1.f,1.f,0.9f));
                ImGui::Text("FPS: %.1f", io.Framerate);
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            SectionLabel("[ INFO ]");
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.5f,0.6f,0.7f,1.f));
            ImGui::Text("Base: 0x%" PRIxPTR, (uintptr_t)g_il2cppBaseMap.startAddress);
            ImGui::Text("Screen: %.0fx%.0f", sw, sh);
            ImGui::Text("Bypass: %s", AnogsGetBypass() ? "ACTIVE" : "off");
            ImGui::PopStyleColor();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4(0.f,1.f,1.f,0.3f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.3f,0.5f,0.6f,0.8f));
    ImGui::Text("Tap G icon to close");
    ImGui::PopStyleColor();
    ImGui::End();

    // FOV overlay on top
    if (bAimbot && bAimFOV) {
        ImGui::GetBackgroundDrawList()->AddCircle(
            {sw*0.5f, sh*0.5f}, fAimFOVSize,
            IM_COL32(0,255,255,80), 64, 1.2f);
    }
}

// ════════════════════════════════════════════════════════════════
//  ESP DRAW
// ════════════════════════════════════════════════════════════════
static void DrawESP(ImDrawList* dl, float sw, float sh) {
    if (!bESP_Box && !bESP_Line && !bESP_Name && !bESP_Health && !bESP_Skeleton) return;
    auto enemies = GetEnemyPawns();
    if (!enemies) return;
    auto items = enemies->getItems();
    if (!items) return;
    uintptr_t local = GetLocalPawn();
    for (int i = 0; i < enemies->getSize(); i++) {
        uintptr_t p = items[i];
        if (!p || p == local || !IsPawnAlive(p)) continue;
        uintptr_t head = GetPawnHeadBone(p);
        uintptr_t mesh = GetPawnMesh(p);
        if (!head || !mesh) continue;
        Vector3 headSc = WorldToScreen(Transform_GetPos(head));
        Vector3 rootSc = WorldToScreen(Transform_GetPos(mesh));
        if (headSc.Z <= 0 || rootSc.Z <= 0) continue;
        float hx=headSc.X, hy=sh-headSc.Y;
        float rx=rootSc.X, ry=sh-rootSc.Y;
        float h=ry-hy, w=h*0.4f;
        ImU32 cBox =IM_COL32((int)(espColorBox[0]*255),(int)(espColorBox[1]*255),
                              (int)(espColorBox[2]*255),(int)(espColorBox[3]*255));
        ImU32 cLine=IM_COL32((int)(espColorLine[0]*255),(int)(espColorLine[1]*255),
                              (int)(espColorLine[2]*255),(int)(espColorLine[3]*255));
        ImU32 cName=IM_COL32((int)(espColorName[0]*255),(int)(espColorName[1]*255),
                              (int)(espColorName[2]*255),(int)(espColorName[3]*255));
        if (bESP_Box) {
            if (bBoxFill)
                dl->AddRectFilled({hx-w,hy},{hx+w,ry},IM_COL32(255,0,0,25));
            if (bCornerBox) {
                float cw=w*0.3f,ch=h*0.2f;
                dl->AddLine({hx-w,hy},{hx-w+cw,hy},cBox,1.5f);
                dl->AddLine({hx-w,hy},{hx-w,hy+ch},cBox,1.5f);
                dl->AddLine({hx+w,hy},{hx+w-cw,hy},cBox,1.5f);
                dl->AddLine({hx+w,hy},{hx+w,hy+ch},cBox,1.5f);
                dl->AddLine({hx-w,ry},{hx-w+cw,ry},cBox,1.5f);
                dl->AddLine({hx-w,ry},{hx-w,ry-ch},cBox,1.5f);
                dl->AddLine({hx+w,ry},{hx+w-cw,ry},cBox,1.5f);
                dl->AddLine({hx+w,ry},{hx+w,ry-ch},cBox,1.5f);
            } else {
                dl->AddRect({hx-w,hy},{hx+w,ry},cBox,0.f,0,1.5f);
            }
        }
        if (bESP_Line||bSnapLine)
            dl->AddLine({sw*0.5f,sh},{rx,ry},cLine,1.f);
        if (bESP_Health) {
            float hp=GetPawnHealth(p), mhp=GetPawnMaxHealth(p);
            float pct=(mhp>0)?(hp/mhp):1.f;
            float bx=hx-w-5.f;
            dl->AddRectFilled({bx-2.f,hy},{bx+2.f,ry},IM_COL32(40,40,40,180));
            dl->AddRectFilled({bx-2.f,hy+h*(1.f-pct)},{bx+2.f,ry},
                IM_COL32((int)((1.f-pct)*255),(int)(pct*255),0,220));
        }
        if (bESP_Name||bESP_Distance) {
            char buf[128];
            float dist=headSc.Z;
            if (bESP_Name&&bESP_Distance) snprintf(buf,sizeof(buf),"%s [%.0fm]",GetPawnName(p).c_str(),dist);
            else if (bESP_Name)           snprintf(buf,sizeof(buf),"%s",GetPawnName(p).c_str());
            else                          snprintf(buf,sizeof(buf),"%.0fm",dist);
            dl->AddText({hx,hy-14.f},cName,buf);
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  AIMBOT TICK
// ════════════════════════════════════════════════════════════════
static void AimbotTick(float sw, float sh) {
    if (!bAimbot) return;
    uintptr_t target = GetAimbotTarget((int)sw,(int)sh);
    if (target) DoAimbot(target);
}

// ════════════════════════════════════════════════════════════════
//  EGL SWAP HOOK
// ════════════════════════════════════════════════════════════════
EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    eglQuerySurface(display, surface, EGL_WIDTH,  &g_width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &g_height);

    if (!g_imgui_init) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize  = {(float)g_width,(float)g_height};
        io.IniFilename  = nullptr;

        ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
        float fs = (float)g_height * 0.022f;
        extern unsigned char Roboto_Regular[];
        io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 168260, fs, &fc);
        io.Fonts->Build();

        ApplyCyberpunkTheme();
        ImGui_ImplOpenGL3_Init("#version 100");
        // NOTE: ImGui_ImplAndroid_Init(nullptr) skipped — nullptr crashes
        // ANativeWindow_getWidth. We drive DeltaTime manually below.

        g_imgui_init = true;
        LOGI("[ENI] ImGui init: %dx%d  font %.1fpx", g_width, g_height, fs);
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {(float)g_width,(float)g_height};

    // Manual DeltaTime — replaces ImGui_ImplAndroid_NewFrame()
    {
        static double _t = 0.0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double now = (double)ts.tv_sec + ts.tv_nsec * 1e-9;
        io.DeltaTime = (_t > 0.0) ? (float)(now - _t) : 1.f/60.f;
        _t = now;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    float sw=(float)g_width, sh=(float)g_height;

    // Background draw list for ESP
    ImDrawList* bgDL = ImGui::GetBackgroundDrawList();
    // DrawESP(bgDL, sw, sh);   // activate after RVA verify
    // AimbotTick(sw, sh);

    DrawMenu();   // floating icon + panel

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glViewport(0, 0, g_width, g_height);

    return old_eglSwapBuffers(display, surface);
}