// screens_system.cpp — Settings page (audio, system, BIOS, memory cards, host
// shortcut keys), first-run jobs (toolchain, PGO, FMV timing, BIOS prepare),
// disc auto-scan, disc-sourced skin assets, Restore defaults, translations.
#include "skin.h"
#include "ui.h"
#include "script.h"
#include "dialogs.h"

#include "r4l/core/discscan.h"
#include "r4l/core/json.h"
#include "r4l/core/memcard.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

namespace r4l {

// ---------------------------------------------------------------- i18n

namespace {
std::map<std::string, std::string> g_tr;
}

const char* tr(const char* english) {
    if (g_tr.empty() || !english) return english;
    auto it = g_tr.find(english);
    return it == g_tr.end() ? english : it->second.c_str();
}

void load_language(const std::string& assets_dir, const std::string& lang) {
    g_tr.clear();
    if (lang.empty() || lang == "en") return;
    std::ifstream f(join_path(join_path(assets_dir, "i18n"), lang + ".json"));
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    std::string err;
    if (!parse_json(ss.str(), &j, &err)) return;
    for (const auto& kv : j.obj) g_tr[kv.first] = kv.second.str();
}

// ---------------------------------------------------------------- helpers

namespace {
void SDLCALL pick_cb(void* user, const char* const* files, int) {
    auto* fn = static_cast<std::function<void(const std::string&)>*>(user);
    if (files && files[0]) (*fn)(files[0]);
    delete fn;
}
void pick_file(const char* desc, const char* patterns, std::function<void(const std::string&)> done, bool save = false) {
    static SDL_DialogFileFilter f;
    static std::string d, p;
    d = desc;
    p = patterns;
    f = SDL_DialogFileFilter{d.c_str(), p.c_str()};
    auto* fn = new std::function<void(const std::string&)>(std::move(done));
    if (save) show_save_file(pick_cb, fn, SDL_GL_GetCurrentWindow(), &f, 1, nullptr);
    else show_open_file(pick_cb, fn, SDL_GL_GetCurrentWindow(), &f, 1, nullptr, false);
}

void SDLCALL job_cb(void* ctx, float pct, const char* msg) {
    Job* j = static_cast<Job*>(ctx);
    std::lock_guard<std::mutex> lk(j->mu);
    j->pct = pct;
    j->message = msg ? msg : "";
}
}  // namespace


// ---------------------------------------------------------------- jobs

void App::start_job(int kind) {
    const RecompLauncherCGameInfo* g = s.game;
    if (!g || job.running) return;
    job_kind = kind;
    job.join();
    job.running = true;
    job.done = false;
    job.rc = 0;
    job.worker = std::thread([this, g, kind]() {
        char out[1024] = {0}, err[512] = {0};
        int ok = 0;
        const std::string disc = s.primary_disc();
        switch (kind) {
        case 1:
            if (g->rebuild_with_progress)
                ok = g->rebuild_with_progress(disc.c_str(), out, sizeof out, err, sizeof err, job_cb, &job);
            break;
        case 2:
            if (g->ensure_toolchain_with_progress)
                ok = g->ensure_toolchain_with_progress(1, nullptr, err, sizeof err, job_cb, &job);
            break;
        case 3:
            if (g->pgo_optimize_with_progress)
                ok = g->pgo_optimize_with_progress(disc.c_str(), out, sizeof out, err, sizeof err, job_cb, &job);
            break;
        case 4:
            if (g->fmv_timing_optimize_with_progress)
                ok = g->fmv_timing_optimize_with_progress(disc.c_str(), out, sizeof out, err, sizeof err, job_cb, &job);
            break;
        case 5:
            if (g->bios_prepare_with_progress)
                ok = g->bios_prepare_with_progress(s.bios_path.c_str(), err, sizeof err, job_cb, &job);
            break;
        default: break;
        }
        {
            std::lock_guard<std::mutex> lk(job.mu);
            job.error = err;
            job.out_path = (ok && kind != 2 && kind != 5 && g->relaunch_after_rebuild) ? out : "";
        }
        job.rc = ok;
        job.running = false;
        job.done = true;
    });
}

void App::restore_defaults() {
    if (!s.io || !have_defaults) return;
    RecompLauncherCSettings keep = *s.io;
    *s.io = defaults;
    // Paths and identity are the player's, not settings.
    std::memcpy(s.io->bios_path, keep.bios_path, sizeof keep.bios_path);
    std::memcpy(s.io->memcard_path, keep.memcard_path, sizeof keep.memcard_path);
    std::memcpy(s.io->netplay_player_name, keep.netplay_player_name, sizeof keep.netplay_player_name);
    std::memcpy(s.io->player_gamepad_guid, keep.player_gamepad_guid, sizeof keep.player_gamepad_guid);
    surf.apply(s.io);
    s.keys = KeyboardBinds::defaults();
    s.pads.global = PadMapping::defaults();
    s.binds_dirty = true;
    s.status = tr("Settings restored to their defaults.");
}

// ---------------------------------------------------------------- disc

void App::autoscan_disc() {
    if (!s.game || !s.game->disc_verify) return;
    const std::string want = title->serial ? title->serial : "";
    const std::string hit = find_disc(default_scan_roots(s.exe_dir), [&](const std::string& p) {
        RecompLauncherCDiscVerify v{};
        if (!s.game->disc_verify(p.c_str(), &v)) return false;
        if (v.verdict == 3) return false;
        return want.empty() || want == v.serial;
    });
    if (hit.empty()) return;
    s.set_disc(0, hit);
    s.status = std::string(tr("Found your disc: ")) + hit;
}

void App::ensure_disc_assets() {
    if (assets_checked || !title || !title->extract_disc_assets) return;
    const DiscPick& d = s.discs[0];
    if (d.path.empty() || !d.verified || d.verify.verdict == 3) return;
    assets_checked = true;
    const std::string root = join_path(s.exe_dir, "disc-assets");
    const std::string dir = join_path(root, title->id);
    const std::string marker = join_path(dir, ".source");
    std::ifstream m(marker);
    std::string src;
    std::getline(m, src);
    if (src == d.path) return;  // already extracted from this disc
    {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    char err[256] = {0};
    if (title->extract_disc_assets(d.path.c_str(), dir.c_str(), err, sizeof err)) {
        std::ofstream(marker) << d.path << "\n";
        if (!skin_dir.empty()) load_skin_named(*this, skin_dir);  // pick the new art up
    } else {
        std::fprintf(stderr, "[r4l] disc assets: %s\n", err);
    }
}

// ---------------------------------------------------------------- memory cards

void App::draw_memcards() {
    RecompLauncherCSettings* io = s.io;
    const RecompLauncherCGameInfo* g = s.game;
    section(tr("Memory cards"));
    for (int slot = 0; slot < 2; ++slot) {
        ImGui::PushID(slot);
        char lbl[32];
        std::snprintf(lbl, sizeof lbl, "%s %d", tr("Slot"), slot + 1);
        row_toggle(lbl, &io->memcard_enabled[slot]);
        if (io->memcard_enabled[slot]) {
            ImGui::TextDisabled("%s", io->memcard_path[slot][0] ? io->memcard_path[slot] : tr("Default card"));
            RecompLauncherCMemcard mc{};
            if (g && g->memcard_inspect && io->memcard_path[slot][0] && g->memcard_inspect(io->memcard_path[slot], &mc) &&
                mc.valid) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float b = 18;
                for (int i = 0; i < 15; ++i) {
                    const ImVec2 a(p.x + i * (b + 4), p.y);
                    dl->AddRectFilled(a, ImVec2(a.x + b, a.y + b),
                                      ImGui::ColorConvertFloat4ToU32(mc.block_used[i] ? theme().accent : theme().surface_hi), 3);
                }
                ImGui::Dummy(ImVec2(15 * (b + 4), b));
                ImGui::SameLine();
                ImGui::TextDisabled("%d / 15 %s", mc.used_blocks, tr("blocks used"));
            }
            if (ImGui::Button(tr("Choose card..."))) {
                pick_file("Memory card", "mcd;mcr;srm;bin", [io, slot](const std::string& p) {
                    std::snprintf(io->memcard_path[slot], sizeof io->memcard_path[slot], "%s", p.c_str());
                });
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("New card..."))) {
                pick_file("Memory card", "mcd", [this, io, slot](const std::string& p) {
                    if (format_memcard(p)) std::snprintf(io->memcard_path[slot], sizeof io->memcard_path[slot], "%s", p.c_str());
                    else s.status = tr("Could not create the memory card.");
                }, true);
            }
            if (io->memcard_path[slot][0]) {
                ImGui::SameLine();
                if (ImGui::Button(tr("Use default"))) io->memcard_path[slot][0] = 0;
            }
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- Settings page

void App::draw_system() {
    RecompLauncherCSettings* io = s.io;
    const RecompLauncherCGameInfo* g = s.game;
    screen_title(tr("Settings"));
    if (!io || !g) return;

    if (surf.any_shown("audio.")) {
        section(tr("Audio"));
        if (S("audio.enable")) row_toggle(tr("Sound"), &io->enable_audio);
        if (S("audio.volume")) row_slider(tr("Volume"), &io->volume, 0, 100, "%d%%");
        if (S("audio.frequency")) {
            static const char* kRates[] = {"44100 Hz", "48000 Hz"};
            int v = io->audio_freq == 48000 ? 1 : 0;
            if (row_combo(tr("Sample rate"), &v, kRates, 2)) io->audio_freq = v ? 48000 : 44100;
        }
        if (S("audio.spu_hq") && g->has_spu_hq) row_toggle(tr("High-quality audio"), &io->spu_hq);
        if (S("audio.device") && g->num_audio_devices > 0 && g->audio_device_labels) {
            int cur = 0;
            for (int i = 0; i < g->num_audio_devices; ++i)
                if (!std::strcmp(g->audio_device_labels[i], io->audio_device)) cur = i;
            if (row_combo(tr("Output device"), &cur, g->audio_device_labels, g->num_audio_devices))
                std::snprintf(io->audio_device, sizeof io->audio_device, "%s", g->audio_device_labels[cur]);
        }
    }
    if (surf.any_shown("system.")) {
        section(tr("System"));
        if (S("system.language") && g->num_languages > 0 && g->language_labels)
            row_combo(tr("Language"), &io->language_index, g->language_labels, g->num_languages);
        if (S("system.skip_fmv") && g->has_skip_fmv) row_toggle(tr("Skip movies"), &io->auto_skip_fmv);
        if (S("system.turbo_loads") && g->has_turbo_loads) row_toggle(tr("Fast loading"), &io->turbo_loads);
        if (S("system.rewind") && g->has_rewind_depth) {
            row_toggle(tr("Rewind"), &io->rewind_enabled);
            if (io->rewind_enabled) {
                row_slider(tr("Rewind buffer"), &io->rewind_depth, 10, 600, "%d");
                row_slider(tr("Snapshot every"), &io->rewind_interval, 1, 60, "%d frames");
            }
        }
        if (S("system.skip_launcher")) row_toggle(tr("Skip this launcher next time"), &io->skip_launcher);
        if (S("system.player_name") && g->has_player_name) {
            if (g->identity_detail) ImGui::TextDisabled("%s", g->identity_detail);
            ImGui::SetNextItemWidth(320);
            ImGui::InputText(tr("Player name"), io->player_name, sizeof io->player_name);
        }
        if (S("system.memcards")) draw_memcards();
    }
    if (S("bios.select") && g->has_bios) {
        section(tr("BIOS"));
        ImGui::TextDisabled("%s", s.bios_path.empty() ? tr("OpenBIOS (bundled)") : s.bios_path.c_str());
        if (ImGui::Button(tr("Choose BIOS..."))) pick_file("BIOS", "bin;rom", [this](const std::string& p) { s.set_bios(p); });
        ImGui::SameLine();
        if (ImGui::Button(tr("Use OpenBIOS"))) s.set_bios("");
        if (!s.bios_path.empty())
            chip(s.bios_verify.detail[0] ? s.bios_verify.detail : (s.bios_verify.ok ? tr("BIOS ok") : tr("Unrecognised BIOS")),
                 s.bios_verify.ok ? theme().ok : theme().warn);
        if (S("bios.prepare") && g->bios_prepare_with_progress && s.bios_verify.needs_regen) {
            if (g->bios_prepare_title) ImGui::TextUnformatted(g->bios_prepare_title);
            ImGui::TextWrapped("%s", g->bios_prepare_note ? g->bios_prepare_note : tr("This BIOS needs to be prepared once."));
            if (ImGui::Button(g->bios_prepare_button ? g->bios_prepare_button : tr("Prepare BIOS"))) start_job(5);
        }
    }
    if (S("controls.hotkeys")) {
        section(tr("Shortcut keys"));
        if (g->has_open_launcher_hotkey) {
            bool have = false;
            for (const auto& h : s.hotkeys) have |= h.first == "OpenLauncher";
            if (!have) s.hotkeys.emplace_back("OpenLauncher", "");
        }
        for (size_t i = 0; i < s.hotkeys.size(); ++i) {
            ImGui::PushID(static_cast<int>(i) + 500);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(s.hotkeys[i].first.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.5f);
            const std::string v = s.hotkeys[i].second.empty() ? "-" : s.hotkeys[i].second;
            if (ImGui::Button((v + "##hk").c_str(), ImVec2(220, 0))) {
                capture.kind = Capture::HostKey;
                capture.input = static_cast<int>(i);
                capture.started = time;
            }
            script_mark(("key:" + s.hotkeys[i].first).c_str());
            ImGui::PopID();
        }
    }
    if (S("disc.pgo") && g->pgo_optimize_with_progress) {
        section(tr("Movies"));
        if (ImGui::Button(tr("Optimize movie playback"))) start_job(3);
    }
    if (S("disc.fmv_timing") && g->fmv_timing_optimize_with_progress) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Apply movie timing"))) start_job(4);
    }
    if (job.running) {
        std::lock_guard<std::mutex> lk(job.mu);
        const char* busy = job_kind == 3 ? g->pgo_busy_status : job_kind == 4 ? g->fmv_timing_busy_status
                         : job_kind == 5 ? g->bios_prepare_busy_status : job_kind == 1 ? g->rebuild_busy_status : nullptr;
        if (busy) ImGui::TextDisabled("%s", busy);
        ImGui::ProgressBar(job.pct / 100.0f, ImVec2(-1, 0), job.message.c_str());
    }
    if (have_defaults) {
        ImGui::Dummy(ImVec2(0, 10));
        if (ImGui::Button(tr("Restore defaults"))) ImGui::OpenPopup("restore");
        script_mark("settings:restore");
        if (ImGui::BeginPopupModal("restore", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(tr("Reset every setting and binding to its default?"));
            script_mark("restore:confirm");
            if (ImGui::Button(tr("Restore"))) {
                restore_defaults();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
}

}  // namespace r4l

// ---------------------------------------------------------------- main screen

#include "recomp_launcher_overlay.h"
#include <SDL3/SDL_opengl.h>
#include <ctime>

namespace r4l {

bool App::states_supported() const {
    return mode == Mode::Overlay && host && host->abi_version >= 3 && host->state_slot_count &&
           host->state_slot_count(host->ctx) > 0;
}

bool App::states_allowed() const {
    return states_supported() && !netplay_locked && host->state_available && host->state_available(host->ctx);
}

void App::navigate_back() {
    if (state_request_pending || race_action_pending || introduction_first) return;
    if (disc_browser_open) { disc_browser_open = false; content_focus = true; return; }
    if (main_screen && options_open) {
        open_options(false);
    } else if (main_screen && screen != Screen::Main && !setup_only) {
        const Screen previous = screen;
        screen = Screen::Main;
        focus_request = previous;
        if (previous == Screen::LocalPlayers) {
            for (int i = 0; title->game_modes && i < title->game_mode_count && i < 6; ++i)
                if (title->game_modes[i].action == title->local_multiplayer_action)
                    focus_request = static_cast<Screen>(static_cast<int>(Screen::GameMode0) + i);
        }
        content_focus = false;
    } else if (!setup_only && !(title->race_actions && (title->race_actions() & 8u))) {
        overlay_open = false;
    } else if (rail_focus != Screen::Count) {
        // The home menu has nothing to go back to; keep the highlight where
        // it was instead of letting the nav cancel clear it.
        focus_request = rail_focus;
    }
}

void App::open_options(bool open) {
    options_open = open;
    if (open) {
        screen = surf.any_shown("graphics.") ? Screen::Graphics : surf.any_shown("controls.") ? Screen::Controls : Screen::System;
        focus_request = Screen::Count;
        content_focus = true;
    } else {
        content_focus = false;
        screen = Screen::Main;
        focus_request = Screen::Options;
    }
}

bool App::introduction_seen() const {
    return std::ifstream(sibling_path(s.launcher_prefs_path(), "introduction-v1.txt")).good();
}

void App::draw_local_players() {
    screen_title(tr("Local split-screen"));
    ImGui::TextWrapped("%s", tr("Race together on this screen. Each player uses their own controller or keyboard bindings."));
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::BeginDisabled(race_action_pending);
    for (unsigned players = 2; players <= static_cast<unsigned>(std::min(title->max_players, 4)); ++players) {
        const std::string label = std::to_string(players) + " " + tr("players");
        if (big_button(label.c_str(), ImVec2(240, 48), false)) {
            if (!netplay_locked && title->request_local_game && title->request_local_game(players)) {
                race_action_pending = true;
                s.status = tr("Loading...");
                if (host && host->set_paused) host->set_paused(host->ctx, 0);
            } else s.status = tr("Connect the players' controllers or assign their keyboard bindings in Controls.");
        }
        script_mark(("local:" + std::to_string(players)).c_str());
    }
    ImGui::EndDisabled();
}

void App::draw_introduction() {
    screen_title(title->display_name, tr("Your controls and ways to race"));
    if (ImGui::BeginTable("intro_sections", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < title->introduction_count; ++i) {
            ImGui::TableNextColumn();
            const TitleNotice& item = title->introduction[i];
            ImGui::TextColored(theme().accent, "%s", tr(item.heading));
            ImGui::TextWrapped("%s", tr(item.body));
            ImGui::Dummy(ImVec2(0, 12));
        }
        ImGui::EndTable();
    }
    if (focus_request == Screen::Introduction) { ImGui::SetKeyboardFocusHere(); ImGui::SetNavCursorVisible(true); }
    const bool confirm_intro = introduction_first &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false));
    if (ImGui::Button(tr(introduction_first ? "Start" : "Back")) || confirm_intro) {
        if (introduction_first) {
            std::ofstream marker(sibling_path(s.launcher_prefs_path(), "introduction-v1.txt"));
            marker << "1\n";
            introduction_first = false;
            if (!(title->race_actions && (title->race_actions() & 8u))) overlay_open = false;
            screen = Screen::Main;
            focus_request = Screen::Resume;
        } else navigate_back();
    }
    if (focus_request == Screen::Introduction && ImGui::IsItemFocused()) focus_request = Screen::Count;
    ImGui::SetItemDefaultFocus();
    script_mark("intro:continue");
}

void App::queue_game_action(unsigned action) {
    if (!netplay_locked && title->request_race_action && title->race_action_status &&
        title->request_race_action(action)) {
        race_action_pending = true;
        s.status = tr("Loading...");
        if (host && host->set_paused) host->set_paused(host->ctx, 0);
    } else s.status = tr("This option is unavailable right now.");
}

void App::draw_race_action(bool restart) {
    screen_title(tr(restart ? "Restart race?" : "Retire from this race?"));
    // Confirmation consequences remain visible even when a skin hides subtitles.
    ImGui::TextWrapped("%s", tr(restart ? "Your progress in this race will be lost." :
                              "Leave this race using the original game rules. This may affect your championship."));
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::BeginDisabled(race_action_pending);
    if (ImGui::Button(tr("Cancel"))) navigate_back();
    script_mark("race:cancel");
    ImGui::SameLine();
    if (ImGui::Button(tr(restart ? "Restart race" : "Retire"))) {
        const unsigned action = restart ? 2u : 4u;
        queue_game_action(action);
    }
    script_mark("race:confirm");
    ImGui::EndDisabled();
}

void App::draw_main() {
    const char* name = title->display_name;
    screen_title(name, tr(game_started ? "Paused" : "Ready to start"));
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text);  // readable on the dark overlay panel

    if (netplay_locked) ImGui::TextWrapped("%s", tr("Save states are unavailable during netplay."));
    ImGui::PopStyleColor();
}

// Save State / Load State: the runtime's slots (overlay host v3). Picking a
// slot queues the request and closes the menu; it runs as the game resumes.
void App::draw_states(bool save) {
    screen_title(tr(save ? "Save State" : "Load State"),
                 tr(save ? "Capture this moment in a snapshot. This is separate from normal career saves."
                         : "Choose a snapshot to resume from that exact moment. Back returns to the menu."));
    if (!states_allowed()) {
        ImGui::TextDisabled("%s", tr("Save states are unavailable right now (netplay)."));
        return;
    }
    const int n = std::min(host->state_slot_count(host->ctx), 32);
    if (static_cast<int>(states.size()) != n) states.assign(n, StateSlot{});
    if (time - states_polled > 1.0) {  // slot files change only through us or the hotkey menu
        states_polled = time;
        for (int i = 0; i < n; ++i) {
            long long t = 0;
            states[i].used = host->state_info && host->state_info(host->ctx, i, &t);
            states[i].mtime = states[i].used ? t : 0;
        }
    }
    int first_slot = -1;
    for (int i = 0; i < n; ++i) if (save || states[i].used) { first_slot = i; break; }
    if (first_slot < 0 && states_focus_requested) {
        focus_request = Screen::Back;
        states_focus_requested = false;
    }
    const float u = ui_scale > 0 ? ui_scale : 1.0f;
    const float th = 60 * u, tw = th * 4 / 3, row = th + 12 * u;
    for (int i = 0; i < n; ++i) {
        StateSlot& sl = states[i];
        // Thumbnail (128x96 ARGB from the runtime) as a GL texture, refreshed when the slot changes.
        if (sl.used && sl.tex_mtime != sl.mtime && host->state_thumb) {
            static unsigned px[128 * 96];
            sl.tex_mtime = sl.mtime;
            if (host->state_thumb(host->ctx, i, px, 128, 96)) {
                static unsigned char rgba[128 * 96 * 4];
                for (int k = 0; k < 128 * 96; ++k) {
                    rgba[k * 4 + 0] = static_cast<unsigned char>(px[k] >> 16);
                    rgba[k * 4 + 1] = static_cast<unsigned char>(px[k] >> 8);
                    rgba[k * 4 + 2] = static_cast<unsigned char>(px[k]);
                    rgba[k * 4 + 3] = 255;
                }
                GLuint id = sl.tex;
                if (!id) glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_2D, id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 96, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                sl.tex = id;
            }
        }
        char label[96];
        if (sl.used && sl.mtime > 0) {
            std::time_t t = static_cast<std::time_t>(sl.mtime);
            char when[48] = "";
            if (const std::tm* lt = std::localtime(&t)) std::strftime(when, sizeof when, "%Y-%m-%d  %H:%M", lt);
            std::snprintf(label, sizeof label, "%s %d    %s", tr("Slot"), i + 1, when);
        } else {
            std::snprintf(label, sizeof label, "%s %d    %s", tr("Slot"), i + 1, sl.used ? "" : tr("Empty"));
        }
        ImGui::PushID(i);
        const bool enabled = !state_request_pending && (save || sl.used);
        ImGui::BeginDisabled(!enabled);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (states_focus_requested && i == first_slot) ImGui::SetKeyboardFocusHere();
        const bool hit = ImGui::Selectable("##slot", states_slot == i, 0, ImVec2(0, row));
        if (ImGui::IsItemFocused()) { states_slot = i; states_focus_requested = false; }
        ImGui::EndDisabled();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a(p.x + 6 * u, p.y + 6 * u), b(a.x + tw, a.y + th);
        if (sl.tex) dl->AddImage(static_cast<ImTextureID>(sl.tex), a, b);
        else dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(theme().surface_hi), 4);
        dl->AddText(ImVec2(b.x + 16 * u, p.y + (row - ImGui::GetFontSize()) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(enabled ? theme().text : theme().text_dim), label);
        script_mark((std::string(save ? "save:" : "load:") + std::to_string(i + 1)).c_str());
        if (hit && enabled) {
            states_slot = i;
            const int ok = save ? (host->state_save && host->state_save(host->ctx, i))
                                : (host->state_load && host->state_load(host->ctx, i));
            if (ok) {
                skin().play("confirm");
                states_polled = -100;
                std::fprintf(stdout, "r4l: %s slot %d queued\n", save ? "save" : "load", i + 1);
                if (host->abi_version >= 4 && host->state_status) {
                    state_request_pending = true;
                    state_request_load = !save;
                    s.status = tr(save ? "Saving snapshot..." : "Loading snapshot...");
                } else overlay_open = false;  // compatibility with older hosts
            } else {
                s.status = tr(save ? "Could not save to that slot." : "Could not load that slot.");
            }
        }
        ImGui::PopID();
    }
}

}  // namespace r4l
