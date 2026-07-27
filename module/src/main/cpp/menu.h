#pragma once
#include <cinttypes>
#include <pthread.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_android.h"
#include "functions.h"

// ════════════════════════════════════════════════════════════════
//  FEATURE TOGGLES
// ════════════════════════════════════════════════════════════════
// — Aimbot
bool  bAimbot       = false;
bool  bAimFOV       = true;
bool  bAimHead      = true;
bool  bSilentAim    = false;   // aim without FOV circle / visible snap
bool  bTriggerbot   = false;
float fAimFOVSize   = 150.f;
float fAimSmooth    = 1.f;
int   iAimBone      = 0;        // 0=Head 1=Neck 2=Chest

// — ESP
bool  bESP_Box      = false;
bool  bESP_Line     = false;
bool  bESP_Health   = false;
bool  bESP_Armor    = false;
bool  bESP_Name     = false;
bool  bESP_Distance = false;
bool  bESP_Skeleton = false;
bool  bBoxFill      = false;
bool  bCornerBox    = false;
bool  bSnapLine     = false;
bool  bRadar        = false;   // mini-map radar overlay
float espColorBox[4]  = { 1.f, 0.18f, 0.18f, 1.f };
float espColorLine[4] = { 0.18f, 1.f,  0.4f,  0.9f };
float espColorName[4] = { 1.f,  1.f,  1.f,   1.f  };
float espColorSkel[4] = { 0.f,  1.f,  1.f,   0.7f };
float fRadarScale     = 0.25f;  // world-to-radar scale (meters per pixel)
float fRadarSize      = 150.f;  // radar box width/height in pixels

// — Weapon
bool  bNoRecoil     = false;
bool  bNoSpread     = false;
bool  bRapidFire    = false;
bool  bNoReload     = false;
bool  bFastScope    = false;

// — Movement
bool  bSpeedHack    = false;
float fSpeedMult    = 1.5f;
bool  bHighJump     = false;
float fJumpMult     = 2.f;
bool  bLongSlide    = false;
bool  bFastSwim     = false;

// — BR
bool  bNoParachute  = false;
bool  bAntiFlash    = false;

// — Misc
bool  bShowFPS      = true;
bool  bAntiDetect   = true;
bool  bShowDebug    = false;

// ════════════════════════════════════════════════════════════════
//  EGL HOOK STATE
// ════════════════════════════════════════════════════════════════
extern EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface);
static bool        g_imgui_init = false;
static int         g_width = 0, g_height = 0;

// ════════════════════════════════════════════════════════════════
//  THEME
// ════════════════════════════════════════════════════════════════
static void ApplyCyberpunkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding=8.f; s.ChildRounding=6.f; s.FrameRounding=4.f;
    s.GrabRounding=4.f; s.TabRounding=4.f; s.ScrollbarRounding=4.f;
    s.WindowBorderSize=1.f; s.FrameBorderSize=0.f;
    s.ItemSpacing={8.f,6.f}; s.FramePadding={8.f,4.f};
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]          = {0.04f,0.03f,0.09f,0.92f};
    c[ImGuiCol_ChildBg]           = {0.06f,0.04f,0.12f,0.85f};
    c[ImGuiCol_PopupBg]           = {0.05f,0.04f,0.10f,0.95f};
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
    c[ImGuiCol_ScrollbarGrabHovered]={0.00f,0.80f,0.90f,0.80f};
    c[ImGuiCol_ScrollbarGrabActive]= {0.00f,1.00f,1.00f,1.00f};
    c[ImGuiCol_Separator]         = {0.00f,0.70f,0.80f,0.25f};
    c[ImGuiCol_SeparatorHovered]  = {0.00f,0.90f,1.00f,0.50f};
    c[ImGuiCol_ResizeGrip]        = {0.00f,0.80f,1.00f,0.20f};
    c[ImGuiCol_ResizeGripHovered] = {0.00f,0.90f,1.00f,0.40f};
    c[ImGuiCol_ResizeGripActive]  = {0.55f,0.00f,1.00f,0.70f};
    c[ImGuiCol_PlotLines]         = {0.00f,1.00f,1.00f,0.80f};
    c[ImGuiCol_PlotHistogram]     = {0.55f,0.00f,1.00f,0.90f};
}

static void SectionLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f,1.f,1.f,0.7f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator(); ImGui::Spacing();
}

static void NeonToggle(const char* label, bool* v, ImVec4 onColor={0.f,1.f,1.f,1.f}) {
    if (*v) ImGui::PushStyleColor(ImGuiCol_CheckMark, onColor);
    ImGui::Checkbox(label, v);
    if (*v) ImGui::PopStyleColor();
}

// ════════════════════════════════════════════════════════════════
//  RADAR DRAW
//  — world-space mini-map, drawn bottom-right corner
//  — red dots = enemies, cyan dot = self
// ════════════════════════════════════════════════════════════════
static void DrawRadar(ImDrawList* dl, float sw, float sh) {
    if (!bRadar) return;

    float rx = sw - fRadarSize - 10.f;   // top-left X of radar box
    float ry = sh - fRadarSize - 10.f;   // top-left Y
    float cx = rx + fRadarSize * 0.5f;   // center X
    float cy = ry + fRadarSize * 0.5f;   // center Y

    // Background
    dl->AddRectFilled({rx,ry},{rx+fRadarSize,ry+fRadarSize}, IM_COL32(5,5,20,180), 6.f);
    dl->AddRect       ({rx,ry},{rx+fRadarSize,ry+fRadarSize}, IM_COL32(0,200,220,120), 6.f, 0, 1.5f);
    // Cross-hair
    dl->AddLine({cx,ry},{cx,ry+fRadarSize}, IM_COL32(0,200,220,30));
    dl->AddLine({rx,cy},{rx+fRadarSize,cy}, IM_COL32(0,200,220,30));

    // Self dot
    dl->AddCircleFilled({cx,cy}, 3.f, IM_COL32(0,255,255,255));

    // Get local position for relative placement
    auto local = GetLocalPawn();
    Vector3 localPos = local ? Transform_GetPos(GetPawnMesh(local)) : Vector3{0,0,0};

    auto enemies = GetEnemyPawns();
    if (!enemies) return;
    auto items = enemies->getItems();
    if (!items) return;

    for (int i = 0; i < enemies->getSize(); i++) {
        auto p = items[i];
        if (!p || p == local || !IsPawnAlive(p)) continue;
        auto mesh = GetPawnMesh(p);
        if (!mesh) continue;
        Vector3 ePos = Transform_GetPos(mesh);

        // Relative offset in world units
        float dx = ePos.X - localPos.X;
        float dz = ePos.Z - localPos.Z;

        // Scale to radar pixels
        float px = cx + dx / fRadarScale;
        float py = cy - dz / fRadarScale;  // Z is forward in Unity

        // Clip to radar box
        if (px < rx+2||px > rx+fRadarSize-2||py < ry+2||py > ry+fRadarSize-2) {
            // Draw arrow at edge instead
            float angle = atan2f(dz, dx);
            float ex = cx + cosf(angle)*(fRadarSize*0.45f);
            float ey = cy - sinf(angle)*(fRadarSize*0.45f);
            dl->AddCircleFilled({ex,ey},2.f,IM_COL32(255,80,80,180));
        } else {
            // Health-tinted dot
            float hp = GetPawnHealth(p), mhp = GetPawnMaxHealth(p);
            float pct = (mhp>0)?hp/mhp:1.f;
            ImU32 dotColor = IM_COL32((int)((1.f-pct)*255),(int)(pct*255),0,220);
            dl->AddCircleFilled({px,py}, 4.f, dotColor);
        }
    }

    // Label
    dl->AddText({rx+4.f,ry+2.f}, IM_COL32(0,220,220,180), "RADAR");
}

// ════════════════════════════════════════════════════════════════
//  ESP DRAW
// ════════════════════════════════════════════════════════════════
static void DrawESP(ImDrawList* dl, float sw, float sh) {
    if (!bESP_Box && !bESP_Line && !bESP_Name && !bESP_Health &&
        !bESP_Skeleton && !bESP_Armor && !bESP_Distance && !bSnapLine) return;

    auto enemies = GetEnemyPawns();
    if (!enemies) return;
    auto items = enemies->getItems(); if (!items) return;
    uintptr_t local = GetLocalPawn();

    for (int i = 0; i < enemies->getSize(); i++) {
        uintptr_t p = items[i];
        if (!p || p==local || !IsPawnAlive(p)) continue;

        uintptr_t head = GetPawnHeadBone(p);
        uintptr_t mesh = GetPawnMesh(p);
        if (!head || !mesh) continue;

        Vector3 headSc = WorldToScreen(Transform_GetPos(head));
        Vector3 rootSc = WorldToScreen(Transform_GetPos(mesh));
        if (headSc.Z<=0 || rootSc.Z<=0) continue;

        float hx = headSc.X, hy = sh - headSc.Y;
        float rx = rootSc.X, ry = sh - rootSc.Y;
        float bh = ry - hy, bw = bh * 0.4f;
        float dist = headSc.Z;

        ImU32 cBox  = IM_COL32((int)(espColorBox[0]*255),(int)(espColorBox[1]*255),(int)(espColorBox[2]*255),(int)(espColorBox[3]*255));
        ImU32 cLine = IM_COL32((int)(espColorLine[0]*255),(int)(espColorLine[1]*255),(int)(espColorLine[2]*255),(int)(espColorLine[3]*255));
        ImU32 cName = IM_COL32((int)(espColorName[0]*255),(int)(espColorName[1]*255),(int)(espColorName[2]*255),(int)(espColorName[3]*255));
        ImU32 cSkel = IM_COL32((int)(espColorSkel[0]*255),(int)(espColorSkel[1]*255),(int)(espColorSkel[2]*255),(int)(espColorSkel[3]*255));

        // Box
        if (bESP_Box) {
            if (bBoxFill) dl->AddRectFilled({hx-bw,hy},{hx+bw,ry},IM_COL32(255,0,0,20));
            if (bCornerBox) {
                float cw=bw*0.3f,ch=bh*0.2f;
                dl->AddLine({hx-bw,hy},{hx-bw+cw,hy},cBox,1.5f); dl->AddLine({hx-bw,hy},{hx-bw,hy+ch},cBox,1.5f);
                dl->AddLine({hx+bw,hy},{hx+bw-cw,hy},cBox,1.5f); dl->AddLine({hx+bw,hy},{hx+bw,hy+ch},cBox,1.5f);
                dl->AddLine({hx-bw,ry},{hx-bw+cw,ry},cBox,1.5f); dl->AddLine({hx-bw,ry},{hx-bw,ry-ch},cBox,1.5f);
                dl->AddLine({hx+bw,ry},{hx+bw-cw,ry},cBox,1.5f); dl->AddLine({hx+bw,ry},{hx+bw,ry-ch},cBox,1.5f);
            } else {
                dl->AddRect({hx-bw,hy},{hx+bw,ry},cBox,0.f,0,1.5f);
            }
        }

        // Snap / traceline
        if (bESP_Line || bSnapLine) dl->AddLine({sw*0.5f,sh},{rx,ry},cLine,1.f);

        // Skeleton — approximate: head→neck(1/5)→chest(2/5)→waist(3/5)→feet
        if (bESP_Skeleton) {
            float steps[5] = {0.f,0.2f,0.45f,0.7f,1.f};
            ImVec2 prev={hx,hy};
            for (int s=1;s<5;s++) {
                ImVec2 cur={hx+(rx-hx)*steps[s], hy+(ry-hy)*steps[s]};
                dl->AddLine(prev,cur,cSkel,1.2f);
                // Shoulder width grows then shrinks
                float sideW = bw*(s==1?0.4f:s==2?1.f:s==3?0.8f:0.5f);
                dl->AddLine({cur.x-sideW,cur.y},{cur.x+sideW,cur.y},cSkel,1.f);
                prev=cur;
            }
        }

        // Health bar (left side)
        if (bESP_Health) {
            float hp=GetPawnHealth(p),mhp=GetPawnMaxHealth(p);
            float pct=(mhp>0)?hp/mhp:1.f;
            float bx=hx-bw-6.f;
            dl->AddRectFilled({bx-2.f,hy},{bx+2.f,ry},IM_COL32(30,30,30,160));
            ImU32 cHp=IM_COL32((int)((1.f-pct)*255),(int)(pct*255),0,220);
            dl->AddRectFilled({bx-2.f,hy+bh*(1.f-pct)},{bx+2.f,ry},cHp);
        }

        // Armor bar (right side)
        if (bESP_Armor) {
            float ap=GetPawnArmor(p);
            float pct=(ap>0)?ap/150.f:0.f; if(pct>1.f)pct=1.f;
            float ax=hx+bw+6.f;
            dl->AddRectFilled({ax-2.f,hy},{ax+2.f,ry},IM_COL32(30,30,30,160));
            dl->AddRectFilled({ax-2.f,hy+bh*(1.f-pct)},{ax+2.f,ry},IM_COL32(80,180,255,200));
        }

        // Name + distance
        if (bESP_Name || bESP_Distance) {
            char buf[128];
            if (bESP_Name && bESP_Distance)
                snprintf(buf,sizeof(buf),"%s [%.0fm]",GetPawnName(p).c_str(),dist);
            else if (bESP_Name)
                snprintf(buf,sizeof(buf),"%s",GetPawnName(p).c_str());
            else
                snprintf(buf,sizeof(buf),"%.0fm",dist);
            dl->AddText({hx-bw,hy-14.f},cName,buf);
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  AIMBOT + TRIGGERBOT TICK
// ════════════════════════════════════════════════════════════════
static void AimbotTick(float sw, float sh) {
    // Silent Aim: aim runs but no FOV circle drawn, and aim only snaps
    // on frame to invisible target (useful for Triggerbot combo)
    bool doAim = bAimbot || (bSilentAim && bTriggerbot);
    if (!doAim) return;

    uintptr_t target = GetAimbotTarget((int)sw,(int)sh);
    if (!target) return;

    if (bAimbot || bSilentAim) DoAimbot(target);

    // Triggerbot: if enemy head is within 15px of crosshair, simulate fire
    if (bTriggerbot) {
        auto head = GetPawnHeadBone(target); if (!head) return;
        Vector3 sc = WorldToScreen(Transform_GetPos(head));
        if (sc.Z <= 0) return;
        float dx = sc.X - sw*0.5f, dy = (sh-sc.Y) - sh*0.5f;
        float dist2 = dx*dx + dy*dy;
        if (dist2 <= 15.f*15.f) {
            // Aggressive aim snap to trigger hit registration
            auto local = GetLocalPawn(); if (!local) return;
            auto myMesh = GetPawnMesh(local); if (!myMesh) return;
            Vector3 myPos = Transform_GetPos(myMesh);
            Quaternion snap = LookAt(myPos, Transform_GetPos(head));
            SetPawnAimRotation(local, snap);
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  MENU DRAW
// ════════════════════════════════════════════════════════════════
static void DrawMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x, sh = io.DisplaySize.y;

    // Plain always-visible draggable window — no toggle, no close button.
    // Drag the title bar to reposition. Safe from rapid-tap crashes.
    ImGui::SetNextWindowPos({sw*0.03f,sh*0.05f},ImGuiCond_Once);
    ImGui::SetNextWindowSize({sw*0.58f,sh*0.86f},ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoNav;
    ImGui::Begin("  ★ GAERIS  v2.0", nullptr, wf);

    ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4(0.f,1.f,1.f,0.6f));
    ImGui::Separator(); ImGui::PopStyleColor(); ImGui::Spacing();

    if (ImGui::BeginTabBar("MainTabs")) {

        // ── TAB 1: AIM ───────────────────────────────────────────
        if (ImGui::BeginTabItem(" AIM ")) {
            ImGui::Spacing();
            SectionLabel("[ AIMBOT ]");
            NeonToggle("Aimbot",     &bAimbot,    {0.f,1.f,1.f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("Silent Aim", &bSilentAim, {1.f,0.f,1.f,1.f});

            NeonToggle("FOV Circle", &bAimFOV);
            ImGui::SameLine(0,20);
            NeonToggle("Triggerbot", &bTriggerbot,{1.f,0.5f,0.f,1.f});

            ImGui::Spacing();
            ImGui::SliderFloat("FOV Size", &fAimFOVSize, 20.f, 500.f, "%.0f px");
            ImGui::SliderFloat("Smooth",   &fAimSmooth,   1.f,  30.f, "%.1f");

            ImGui::Spacing();
            SectionLabel("[ BONE TARGET ]");
            const char* bones[] = {"Head","Neck","Chest"};
            ImGui::Combo("Bone",&iAimBone,bones,3);
            bAimHead = (iAimBone == 0);

            ImGui::EndTabItem();
        }

        // ── TAB 2: ESP ───────────────────────────────────────────
        if (ImGui::BeginTabItem(" ESP ")) {
            ImGui::Spacing();
            SectionLabel("[ PLAYERS ]");

            ImGui::Columns(2,nullptr,false);
            NeonToggle("Box ESP",    &bESP_Box);
            NeonToggle("Corner Box", &bCornerBox);
            NeonToggle("Fill Box",   &bBoxFill);
            NeonToggle("Skeleton",   &bESP_Skeleton);
            NeonToggle("Radar",      &bRadar,{0.f,1.f,0.5f,1.f});
            ImGui::NextColumn();
            NeonToggle("Health Bar", &bESP_Health);
            NeonToggle("Armor Bar",  &bESP_Armor, {0.3f,0.6f,1.f,1.f});
            NeonToggle("Name",       &bESP_Name);
            NeonToggle("Distance",   &bESP_Distance);
            NeonToggle("Snap Line",  &bSnapLine);
            ImGui::Columns(1);

            ImGui::Spacing();
            SectionLabel("[ RADAR ]");
            if (bRadar) {
                ImGui::SliderFloat("Radar Size",  &fRadarSize,  80.f, 250.f,"%.0f px");
                ImGui::SliderFloat("Radar Scale", &fRadarScale,  5.f, 100.f,"%.0f m/px");
            }

            ImGui::Spacing();
            SectionLabel("[ COLORS ]");
            ImGui::ColorEdit4("Box",      espColorBox,  ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0,12);
            ImGui::ColorEdit4("Line",     espColorLine, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0,12);
            ImGui::ColorEdit4("Name",     espColorName, ImGuiColorEditFlags_NoInputs);
            ImGui::SameLine(0,12);
            ImGui::ColorEdit4("Skeleton", espColorSkel, ImGuiColorEditFlags_NoInputs);

            ImGui::EndTabItem();
        }

        // ── TAB 3: WEAPON ────────────────────────────────────────
        if (ImGui::BeginTabItem(" WPN ")) {
            ImGui::Spacing();
            SectionLabel("[ RECOIL / SPREAD ]");
            NeonToggle("No Recoil", &bNoRecoil, {0.f,1.f,0.5f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("No Spread", &bNoSpread, {0.f,0.8f,0.4f,1.f});

            ImGui::Spacing();
            SectionLabel("[ FIRE / RELOAD ]");
            NeonToggle("Rapid Fire", &bRapidFire, {1.f,0.4f,0.f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("No Reload",  &bNoReload,  {1.f,0.8f,0.f,1.f});

            ImGui::Spacing();
            SectionLabel("[ ADS / SCOPE ]");
            NeonToggle("Fast Scope / ADS", &bFastScope, {0.f,1.f,1.f,1.f});

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.4f,0.5f,0.6f,1.f));
            ImGui::TextWrapped("Weapon hooks pakai Dobby — uncomment RVA di\nfunctions.h setelah verify di dump.cs.");
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        // ── TAB 4: MOVEMENT ──────────────────────────────────────
        if (ImGui::BeginTabItem(" MOV ")) {
            ImGui::Spacing();
            SectionLabel("[ MOVEMENT ]");

            NeonToggle("Speed Hack", &bSpeedHack, {0.f,1.f,1.f,1.f});
            if (bSpeedHack)
                ImGui::SliderFloat("Speed Mult",&fSpeedMult,1.f,8.f,"%.1fx");

            ImGui::Spacing();
            NeonToggle("High Jump",  &bHighJump, {1.f,0.f,1.f,1.f});
            if (bHighJump)
                ImGui::SliderFloat("Jump Mult",&fJumpMult,1.f,15.f,"%.1fx");

            ImGui::Spacing();
            NeonToggle("Long Slide", &bLongSlide, {0.f,0.8f,1.f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("Fast Swim",  &bFastSwim,  {0.f,0.6f,1.f,1.f});

            ImGui::EndTabItem();
        }

        // ── TAB 5: BR ────────────────────────────────────────────
        if (ImGui::BeginTabItem(" BR ")) {
            ImGui::Spacing();
            SectionLabel("[ BATTLE ROYALE ]");

            NeonToggle("No Parachute", &bNoParachute, {1.f,0.8f,0.f,1.f});
            ImGui::SameLine(0,20);
            NeonToggle("Anti Flash",   &bAntiFlash,   {0.f,1.f,0.5f,1.f});

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.4f,0.5f,0.6f,1.f));
            ImGui::TextWrapped("No Parachute & Anti Flash pakai Dobby hook.\nVerify RVA di dump.cs dulu ya sayang.");
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        // ── TAB 6: MISC ──────────────────────────────────────────
        if (ImGui::BeginTabItem(" MISC ")) {
            ImGui::Spacing();
            SectionLabel("[ MISC ]");

            NeonToggle("Show FPS",    &bShowFPS);
            ImGui::SameLine(0,20);
            NeonToggle("Anti Detect", &bAntiDetect,{0.f,1.f,0.5f,1.f});
            NeonToggle("Debug Info",  &bShowDebug, {0.8f,0.5f,0.f,1.f});

            if (bShowFPS) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.f,1.f,1.f,0.9f));
                ImGui::Text("FPS: %.1f  |  Frame: %d", ImGui::GetIO().Framerate, g_frame_count);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            SectionLabel("[ INFO ]");
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.5f,0.6f,0.7f,1.f));
            ImGui::Text("Base: 0x%" PRIxPTR,(uintptr_t)g_il2cppBaseMap.startAddress);
            ImGui::Text("Screen: %.0fx%.0f | Res: %.2f", sw, sh, sh/1080.f);
            if (bShowDebug) {
                auto local = GetLocalPawn();
                ImGui::Text("LocalPawn: 0x%" PRIxPTR, local);
                if (local) {
                    ImGui::Text("HP: %.0f / %.0f", GetPawnHealth(local), GetPawnMaxHealth(local));
                    auto mesh = GetPawnMesh(local);
                    if (mesh) {
                        Vector3 p = Transform_GetPos(mesh);
                        ImGui::Text("Pos: %.1f  %.1f  %.1f", p.X, p.Y, p.Z);
                    }
                }
            }
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Bottom bar
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4(0.f,1.f,1.f,0.3f));
    ImGui::Separator(); ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.3f,0.5f,0.6f,0.8f));
    ImGui::Text("Drag title bar to move");
    ImGui::PopStyleColor();
    ImGui::End();

    // FOV circle overlay
    if (bAimbot && bAimFOV && !bSilentAim) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddCircle({sw*0.5f,sh*0.5f},fAimFOVSize,
                      IM_COL32(0,255,255,70),64,1.2f);
    }
}

// ════════════════════════════════════════════════════════════════
//  EGL SWAP HOOK
// ════════════════════════════════════════════════════════════════
static int             g_frame_count  = 0;
static pthread_mutex_t g_render_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t       g_render_tid   = 0;

EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    pthread_t self = pthread_self();
    if (g_render_tid == 0) g_render_tid = self;
    if (g_render_tid != self) {
        return orig_eglSwapBuffers ? orig_eglSwapBuffers(display,surface) : EGL_TRUE;
    }
    if (pthread_mutex_trylock(&g_render_mutex) != 0) {
        return orig_eglSwapBuffers ? orig_eglSwapBuffers(display,surface) : EGL_TRUE;
    }

    g_frame_count++;
    eglQuerySurface(display,surface,EGL_WIDTH, &g_width);
    eglQuerySurface(display,surface,EGL_HEIGHT,&g_height);

    if (!g_imgui_init) {
        LOGI("[ENI] ImGui init %dx%d", g_width, g_height);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {(float)g_width,(float)g_height};
        io.IniFilename = nullptr;
        ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
        float fs = (float)g_height * 0.022f;
        extern unsigned char Roboto_Regular[];
        io.Fonts->AddFontFromMemoryTTF(Roboto_Regular,168260,fs,&fc);
        io.Fonts->Build();
        ApplyCyberpunkTheme();
        ImGui_ImplAndroid_Init(nullptr);
        ImGui_ImplOpenGL3_Init("#version 100");
        g_imgui_init = true;
        LOGI("[ENI] ImGui ready (font %.1fpx)", fs);
    }

    bool verbose = (g_frame_count<=5)||(g_frame_count%200==0);

    // Save GL state
    GLint gl_prog,gl_tex,gl_ab,gl_vab,gl_fbo,gl_vp[4],gl_sc[4];
    GLboolean gl_blend,gl_cull,gl_depth,gl_stest,gl_stencil;
    glGetIntegerv(GL_CURRENT_PROGRAM,&gl_prog);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,&gl_tex);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&gl_ab);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&gl_vab);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,&gl_fbo);
    glGetIntegerv(GL_VIEWPORT,gl_vp);
    glGetIntegerv(GL_SCISSOR_BOX,gl_sc);
    gl_blend  =glIsEnabled(GL_BLEND);
    gl_cull   =glIsEnabled(GL_CULL_FACE);
    gl_depth  =glIsEnabled(GL_DEPTH_TEST);
    gl_stest  =glIsEnabled(GL_SCISSOR_TEST);
    gl_stencil=glIsEnabled(GL_STENCIL_TEST);

    // Clean GL state for ImGui
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDisable(GL_DEPTH_TEST);  glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);   glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0,0,g_width,g_height);

    ImGui::GetIO().DisplaySize = {(float)g_width,(float)g_height};
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    DrawMenu();

    ImDrawList* bgDL = ImGui::GetBackgroundDrawList();
    DrawESP(bgDL,(float)g_width,(float)g_height);
    DrawRadar(bgDL,(float)g_width,(float)g_height);
    AimbotTick((float)g_width,(float)g_height);
    TickPatches();

    if (verbose) LOGI("[ENI] frame %d render",g_frame_count);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Restore GL state
    glUseProgram(gl_prog);
    glBindTexture(GL_TEXTURE_2D,gl_tex);
    glBindBuffer(GL_ARRAY_BUFFER,gl_ab);
    glBindVertexArray(gl_vab);
    glBindFramebuffer(GL_FRAMEBUFFER,gl_fbo);
    glViewport(gl_vp[0],gl_vp[1],gl_vp[2],gl_vp[3]);
    glScissor(gl_sc[0],gl_sc[1],gl_sc[2],gl_sc[3]);
    gl_blend  ?glEnable(GL_BLEND)       :glDisable(GL_BLEND);
    gl_cull   ?glEnable(GL_CULL_FACE)   :glDisable(GL_CULL_FACE);
    gl_depth  ?glEnable(GL_DEPTH_TEST)  :glDisable(GL_DEPTH_TEST);
    gl_stest  ?glEnable(GL_SCISSOR_TEST):glDisable(GL_SCISSOR_TEST);
    gl_stencil?glEnable(GL_STENCIL_TEST):glDisable(GL_STENCIL_TEST);

    EGLBoolean r = orig_eglSwapBuffers
        ? orig_eglSwapBuffers(display,surface) : EGL_TRUE;
    pthread_mutex_unlock(&g_render_mutex);
    return r;
}
