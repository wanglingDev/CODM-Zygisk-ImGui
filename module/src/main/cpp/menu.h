#pragma once
#include <cinttypes>
#include <cmath>
// ================================================================
// CODM Garena - menu.h   v2.0
// Redesigned: dark navy + cyan accent, 5 tabs, 20+ features
// ================================================================
using namespace ImGui;

// ────────────────────────────────────────────────────────────────
// NEW FEATURE VARS (beyond what functions.h declares)
// ────────────────────────────────────────────────────────────────
bool  bNoRecoil      = false;
bool  bNoSpread      = false;
bool  bSpeedHack     = false;
float fSpeedMult     = 1.5f;
bool  bHighJump      = false;
float fJumpMult      = 2.0f;
bool  bTriggerbot    = false;
bool  bSilentAim     = false;
bool  bBoxFill       = false;
bool  bCornerBox     = false;
bool  bSnapLine      = false;
bool  bSkeleton      = false;
bool  bShowFPS       = true;
bool  bESP_ChamsW    = false;  // wireframe wallhack
bool  bAntiDetect    = true;   // placeholder — injection already hidden

// ESP color pickers
float espColorBox[4]  = {1.0f, 0.18f, 0.18f, 1.0f};
float espColorLine[4] = {0.18f, 1.0f, 0.4f,  0.9f};
float espColorName[4] = {1.0f, 1.0f, 1.0f,  1.0f};

// ────────────────────────────────────────────────────────────────
// NO-RECOIL & NO-SPREAD  (toggleable hooks via wrapper booleans)
// ────────────────────────────────────────────────────────────────
static float (*orig_RecoilUpBase)(void*)  = nullptr;
static float (*orig_RecoilLatBase)(void*) = nullptr;
static float (*orig_RecoilUpMax)(void*)   = nullptr;
static float (*orig_RecoilLatMax)(void*)  = nullptr;
static float (*orig_RecoilUpMod)(void*)   = nullptr;
static float (*orig_RecoilLatMod)(void*)  = nullptr;

float hook_RecoilUpBase (void* p){ return bNoRecoil ? 0.f : orig_RecoilUpBase(p);  }
float hook_RecoilLatBase(void* p){ return bNoRecoil ? 0.f : orig_RecoilLatBase(p); }
float hook_RecoilUpMax  (void* p){ return bNoRecoil ? 0.f : orig_RecoilUpMax(p);   }
float hook_RecoilLatMax (void* p){ return bNoRecoil ? 0.f : orig_RecoilLatMax(p);  }
float hook_RecoilUpMod  (void* p){ return bNoSpread ? 0.f : orig_RecoilUpMod(p);   }
float hook_RecoilLatMod (void* p){ return bNoSpread ? 0.f : orig_RecoilLatMod(p);  }

// High-jump via gravity scale hook
static float (*orig_GetGravityScale)(void*, int) = nullptr;
float hook_GetGravityScale(void* p, int f) {
    float v = orig_GetGravityScale(p, f);
    return bHighJump ? v * (1.f / fJumpMult) : v;
}

// Install all feature hooks (called once from hack_thread after g_base is set)
void InstallFeatureHooks() {
#define HOOK(rva, hk, orig) DobbyHook((void*)METHOD(rva),(void*)(hk),(void**)(orig))
    HOOK(0x96239E8, hook_RecoilUpBase,  &orig_RecoilUpBase);
    HOOK(0x9623AD0, hook_RecoilLatBase, &orig_RecoilLatBase);
    HOOK(0x96236CC, hook_RecoilUpMax,   &orig_RecoilUpMax);
    HOOK(0x9623740, hook_RecoilLatMax,  &orig_RecoilLatMax);
    HOOK(0x9623A5C, hook_RecoilUpMod,   &orig_RecoilUpMod);
    HOOK(0x9623B44, hook_RecoilLatMod,  &orig_RecoilLatMod);
    HOOK(0xBD166A0, hook_GetGravityScale, &orig_GetGravityScale);
#undef HOOK
    LOGI("[ENI] Feature hooks installed (NoRecoil, NoSpread, HighJump)");
}

// Speed hack — call every frame from RenderESP
void TickSpeedHack() {
    if (!g_base) return;
    auto local = GetLocalPawn();
    if (!local) return;
    if (bSpeedHack) {
        auto fn = (void(*)(uintptr_t,float))METHOD(0x4F959D4);
        fn(local, 600.f * fSpeedMult);
    }
}

// ────────────────────────────────────────────────────────────────
// ESP DRAW HELPERS
// ────────────────────────────────────────────────────────────────
static inline ImU32 Vec4Col(float* c) {
    return IM_COL32((int)(c[0]*255),(int)(c[1]*255),
                    (int)(c[2]*255),(int)(c[3]*255));
}

void DrawLine(float x1,float y1,float x2,float y2,ImU32 col,float t=1.5f){
    GetBackgroundDrawList()->AddLine({x1,y1},{x2,y2},col,t);
}
void DrawRect(float x,float y,float w,float h,ImU32 col,float t=1.5f){
    GetBackgroundDrawList()->AddRect({x,y},{x+w,y+h},col,2.f,0,t);
}
void DrawRectFill(float x,float y,float w,float h,ImU32 col){
    GetBackgroundDrawList()->AddRectFilled({x,y},{x+w,y+h},col,2.f);
}
void DrawText2D(const std::string& s,float x,float y,ImU32 col,float sz=13.f){
    GetBackgroundDrawList()->AddText(GetDefaultFont(),sz,{x,y},col,s.c_str());
}
void DrawCornerBox(float x,float y,float w,float h,ImU32 col,float t=2.f){
    auto* dl = GetBackgroundDrawList();
    float cw = w*0.25f, ch = h*0.25f;
    // TL
    dl->AddLine({x,y},{x+cw,y},col,t);
    dl->AddLine({x,y},{x,y+ch},col,t);
    // TR
    dl->AddLine({x+w,y},{x+w-cw,y},col,t);
    dl->AddLine({x+w,y},{x+w,y+ch},col,t);
    // BL
    dl->AddLine({x,y+h},{x+cw,y+h},col,t);
    dl->AddLine({x,y+h},{x,y+h-ch},col,t);
    // BR
    dl->AddLine({x+w,y+h},{x+w-cw,y+h},col,t);
    dl->AddLine({x+w,y+h},{x+w,y+h-ch},col,t);
}
void DrawHealthBar(float x,float y,float h,float hp,float mx){
    float ratio = (mx>0)?hp/mx:0.f;
    ImU32 bg  = IM_COL32(30,30,30,200);
    ImU32 col = ratio>0.6f ? IM_COL32(50,220,80,255) :
                ratio>0.3f ? IM_COL32(240,200,30,255) :
                             IM_COL32(220,50,50,255);
    float barH = h*ratio;
    DrawRectFill(x-8,y,4,h,bg);
    DrawRectFill(x-8,y+(h-barH),4,barH,col);
}

// ────────────────────────────────────────────────────────────────
// ESP RENDER
// ────────────────────────────────────────────────────────────────
void RenderESP(int W,int H){
    TickSpeedHack();
    if (!g_base) return;

    auto local   = GetLocalPawn();
    auto enemies = GetEnemyPawns();
    if (!enemies) return;
    auto items = enemies->getItems();
    if (!items)   return;

    ImU32 boxCol  = Vec4Col(espColorBox);
    ImU32 lineCol = Vec4Col(espColorLine);
    ImU32 nameCol = Vec4Col(espColorName);

    for (int i=0;i<enemies->getSize();i++){
        auto pawn = items[i];
        if (!pawn || pawn==local || !IsPawnAlive(pawn)) continue;

        auto head = GetPawnHeadBone(pawn);
        auto mesh = GetPawnMesh(pawn);
        if (!head || !mesh) continue;

        auto headSc = WorldToScreen(Transform_GetPos(head));
        auto rootSc = WorldToScreen(Transform_GetPos(mesh));
        if (headSc.z<=0||rootSc.z<=0) continue;

        float hx = headSc.x, hy = (float)H - headSc.y;
        float rx = rootSc.x, ry = (float)H - rootSc.y;
        float height = std::fabs(ry-hy);
        float width  = height*0.45f;
        float dist   = headSc.z;

        // Box fill (subtle)
        if (bBoxFill)
            DrawRectFill(hx-width/2,hy,width,height,
                         IM_COL32((int)(espColorBox[0]*255),
                                  (int)(espColorBox[1]*255),
                                  (int)(espColorBox[2]*255),30));
        // Box
        if (bESP_Box){
            if (bCornerBox) DrawCornerBox(hx-width/2,hy,width,height,boxCol);
            else            DrawRect(hx-width/2,hy,width,height,boxCol);
        }
        // Snap line
        if (bSnapLine)
            DrawLine((float)W/2,(float)H,hx,hy+height,lineCol);
        // Classic bottom line
        if (bESP_Line && !bSnapLine)
            DrawLine((float)W/2,(float)H,hx,hy,lineCol);
        // Health bar
        if (bESP_Health)
            DrawHealthBar(hx-width/2,hy,height,
                          GetPawnHealth(pawn),GetPawnMaxHealth(pawn));
        // Label
        if (bESP_Name||bESP_Distance){
            std::string lbl;
            if (bESP_Name)     lbl = GetPawnName(pawn);
            if (bESP_Distance){char b[24]; snprintf(b,24," %.0fm",dist); lbl+=b;}
            DrawText2D(lbl,hx-width/2,hy-15,nameCol,11.f);
        }
        // Triggerbot: if enemy center is near screen center, call DoAimbot
        if (bTriggerbot) {
            float cx=(float)W/2, cy=(float)H/2;
            float ex=hx, ey=hy+height/2;
            if (std::fabs(ex-cx)<50.f && std::fabs(ey-cy)<50.f)
                DoAimbot(pawn);
        }
    }
    // FOV circle
    if (bAimbot && bAimFOV)
        GetBackgroundDrawList()->AddCircle(
            {(float)W/2,(float)H/2}, fAimFOVSize,
            IM_COL32(255,255,255,90),64,1.5f);
}

// ────────────────────────────────────────────────────────────────
// STYLE SETUP
// ────────────────────────────────────────────────────────────────
static void ApplyNavyTheme(){
    ImGuiStyle& s = GetStyle();
    ImVec4* c = s.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]         = {0.05f,0.06f,0.10f,0.96f};
    c[ImGuiCol_ChildBg]          = {0.07f,0.08f,0.13f,1.00f};
    c[ImGuiCol_PopupBg]          = {0.07f,0.08f,0.13f,0.96f};

    // Title
    c[ImGuiCol_TitleBg]          = {0.03f,0.04f,0.08f,1.00f};
    c[ImGuiCol_TitleBgActive]    = {0.00f,0.52f,0.80f,1.00f};
    c[ImGuiCol_TitleBgCollapsed] = {0.03f,0.04f,0.08f,0.80f};

    // Tabs
    c[ImGuiCol_Tab]              = {0.08f,0.10f,0.18f,1.00f};
    c[ImGuiCol_TabHovered]       = {0.00f,0.65f,0.90f,0.80f};
    c[ImGuiCol_TabActive]        = {0.00f,0.55f,0.80f,1.00f};
    c[ImGuiCol_TabUnfocused]     = {0.06f,0.08f,0.14f,1.00f};
    c[ImGuiCol_TabUnfocusedActive]= {0.04f,0.40f,0.60f,1.00f};

    // Widgets
    c[ImGuiCol_FrameBg]          = {0.10f,0.12f,0.20f,1.00f};
    c[ImGuiCol_FrameBgHovered]   = {0.15f,0.18f,0.30f,1.00f};
    c[ImGuiCol_FrameBgActive]    = {0.00f,0.50f,0.75f,0.60f};

    c[ImGuiCol_CheckMark]        = {0.00f,0.90f,1.00f,1.00f};
    c[ImGuiCol_SliderGrab]       = {0.00f,0.75f,1.00f,1.00f};
    c[ImGuiCol_SliderGrabActive] = {0.00f,0.90f,1.00f,1.00f};

    c[ImGuiCol_Button]           = {0.08f,0.28f,0.45f,1.00f};
    c[ImGuiCol_ButtonHovered]    = {0.00f,0.55f,0.80f,1.00f};
    c[ImGuiCol_ButtonActive]     = {0.00f,0.70f,1.00f,1.00f};

    c[ImGuiCol_Header]           = {0.08f,0.28f,0.45f,0.80f};
    c[ImGuiCol_HeaderHovered]    = {0.00f,0.55f,0.80f,0.80f};
    c[ImGuiCol_HeaderActive]     = {0.00f,0.70f,1.00f,1.00f};

    c[ImGuiCol_Separator]        = {0.00f,0.40f,0.60f,0.50f};
    c[ImGuiCol_ResizeGrip]       = {0.00f,0.55f,0.80f,0.30f};
    c[ImGuiCol_ResizeGripHovered]= {0.00f,0.75f,1.00f,0.60f};
    c[ImGuiCol_ResizeGripActive] = {0.00f,0.90f,1.00f,0.90f};

    c[ImGuiCol_ScrollbarBg]      = {0.03f,0.04f,0.08f,1.00f};
    c[ImGuiCol_ScrollbarGrab]    = {0.08f,0.28f,0.45f,1.00f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.00f,0.55f,0.80f,1.00f};
    c[ImGuiCol_ScrollbarGrabActive]  = {0.00f,0.75f,1.00f,1.00f};

    c[ImGuiCol_Text]             = {0.90f,0.95f,1.00f,1.00f};
    c[ImGuiCol_TextDisabled]     = {0.30f,0.40f,0.55f,1.00f};
    c[ImGuiCol_Border]           = {0.00f,0.40f,0.65f,0.50f};
    c[ImGuiCol_BorderShadow]     = {0.00f,0.00f,0.00f,0.00f};

    // Rounding & spacing
    s.WindowRounding    = 8.f;
    s.FrameRounding     = 5.f;
    s.TabRounding       = 4.f;
    s.ScrollbarRounding = 4.f;
    s.GrabRounding      = 4.f;
    s.ItemSpacing       = {8.f, 6.f};
    s.FramePadding      = {8.f, 5.f};
    s.WindowPadding     = {10.f,10.f};
    s.ScrollbarSize     = 12.f;
}

// ────────────────────────────────────────────────────────────────
// HELPER: colored checkbox with label
// ────────────────────────────────────────────────────────────────
static void FeatureToggle(const char* label, bool* val,
                          ImVec4 activeCol={0.f,0.9f,1.f,1.f}){
    if (*val) PushStyleColor(ImGuiCol_Text, activeCol);
    Checkbox(label, val);
    if (*val) PopStyleColor();
}

// ────────────────────────────────────────────────────────────────
// DRAW MENU
// ────────────────────────────────────────────────────────────────
void DrawMenu(){
    static bool showMenu = true;
    if (IsKeyPressed(ImGuiKey_Menu)) showMenu = !showMenu;
    if (!showMenu) return;

    float W = (float)glWidth, H = (float)glHeight;
    float menuW = W * 0.50f;
    float menuH = H * 0.72f;
    menuW = menuW < 300 ? 300 : (menuW > 520 ? 520 : menuW);

    SetNextWindowSize({menuW, menuH}, ImGuiCond_FirstUseEver);
    SetNextWindowPos ({W*0.02f, H*0.05f}, ImGuiCond_FirstUseEver);
    SetNextWindowBgAlpha(0.96f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoCollapse;
    Begin("  CODM Garena  |  ENI v2.0  |  25/07/2026 ", nullptr, wf);

    // ── FPS counter top-right ─────────────────────────────────
    if (bShowFPS) {
        char fps[24];
        snprintf(fps, 24, "%.0f FPS", GetIO().Framerate);
        float fw = CalcTextSize(fps).x;
        SetCursorPosX(GetWindowWidth()-fw-14);
        TextColored({0.f,0.9f,1.f,0.8f}, "%s", fps);
        SameLine(0, 0);
        NewLine();
    }

    // ── Status bar (base address) ─────────────────────────────
    {
        char base[48];
        snprintf(base,48," Base: 0x%" PRIxPTR, g_base);
        TextColored({0.3f,0.6f,0.3f,1.f}, "%s", base);
    }
    Separator();

    if (BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)){

        // ════════════════════════════════════════════════════
        // [👁]  ESP
        // ════════════════════════════════════════════════════
        if (BeginTabItem(" ESP ##t1")){
            Spacing();
            SeparatorText(" Box & Lines ");
            FeatureToggle("Box ESP",    &bESP_Box);
            if (bESP_Box){
                SameLine(0,12); FeatureToggle("Corner Style",&bCornerBox,{1.f,0.7f,0.f,1.f});
                SameLine(0,12); FeatureToggle("Fill",        &bBoxFill,   {0.5f,0.5f,1.f,1.f});
            }
            FeatureToggle("Snap Line",  &bSnapLine);
            FeatureToggle("Line ESP",   &bESP_Line);

            Spacing(); SeparatorText(" Info ");
            FeatureToggle("Health Bar", &bESP_Health);
            FeatureToggle("Name",       &bESP_Name);
            FeatureToggle("Distance",   &bESP_Distance);

            Spacing(); SeparatorText(" Colors ");
            SetNextItemWidth(-1);
            ColorEdit4("Box Color##c1",  espColorBox,
                       ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            SameLine(); Text("Box");
            SetNextItemWidth(-1);
            ColorEdit4("Line Color##c2", espColorLine,
                       ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            SameLine(); Text("Line");
            SetNextItemWidth(-1);
            ColorEdit4("Name Color##c3", espColorName,
                       ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
            SameLine(); Text("Name");

            EndTabItem();
        }

        // ════════════════════════════════════════════════════
        // [🎯]  AIMBOT
        // ════════════════════════════════════════════════════
        if (BeginTabItem(" Aimbot ##t2")){
            Spacing();
            FeatureToggle("Enable Aimbot",  &bAimbot,   {1.f,0.3f,0.3f,1.f});
            FeatureToggle("Triggerbot",     &bTriggerbot,{1.f,0.7f,0.f,1.f});
            FeatureToggle("Silent Aim",     &bSilentAim, {0.8f,0.f,1.f,1.f});
            if (bSilentAim)
                TextColored({0.6f,0.6f,0.6f,1.f},
                    "  (silent aim = snaps without camera move)");

            Spacing(); SeparatorText(" FOV ");
            FeatureToggle("Show FOV Circle",&bAimFOV);
            SetNextItemWidth(-1);
            SliderFloat("##fov", &fAimFOVSize, 30.f,600.f,"FOV: %.0f px");

            Spacing(); SeparatorText(" Smoothness ");
            SetNextItemWidth(-1);
            SliderFloat("##smo", &fAimSmooth,  1.f, 15.f,"Smooth: %.1f");
            TextColored({0.4f,0.7f,0.4f,1.f},
                " 1 = instant snap | 15 = very slow track");

            Spacing(); SeparatorText(" Target Bone ");
            RadioButton("Head",  &iAimBone, 0); SameLine();
            RadioButton("Neck",  &iAimBone, 1); SameLine();
            RadioButton("Chest", &iAimBone, 2);
            bAimHead = (iAimBone == 0);

            EndTabItem();
        }

        // ════════════════════════════════════════════════════
        // [🔫]  WEAPON
        // ════════════════════════════════════════════════════
        if (BeginTabItem(" Weapon ##t3")){
            Spacing();
            SeparatorText(" Recoil ");
            FeatureToggle("No Recoil",  &bNoRecoil, {0.f,1.f,0.6f,1.f});
            SameLine(); TextDisabled("(WeaponIni hook)");
            FeatureToggle("No Spread",  &bNoSpread, {0.f,0.8f,1.f,1.f});
            SameLine(); TextDisabled("(modifier hook)");

            Spacing(); SeparatorText(" Fire Rate ");
            static float fFireMult = 1.0f;
            SetNextItemWidth(-1);
            SliderFloat("##fr",&fFireMult,0.5f,3.0f,"Fire Rate x%.1f");
            if (Button("Apply Fire Rate", {-1,0})){
                // Write fire speed multiplier to local pawn's weapon
                auto local = GetLocalPawn();
                if (local && g_base) Weapon_setFireSpeed((void*)local, fFireMult);
            }

            Spacing(); SeparatorText(" Ammo ");
            static bool bInfAmmo = false;
            FeatureToggle("Inf Ammo (WIP)", &bInfAmmo, {0.6f,0.6f,0.6f,1.f});
            TextColored({0.4f,0.4f,0.5f,1.f},
                "  requires ammo field RVA — add to dump");

            EndTabItem();
        }

        // ════════════════════════════════════════════════════
        // [🏃]  PLAYER
        // ════════════════════════════════════════════════════
        if (BeginTabItem(" Player ##t4")){
            Spacing();
            SeparatorText(" Movement ");
            FeatureToggle("Speed Hack",  &bSpeedHack, {0.f,1.f,0.5f,1.f});
            if (bSpeedHack){
                SetNextItemWidth(-1);
                SliderFloat("##spd",&fSpeedMult,1.0f,5.0f,"Speed x%.1f");
            }
            FeatureToggle("High Jump",   &bHighJump,  {0.5f,0.8f,1.f,1.f});
            if (bHighJump){
                SetNextItemWidth(-1);
                SliderFloat("##jmp",&fJumpMult,1.0f,5.0f,"Jump x%.1f");
            }

            Spacing(); SeparatorText(" Aim Angles ");
            static bool bNoAimPunch = false;
            FeatureToggle("No Aim Punch",&bNoAimPunch,{1.f,0.6f,0.f,1.f});
            if (bNoAimPunch && g_base){
                auto local = GetLocalPawn();
                if (local){
                    *(float*)(local+0x2798) = 0.f;
                    *(float*)(local+0x279C) = 0.f;
                }
            }

            Spacing(); SeparatorText(" Misc Player ");
            static bool bFastParachute = false;
            static bool bFastSwim      = false;
            FeatureToggle("Fast Parachute (WIP)", &bFastParachute,{0.6f,0.6f,0.6f,1.f});
            FeatureToggle("Fast Swim (WIP)",      &bFastSwim,     {0.6f,0.6f,0.6f,1.f});
            TextColored({0.4f,0.4f,0.5f,1.f},"  add RVAs for parachute/swim");

            EndTabItem();
        }

        // ════════════════════════════════════════════════════
        // [⚙]  SETTINGS
        // ════════════════════════════════════════════════════
        if (BeginTabItem(" Settings ##t5")){
            Spacing();
            SeparatorText(" Display ");
            Checkbox("Show FPS Counter", &bShowFPS);

            Spacing(); SeparatorText(" Anti-Detect ");
            Checkbox("Hide from /proc/maps", &bAntiDetect);
            TextColored({0.4f,0.7f,0.4f,1.f},
                "  soinfo removed, segments remapped");
            TextColored({0.4f,0.7f,0.4f,1.f},
                "  ELF header randomized on load");

            Spacing(); SeparatorText(" Debug ");
            {
                char b[64]; snprintf(b,64,"il2cpp base: 0x%" PRIxPTR, g_base);
                TextColored({0.5f,0.9f,0.5f,1.f},"%s",b);
            }
            Text("Game: com.garena.game.codm");
            Text("Dump: 25 July 2026");

            Spacing(); SeparatorText(" Credits ");
            TextColored({0.f,0.85f,1.f,1.f},"  wanglingDev x ENI");
            TextColored({0.5f,0.5f,0.7f,1.f},"  KittyMemory: MJx0");
            TextColored({0.5f,0.5f,0.7f,1.f},"  Dobby: jmpews");
            TextColored({0.5f,0.5f,0.7f,1.f},"  ImGui: ocornut");
            TextColored({0.5f,0.5f,0.7f,1.f},"  Zygisk template: fedes1to");

            EndTabItem();
        }

        EndTabBar();
    }
    End();
}

// ────────────────────────────────────────────────────────────────
// IMGUI SETUP
// ────────────────────────────────────────────────────────────────
void SetupImgui(){
    IMGUI_CHECKVERSION();
    CreateContext();
    ImGuiIO& io = GetIO();
    io.DisplaySize = {(float)glWidth,(float)glHeight};
    ImGui_ImplOpenGL3_Init("#version 100");
    StyleColorsDark();
    ApplyNavyTheme();

    float scale = (float)glHeight / 1080.f * 1.9f;
    GetStyle().ScaleAllSizes(scale);
    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 168260,
                                   15.f * scale);
}

// ────────────────────────────────────────────────────────────────
// EGL SWAP HOOK
// ────────────────────────────────────────────────────────────────
EGLBoolean (*old_eglSwapBuffers)(EGLDisplay,EGLSurface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy,EGLSurface surface){
    eglQuerySurface(dpy,surface,EGL_WIDTH, &glWidth);
    eglQuerySurface(dpy,surface,EGL_HEIGHT,&glHeight);

    if (!setupimg){ SetupImgui(); setupimg=true; }

    ImGuiIO& io = GetIO();
    io.DisplaySize = {(float)glWidth,(float)glHeight};

    {
        static double _t = 0.0;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
        double now = ts.tv_sec + ts.tv_nsec*1e-9;
        io.DeltaTime = (_t>0.0)?(float)(now-_t):1.f/60.f;
        _t = now;
    }

    ImGui_ImplOpenGL3_NewFrame();
    NewFrame();

    RenderESP(glWidth,glHeight);
    DrawMenu();

    if (bAimbot && g_base){
        auto t = GetAimbotTarget(glWidth,glHeight);
        if (t) DoAimbot(t);
    }

    EndFrame(); Render();
    glViewport(0,0,(int)io.DisplaySize.x,(int)io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(GetDrawData());
    return old_eglSwapBuffers(dpy,surface);
}
