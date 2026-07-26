#pragma once
#include <cinttypes>
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
bool  bSilentAim    = false;
bool  bTriggerbot   = false;
float fAimFOVSize   = 150.f;
float fAimSmooth    = 1.f;
int   iAimBone      = 0;       // 0=head 1=neck 2=chest

// — ESP
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

// — Weapon
bool  bNoRecoil     = false;
bool  bNoSpread     = false;
bool  bRapidFire    = false;

// — Movement
bool  bSpeedHack    = false;
float fSpeedMult    = 1.5f;
bool  bHighJump     = false;
float fJumpMult     = 2.f;

// — Misc
bool  bShowFPS      = true;
bool  bAntiDetect   = true;

// ════════════════════════════════════════════════════════════════
//  NO-RECOIL HOOKS  (defined here, installed in InstallFeatureHooks)
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
    // No-Recoil — RVA dari dump.cs (ganti kalau versi beda)
    #define _H(rva, fn, orig) \
        DobbyHook((void*)((uintptr_t)g_il2cppBaseMap.startAddress + (rva)), (void*)(fn), (void**)&(orig))

    //     _H(0x5C3A100, hook_RecoilUpBase,  orig_RecoilUpBase );
    //     _H(0x5C3A160, hook_RecoilLatBase, orig_RecoilLatBase);
    //     _H(0x5C3A1C0, hook_RecoilUpMax,   orig_RecoilUpMax  );
    //     _H(0x5C3A220, hook_RecoilLatMax,  orig_RecoilLatMax );
    //     _H(0x5C3A280, hook_RecoilUpMod,   orig_RecoilUpMod  );
    //     _H(0x5C3A2E0, hook_RecoilLatMod,  orig_RecoilLatMod );
    #undef _H

    LOGI("[ENI] InstallFeatureHooks: done (hooks disabled, pending RVA verify)");
}

// ════════════════════════════════════════════════════════════════
//  EGL SWAP HOOK  (render loop)
// ════════════════════════════════════════════════════════════════
// orig_eglSwapBuffers is defined in hook.cpp (set by DobbyHook)
extern EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay, EGLSurface);
static bool        g_imgui_init = false;
static int         g_width = 0, g_height = 0;

// ── helpers ──────────────────────────────────────────────────────
static void ApplyCyberpunkTheme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 4.f;
    s.GrabRounding      = 4.f;
    s.TabRounding       = 4.f;
    s.ScrollbarRounding = 4.f;
    s.WindowBorderSize  = 1.f;
    s.FrameBorderSize   = 0.f;
    s.ItemSpacing       = { 8.f, 6.f };
    s.FramePadding      = { 8.f, 4.f };

    ImVec4* c = s.Colors;
    // Base dark navy-purple
    c[ImGuiCol_WindowBg]         = { 0.04f, 0.03f, 0.09f, 0.92f };
    c[ImGuiCol_ChildBg]          = { 0.06f, 0.04f, 0.12f, 0.85f };
    c[ImGuiCol_PopupBg]          = { 0.05f, 0.04f, 0.10f, 0.95f };
    // Border cyan
    c[ImGuiCol_Border]           = { 0.00f, 0.90f, 0.90f, 0.35f };
    // Header magenta tint
    c[ImGuiCol_Header]           = { 0.55f, 0.00f, 0.75f, 0.40f };
    c[ImGuiCol_HeaderHovered]    = { 0.65f, 0.00f, 0.90f, 0.55f };
    c[ImGuiCol_HeaderActive]     = { 0.75f, 0.00f, 1.00f, 0.70f };
    // Checkbox / button cyan
    c[ImGuiCol_CheckMark]        = { 0.00f, 1.00f, 1.00f, 1.00f };
    c[ImGuiCol_Button]           = { 0.00f, 0.60f, 0.70f, 0.35f };
    c[ImGuiCol_ButtonHovered]    = { 0.00f, 0.80f, 0.90f, 0.55f };
    c[ImGuiCol_ButtonActive]     = { 0.00f, 1.00f, 1.00f, 0.70f };
    // Slider
    c[ImGuiCol_SliderGrab]       = { 0.00f, 1.00f, 1.00f, 0.80f };
    c[ImGuiCol_SliderGrabActive] = { 0.55f, 0.00f, 1.00f, 1.00f };
    // Frame input
    c[ImGuiCol_FrameBg]          = { 0.08f, 0.06f, 0.16f, 0.80f };
    c[ImGuiCol_FrameBgHovered]   = { 0.10f, 0.08f, 0.22f, 0.85f };
    c[ImGuiCol_FrameBgActive]    = { 0.14f, 0.10f, 0.28f, 0.90f };
    // Title
    c[ImGuiCol_TitleBg]          = { 0.02f, 0.01f, 0.07f, 1.00f };
    c[ImGuiCol_TitleBgActive]    = { 0.04f, 0.02f, 0.12f, 1.00f };
    // Tab
    c[ImGuiCol_Tab]              = { 0.03f, 0.02f, 0.09f, 0.90f };
    c[ImGuiCol_TabHovered]       = { 0.00f, 0.70f, 0.80f, 0.60f };
    c[ImGuiCol_TabActive]        = { 0.35f, 0.00f, 0.65f, 0.80f };
    c[ImGuiCol_TabUnfocusedActive]={ 0.20f, 0.00f, 0.45f, 0.75f };
    // Text
    c[ImGuiCol_Text]             = { 0.88f, 0.95f, 1.00f, 1.00f };
    c[ImGuiCol_TextDisabled]     = { 0.35f, 0.40f, 0.50f, 1.00f };
    // Scrollbar
    c[ImGuiCol_ScrollbarBg]      = { 0.02f, 0.01f, 0.06f, 0.60f };
    c[ImGuiCol_ScrollbarGrab]    = { 0.00f, 0.60f, 0.70f, 0.60f };
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.00f, 0.80f, 0.90f, 0.80f };
    c[ImGuiCol_ScrollbarGrabActive]  = { 0.00f, 1.00f, 1.00f, 1.00f };
    // Separator
    c[ImGuiCol_Separator]        = { 0.00f, 0.70f, 0.80f, 0.25f };
    c[ImGuiCol_SeparatorHovered] = { 0.00f, 0.90f, 1.00f, 0.50f };
    // Resize grip
    c[ImGuiCol_ResizeGrip]       = { 0.00f, 0.80f, 1.00f, 0.20f };
    c[ImGuiCol_ResizeGripHovered]= { 0.00f, 0.90f, 1.00f, 0.40f };
    c[ImGuiCol_ResizeGripActive] = { 0.55f, 0.00f, 1.00f, 0.70f };
    // Plot
    c[ImGuiCol_PlotLines]        = { 0.00f, 1.00f, 1.00f, 0.80f };
    c[ImGuiCol_PlotHistogram]    = { 0.55f, 0.00f, 1.00f, 0.90f };
}

// ── section label helper ─────────────────────────────────────────
static void SectionLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f, 1.f, 1.f, 0.7f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// ── neon checkbox ────────────────────────────────────────────────
static void NeonToggle(const char* label, bool* v, ImVec4 onColor = {0.f, 1.f, 1.f, 1.f}) {
    if (*v)
        ImGui::PushStyleColor(ImGuiCol_CheckMark, onColor);
    ImGui::Checkbox(label, v);
    if (*v)
        ImGui::PopStyleColor();
}

// ════════════════════════════════════════════════════════════════
//  MAIN MENU DRAW
// ════════════════════════════════════════════════════════════════
static void DrawMenu() {
    static bool showMenu = true;

    // Volume-down toggle (ImGuiKey_Menu = hardware menu btn)
    if (ImGui::IsKeyPressed(ImGuiKey_Menu)) showMenu = !showMenu;
    if (!showMenu) return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x, sh = io.DisplaySize.y;

    // Anchor: top-left 10% margin, width 55%, height 80%
    ImGui::SetNextWindowPos({ sw * 0.05f, sh * 0.07f }, ImGuiCond_Once);
    ImGui::SetNextWindowSize({ sw * 0.55f, sh * 0.80f }, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("  ★ GAERIS  v1.0", &showMenu, wf);

    // Glowing title separator
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.f, 1.f, 1.f, 0.6f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── TABS ─────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("MainTabs")) {

        // ── TAB 1: AIMBOT ────────────────────────────────────────
        if (ImGui::BeginTabItem(" AIM ")) {
            ImGui::Spacing();
            SectionLabel("[ AIMBOT ]");

            NeonToggle("Aimbot",     &bAimbot,    {0.f,1.f,1.f,1.f});
            ImGui::SameLine(0, 20);
            NeonToggle("Silent Aim", &bSilentAim, {1.f,0.f,1.f,1.f});

            NeonToggle("FOV Circle", &bAimFOV);
            ImGui::SameLine(0, 20);
            NeonToggle("Triggerbot", &bTriggerbot, {1.f,0.5f,0.f,1.f});

            ImGui::Spacing();
            ImGui::SliderFloat("FOV Size", &fAimFOVSize, 20.f, 400.f, "%.0f px");
            ImGui::SliderFloat("Smooth",   &fAimSmooth,   1.f,  20.f, "%.1f");

            ImGui::Spacing();
            SectionLabel("[ BONE ]");
            const char* bones[] = { "Head", "Neck", "Chest" };
            ImGui::Combo("Target Bone", &iAimBone, bones, 3);

            ImGui::EndTabItem();
        }

        // ── TAB 2: ESP ───────────────────────────────────────────
        if (ImGui::BeginTabItem(" ESP ")) {
            ImGui::Spacing();
            SectionLabel("[ VISUALS ]");

            ImGui::Columns(2, nullptr, false);
            NeonToggle("Box ESP",    &bESP_Box);
            NeonToggle("Corner Box", &bCornerBox);
            NeonToggle("Fill Box",   &bBoxFill);
            NeonToggle("Skeleton",   &bESP_Skeleton);
            ImGui::NextColumn();
            NeonToggle("Health Bar", &bESP_Health);
            NeonToggle("Name",       &bESP_Name);
            NeonToggle("Distance",   &bESP_Distance);
            NeonToggle("Snap Line",  &bSnapLine);
            ImGui::Columns(1);

            ImGui::Spacing();
            SectionLabel("[ COLORS ]");
            ImGui::ColorEdit4("Box Color",  espColorBox,  ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit4("Line Color", espColorLine, ImGuiColorEditFlags_NoInputs);
            ImGui::ColorEdit4("Name Color", espColorName, ImGuiColorEditFlags_NoInputs);

            ImGui::EndTabItem();
        }

        // ── TAB 3: WEAPON ────────────────────────────────────────
        if (ImGui::BeginTabItem(" WPN ")) {
            ImGui::Spacing();
            SectionLabel("[ CONTROL ]");

            NeonToggle("No Recoil",  &bNoRecoil,  {0.f,1.f,0.5f,1.f});
            ImGui::SameLine(0, 20);
            NeonToggle("No Spread",  &bNoSpread,  {0.f,0.8f,0.4f,1.f});
            NeonToggle("Rapid Fire", &bRapidFire, {1.f,0.4f,0.f,1.f});

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.6f,0.7f,1.f));
            ImGui::TextWrapped("No Recoil & No Spread menggunakan Dobby hook\nke recoil methods. Pastikan RVA sesuai dump.");
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        // ── TAB 4: MOVEMENT ──────────────────────────────────────
        if (ImGui::BeginTabItem(" MOV ")) {
            ImGui::Spacing();
            SectionLabel("[ MOVEMENT ]");

            NeonToggle("Speed Hack", &bSpeedHack, {0.f,1.f,1.f,1.f});
            if (bSpeedHack)
                ImGui::SliderFloat("Speed Mult", &fSpeedMult, 1.f, 5.f, "%.1fx");

            ImGui::Spacing();
            NeonToggle("High Jump",  &bHighJump, {1.f,0.f,1.f,1.f});
            if (bHighJump)
                ImGui::SliderFloat("Jump Mult", &fJumpMult, 1.f, 10.f, "%.1fx");

            ImGui::EndTabItem();
        }

        // ── TAB 5: MISC ──────────────────────────────────────────
        if (ImGui::BeginTabItem(" MISC ")) {
            ImGui::Spacing();
            SectionLabel("[ MISC ]");

            NeonToggle("Show FPS",    &bShowFPS);
            NeonToggle("Anti Detect", &bAntiDetect, {0.f,1.f,0.5f,1.f});

            if (bShowFPS) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.f,1.f,1.f,0.9f));
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            SectionLabel("[ INFO ]");
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f,0.6f,0.7f,1.f));
            ImGui::Text("Base: 0x%" PRIxPTR, (uintptr_t)g_il2cppBaseMap.startAddress);
            ImGui::Text("Screen: %.0fx%.0f", sw, sh);
            ImGui::PopStyleColor();

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Bottom bar — keybind reminder
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.f,1.f,1.f,0.3f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f,0.5f,0.6f,0.8f));
    ImGui::Text("Toggle: Menu Button");
    ImGui::PopStyleColor();

    ImGui::End();

    // ── FOV CIRCLE overlay ───────────────────────────────────────
    if (bAimbot && bAimFOV) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddCircle({ sw * 0.5f, sh * 0.5f },
                      fAimFOVSize,
                      IM_COL32(0, 255, 255, 80), 64, 1.2f);
    }
}

// ════════════════════════════════════════════════════════════════
//  ESP DRAW  (called inside eglSwapBuffers)
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

        float hx = headSc.X, hy = sh - headSc.Y;
        float rx = rootSc.X, ry = sh - rootSc.Y;
        float h  = ry - hy;
        float w  = h * 0.4f;

        ImU32 cBox  = IM_COL32((int)(espColorBox[0]*255),  (int)(espColorBox[1]*255),
                                (int)(espColorBox[2]*255),  (int)(espColorBox[3]*255));
        ImU32 cLine = IM_COL32((int)(espColorLine[0]*255), (int)(espColorLine[1]*255),
                                (int)(espColorLine[2]*255), (int)(espColorLine[3]*255));
        ImU32 cName = IM_COL32((int)(espColorName[0]*255), (int)(espColorName[1]*255),
                                (int)(espColorName[2]*255), (int)(espColorName[3]*255));

        // Box
        if (bESP_Box) {
            if (bBoxFill)
                dl->AddRectFilled({ hx-w, hy }, { hx+w, ry }, IM_COL32(255,0,0,25));
            if (bCornerBox) {
                float cw = w * 0.3f, ch = h * 0.2f;
                // TL
                dl->AddLine({hx-w,hy},{hx-w+cw,hy},cBox,1.5f);
                dl->AddLine({hx-w,hy},{hx-w,hy+ch},cBox,1.5f);
                // TR
                dl->AddLine({hx+w,hy},{hx+w-cw,hy},cBox,1.5f);
                dl->AddLine({hx+w,hy},{hx+w,hy+ch},cBox,1.5f);
                // BL
                dl->AddLine({hx-w,ry},{hx-w+cw,ry},cBox,1.5f);
                dl->AddLine({hx-w,ry},{hx-w,ry-ch},cBox,1.5f);
                // BR
                dl->AddLine({hx+w,ry},{hx+w-cw,ry},cBox,1.5f);
                dl->AddLine({hx+w,ry},{hx+w,ry-ch},cBox,1.5f);
            } else {
                dl->AddRect({ hx-w, hy }, { hx+w, ry }, cBox, 0.f, 0, 1.5f);
            }
        }

        // Snap line
        if (bESP_Line || bSnapLine)
            dl->AddLine({ sw*0.5f, sh }, { rx, ry }, cLine, 1.f);

        // Health bar
        if (bESP_Health) {
            float hp  = GetPawnHealth(p);
            float mhp = GetPawnMaxHealth(p);
            float pct = (mhp > 0) ? (hp / mhp) : 1.f;
            float bx  = hx - w - 5.f;
            ImU32 cHpBg  = IM_COL32(40,  40,  40, 180);
            ImU32 cHpFg  = IM_COL32((int)((1.f-pct)*255), (int)(pct*255), 0, 220);
            dl->AddRectFilled({ bx-2.f, hy }, { bx+2.f, ry }, cHpBg);
            dl->AddRectFilled({ bx-2.f, hy + h*(1.f-pct) }, { bx+2.f, ry }, cHpFg);
        }

        // Name + distance
        if (bESP_Name || bESP_Distance) {
            float dist = headSc.Z;
            char buf[128];
            if (bESP_Name && bESP_Distance)
                snprintf(buf, sizeof(buf), "%s [%.0fm]", GetPawnName(p).c_str(), dist);
            else if (bESP_Name)
                snprintf(buf, sizeof(buf), "%s", GetPawnName(p).c_str());
            else
                snprintf(buf, sizeof(buf), "%.0fm", dist);
            dl->AddText({ hx, hy - 14.f }, cName, buf);
        }
    }
}

// ════════════════════════════════════════════════════════════════
//  AIMBOT TICK  (called inside eglSwapBuffers)
// ════════════════════════════════════════════════════════════════
static void AimbotTick(float sw, float sh) {
    if (!bAimbot) return;
    uintptr_t target = GetAimbotTarget((int)sw, (int)sh);
    if (target) DoAimbot(target);
}

// ════════════════════════════════════════════════════════════════
//  EGL SWAP HOOK IMPLEMENTATION
// ════════════════════════════════════════════════════════════════
static int g_frame_count = 0;

EGLBoolean hook_eglSwapBuffers(EGLDisplay display, EGLSurface surface) {
    g_frame_count++;

    // Get surface dimensions
    eglQuerySurface(display, surface, EGL_WIDTH,  &g_width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &g_height);

    if (!g_imgui_init) {
        LOGI("[ENI] frame %d: init start (display=%p surface=%p %dx%d)",
             g_frame_count, display, surface, g_width, g_height);

        IMGUI_CHECKVERSION();
        LOGI("[ENI] frame %d: CreateContext", g_frame_count);
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = { (float)g_width, (float)g_height };
        io.IniFilename = nullptr;

        LOGI("[ENI] frame %d: loading font", g_frame_count);
        ImFontConfig fc;
        fc.FontDataOwnedByAtlas = false;
        float fontSize = (float)g_height * 0.022f;
        extern unsigned char Roboto_Regular[];
        io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 168260, fontSize, &fc);
        io.Fonts->Build();
        LOGI("[ENI] frame %d: font built", g_frame_count);

        ApplyCyberpunkTheme();
        LOGI("[ENI] frame %d: ImGui_ImplAndroid_Init", g_frame_count);
        ImGui_ImplAndroid_Init(nullptr);
        LOGI("[ENI] frame %d: ImGui_ImplOpenGL3_Init", g_frame_count);
        ImGui_ImplOpenGL3_Init("#version 100");

        g_imgui_init = true;
        LOGI("[ENI] ImGui initialized (%dx%d, font %.1fpx)", g_width, g_height, fontSize);
    }

    // Only log every 100 frames to avoid spam
    bool verbose = (g_frame_count <= 5) || (g_frame_count % 100 == 0);
    if (verbose) LOGI("[ENI] frame %d: NewFrame", g_frame_count);

    ImGui::GetIO().DisplaySize = { (float)g_width, (float)g_height };

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    if (verbose) LOGI("[ENI] frame %d: DrawMenu", g_frame_count);
    DrawMenu();

    if (verbose) LOGI("[ENI] frame %d: Render", g_frame_count);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glViewport(0, 0, g_width, g_height);

    if (verbose) LOGI("[ENI] frame %d: calling orig (ptr=%p)", g_frame_count, (void*)orig_eglSwapBuffers);
    if (orig_eglSwapBuffers)
        return orig_eglSwapBuffers(display, surface);
    return EGL_TRUE;
}


