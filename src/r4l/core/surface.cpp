#include "surface.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace r4l {

#define S_OFF(f) static_cast<int>(offsetof(RecompLauncherCSettings, f))

const std::vector<Capability>& capabilities() {
    static const std::vector<Capability> k = {
        // ---- Launcher
        {"launcher.in_game", false, "Launcher", "Boot into the game; launcher pages live in the in-game menu", -1},
        {"launcher.boot_menu", false, "Launcher", "In-game menu open at boot; Resume starts the game", -1},
        {"launcher.select_menu", false, "Launcher", "A Select (Back) tap opens the in-game menu", -1},
        // ---- Disc / first run
        {"disc.setup", true, "Disc", "Disc setup (pick, verify)", -1},
        {"disc.autoscan", true, "Disc", "Find the disc automatically", -1},
        {"disc.sbi", true, "Disc", "Import SBI subchannel", -1},
        {"disc.multi", true, "Disc", "Disc selection (multi-disc)", S_OFF(disc_index)},
        {"disc.prepare", true, "Disc", "Generate and build (first run)", -1},
        {"disc.toolchain", true, "Disc", "Toolchain download / repair", -1},
        {"disc.pgo", false, "Disc", "Optimize FMV (PGO)", -1},
        {"disc.fmv_timing", false, "Disc", "Apply FMV timing", -1},
        {"disc.rom_patch", false, "Disc", "Disc patch (IPS)", S_OFF(rom_patch_enabled)},
        {"bios.select", false, "Disc", "BIOS file", -1},
        {"bios.prepare", false, "Disc", "Prepare BIOS", -1},
        // ---- Graphics
        {"graphics.preset", true, "Graphics", "Graphics preset (Low..Ultra, Auto)", S_OFF(quality_preset)},
        {"graphics.redetect", true, "Graphics", "Re-detect hardware", -1},
        {"graphics.renderer", false, "Graphics", "Renderer", S_OFF(renderer)},
        {"graphics.fullscreen", true, "Graphics", "Fullscreen", S_OFF(fullscreen)},
        {"graphics.window_size", false, "Graphics", "Window size", S_OFF(window_width)},
        {"graphics.vsync", false, "Graphics", "V-Sync", S_OFF(vsync)},
        {"graphics.internal_resolution", false, "Graphics", "Internal resolution", S_OFF(internal_resolution)},
        {"graphics.supersampling", false, "Graphics", "Supersampling", S_OFF(supersampling)},
        {"graphics.dynamic_resolution", false, "Graphics", "Dynamic resolution", S_OFF(dynamic_resolution)},
        {"graphics.antialiasing", false, "Graphics", "Smooth scaling", S_OFF(antialiasing)},
        {"graphics.texture_filter", false, "Graphics", "Texture filtering", S_OFF(texture_filter)},
        {"graphics.fmv_filter", false, "Graphics", "Movie filter", S_OFF(fmv_filter)},
        {"graphics.screen_kind", false, "Graphics", "Screen type (CRT look)", S_OFF(screen_kind)},
        {"graphics.scanlines", false, "Graphics", "CRT scanlines", S_OFF(scanlines)},
        {"graphics.geometry", false, "Graphics", "Geometry correction / perspective textures", S_OFF(geometry_correction)},
        {"graphics.render_thread", false, "Graphics", "Render thread", S_OFF(render_thread)},
        {"graphics.present_thread", false, "Graphics", "Present thread", S_OFF(present_thread)},
        {"graphics.frame_generation", false, "Graphics", "Smooth motion (frame generation)", S_OFF(frame_generation)},
        {"graphics.frame_interp", false, "Graphics", "Frame interpolation", S_OFF(frame_interp)},
        {"graphics.frame_blend", false, "Graphics", "Frame blending", S_OFF(frame_blend)},
        {"graphics.run_ahead", false, "Graphics", "Run-ahead", S_OFF(run_ahead)},
        {"graphics.shader", false, "Graphics", "Post shader", -1},
        {"graphics.aspect", false, "Graphics", "Aspect ratio", S_OFF(aspect_index)},
        {"graphics.integer_scale", false, "Graphics", "Integer scaling", S_OFF(integer_scale)},
        {"graphics.title_features", true, "Graphics", "Title enhancement features", -1},
        {"graphics.skin", false, "Graphics", "Menu skin", -1},
        // ---- Audio
        {"audio.volume", true, "Audio", "Volume", S_OFF(volume)},
        {"audio.enable", false, "Audio", "Sound on/off", S_OFF(enable_audio)},
        {"audio.device", false, "Audio", "Output device", -1},
        {"audio.frequency", false, "Audio", "Sample rate", S_OFF(audio_freq)},
        {"audio.spu_hq", false, "Audio", "High-quality SPU", S_OFF(spu_hq)},
        // ---- System
        {"system.language", true, "System", "Language", S_OFF(language_index)},
        {"system.skip_launcher", false, "System", "Skip launcher next time", S_OFF(skip_launcher)},
        {"system.turbo_loads", false, "System", "Fast loading", S_OFF(turbo_loads)},
        {"system.skip_fmv", false, "System", "Skip movies", S_OFF(auto_skip_fmv)},
        {"system.rewind", false, "System", "Rewind (enable, depth, interval)", S_OFF(rewind_enabled)},
        {"system.memcards", true, "System", "Memory cards", -1},
        {"system.player_name", false, "System", "Player name", -1},
        // ---- Controls
        {"controls.scheme", true, "Controls", "Control scheme (title)", -1},
        {"controls.bindings", true, "Controls", "Keyboard / gamepad rebinding", -1},
        {"controls.devices", true, "Controls", "Input device per player", S_OFF(player_src)},
        {"controls.profile", false, "Controls", "Controller type (DualShock, NeGcon, JogCon)", S_OFF(pad_mode)},
        {"controls.deadzone", false, "Controls", "Stick deadzone", S_OFF(deadzone)},
        {"controls.multitap", false, "Controls", "Multitap", S_OFF(multitap_enabled)},
        {"controls.hotkeys", false, "Controls", "Host shortcut keys (config.ini)", -1},
        {"controls.assist", true, "Controls", "Rewind / save-state / fast-forward buttons", -1},
        {"controls.mouse", false, "Controls", "Mouse controls", S_OFF(mouse_enabled)},
        {"controls.force_feedback", false, "Controls", "Wheel force feedback (JogCon, experimental)", S_OFF(jogcon_force_feedback)},
        // ---- Mods
        {"mods.list", true, "Mods", "Mod features and options", -1},
        {"mods.install", true, "Mods", "Install / remove mods", -1},
        {"mods.versions", false, "Mods", "Mod version selection", -1},
        {"mods.resources", false, "Mods", "Mod resource folders", -1},
        {"mods.diagnostics", true, "Mods", "Mod problems", -1},
        {"mods.experimental", false, "Mods", "Experimental mods", -1},
        // ---- Netplay
        {"netplay.host", true, "Netplay", "Host a race", -1},
        {"netplay.join", true, "Netplay", "Join by code / address", -1},
        {"netplay.browse", true, "Netplay", "Open lobbies", -1},
        {"netplay.lobby", true, "Netplay", "Lobby seats, ready, start", -1},
        {"netplay.chat", true, "Netplay", "Lobby chat", -1},
        {"netplay.seat_swap", true, "Netplay", "Seat swaps", -1},
        {"netplay.automatch", false, "Netplay", "Quick match (automatch)", -1},
        {"netplay.account", false, "Netplay", "Account sign-in", -1},
        {"netplay.online", false, "Netplay", "Online players and server chat", -1},
        {"netplay.moderation", false, "Netplay", "Report / block players", -1},
        {"netplay.spectators", false, "Netplay", "Spectators", -1},
        {"netplay.mod_transfer", false, "Netplay", "Lobby mods and downloads", -1},
        {"netplay.tuning", false, "Netplay", "Input delay, prediction, rollback, relay", -1},
        {"netplay.memcard", false, "Netplay", "Memory card sharing", -1},
        {"netplay.variant", false, "Netplay", "Session variant", -1},
        {"netplay.lobby_server", false, "Netplay", "Lobby server address", -1},
        // ---- About
        {"about.credits", true, "About", "Credits and notices", -1},
        {"about.version", true, "About", "Version information", -1},
        {"about.updates", true, "About", "Toolchain / update check", -1},
        {"about.logs", false, "About", "Open log folder", -1},
    };
    return k;
}

const Capability* find_capability(const std::string& key) {
    for (const auto& c : capabilities())
        if (key == c.key) return &c;
    return nullptr;
}

const SurfaceRule* Surface::find(const std::string& key) const {
    for (int i = 0; i < count_; ++i)
        if (rules_[i].key && key == rules_[i].key) return &rules_[i];
    return nullptr;
}

Vis Surface::vis(const std::string& key) const {
    // R4L_SURFACE=all reveals every row; it does not change how the
    // launcher itself runs (launcher.* are modes, not rows).
    if (show_all_ && key.compare(0, 9, "launcher.") != 0) return Vis::Shown;
    if (const SurfaceRule* r = find(key))
        if (r->vis != Vis::Default) return r->vis;
    const Capability* c = find_capability(key);
    return (c && c->essential) ? Vis::Shown : Vis::Hidden;
}

bool Surface::shown(const std::string& key) const {
    const Vis v = vis(key);
    return v == Vis::Shown || v == Vis::Locked;
}
bool Surface::locked(const std::string& key) const { return vis(key) == Vis::Locked; }
bool Surface::automatic(const std::string& key) const { return vis(key) == Vis::Auto; }

const char* Surface::value(const std::string& key) const {
    const SurfaceRule* r = find(key);
    return r ? r->value : nullptr;
}

bool Surface::any_shown(const std::string& prefix) const {
    for (const auto& c : capabilities())
        if (std::strncmp(c.key, prefix.c_str(), prefix.size()) == 0 && shown(c.key)) return true;
    return false;
}

int Surface::apply(RecompLauncherCSettings* io) const {
    if (!io) return 0;
    int n = 0;
    for (int i = 0; i < count_; ++i) {
        const SurfaceRule& r = rules_[i];
        if (!r.key || !r.value || (r.vis != Vis::Locked && r.vis != Vis::Auto)) continue;
        if (!std::strcmp(r.value, "auto")) continue;
        const Capability* c = find_capability(r.key);
        if (!c || c->settings_offset < 0) continue;
        char* end = nullptr;
        const long v = std::strtol(r.value, &end, 10);
        if (!end || *end) continue;
        int* field = reinterpret_cast<int*>(reinterpret_cast<char*>(io) + c->settings_offset);
        *field = static_cast<int>(v);
        ++n;
    }
    return n;
}

std::vector<std::string> Surface::unknown_keys() const {
    std::vector<std::string> out;
    for (int i = 0; i < count_; ++i)
        if (rules_[i].key && !find_capability(rules_[i].key)) out.push_back(rules_[i].key);
    return out;
}

}  // namespace r4l
