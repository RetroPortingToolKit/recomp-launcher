# recomp-launcher design

A new Dear ImGui launcher and in-game menu for psxrecomp titles, starting with
R4: Ridge Racer Type 4 (SLUS-00797). It replaces `RetroPortingToolKit/recomp-ui`
in a game build without any change to psxrecomp or the game: psxrecomp still
`include()`s `recomp_ui.cmake` from `RECOMP_UI_ROOT` and links the same four C
symbols. UI, layout, navigation, theming, state and the settings model are new.
Only the C ABI header is shared with recomp-ui, so that struct layouts stay
identical.

Status: everything in Part 2 is built, and covered by unit, ABI and screenshot
tests. The in-game overlay (Part 3) runs in R4 on the psxrecomp hook stack
P1–P6 (PRs #598–#603, open, stacked).

---

## Part 1 — The contract (ABI/API inventory)

Surveyed at the R4 pins: psxrecomp `67a21b73` and recomp-ui `7e884a22`. The
quality-preset fields come from recomp-ui PR #86 (`3fa96c3`) and psxrecomp
PR #591. Game Mode comes from psxrecomp #593.

### 1.1 Linked symbols

Measured, not inferred. In a full R4 build, these are the symbols that
recomp-ui objects define and non-recomp-ui objects reference:

| Symbol | Called by | Contract |
|---|---|---|
| `int recomp_launcher_run_window(const char* title, RecompLauncherCSettings* io, const RecompLauncherCGameInfo* game, const char* assets_dir, const char* initial_rom, char* out_rom, size_t out_len)` | `main.cpp`: first boot, and the netplay soft-return / rematch | Returns 0 LAUNCH, 1 QUIT, 2 UNAVAILABLE, 3 RELAUNCH. `*io` always comes back edited. `io->netplay_launch` is zeroed unless the result is LAUNCH. |
| `int recomp_launcher_relaunch_exe(char* out, size_t cap)` | `main.cpp` after RELAUNCH | Gives the exe that `rebuild_with_progress` produced. |
| `void recomp_launcher_set_preserve_sdl(int)` | `main.cpp` before each `run_window` | When set, tear down the window and context but not `SDL_Quit`. |
| `void launcher_boot_timing_mark(const char*)` | `main.cpp`, 9 call sites | Opt-in stamps (`PSX_LAUNCHER_BOOT_TIMING`). |

No ImGui, in-game overlay, netplay-host or keybind symbols cross the boundary.
`recomp_runtime_ui_*`, `recomp_frame_blend_*`, `recomp_flash_guard_*` and
`recomp_moderation_*` are compiled into the runtime but never called.

### 1.2 Headers and macros

All three are on the include path as `RECOMP_UI_ROOT/src`.

- **`recomp_launcher.h`.** The ABI is the struct layouts plus the
  `RECOMP_LAUNCHER_HAS_*` macros that the runtime `#if`s on.
  - **Used types:** `RecompLauncherCSettings`, `RecompLauncherCGameInfo`,
    `RecompLauncherCModProvider` and its `…ModPackage/Feature/Option/Choice/Version/Diagnostic/Resource`
    structs, `RecompLauncherCNetplayCallbacks` and its
    `…Lobby/Member/OnlinePlayer/NeedMod/LobbyMod/Ruleset/Found/ChatMessage/Launch/LocalAddress`
    structs, `…DiscVerify`, `…BiosVerify`, `…Memcard`, `…Disc` and
    `…PrepareProgressFn`.
  - **Used macros:** `MAX_PLAYERS`, `MAX_ASSIST_BINDINGS`,
    `NETPLAY_MAX_MEMBERS`, `MOD_AUTHOR_LINK_MAX`, `RESULT_*`, `HAS_INTERNAL_RESOLUTION`,
    `HAS_DYNAMIC_RESOLUTION`, `HAS_RENDER_PIPELINE`, `HAS_SCANLINES`, `HAS_MULTITAP_*`,
    `HAS_PLAYER_ACCOUNT`, `HAS_HOST_RELAY`, `HAS_SBI_STATUS`, `HAS_CHAT_REPORT`,
    `HAS_AUTOMATCH`, `HAS_SET_BLOCKS`, `HAS_LIST_SCOPE`, `HAS_ACCOUNT`,
    `HAS_NETPLAY_HANDOFF`, `HAS_WORKER_MOD_COMMIT`, `HAS_PREPARED_MOD_COMMIT`,
    `HAS_DIRECT_ASSIST_BIND`, `HAS_QUALITY_PRESETS`, `RECOMP_MOD_OPTION_*`,
    `RECOMP_MOD_CHANNEL_*`, `RECOMP_SBI_*`, `RECOMP_LAUNCHER_LIST_SCOPE_*` and
    `RECOMP_LAUNCHER_AUTOMATCH_*`.
  - **Copy:** this repo carries the header verbatim from recomp-ui `3fa96c3`,
    under MIT (`LICENSES/recomp-ui-MIT.txt`).
- **`launcher_profile.h`.** `static inline int launcher_profile_apply(const char* console, RecompLauncherCGameInfo*)`.
  `main.cpp` calls it with `"psx"`. It sets theme `psx`, platform, `rom_noun`,
  pad-mode flags and the PSX `has_*` row flags (rewind follows
  `RECOMP_UI_PSX_HAS_REWIND`). The implementation here is new and sets the
  same values.
- **`launcher_boot_timing.h`.** Declares the mark function.
- **Consumers in psxrecomp:** `main.cpp` (guarded by `#if defined(RECOMP_LAUNCHER)`),
  `mod_runtime.h/.cpp` (the provider types), and `host/psxrecomp_codegen_host.h`
  plus R4 `codegen_setup.h` (`RecompLauncherCGameInfo`, progress fn).
- **Conformance:** `tests/abi_conformance.c` compiles all of this as C, the way
  the host does. `tests/abi_layout.sh` diffs clang record layouts of all 27
  structs against a recomp-ui checkout.
  - Against `3fa96c3` (PR #86): identical.
  - Against `7e884a22` (the R4 pin): identical, except for the 8 appended
    quality fields. Those are additive and at the tail.

### 1.3 CMake integration

`psxrecomp/runtime/runtime.cmake`:

- Resolves `RECOMP_UI_ROOT` (cache) or `<game>/recomp-ui`, and requires
  `${RECOMP_UI_ROOT}/recomp_ui.cmake`.
- Sets `RECOMP_UI_ENABLE_MODS=ON` and `RECOMP_UI_SDL3=${PSX_SDL3}`, then
  `include(recomp_ui.cmake)`.
- Calls `recomp_target_launcher_ui(<runtime> CONSOLE psx [BOXART f] [PAD f] [BRAND f])`
  and adds `RECOMP_UI_PSX_HAS_REWIND=0|1`.
- Includes `${RECOMP_UI_ROOT}/cmake/recomp_gl.cmake` when it exists and calls
  `recomp_resolve_gl(<out>)` for the runtime's GL link (GLVND / Steam Deck).
- Adds `${RECOMP_UI_ROOT}/src` to the codegen-host include path.

`psxrecomp_add_game_runtime(... LAUNCHER_BOXART, LAUNCHER_PAD, LAUNCHER_BRAND,
ENABLE_SETUP_WIZARD, ENABLE_NETPLAY_IF_PRESENT, MAX_PLAYERS ...)` feeds those
arguments. Here, `recomp_ui.cmake` provides `recomp_target_launcher_ui`,
`recomp_stage_launcher_assets` and `recomp_resolve_gl` with the same
signatures. It compiles the launcher, pinned Dear ImGui `v1.92.9b` and its
SDL3 + OpenGL3 backends into the runtime target. It also stages
`assets/fonts` and `assets/skins` next to the exe.

**Differences:**
- SDL2 and `HOST_IMGUI` stop with a clear error; neither is needed by any
  psxrecomp title.
- `recomp_target_launcher_netplay` is not provided; psxrecomp does not call it.

### 1.4 Files

| File | Writer | Format / keys | Launcher's role |
|---|---|---|---|
| `settings.toml` (exe dir) | host only (`save_user_settings`) | `[video]` renderer, internal_resolution, dynamic_resolution(_min), antialiasing, texture_filtering, fmv_filter, scanlines(_strength), vsync, render/present_thread, frame_generation, rewind(_depth/_interval), fullscreen, …; `[audio]`; `[hotkeys]` *_pad; `[launcher] skip_launcher`; `[netplay]`; `[disc]`; `[memcard]`; `[controller] pN_device/_mode/_deadzone, multitap`; `[localization]` | None. It edits `RecompLauncherCSettings` and the host persists it. |
| `game.toml` `[video]` | game repo | the defaults the host seeds into `io`; quality presets (#591) | read-only through the host |
| `keybinds.ini` (`GameInfo.keybinds_path`) | launcher + runtime (`psx_keybinds.c`) | `[player1..5]` 24 keys `up…rs_right = Scancode[, Alt]`, `None` | read/write, keeps foreign lines |
| `input.ini` (sibling) | launcher + runtime | `[controller] enabled/device/deadzone`, `[mapping]` and `[mapping.<guid>]`, SDL gamepad names | read/write |
| `config.ini` `[KeyMap]` (`GameInfo.config_path`) | launcher + `host_keymap.c` | Fullscreen, Reset, Pause, Turbo, TurboToggle, Rewind, SaveStateMenu, VolumeUp/Down, DisplayPerf, OpenLauncher | read/write that section only |
| `rom.cfg` / `disc.cfg` / `bios.cfg` | launcher (unless `host_persists_paths`) + runtime | one absolute path per line; `disc.cfg` one line per disc, with blank lines for missing discs | written on Continue/Play, in the exe dir and the relaunch dir |
| `launcher-window.ini` | launcher | `logical_width=`, `logical_height=` | read/write |
| `launcher-skin.txt` | launcher (new) | the chosen skin name | read/write |
| `mods/state.toml`, `mods/bundled/<id>/<ver>/manifest.toml` | host via `RecompLauncherCModProvider` | features have `group` strings; options are `boolean/choice/integer/text` | provider calls only; `commit(disc)` on Play |
| `<record>.status` | launcher | `{"ok":true}` / `{"ok":false,"why":…}` | written for `RECOMP_NETPLAY_LAUNCH` handoffs |

**Assist bindings:** `assist_pad_bind[0..3]` are Rewind, Save states,
Fast-forward and Fast-forward toggle. They use the `RECOMP_LAUNCHER_PAD_*`
encoding (`1+button`, `100+2*axis+pos` or `1000+mask`). The host normalises
them and saves them to `[hotkeys]`. A single-button capture for
`assist_direct_pad_bind_action` is stored as a one-bit combination.

### 1.5 First-run disc setup

- **Open condition:** the launcher opens the wizard when `needs_setup` is set,
  or when no disc is set up. R4's `codegen_setup.c` →
  `psxrecomp_codegen_host_apply` sets `setup_wizard_supported`, `needs_setup`,
  `prepare_required_before_continue`, `prepare_with_progress`,
  `rebuild_with_progress` (with `rebuild_after_prepare`,
  `relaunch_after_rebuild`), `persist_setup(_discs)`, the toolchain callbacks
  and `pgo_optimize_with_progress`.
- **Verification:**
  - `disc_verify` runs on every pick and reports serial, region, verdict
    0–3, track count, netplay detail and `sbi_status`.
  - `import_sbi` is offered when `sbi_status == MISSING`.
  - `bios_verify(_for_rom)` checks the optional BIOS. The bundled OpenBIOS is
    the default.
- **Prepare and rebuild:** both run on a worker thread with progress. A
  successful rebuild returns RELAUNCH.

### 1.6 Launch flow

**When the host opens the launcher:**
- It opens unless `--no-launcher`, `PSX_NO_LAUNCHER`, `[launcher] skip_launcher`,
  `--headless*` or `--hidden-window` applies; `--launcher` always wins.
- Game Mode (#593): in a gamescope session with a disc already set up, the
  host skips the launcher. On first run it still opens, so the player can
  pick a disc.

**`run_window` order:**
1. `RECOMP_NETPLAY_LAUNCH` handoff (no window): ingest → fill → `commit_netplay` → status file.
2. Window: SDL3, GL 3.3 core, GLSL 330, the same profile as the psxrecomp GL renderer.
3. UI loop.
4. On exit: bindings written, size saved, ImGui and GL torn down, `SDL_Quit` unless preserved.

**Netplay entry points** (`RecompLauncherCNetplayCallbacks`, implemented by
psxrecomp's `ae_np_*` over `psx_lobby_client`):

| Action | Callbacks |
|---|---|
| Connection | `connect` / `connected` / `connecting`; `pump` every frame |
| Name | `set_player_name` |
| Host | `create(name, endpoint, password, settings, lan_only, max_slots)`; returns -4 when the port is busy |
| Join by code | `join(lobby_id, …)` |
| Join by address | `join("lan:host:port", …)`; returns -3 when nothing answers |
| Lobby list | `request_list` / `list_*` |
| Seats P1–Pn | `member_*` (slot), `seat_move_self`, `kick_member`, `lobby_max_slots` |
| Start | `set_ready`, `all_ready`, `request_start`; then `launch_pending` → `fill_launch` → `commit_netplay` → LAUNCH with `io->netplay_launch` |
| Chat | `chat_*` |
| Lobby options | `input_delay_*` and `rollback_*` (host) |

Not surfaced yet: automatch, accounts, spectators, mod transfer, seat-swap
requests, relay host.

---

## Part 2 — The launcher

### 2.1 Screens and navigation

A left rail, a content pane and a footer of button hints:

| Screen | Contents |
|---|---|
| **Play (Home)** | Hero (title layer art or skin), disc card with verdict chips and "Change disc", quick tiles for Graphics preset, Control scheme and Mods count. **PLAY** sits at the bottom of the rail. |
| **Disc setup** | Disc path (typed or file dialog) with verdict, SBI import, optional BIOS, and *Generate and build* with progress. Continue persists the picks. Replaces Play in the rail until media is ready. |
| **Graphics** | Preset row: Low/Medium/High/Ultra, the detected preset marked, Custom with "started from X", hardware summary, reason and **Re-detect**. Then Display, Resolution, Image, Pipeline, the title's enhancement features inline, and the menu skin picker. Rows show only when the matching `has_*` is set. |
| **Mods** | Features grouped by their `group`, each with a toggle. The detail pane has options (choice, boolean, integer), diagnostics, source and channel. |
| **Controls** | Modern/Classic scheme (the title's mod option), players P1–P4, device (None, Keyboard, Gamepad with GUID), controller profile (DualShock, Digital, NeGcon, JogCon), deadzone, multitap, and a bindings table (key, alt key, gamepad) with capture. Shortcuts: rewind toggle and depth, and assist pad-combo capture (Rewind etc.). |
| **Netplay** | Name and status. Host (name, password, seats 2–4, LAN only) and Join (code, or address host:port, plus your addresses). Open lobbies list. The lobby view has P1–P4 seat cards (host, ready, ping, kick, sit here), Ready / Start race / Leave, delay, rollback and chat. |
| **About** | Title blurb, then notices: HD HUD (T4HDHUD) by Kuid0us, Lato (OFL), Dear ImGui, psxrecomp, the trademark line, and versions. |

**Graphics presets:**
- Picking a preset calls `quality_apply`.
- The launcher learns which bytes the preset governs by applying it to an
  all-zero and an all-ones scratch copy; the bytes that come out equal are
  governed. When any governed byte changes, the preset becomes Custom (5) and
  `quality_base` is kept. Rows a preset does not touch never flip it.
- Re-detect calls `quality_redetect` and applies the preset it returns.

### 2.2 Input model

- **Mouse, keyboard and gamepad** all drive the same ImGui widgets, with
  `NavEnableKeyboard` and `NavEnableGamepad` on.
- **Buttons:** A activates, B goes back, LB/RB page through the rail,
  START = Play. The footer always shows the hints.
- **Rows** are at least 44 reference px tall and scale with window height,
  for Steam Deck readability.
- **Rebinding** opens a modal capture:
  - Key or alt key: the next key wins; Backspace clears.
  - Gamepad source: the next button or axis beyond 75%. Sticks get `+`/`-`;
    triggers don't.
  - Assist combo: buttons are collected until the first release.
  - Esc cancels. While the modal is open, events go to the capture, not to ImGui.
- **Steam Deck Game Mode:** the launcher only shows a note; skipping it is
  the host's job (#593).

### 2.3 State model

- **`Session`** (core, no UI) owns `io` (the ABI struct, edited in place),
  `game`, disc picks and verdicts, the BIOS and its verdict, the bind files,
  the preset tracker, the outcome and the relaunch exe.
- **`ModCatalog`** wraps the provider.
- **`App`** (UI) holds the screen, capture, job, netplay form state and the
  mode (Launcher or Overlay).
- **No duplicated settings store:** the host's struct is the single source
  of truth, the bind files are owned files, and mods live in the provider.
- **Persistence:**
  - **Play:** sidecars and persist hooks, then `mods.commit(disc)`, then the
    bind files, then LAUNCH.
  - **Quit:** bind files, then QUIT, with `*io` edited.

### 2.4 Visual design system

- **Palette tokens:** bg, surface, surface_hi, line, text, text_dim, accent,
  accent2, ok, warn, bad, plus per-element colours (rail, footer, play, nav,
  focus, control, overlay_dim and so on).
- **Text roles:** body, heading, display, logo, nav, button and label.
- **Spacing:** sizes are in reference units of a 720 px-high screen.
- Every one of these comes from the skin (Part 4). The two shipped skins:
  - **Default:** dark, modern.
  - **R4:** black and amber/red, italic uppercase, slanted highlights, a
    chevron cursor, scrolling diagonal stripes and synthesised blips. All
    original art; no game assets.

### 2.5 Title seam

`src/r4l/title.h` defines `TitleLayer`:
- name, tagline, serial and accent colours;
- seats and netplay seats;
- the Modern/Classic feature and option ids;
- the controller profiles (JogCon tied to `r4.compat.jogcon-input`);
- the enhancement features shown on Graphics;
- notices, the About text and the hero drawing.

One `titles/<id>/title_<id>.cpp` is compiled in, chosen with `-DR4L_TITLE=<id>`.
The core, screens and skins are title-agnostic. A title skin is just
`assets/skins/<id>`, which is the default skin choice for that title.

---

## Part 3 — In-game overlay

### 3.1 What it is

One menu system with two entry points:
- **Launcher:** `recomp_launcher_run_window`.
- **Overlay:** `src/recomp_launcher_overlay.h`. It is the same `App` in
  `Mode::Overlay`, with its own ImGui context on the runtime's window and GL
  context.

**Opening it:** Esc, Guide, or the pad combo (default Start+Select,
`R4L_OVERLAY_COMBO=<SDL button mask>`). The Deck's "…" button belongs to Steam
and is not delivered to games; with Steam Input, bind it to Guide or to the
combo.

Two per-title opt-ins (surface keys, off by default):
- `launcher.select_menu`: a Select (Back) tap opens the menu, for titles that
  have no use for Select on its own. The game never sees that press: while
  Select is held the overlay reports itself open, so the host hands the game
  released pads. Another button during the hold makes it a chord (the host's
  Select + button shortcuts), and the release then opens nothing. In the menu,
  Select acts as B; the game does not see that press either.
- `launcher.boot_menu`: the menu opens at boot, before the first guest frame,
  with Resume focused; Resume starts the game. It stays closed when the run
  skipped the launcher (`skip_launcher` from the host: `--no-launcher`,
  `[launcher] skip_launcher`, `PSX_NO_LAUNCHER`, Game Mode), after a
  restart-only relaunch from the menu, and for later sessions of the same
  process.

**While open:**
- **Rail:** Resume, Graphics, Controls, Mods, About, Quit game. B or Esc resumes.
- **Pause:** it asks the host to pause emulation and audio; on resume it
  writes the bind files and unpauses.
- **Netplay:** the host refuses the pause and `netplay_locked` is set. The
  menu then opens non-pausing, the footer says the race keeps running, and
  Netplay and anything that would desync stays out.
- **Live changes:** edits go to `apply_settings` every frame they change.
  Rows the host reports as restart-only (renderer, internal resolution,
  render/present threads) show **Applies after restart**, based on a diff
  against the settings when the overlay opened. Mod toggles go through the
  provider (saved) and `mod_set_live`; a 0 return marks the feature
  "Applies after restart".

**Drawing:** `recomp_overlay_render(fb_w, fb_h)` draws over the game frame into
the bound framebuffer. It must run on the thread that owns the GL context:
psxrecomp's render thread when it is on, otherwise the main thread.
`tests/fake_host.cpp` renders it over a stand-in frame (`overlay.png`,
`overlay-netplay.png`).

### 3.2 What psxrecomp already has

Surveyed at pin `67a21b73`; origin/master `4e758ff9` is unchanged in these areas.

- **Pause:**
  - There is no generic pause. `debug_server.c` `s_paused` is always 0.
  - The save-state and rewind menus block the emulation thread in
    `savestate_menu_host_pause_loop` / `rewind_host_pause_loop` (`main.cpp`)
    and redraw with `gl_renderer_present_hold_last()`.
  - Audio is not paused; it starves.
- **Overlay drawing:**
  - Fixed CPU ARGB layers are composited in `gl_swap_with_osd`
    (`gpu_gl_renderer.c`) and replayed on the render thread (`s_rth_ov`).
  - There is no draw callback. recomp-ui's `recomp_runtime_ui_*` overlay is
    never called.
- **Live video:**
  - The renderer has live setters: `gl_renderer_set_post_aa`, `_scanlines`,
    `_fmv_filter`, `_swap_interval` (vsync), `_dynamic_resolution`,
    `_frame_generation`, `_present_thread`, `_display_aspect`, plus fullscreen
    and scanline hotkeys.
  - Internal resolution (FBO scale fixed at context creation) and the
    renderer switch need a restart.
  - The debug server has no general `set` command (only `post_aa`).
- **Input gate:**
  - `pad_ext_live()` gates game input on `savestate_menu_open`, `rewind_open`
    and `input_guard`.
  - `savestate_input_guard_arm()` swallows the held button on close.
- **Mods:**
  - Options are read from the committed plan (`psx_mod_option_value`).
  - There is no runtime set or `on_option_changed` hook.
  - A few features have plugin-side live setters
    (`psx_mod_set_draw_distance_clamp`, `_auto_skip_fmv`,
    `_frame_interpolation`).
- **Netplay:** `psx_netplay_active()`, `psx_netplay_is_host()`. Rewind is
  already disabled in netplay. The save-state menu has no netplay guard.

### 3.3 psxrecomp PRs (small, stacked, opt-in, default behaviour unchanged)

Opened as RetroPortingToolKit/psxrecomp #598 (P1) → #599 (P2) → #600 (P3) →
#601 (P4) → #602 (P5) → #603 (P6), each based on the previous branch. No
launcher ABI struct changed (the overlay API is this repo's own header), so no
recomp-ui PR was needed.

Verified in R4 (hidden window, race savestate): Esc pauses (0 frames advance),
scanlines toggle live and save, Cross rebinds to K, a PGXP option is marked
restart-only, Esc resumes; 900 post-load frames match a never-opened run on
cyc/mmio/mc/sp/sc/ws/qc, 0 dispatch misses. Two local LAN peers: menu open on
the host does not pause (115 frames in 2 s), 0 digest mismatches.

| # | PR | Change | Size |
|---|---|---|---|
| **P1** | Host pause API | Add `psx_host_pause_push(reason)` / `psx_host_pause_pop()`, generalised from the save-state pause loop (hold last frame, freeze heartbeat), plus `psx_audio_pause(int)` (`SDL_PauseAudioStreamDevice`). It refuses while `psx_netplay_active()`. The save-state and rewind menus move onto it. | S |
| **P2** | Overlay draw callback | Add `psx_host_overlay_set_draw_cb(void (*)(int w,int h,void*), void*)`. It is called in `gl_swap_with_osd` after the OSD layers, on the GL-owning thread (render thread when on, main otherwise), and recorded or replayed like the other layers so it runs during the pause loop's hold-last presents. On Vulkan/SDL it is a no-op; the overlay is GL-only, like the launcher. | S |
| **P3** | Input sink | Add `psx_host_set_input_sink(int (*)(const SDL_Event*, void*), void*)`, consulted first in `drain_host_events` and in the pause loop. A non-zero return consumes the event and adds `ui_capture` to the `pad_ext_live` gate, so no game input leaks. On close it reuses `savestate_input_guard_arm()`. | S |
| **P4** | Live video apply | Add `psx_video_apply_settings(const RecompLauncherCSettings*)`, which dispatches to the existing `gl_renderer_set_*` setters and fullscreen. It returns restart bits for renderer, internal resolution and render/present thread, and persists through `save_user_settings`. It reuses the field mapping the QUIT path already has (`main.cpp` ≈17606–17880), factored out. | M |
| **P5** | Live mod options | Add `psx_mod_set_option_live(pkg, feature, option, value)`, which updates the plan value, plus `psx_mod_register_option_changed_plugin(cb)` for trusted plugins. Live = 1 only when the plugin registered. It is refused during netplay (the plan fingerprint is negotiated). | M |
| **P6** | Wire the overlay | Behind `PSX_RECOMP_OVERLAY` (default ON only when the launcher exposes `RECOMP_OVERLAY_ABI_VERSION`): call `recomp_overlay_init` after the GL context exists, feed events (P3), draw (P2), and implement `RecompOverlayHost` with P1/P4/P5, `psx_keybinds_init` reload and quit-to-launcher (the existing soft-return path). The "Open launcher" hotkey opens the overlay. | M |

P1–P3 are independent. P4 and P5 are independent. P6 depends on all of them.
R4 needs nothing game-specific; the R4 plugins that can apply live register
for P5.

---

## Part 4 — Skins

A skin is data: `assets/skins/<id>/skin.json` plus assets. The schema is in
`docs/SKIN_SCHEMA.md`.

**What a skin controls:**
- the positions, anchors and sizes of every region (resolution-independent,
  in reference units, `%`/`vw`/`vh` or `fill[-N]`);
- fonts per role (TTF, or BMFont bitmap atlas), with colour, shadow, outline,
  uppercase and faux italic;
- the palette;
- background layers (solid, gradient, image with cover/contain/tile and
  scroll, procedural stripes);
- sprites (images or shapes, per-screen, bob and spin);
- the selection highlight (bar, box, fill, glow, slant or sprite) and the
  cursor;
- screen transitions;
- sounds (move, confirm, back);
- label overrides.

**Assets:** `$exe/…` paths reach the player's own files at runtime (disc
extracts, HD packs). With `"optional": true`, a missing asset is not an error.
No copyrighted game art is committed.

**Hot reload:** `skin.json` is polled twice a second. A parse error keeps the
previous skin and shows the line in the footer.

**AI loop:**
1. Edit `skin.json`.
2. Render each screen to PNG:
   `r4l-skin-render --shots out/ --assets assets --skin r4 --size 1280x800 [--screens home,graphics]`
   (hidden window, FBO readback, no audio).
3. Compare with a reference:
   `tools/skin_diff.py out/home.png ref.png -o diff.png --json`
   (side-by-side sheet, MAE, SSIM, both palettes, a 4×4 per-region error grid).

---

## Part 5 — Gaps

- **Not surfaced in the UI:** automatch, accounts, spectators, mod transfer,
  host relay, version selection, mod resources, the toolchain repair flow, the PGO/FMV-timing buttons and
  memory-card inspection. The ABI fields are all present, so these are
  UI-only additions.
- **NeGcon:** there is no ABI field for a NeGcon pad type. The profile maps
  to analog `pad_mode`. A real NeGcon device type needs a psxrecomp setting.
- **Bitmap fonts:** BMFont text format only, single page.
- **Sounds:** WAV only.
- **Skin text roles:** only nav, headings, buttons, labels and the hero use
  skin text roles. Ordinary widget text uses the body font and palette, with
  no shadow or outline.
- **Overlay:** mod toggles made in-game are live only for plugins that register
  an option-changed callback (none in R4 yet) and are not written to
  mods/state.toml until the next launcher Play. Player device/pad-mode edits
  apply on the next start.
- **Layout overrides:** `<region>@<Screen>` rects (the R4 skin centres the
  Home menu like the game's title screen).
- **File dialogs:** use SDL3's portal/native dialog. On Game Mode, typing a
  path with the Steam keyboard also works.
