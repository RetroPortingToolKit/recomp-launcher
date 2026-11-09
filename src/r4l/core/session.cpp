#include "session.h"

#include "ini.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace r4l {

std::string dir_of(const std::string& path) {
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? "." : path.substr(0, s);
}

std::string join_path(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::string current_exe_dir() {
    char buf[4096] = {0};
#if defined(__APPLE__)
    uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) != 0) return ".";
#elif defined(_WIN32)
    if (!GetModuleFileNameA(nullptr, buf, sizeof(buf))) return ".";
#else
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0;
#endif
    return dir_of(buf);
}

std::string resolve_assets_dir(const char* given) {
    auto has = [](const std::string& d) {
        struct stat st{};
        return stat(join_path(d, "fonts").c_str(), &st) == 0 || stat(join_path(d, "skins").c_str(), &st) == 0;
    };
    const std::string g = given && *given ? given : "assets";
    for (const std::string& c : {g, join_path(g, "assets"), join_path(current_exe_dir(), "assets")})
        if (has(c)) return c;
    return g;
}

void Session::begin(RecompLauncherCSettings* io_, const RecompLauncherCGameInfo* game_,
                    const char* assets, const char* initial_rom) {
    io = io_;
    game = game_;
    assets_dir = assets ? assets : "";
    exe_dir = current_exe_dir();
    outcome = Outcome::None;

    const int n = (game && game->num_discs > 1) ? game->num_discs : 1;
    discs.assign(static_cast<size_t>(n), DiscPick{});
    if (game && game->num_discs > 0 && game->discs) {
        for (int i = 0; i < n; ++i)
            if (game->discs[i].path) discs[i].path = game->discs[i].path;
    }
    if (initial_rom && *initial_rom && discs[0].path.empty()) discs[0].path = initial_rom;
    if (discs[0].path.empty() && game && game->rom_cache_path && *game->rom_cache_path)
        discs[0].path = read_rom_cache(game->rom_cache_path);
    if (discs[0].path.empty()) {
        const auto cfg = read_disc_cfg(exe_dir);
        for (size_t i = 0; i < cfg.size() && i < discs.size(); ++i) discs[i].path = cfg[i];
    }
    for (int i = 0; i < n; ++i)
        if (!discs[i].path.empty()) set_disc(i, discs[i].path);
    if (io && io->bios_path[0]) set_bios(io->bios_path);

    load_keyboard_binds(keybinds_path(), &keys);
    load_pad_binds(input_path(), &pads);
    load_hotkeys(config_ini_path(), &hotkeys);
    binds_dirty = false;

    if (game && io) {
        quality.bind(game->quality_apply, game->quality_offered_mask);
        quality.adopt(*io);
    }
}

std::string Session::keybinds_path() const {
    if (game && game->keybinds_path && *game->keybinds_path) return game->keybinds_path;
    return join_path(exe_dir, "keybinds.ini");
}
std::string Session::input_path() const { return sibling_path(keybinds_path(), "input.ini"); }
std::string Session::config_ini_path() const {
    if (game && game->config_path && *game->config_path) return game->config_path;
    return join_path(exe_dir, "config.ini");
}
std::string Session::launcher_prefs_path() const {
    return sibling_path(config_ini_path(), "launcher-window.ini");
}

void Session::set_disc(int index, const std::string& path) {
    if (index < 0 || index >= static_cast<int>(discs.size())) return;
    DiscPick& d = discs[static_cast<size_t>(index)];
    d.path = path;
    d.verify = RecompLauncherCDiscVerify{};
    d.verified = false;
    if (!path.empty() && game && game->disc_verify) d.verified = game->disc_verify(path.c_str(), &d.verify) != 0;
}

void Session::set_bios(const std::string& path) {
    bios_path = path;
    bios_verify = RecompLauncherCBiosVerify{};
    bios_verified = false;
    if (io) std::snprintf(io->bios_path, sizeof(io->bios_path), "%s", path.c_str());
    if (path.empty() || !game) return;
    if (game->bios_verify_for_rom)
        bios_verified = game->bios_verify_for_rom(game->bios_verify_ctx, path.c_str(),
                                                  primary_disc().c_str(), &bios_verify) != 0;
    else if (game->bios_verify)
        bios_verified = game->bios_verify(path.c_str(), &bios_verify) != 0;
}

const std::string& Session::primary_disc() const { return discs[0].path; }

bool Session::media_ready() const {
    if (primary_disc().empty()) return false;
    if (discs[0].verified && discs[0].verify.verdict == 3) return false;  // bad dump
    return true;
}

bool Session::needs_setup() const {
    if (!game) return false;
    if (game->needs_setup) return true;
    return !media_ready();
}

bool write_sidecars(const std::vector<std::string>& dirs, const std::vector<std::string>& discs,
                    const std::string& bios) {
    bool ok = true;
    for (const std::string& dir : dirs) {
        if (!discs.empty()) {
            std::ofstream r(join_path(dir, "rom.cfg"), std::ios::trunc);
            r << discs[0] << "\n";
            std::ofstream d(join_path(dir, "disc.cfg"), std::ios::trunc);
            for (const std::string& p : discs) d << p << "\n";
            ok = ok && r && d;
        }
        if (!bios.empty()) {
            std::ofstream b(join_path(dir, "bios.cfg"), std::ios::trunc);
            b << bios << "\n";
            ok = ok && b;
        }
    }
    return ok;
}

std::string read_rom_cache(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::getline(f, line);
    return trim(line);
}

std::vector<std::string> read_disc_cfg(const std::string& dir) {
    std::vector<std::string> out;
    std::ifstream f(join_path(dir, "disc.cfg"));
    if (!f) f.open(join_path(dir, "rom.cfg"));
    std::string line;
    while (std::getline(f, line)) out.push_back(trim(line));
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

bool Session::persist_media(std::string* err) {
    std::vector<std::string> paths;
    for (const DiscPick& d : discs) paths.push_back(d.path);
    if (!game || !game->host_persists_paths) {
        std::vector<std::string> dirs{exe_dir};
        if (!relaunch_exe.empty() && dir_of(relaunch_exe) != exe_dir) dirs.push_back(dir_of(relaunch_exe));
        if (!write_sidecars(dirs, paths, bios_path) && err) *err = "Could not write disc.cfg";
        if (game && game->rom_cache_path && *game->rom_cache_path && !paths.empty())
            std::ofstream(game->rom_cache_path, std::ios::trunc) << paths[0] << "\n";
    }
    if (!game) return true;
    if (game->num_discs > 1 && game->persist_setup_discs) {
        std::vector<const char*> c;
        for (const std::string& p : paths) c.push_back(p.c_str());
        return game->persist_setup_discs(game->persist_setup_ctx, c.data(),
                                         static_cast<int>(c.size()), bios_path.c_str()) != 0;
    }
    if (game->persist_setup)
        return game->persist_setup(game->persist_setup_ctx, primary_disc().c_str(),
                                   bios_path.c_str()) != 0;
    return true;
}

bool Session::commit_files(std::string* err) {
    if (!binds_dirty) return true;
    bool ok = save_keyboard_binds(keybinds_path(), keys);
    ok = save_pad_binds(input_path(), pads) && ok;
    ok = save_hotkeys(config_ini_path(), hotkeys) && ok;
    if (!ok && err) *err = "Could not save bindings";
    binds_dirty = !ok;
    return ok;
}

void Session::tick() {
    if (io) quality.observe(io);
}

bool load_window_size(const std::string& path, int* w, int* h) {
    IniDoc d;
    if (!d.load(path)) return false;
    const int lw = d.get_int("", "logical_width", 0), lh = d.get_int("", "logical_height", 0);
    if (lw < 200 || lh < 200 || lw > 16384 || lh > 16384) return false;
    *w = lw;
    *h = lh;
    return true;
}

bool save_window_size(const std::string& path, int w, int h) {
    std::ofstream f(path, std::ios::trunc);
    f << "logical_width=" << w << "\nlogical_height=" << h << "\n";
    return static_cast<bool>(f);
}

namespace {
void write_status(const std::string& record, bool ok, const std::string& why) {
    std::ofstream f(record + ".status", std::ios::trunc);
    if (ok) {
        f << "{\"ok\":true}\n";
        return;
    }
    std::string esc;
    for (char c : why) {
        if (c == '"' || c == '\\') esc += '\\';
        if (static_cast<unsigned char>(c) >= 0x20) esc += c;
    }
    f << "{\"ok\":false,\"why\":\"" << esc << "\"}\n";
}
}  // namespace

int run_netplay_handoff(RecompLauncherCSettings* io, const RecompLauncherCGameInfo* game,
                        const char* initial_rom, char* out_rom, size_t out_len) {
    const char* env = std::getenv("RECOMP_NETPLAY_LAUNCH");
    if (!env || !*env) return -1;
    const std::string record = env;
#if defined(_WIN32)
    _putenv_s("RECOMP_NETPLAY_LAUNCH", "");
#else
    unsetenv("RECOMP_NETPLAY_LAUNCH");  // a rematch launcher opens normally
#endif
    if (io) std::memset(&io->netplay_launch, 0, sizeof(io->netplay_launch));
    std::ifstream f(record, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();
    if (!f || json.empty() || json.size() > (4u << 20)) {
        write_status(record, false, "cannot read launch record");
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
    const RecompLauncherCNetplayCallbacks* np = game ? game->netplay : nullptr;
    if (!np || !np->ingest_launch || !np->fill_launch) {
        write_status(record, false, "netplay is not available in this build");
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
    char why[256] = {0};
    if (!np->ingest_launch(np->ctx, json.c_str(), why, sizeof(why))) {
        write_status(record, false, why[0] ? why : "launch record rejected");
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
    RecompLauncherCNetplayLaunch l{};
    if (!np->fill_launch(np->ctx, &l) || !l.enabled) {
        write_status(record, false, "no session in launch record");
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
    if (game->mods && game->mods->commit_netplay &&
        !game->mods->commit_netplay(game->mods->ctx, initial_rom ? initial_rom : "")) {
        const char* e = game->mods->last_error ? game->mods->last_error(game->mods->ctx) : nullptr;
        write_status(record, false, e && *e ? e : "mods could not be prepared");
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
    if (np->clear_launch_pending) np->clear_launch_pending(np->ctx);
    if (io) io->netplay_launch = l;
    if (out_rom && out_len) std::snprintf(out_rom, out_len, "%s", initial_rom ? initial_rom : "");
    write_status(record, true, "");
    return RECOMP_LAUNCHER_RESULT_LAUNCH;
}

}  // namespace r4l
