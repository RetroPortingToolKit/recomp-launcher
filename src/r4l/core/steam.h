// steam.h — "Add to Steam": register this executable as a non-Steam game.
//
// A Steam Controller only acts as a gamepad in a game Steam launched; any
// other focused program gets Steam's Desktop Layout (mouse/keyboard). This
// writes a non-Steam shortcut into every local Steam user's
// userdata/<id>/config/shortcuts.vdf (binary VDF), so the game can be
// started from the Steam library with Steam's gamepad template.
//
// Self-contained (no SDL): the binary VDF reader/writer keeps every field
// it does not know, the file is backed up to shortcuts.vdf.bak before a
// write, and a second add is a no-op. R4L_STEAM_ROOT overrides the Steam
// folder (tests and manual runs must point it at a copy).
//
// Controller layout: not written. Steam keeps per-app controller choices in
// its own undocumented configs; with no safe documented way to set them, the
// shortcut relies on Steam's default for a non-Steam game, which gives a
// Steam Controller the Gamepad template.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace r4l {

// ---- Binary VDF (shortcuts.vdf, appinfo-style key/value tree)
struct VdfNode {
    enum Type : uint8_t { Map = 0, String = 1, Int32 = 2, Float = 3, Ptr = 4, Color = 6, UInt64 = 7, Int64 = 10 };
    uint8_t type = Map;
    std::string key;
    std::string str;              // String
    std::vector<uint8_t> raw;     // fixed-width values, little endian, kept verbatim
    std::vector<VdfNode> kids;    // Map

    VdfNode* find(const std::string& k);  // case-insensitive
    const VdfNode* find(const std::string& k) const;
    uint32_t u32() const;
    static VdfNode string(const std::string& k, const std::string& v);
    static VdfNode int32(const std::string& k, uint32_t v);
    static VdfNode map(const std::string& k);
};
// Parses a whole file: top-level nodes up to the closing 0x08. False on
// malformed or unknown-typed data (the caller must not overwrite the file).
bool vdf_parse(const std::string& bytes, std::vector<VdfNode>* top);
std::string vdf_write(const std::vector<VdfNode>& top);

// ---- Steam shortcuts
struct SteamShortcut {
    std::string name;            // AppName
    std::string exe;             // unquoted path; written quoted
    std::string start_dir;       // unquoted directory; written quoted
    std::string icon;            // icon file, may be empty
    std::string launch_options;  // usually empty
};

uint32_t steam_crc32(const std::string& s);
// The id Steam gives a non-Steam game: crc32(quoted exe + name) | 0x80000000.
uint32_t steam_shortcut_appid(const std::string& exe, const std::string& name);

// Steam's folder: $R4L_STEAM_ROOT, else the platform default; "" when absent.
std::string steam_root();
// userdata/<id>/config directories of the local Steam users under root.
std::vector<std::string> steam_user_config_dirs(const std::string& root);
// Started by Steam (SteamAppId / SteamGameId / SteamClientLaunch set).
bool running_under_steam();
// This process's executable path (UTF-8); "" if unknown.
std::string current_exe_path();
// The icon to give Steam for exe: <stem>.ico / icon.ico / icon.png next to
// it, else the executable itself on Windows (embedded icon), else "".
std::string steam_icon_for(const std::string& exe);

// Every local user already has a shortcut for exe (compared unquoted;
// case-insensitive on Windows).
bool steam_shortcut_registered(const std::string& root, const std::string& exe);
// Adds sc to every user that lacks it. Returns the number of shortcuts.vdf
// files written (0: already everywhere), -1 on error (err filled).
int steam_add_shortcut(const std::string& root, const SteamShortcut& sc, std::string* err);

}  // namespace r4l
