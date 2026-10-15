// abi_entry.cpp — the four C symbols the psxrecomp runtime links against
// (see docs/DESIGN.md, "ABI inventory"): recomp_launcher_run_window,
// recomp_launcher_relaunch_exe, recomp_launcher_set_preserve_sdl and
// launcher_boot_timing_mark (launcher_boot_timing.c).
#include "recomp_launcher.h"

#include "r4l/core/session.h"
#include "r4l/ui/platform.h"
#include "r4l/ui/script.h"

#include "imgui_internal.h"
#include "r4l/ui/skin.h"
#include "r4l/ui/ui.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {
using r4l::join_path;
int g_preserve_sdl = 0;
std::string g_relaunch_exe;
}  // namespace

extern "C" void recomp_launcher_set_preserve_sdl(int preserve) { g_preserve_sdl = preserve; }

extern "C" int recomp_launcher_relaunch_exe(char* out, size_t out_cap) {
    if (g_relaunch_exe.empty() || !out || !out_cap) return 0;
    std::snprintf(out, out_cap, "%s", g_relaunch_exe.c_str());
    return 1;
}

extern "C" int recomp_launcher_run_window(const char* window_title, RecompLauncherCSettings* io,
                                          const RecompLauncherCGameInfo* game, const char* assets_dir,
                                          const char* initial_rom, char* out_rom_path,
                                          size_t out_rom_path_len) {
    using namespace r4l;
    if (std::getenv("RECOMP_UI_PICKER_SELFTEST")) return RECOMP_LAUNCHER_RESULT_UNAVAILABLE;
    const int handoff = run_netplay_handoff(io, game, initial_rom, out_rom_path, out_rom_path_len);
    if (handoff >= 0) return handoff;

    auto holder = std::make_unique<App>();
    App& app = *holder;
    std::string assets = resolve_assets_dir(assets_dir);
    app.begin(io, game, assets.c_str(), initial_rom);
    auto write_rom = [&] {
        if (out_rom_path && out_rom_path_len) {
            const std::string rom =
                app.s.primary_disc().empty() ? (initial_rom ? initial_rom : "") : app.s.primary_disc();
            std::snprintf(out_rom_path, out_rom_path_len, "%s", rom.c_str());
        }
    };
    // In-game launcher (launcher.in_game): no pre-game window. A match the
    // in-game menu's lobby started launches now; otherwise the game starts
    // when the disc is ready, and a first run shows disc setup only.
    if (app.in_game && !std::getenv("R4L_SCREEN") && !std::getenv("R4L_SCREENSHOT")) {
        const RecompLauncherCNetplayCallbacks* np = game ? game->netplay : nullptr;
        if (io && np && np->launch_pending && np->launch_pending(np->ctx) && np->fill_launch) {
            RecompLauncherCNetplayLaunch l{};
            if (np->fill_launch(np->ctx, &l) && l.enabled &&
                (!game->mods || !game->mods->commit_netplay ||
                 game->mods->commit_netplay(game->mods->ctx, app.s.primary_disc().c_str()))) {
                std::string err;
                io->netplay_launch = l;
                app.s.persist_media(&err);
                app.s.commit_files(&err);
                app.s.outcome = Outcome::Launch;
                std::fprintf(stdout, "r4l: in-game menu match -> launch\n");
            }
            if (np->clear_launch_pending) np->clear_launch_pending(np->ctx);
        }
        if (app.s.outcome == Outcome::None && app.s.media_ready()) {
            app.ensure_disc_assets();  // the in-game menu's disc-sourced skin art
            app.request_launch();
        }
        if (app.s.outcome == Outcome::Launch) {
            app.job.join();
            write_rom();
            return RECOMP_LAUNCHER_RESULT_LAUNCH;
        }
        std::fprintf(stderr, "r4l: in-game launch held: %s\n",
                     app.s.status.empty() ? "no disc yet" : app.s.status.c_str());
        if (!app.s.media_ready()) {
            app.setup_only = true;
            app.screen = Screen::Setup;
        }
    }

    int w = 1280, h = 800;
    load_window_size(app.s.launcher_prefs_path(), &w, &h);
    if (const char* sz = std::getenv("R4L_SIZE")) std::sscanf(sz, "%dx%d", &w, &h);
    const bool hidden = std::getenv("R4L_HIDDEN") != nullptr;
    Platform plat;
    if (!plat.open(window_title ? window_title : "Launcher", w, h, hidden,
                   game ? game->window_icon_path : nullptr, assets)) {
        plat.close(g_preserve_sdl != 0);
        return RECOMP_LAUNCHER_RESULT_UNAVAILABLE;
    }
    apply_theme(*app.title, 1.0f);
    app.init_skin();
    const char* shot = std::getenv("R4L_SCREENSHOT");  // capture one frame and quit
    // Test hook: press Play after a few frames (scripted first-run checks).
    const bool autoplay = std::getenv("R4L_AUTOPLAY") && !shot;
    int frames = 0;
    Script script;
    const bool scripted = std::getenv("R4L_SCRIPT") && script.load(std::getenv("R4L_SCRIPT"));
    while (app.s.outcome == Outcome::None) {
        if (scripted) {
            script.step(app, plat);
            script_frame_begin();
            ImGui::GetCurrentContext()->TestEngineHookItems = true;
        }
        if (!plat.pump(app)) app.request_quit();
        plat.begin_frame();
        app.frame();
        plat.end_frame(shot != nullptr || scripted);
        if (autoplay && ++frames == 8) app.request_launch();
        if (shot && ++frames == 4) {
            plat.capture_png(shot);
            app.request_quit();
        }
    }
    if (scripted) { std::fprintf(stderr, "[r4l-script] launcher script done, failures=%d\n", script.failures()); std::fflush(stderr); }
    if (!hidden) {
        int cw = 0, ch = 0;
        SDL_GetWindowSize(plat.window, &cw, &ch);
        if (cw > 0) save_window_size(app.s.launcher_prefs_path(), cw, ch);
    }
    app.job.join();
    skin().unload();  // fonts/textures belong to this ImGui/GL context
    plat.close(g_preserve_sdl != 0);

    write_rom();
    switch (app.s.outcome) {
    case Outcome::Launch: return RECOMP_LAUNCHER_RESULT_LAUNCH;
    case Outcome::Relaunch:
        g_relaunch_exe = app.s.relaunch_exe;
        return RECOMP_LAUNCHER_RESULT_RELAUNCH;
    default:
        if (io) std::memset(&io->netplay_launch, 0, sizeof(io->netplay_launch));
        return RECOMP_LAUNCHER_RESULT_QUIT;
    }
}
