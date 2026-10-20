# Third-party notices

| Component | Where | License |
|---|---|---|
| Dear ImGui v1.92.9b, Omar Cornut | `third_party/imgui` (git submodule) | MIT, `third_party/imgui/LICENSE.txt` |
| LatoLatin Regular/Bold 2.015, Łukasz Dziedzic (Reserved Font Name "Lato") | `assets/fonts/` | SIL Open Font License 1.1, `LICENSES/Lato-OFL-1.1.txt` |
| `src/recomp_netplay_account.h`, the shared account-sync helper, from RetroPortingToolKit/recomp-ui (`41c88ff`), Copyright (c) 2026 Matthew Stanley | `src/recomp_netplay_account.h` | MIT, `LICENSES/recomp-ui-MIT.txt` |
| `src/recomp_launcher.h`, the launcher C ABI, from RetroPortingToolKit/recomp-ui (`41c88ff`), Copyright (c) 2026 Matthew Stanley | `src/recomp_launcher.h` | MIT, `LICENSES/recomp-ui-MIT.txt` |
| `src/recomp_runtime_ui.h`, `src/common/recomp_runtime_ui.c`, `src/common/recomp_runtime_ui_internal.h` and `src/common/launcher_theme.h`, the renderer-agnostic runtime menu psxrecomp links as its no-provider fallback, from RetroPortingToolKit/recomp-ui (`6771ea0`), Copyright (c) 2026 Matthew Stanley | `src/recomp_runtime_ui.h`, `src/common/` | MIT, `LICENSES/recomp-ui-MIT.txt` |
| stb_image v2.30, Sean Barrett | `third_party/stb/stb_image.h` | Public domain or MIT (choice in the file) |
| SDL3, Sam Lantinga and contributors | not vendored; linked by the host build (psxrecomp pins it) | zlib license, https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt |

## Original assets

- `assets/skins/r4/sfx/*.wav`: synthesised for this project by `tools/gen_sfx.py`
  (sine sweeps with a short envelope). No recorded or game audio.
- Both skins use only procedural shapes, colours and the Lato fonts. No game
  art, logos or screen captures are included. A player may point a skin at
  their own files (`$exe/...`, `"optional": true`), which are never shipped.

R4: Ridge Racer Type 4 is a trademark of Bandai Namco Entertainment. This
project is not affiliated with or endorsed by Bandai Namco.
