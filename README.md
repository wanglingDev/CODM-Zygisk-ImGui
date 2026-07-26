# CODM Garena - Zygisk ImGui Mod Menu
**Dump: 25 July 2026 | com.garena.game.codm**

## Features
- **ESP Box** — Box around enemies
- **ESP Line** — Line from screen bottom to enemy
- **Health Bar** — Color-coded HP bar
- **Name + Distance** — Enemy info overlay
- **Aimbot** — Head/Neck/Body targeting with FOV circle
- **ImGui Menu** — Clean tabbed UI, draggable

## Architecture
- **Zygisk** — Proper injection via Magisk
- **ImGui + OpenGL3** — Hardware-accelerated overlay
- **Direct RVA** — No dlsym (libunity.so is stripped!)
- **eglSwapBuffers Hook** — Render every frame

## Key Offsets (25/07/2026)
| Function | RVA |
|----------|-----|
| BRGamePlay::get_LocalPawn | 0x5B671AC |
| GamePlay::get_Game | 0xC3FE434 |
| BaseGame::GetEnemyPawns | 0x73B8D9C |
| Camera::get_main | 0x4E09834 |
| Transform::get_position | 0x4E63820 |
| Pawn::SetAimRotation | 0x4FAE5DC |

| Field | Offset |
|-------|--------|
| Pawn::m_HeadBone | 0x308 |
| Pawn::m_IsAlive | 0x548 |
| Pawn::m_PlayerInfo | 0x5C0 |
| Pawn::m_Mesh | 0x628 |
| AttackableTarget::m_AttackableInfo | 0x18 |
| AttackableTargetInfo::m_Health | 0x34 |
| AttackableTargetInfo::m_MaxHealth | 0x38 |
| PlayerInfo::m_NickName | 0x158 |

## Build
1. Fork → GitHub Actions → `Build CODM Garena Zygisk Module`
2. Download artifact → install via Magisk

## Credits
- Template: fedes1to/Zygisk-ImGui-Menu
- ESP Base: LGLTeam/springmusk026
- Dump: PMT Dumper + CodMDumper (tien0246)
- wanglingDev x ENI

## ⚠️ DISCLAIMER
Educational purposes only. Test on alt account.
