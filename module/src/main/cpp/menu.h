#pragma once
// ================================================================
// CODM Garena - ImGui Menu
// Fresh Dump: 25 July 2026
// ================================================================
using namespace ImGui;

// ================================================================
// ESP DRAWING (OpenGL Lines)
// ================================================================
void DrawLine(float x1, float y1, float x2, float y2,
              ImVec4 color, float thickness = 1.5f) {
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddLine(ImVec2(x1, y1), ImVec2(x2, y2),
                ImGui::ColorConvertFloat4ToU32(color), thickness);
}

void DrawRect(float x, float y, float w, float h,
              ImVec4 color, float thickness = 1.5f) {
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddRect(ImVec2(x, y), ImVec2(x+w, y+h),
                ImGui::ColorConvertFloat4ToU32(color), 0, 0, thickness);
}

void DrawText2D(const std::string& text, float x, float y,
                ImVec4 color = {1,1,1,1}, float size = 13.0f) {
    auto* dl = ImGui::GetBackgroundDrawList();
    dl->AddText(ImGui::GetDefaultFont(), size,
                ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color),
                text.c_str());
}

void DrawHealthBar(float x, float y, float h, float hp, float maxHp) {
    float ratio = hp / maxHp;
    ImVec4 color = ratio > 0.6f ? ImVec4(0,1,0,1) :
                   ratio > 0.3f ? ImVec4(1,1,0,1) :
                                  ImVec4(1,0,0,1);
    float barH = h * ratio;
    DrawRect(x-6, y, 3, h, {0.2f,0.2f,0.2f,0.8f});
    DrawRect(x-6, y+(h-barH), 3, barH, color);
}

// ================================================================
// ESP RENDER (called every frame from eglSwapBuffers hook)
// ================================================================
void RenderESP(int screenW, int screenH) {
    if (!bESP_Box && !bESP_Line && !bESP_Health && !bESP_Name && !bESP_Distance)
        return;
    if (!g_base) return;

    auto local = GetLocalPawn();
    auto enemies = GetEnemyPawns();
    if (!enemies) return;
    auto items = enemies->getItems();
    if (!items) return;

    for (int i = 0; i < enemies->getSize(); i++) {
        auto pawn = items[i];
        if (!pawn || pawn == local || !IsPawnAlive(pawn)) continue;

        auto head = GetPawnHeadBone(pawn);
        auto mesh = GetPawnMesh(pawn);
        if (!head || !mesh) continue;

        auto headSc = WorldToScreen(Transform_GetPos(head));
        auto rootSc = WorldToScreen(Transform_GetPos(mesh));

        if (headSc.Z <= 0 || rootSc.Z <= 0) continue;

        // Screen coords (Unity Y axis flip)
        float hx = headSc.X, hy = screenH - headSc.Y;
        float rx = rootSc.X, ry = screenH - rootSc.Y;
        float height = std::abs(ry - hy);
        float width  = height * 0.45f;
        float dist   = headSc.Z;

        // Distance color fade
        ImVec4 espColor = {1.0f, 0.2f, 0.2f, 1.0f};
        if (dist > 100) espColor = {1.0f, 0.6f, 0.0f, 1.0f};
        if (dist > 200) espColor = {1.0f, 1.0f, 0.0f, 1.0f};

        // Box
        if (bESP_Box)
            DrawRect(hx - width/2, hy, width, height, espColor);

        // Line from bottom center
        if (bESP_Line)
            DrawLine(screenW/2.0f, (float)screenH, hx, hy,
                     {0.2f, 1.0f, 0.2f, 0.8f});

        // Health bar
        if (bESP_Health) {
            float hp = GetPawnHealth(pawn);
            float maxHp = GetPawnMaxHealth(pawn);
            DrawHealthBar(hx - width/2, hy, height, hp, maxHp);
        }

        // Name + distance
        if (bESP_Name || bESP_Distance) {
            std::string label = "";
            if (bESP_Name) label = GetPawnName(pawn);
            if (bESP_Distance) {
                char buf[32];
                snprintf(buf, sizeof(buf), " [%.0fm]", dist);
                label += buf;
            }
            DrawText2D(label, hx - width/2, hy - 16, {1,1,1,1}, 12.0f);
        }
    }

    // FOV Circle
    if (bAimbot && bAimFOV) {
        auto* dl = ImGui::GetBackgroundDrawList();
        dl->AddCircle(ImVec2(screenW/2.0f, screenH/2.0f),
                      fAimFOVSize,
                      IM_COL32(255, 255, 255, 100), 64, 1.0f);
    }
}

// ================================================================
// IMGUI MENU
// ================================================================
void DrawMenu() {
    static bool showMenu = true;

    // Toggle menu dengan button invisible di pojok
    if (IsKeyPressed(ImGuiKey_Menu)) showMenu = !showMenu;
    if (!showMenu) return;

    SetNextWindowSize(ImVec2(320, 480), ImGuiCond_FirstUseEver);
    SetNextWindowBgAlpha(0.88f);

    PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
    PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
    PushStyleColor(ImGuiCol_CheckMark,     ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

    Begin("CODM Garena | Dump: 25/07/2026");

    ImGuiTabBarFlags tab_flags = ImGuiTabBarFlags_FittingPolicyResizeDown;

    if (BeginTabBar("MainTabs", tab_flags)) {

        // ========================
        // ESP TAB
        // ========================
        if (BeginTabItem("ESP")) {
            SeparatorText("Visual");
            Checkbox("Box ESP",      &bESP_Box);
            SameLine(); TextDisabled("(Box around enemy)");

            Checkbox("Line ESP",     &bESP_Line);
            SameLine(); TextDisabled("(Line from bottom)");

            Checkbox("Health Bar",   &bESP_Health);
            Checkbox("Name",         &bESP_Name);
            Checkbox("Distance",     &bESP_Distance);

            Separator();
            TextColored(ImVec4(0.5f,0.5f,0.5f,1), "Dump: 25/07/2026 | Garena");
            EndTabItem();
        }

        // ========================
        // AIMBOT TAB
        // ========================
        if (BeginTabItem("Aimbot")) {
            SeparatorText("Aimbot Settings");
            Checkbox("Enable Aimbot", &bAimbot);

            if (bAimbot) {
                Checkbox("FOV Circle",    &bAimFOV);
                SliderFloat("FOV Size",   &fAimFOVSize, 50.0f, 500.0f);
                SliderFloat("Smoothness", &fAimSmooth, 1.0f, 10.0f);
                Separator();
                SeparatorText("Aim Bone");
                RadioButton("Head",  &iAimBone, 0);
                SameLine();
                RadioButton("Neck",  &iAimBone, 1);
                SameLine();
                RadioButton("Body",  &iAimBone, 2);
                bAimHead = (iAimBone == 0);
            }
            EndTabItem();
        }

        // ========================
        // MISC TAB
        // ========================
        if (BeginTabItem("Misc")) {
            SeparatorText("Info");
            Text("Base: 0x%" PRIxPTR, g_base);
            Text("Game: com.garena.game.codm");
            Text("Dump: 25 July 2026");

            Separator();
            SeparatorText("Credits");
            TextColored(ImVec4(1,0.5f,0,1), "wanglingDev x ENI");
            Text("Template: fedes1to/Zygisk-ImGui-Menu");
            Text("ESP: LGLTeam/springmusk026");
            Text("Dump: PMT Dumper + CodMDumper");
            EndTabItem();
        }

        EndTabBar();
    }
    End();
    PopStyleColor(3);
}

// ================================================================
// SETUP IMGUI
// ================================================================
void SetupImgui() {
    IMGUI_CHECKVERSION();
    CreateContext();
    ImGuiIO& io = GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);
    ImGui_ImplOpenGL3_Init("#version 100");
    StyleColorsDark();

    // Scale untuk layar HP
    float scale = glHeight / 1080.0f * 1.8f;
    GetStyle().ScaleAllSizes(scale);

    io.Fonts->AddFontFromMemoryTTF(Roboto_Regular, 30, 14.0f * scale);
}

// ================================================================
// EGL SWAP BUFFERS HOOK
// ================================================================
EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH,  &glWidth);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &glHeight);

    if (!setupimg) {
        SetupImgui();
        setupimg = true;
    }

    ImGuiIO& io = GetIO();
    io.DisplaySize = ImVec2((float)glWidth, (float)glHeight);

    ImGui_ImplOpenGL3_NewFrame();
    NewFrame();

    // Draw ESP (background layer)
    RenderESP(glWidth, glHeight);

    // Draw Menu (foreground)
    DrawMenu();

    // Aimbot
    if (bAimbot && g_base) {
        auto target = GetAimbotTarget(glWidth, glHeight);
        if (target) DoAimbot(target);
    }

    EndFrame();
    Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return old_eglSwapBuffers(dpy, surface);
}
