# What a title can show, hide, lock or automate

recomp-launcher can do everything recomp-ui does (docs/PARITY.md). What a
player sees is the title developer's choice, made in one place: the title
layer's **surface manifest** (`titles/<id>/title_<id>.cpp`, `TitleLayer::surface`).

## How a title opts in

The title layer is one C++ file. A game repo can own it: set
`R4L_TITLE_SOURCE` to its path before psxrecomp's `runtime.cmake` is included
(R4 keeps `launcher/title_r4.cpp`). `titles/r4/` here is the reference copy.

```cpp
const SurfaceRule kSurface[] = {
    {"graphics.preset", Vis::Shown, "auto"},        // shown; Auto = detected preset
    {"graphics.frame_generation", Vis::Shown, nullptr},
    {"graphics.dynamic_resolution", Vis::Auto, nullptr},  // hidden, host/preset decides
    {"graphics.vsync", Vis::Locked, "1"},           // shown read-only, pinned to 1
    {"bios.select", Vis::Hidden, nullptr},
};
// TitleLayer{ ..., kSurface, std::size(kSurface), ... }
```

| Visibility | Row | Value |
|---|---|---|
| `Shown` | drawn, editable | the player's |
| `Hidden` | not drawn | whatever the host seeded (settings.toml / game.toml) |
| `Locked` | drawn, read-only | pinned to `value` on every open |
| `Auto` | not drawn | pinned to `value`; `"auto"` or `nullptr` = the automatic behaviour |

## The default rule (keys a title does not declare)

- **Essentials are shown:** disc setup and auto-scan, the graphics preset (Auto)
  and Re-detect, fullscreen, controls basics (scheme, devices, bindings, the
  rewind / save-state buttons), volume, language, memory cards, the core netplay
  flow (host, join, browse, lobby, chat, seat swaps), mods (list, install,
  problems), credits, version and updates.
- **Everything else is hidden** and keeps the host's value: renderer, resolution,
  filters, pipeline threads, audio details, rewind tuning, BIOS, hotkeys,
  quick match, accounts, spectators, moderation, lobby mods, tuning, the lobby
  server, mod versions/resources, experimental mods, logs.
- A page with nothing shown disappears from the menu. `R4L_SURFACE=all` shows
  every capability (for developers and screenshots).
- `Locked` / `Auto` values are applied to the ABI settings each time the menu
  opens. Array fields (per-player) take the value for player 1.

Unknown keys are reported on stderr (`surface: unknown key ...`). The table
below is generated from the catalog (`tools/gen_supported.py`, checked by ctest).

## Capabilities

<!-- BEGIN CAPABILITIES -->
| Key | Area | What it is | Default | Settings field |
|---|---|---|---|---|
| `launcher.in_game` | Launcher | Boot into the game; launcher pages live in the in-game menu | Hidden | - |
| `disc.setup` | Disc | Disc setup (pick, verify) | Shown | - |
| `disc.autoscan` | Disc | Find the disc automatically | Shown | - |
| `disc.sbi` | Disc | Import SBI subchannel | Shown | - |
| `disc.multi` | Disc | Disc selection (multi-disc) | Shown | `disc_index` |
| `disc.prepare` | Disc | Generate and build (first run) | Shown | - |
| `disc.toolchain` | Disc | Toolchain download / repair | Shown | - |
| `disc.pgo` | Disc | Optimize FMV (PGO) | Hidden | - |
| `disc.fmv_timing` | Disc | Apply FMV timing | Hidden | - |
| `disc.rom_patch` | Disc | Disc patch (IPS) | Hidden | `rom_patch_enabled` |
| `bios.select` | Disc | BIOS file | Hidden | - |
| `bios.prepare` | Disc | Prepare BIOS | Hidden | - |
| `graphics.preset` | Graphics | Graphics preset (Low..Ultra, Auto) | Shown | `quality_preset` |
| `graphics.redetect` | Graphics | Re-detect hardware | Shown | - |
| `graphics.renderer` | Graphics | Renderer | Hidden | `renderer` |
| `graphics.fullscreen` | Graphics | Fullscreen | Shown | `fullscreen` |
| `graphics.window_size` | Graphics | Window size | Hidden | `window_width` |
| `graphics.vsync` | Graphics | V-Sync | Hidden | `vsync` |
| `graphics.internal_resolution` | Graphics | Internal resolution | Hidden | `internal_resolution` |
| `graphics.supersampling` | Graphics | Supersampling | Hidden | `supersampling` |
| `graphics.dynamic_resolution` | Graphics | Dynamic resolution | Hidden | `dynamic_resolution` |
| `graphics.antialiasing` | Graphics | Smooth scaling | Hidden | `antialiasing` |
| `graphics.texture_filter` | Graphics | Texture filtering | Hidden | `texture_filter` |
| `graphics.fmv_filter` | Graphics | Movie filter | Hidden | `fmv_filter` |
| `graphics.screen_kind` | Graphics | Screen type (CRT look) | Hidden | `screen_kind` |
| `graphics.scanlines` | Graphics | CRT scanlines | Hidden | `scanlines` |
| `graphics.geometry` | Graphics | Geometry correction / perspective textures | Hidden | `geometry_correction` |
| `graphics.render_thread` | Graphics | Render thread | Hidden | `render_thread` |
| `graphics.present_thread` | Graphics | Present thread | Hidden | `present_thread` |
| `graphics.frame_generation` | Graphics | Smooth motion (frame generation) | Hidden | `frame_generation` |
| `graphics.frame_interp` | Graphics | Frame interpolation | Hidden | `frame_interp` |
| `graphics.frame_blend` | Graphics | Frame blending | Hidden | `frame_blend` |
| `graphics.run_ahead` | Graphics | Run-ahead | Hidden | `run_ahead` |
| `graphics.shader` | Graphics | Post shader | Hidden | - |
| `graphics.aspect` | Graphics | Aspect ratio | Hidden | `aspect_index` |
| `graphics.integer_scale` | Graphics | Integer scaling | Hidden | `integer_scale` |
| `graphics.title_features` | Graphics | Title enhancement features | Shown | - |
| `graphics.skin` | Graphics | Menu skin | Hidden | - |
| `audio.volume` | Audio | Volume | Shown | `volume` |
| `audio.enable` | Audio | Sound on/off | Hidden | `enable_audio` |
| `audio.device` | Audio | Output device | Hidden | - |
| `audio.frequency` | Audio | Sample rate | Hidden | `audio_freq` |
| `audio.spu_hq` | Audio | High-quality SPU | Hidden | `spu_hq` |
| `system.language` | System | Language | Shown | `language_index` |
| `system.skip_launcher` | System | Skip launcher next time | Hidden | `skip_launcher` |
| `system.turbo_loads` | System | Fast loading | Hidden | `turbo_loads` |
| `system.skip_fmv` | System | Skip movies | Hidden | `auto_skip_fmv` |
| `system.rewind` | System | Rewind (enable, depth, interval) | Hidden | `rewind_enabled` |
| `system.memcards` | System | Memory cards | Shown | - |
| `system.player_name` | System | Player name | Hidden | - |
| `controls.scheme` | Controls | Control scheme (title) | Shown | - |
| `controls.bindings` | Controls | Keyboard / gamepad rebinding | Shown | - |
| `controls.devices` | Controls | Input device per player | Shown | `player_src` |
| `controls.profile` | Controls | Controller type (DualShock, NeGcon, JogCon) | Hidden | `pad_mode` |
| `controls.deadzone` | Controls | Stick deadzone | Hidden | `deadzone` |
| `controls.multitap` | Controls | Multitap | Hidden | `multitap_enabled` |
| `controls.hotkeys` | Controls | Host shortcut keys (config.ini) | Hidden | - |
| `controls.assist` | Controls | Rewind / save-state / fast-forward buttons | Shown | - |
| `controls.mouse` | Controls | Mouse controls | Hidden | `mouse_enabled` |
| `mods.list` | Mods | Mod features and options | Shown | - |
| `mods.install` | Mods | Install / remove mods | Shown | - |
| `mods.versions` | Mods | Mod version selection | Hidden | - |
| `mods.resources` | Mods | Mod resource folders | Hidden | - |
| `mods.diagnostics` | Mods | Mod problems | Shown | - |
| `mods.experimental` | Mods | Experimental mods | Hidden | - |
| `netplay.host` | Netplay | Host a race | Shown | - |
| `netplay.join` | Netplay | Join by code / address | Shown | - |
| `netplay.browse` | Netplay | Open lobbies | Shown | - |
| `netplay.lobby` | Netplay | Lobby seats, ready, start | Shown | - |
| `netplay.chat` | Netplay | Lobby chat | Shown | - |
| `netplay.seat_swap` | Netplay | Seat swaps | Shown | - |
| `netplay.automatch` | Netplay | Quick match (automatch) | Hidden | - |
| `netplay.account` | Netplay | Account sign-in | Hidden | - |
| `netplay.online` | Netplay | Online players and server chat | Hidden | - |
| `netplay.moderation` | Netplay | Report / block players | Hidden | - |
| `netplay.spectators` | Netplay | Spectators | Hidden | - |
| `netplay.mod_transfer` | Netplay | Lobby mods and downloads | Hidden | - |
| `netplay.tuning` | Netplay | Input delay, prediction, rollback, relay | Hidden | - |
| `netplay.memcard` | Netplay | Memory card sharing | Hidden | - |
| `netplay.variant` | Netplay | Session variant | Hidden | - |
| `netplay.lobby_server` | Netplay | Lobby server address | Hidden | - |
| `about.credits` | About | Credits and notices | Shown | - |
| `about.version` | About | Version information | Shown | - |
| `about.updates` | About | Toolchain / update check | Shown | - |
| `about.logs` | About | Open log folder | Hidden | - |
<!-- END CAPABILITIES -->

## R4's choices

Graphics: the preset only (Auto: detected Low..Ultra; dynamic resolution and
internal resolution follow it; the widescreen mod stays on Fit, so the aspect
follows the window), plus Smooth motion and Screen (windowed / fullscreen).
Controls: Modern / Classic, devices and rebinding. Disc: found automatically,
OpenBIOS bundled (no BIOS prompt). Settings: volume. Netplay: host / join and
the lobby. Mods: the feature list, install and remove. See `titles/r4/title_r4.cpp`.
