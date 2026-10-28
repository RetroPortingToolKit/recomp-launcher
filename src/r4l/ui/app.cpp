// app.cpp — frame layout, navigation, Home / Disc setup / About, widgets.
#include "skin.h"
#include "ui.h"
#include "script.h"
#include "recomp_launcher_overlay.h"
#include "dialogs.h"

#include <dirent.h>
#include <fstream>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace r4l {

namespace {
Theme g_theme;

ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
ImVec4 from(const Rgb& c, float a = 1.0f) { return rgb(c.r, c.g, c.b, a); }
ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

}  // namespace

const char* screen_name(Screen s) {
    switch (s) {
    case Screen::Home: return "Play";
    case Screen::Graphics: return "Graphics";
    case Screen::Mods: return "Mods";
    case Screen::Controls: return "Controls";
    case Screen::Netplay: return "Netplay";
    case Screen::Setup: return "Disc setup";
    case Screen::About: return "About";
    case Screen::System: return "Settings";
    case Screen::Resume: return "Resume";
    case Screen::QuitGame: return "Quit game";
    default: return "";
    }
}

const Theme& theme() { return g_theme; }

void apply_theme(const TitleLayer& t, float ui_scale) {
    Theme& th = g_theme;
    th.bg = rgb(11, 12, 20);
    th.surface = rgb(21, 23, 36);
    th.surface_hi = rgb(32, 35, 54);
    th.line = rgb(52, 56, 84);
    th.text = rgb(236, 238, 248);
    th.text_dim = rgb(150, 156, 186);
    th.accent = from(t.accent);
    th.accent2 = from(t.accent2);
    th.ok = rgb(76, 214, 140);
    th.warn = rgb(255, 196, 70);
    th.bad = rgb(255, 86, 96);

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 0;
    st.ChildRounding = th.radius;
    st.FrameRounding = 8;
    st.PopupRounding = th.radius;
    st.GrabRounding = 8;
    st.TabRounding = 8;
    st.FramePadding = ImVec2(14, 10);
    st.ItemSpacing = ImVec2(12, 10);
    st.WindowPadding = ImVec2(24, 20);
    st.ScrollbarSize = 12;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.FrameBorderSize = 0;
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = th.text;
    c[ImGuiCol_TextDisabled] = th.text_dim;
    c[ImGuiCol_WindowBg] = th.bg;
    c[ImGuiCol_ChildBg] = th.surface;
    c[ImGuiCol_PopupBg] = th.surface_hi;
    c[ImGuiCol_Border] = th.line;
    c[ImGuiCol_FrameBg] = th.surface_hi;
    c[ImGuiCol_FrameBgHovered] = rgb(44, 48, 74);
    c[ImGuiCol_FrameBgActive] = rgb(54, 58, 90);
    c[ImGuiCol_Button] = th.surface_hi;
    c[ImGuiCol_ButtonHovered] = rgb(48, 52, 80);
    c[ImGuiCol_ButtonActive] = th.accent;
    c[ImGuiCol_Header] = rgb(40, 44, 70);
    c[ImGuiCol_HeaderHovered] = rgb(50, 54, 86);
    c[ImGuiCol_HeaderActive] = rgb(60, 64, 100);
    c[ImGuiCol_CheckMark] = th.accent;
    c[ImGuiCol_SliderGrab] = th.accent;
    c[ImGuiCol_SliderGrabActive] = th.accent;
    c[ImGuiCol_Separator] = th.line;
    c[ImGuiCol_NavCursor] = th.accent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = th.line;
    c[ImGuiCol_TextSelectedBg] = from(t.accent, 0.35f);
    c[ImGuiCol_ModalWindowDimBg] = rgb(0, 0, 0, 0.6f);
    st.ScaleAllSizes(ui_scale);
    th.row_h = 44.0f * ui_scale;
}

// ---------------------------------------------------------------- widgets

bool section(const char* label) {
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(g_theme.bold, g_theme.body_size * 0.82f);
    ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_dim);
    std::string up = label;
    for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    ImGui::TextUnformatted(up.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                                        u32(g_theme.line));
    ImGui::Dummy(ImVec2(0, 4));
    return true;
}

namespace {
// Two-column row: label left, control right. Returns the control width.
float row_begin(const char* label, const char* help) {
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) {
        ImGui::SameLine();
        help_marker(help);
    }
    const float cw = std::min(380.0f, w * 0.48f);
    ImGui::SameLine(w - cw + ImGui::GetStyle().WindowPadding.x * 0.0f);
    ImGui::SetNextItemWidth(cw);
    return cw;
}
}  // namespace

bool row_combo(const char* label, int* v, const char* const* items, int count, const char* help) {
    ImGui::PushID(label);
    row_begin(label, help);
    int cur = (*v >= 0 && *v < count) ? *v : 0;
    bool changed = false;
    const bool ct = skin().loaded() && skin().has_color("control_text");
    if (ct) ImGui::PushStyleColor(ImGuiCol_Text, skin().color("control_text", g_theme.text));
    const bool open = ImGui::BeginCombo("##c", count ? items[cur] : "");
    if (open) {
        for (int i = 0; i < count; ++i) {
            const bool sel = i == cur;
            if (ImGui::Selectable(items[i], sel)) {
                *v = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ct) ImGui::PopStyleColor();
    script_mark(label);
    ImGui::PopID();
    return changed;
}

bool row_toggle(const char* label, int* v, const char* help) {
    ImGui::PushID(label);
    const float cw = row_begin(label, help);
    // Pill switch drawn over an invisible button so mouse, keyboard and
    // gamepad all activate it the same way.
    const float h = ImGui::GetFrameHeight();
    const float w = h * 1.9f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cw - w);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##t", ImVec2(w, h));
    if (clicked) *v = *v ? 0 : 1;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 bg = *v ? g_theme.accent : g_theme.surface_hi;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), u32(bg), h * 0.5f);
    if (ImGui::IsItemHovered() || ImGui::IsItemFocused())
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), u32(g_theme.text_dim), h * 0.5f, 0, 1.5f);
    const float r = h * 0.5f - 4;
    const float cx = *v ? p.x + w - h * 0.5f : p.x + h * 0.5f;
    dl->AddCircleFilled(ImVec2(cx, p.y + h * 0.5f), r, u32(g_theme.text), 24);
    script_mark(label);
    ImGui::PopID();
    return clicked;
}

bool row_slider(const char* label, int* v, int lo, int hi, const char* fmt, const char* help) {
    ImGui::PushID(label);
    row_begin(label, help);
    const bool c = ImGui::SliderInt("##s", v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp);
    script_mark(label);
    ImGui::PopID();
    return c;
}

void row_text(const char* label, const char* value, ImVec4 color) {
    ImGui::PushID(label);
    row_begin(label, nullptr);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::PopID();
}

bool big_button(const char* label, ImVec2 size, bool primary) {
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, g_theme.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(g_theme.accent.x * 1.1f, g_theme.accent.y * 1.1f,
                                     g_theme.accent.z * 1.1f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_theme.accent2);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    }
    const bool ct = !primary && skin().loaded() && skin().has_color("control_text");
    if (ct) ImGui::PushStyleColor(ImGuiCol_Text, skin().color("control_text", g_theme.text));
    ImGui::PushFont(g_theme.bold, 0.0f);
    const bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    if (ct) ImGui::PopStyleColor();
    if (primary) ImGui::PopStyleColor(4);
    return r;
}

void chip(const char* text, ImVec4 color) {
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 sz(ts.x + 20, ts.y + 8);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                      u32(ImVec4(color.x, color.y, color.z, 0.18f)), sz.y * 0.5f);
    dl->AddText(ImVec2(p.x + 10, p.y + 4), u32(color), text);
    ImGui::Dummy(sz);
}

void help_marker(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

extern bool g_title_body_open;
void screen_title(const char* t, const char* sub) {
    Skin& sk = skin();
    if (sk.loaded()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const std::string txt = sk.label(std::string("title.") + t, t);
        sk.text(ImGui::GetWindowDrawList(), "heading", p, txt);
        ImGui::Dummy(sk.text_size("heading", txt));
    } else {
        ImGui::PushFont(g_theme.bold, g_theme.body_size * 1.8f);
        ImGui::TextUnformatted(t);
        ImGui::PopFont();
    }
    if (sk.loaded()) {  // gap below the heading, e.g. to clear a header rule
        const float gap = sk.model().metric("heading_gap@" + std::string(t), sk.model().metric("heading_gap", 0));
        if (gap > 0) ImGui::Dummy(ImVec2(0, gap * sk.s()));
    }
    if (sub && (!sk.loaded() || sk.model().metric("subtitle", 1) != 0)) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_theme.text_dim);
        ImGui::TextWrapped("%s", sub);
        ImGui::PopStyleColor();
    }
    // Everything below the title scrolls on its own; the title stays pinned.
    if (!g_title_body_open) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##title_body", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        ImGui::PopStyleColor();
        g_title_body_open = true;
    }
}

bool g_title_body_open = false;
void screen_body_end() {
    if (!g_title_body_open) return;
    ImGui::EndChild();
    g_title_body_open = false;
}

std::vector<std::string> list_skins(const std::string& assets_dir) {
    std::vector<std::string> out;
    const std::string root = join_path(assets_dir, "skins");
    if (DIR* d = opendir(root.c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            if (std::ifstream(join_path(join_path(root, e->d_name), "skin.json"))) out.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool load_skin_named(App& app, const std::string& name) {
    std::string dir = name;
    if (name.find('/') == std::string::npos) dir = join_path(join_path(app.s.assets_dir, "skins"), name);
    std::string err;
    if (!skin().load(dir, &err)) {
        app.s.status = "Skin: " + err;
        return false;
    }
    app.skin_dir = dir;
    apply_theme(*app.title, 1.0f);
    apply_skin_theme();
    for (const auto& w : skin().model().warnings) std::fprintf(stderr, "[skin] %s\n", w.c_str());
    if (app.mode == Mode::Launcher && name.find('/') == std::string::npos) {
        // Remember the choice beside launcher-window.ini.
        std::ofstream(sibling_path(app.s.launcher_prefs_path(), "launcher-skin.txt")) << name << "\n";
    }
    return true;
}

// "*.cue" style patterns -> "cue;bin" for SDL's dialog filter.
std::string patterns_of(const char* const* pats, int n, const char* fallback) {
    std::string out;
    for (int i = 0; pats && i < n; ++i) {
        std::string p = pats[i] ? pats[i] : "";
        const size_t dot = p.find_last_of('.');
        if (dot != std::string::npos) p = p.substr(dot + 1);
        if (!p.empty() && p != "*") out += (out.empty() ? "" : ";") + p;
    }
    return out.empty() ? fallback : out;
}

// ---------------------------------------------------------------- lifecycle

void App::begin(RecompLauncherCSettings* io, const RecompLauncherCGameInfo* game,
                const char* assets_dir, const char* initial_rom) {
    title = &active_title();
    surf.bind(title->surface, title->surface_count);
    surf.set_show_all(std::getenv("R4L_SURFACE") && !std::strcmp(std::getenv("R4L_SURFACE"), "all"));
    for (const auto& k : surf.unknown_keys()) std::fprintf(stderr, "[r4l] surface: unknown key %s\n", k.c_str());
    if (io) {
        // Restore defaults: the host's defaults when it gives them, else the
        // settings as they were when the launcher opened.
        defaults = (game && game->default_settings) ? *game->default_settings : *io;
        have_defaults = true;
        surf.apply(io);  // Locked / Auto values from the title's manifest
    }
    s.begin(io, game, assets_dir, initial_rom);
    // RECOMP_LAUNCHER_PREGAME (psxrecomp --launcher) keeps the old pre-game launcher.
    in_game = surf.automatic("launcher.in_game") && !std::getenv("RECOMP_LAUNCHER_PREGAME");
    mods.bind(game ? game->mods : nullptr);
#if defined(PSX_MOD_DEVELOPER_CHANNEL)
    mods.refresh(true);
#else
    mods.refresh(false);
#endif
    const char* xdg = std::getenv("XDG_CURRENT_DESKTOP");
    game_mode = (xdg && std::strstr(xdg, "gamescope")) || std::getenv("GAMESCOPE_WAYLAND_DISPLAY") ||
                (std::getenv("SteamGamepadUI") && std::strcmp(std::getenv("SteamGamepadUI"), "1") == 0);
    if (const char* p = std::getenv("R4L_SCREEN")) {  // screenshots / deep link
        for (int i = 0; i < static_cast<int>(Screen::Count); ++i)
            if (!std::strcmp(p, screen_name(static_cast<Screen>(i)))) screen = static_cast<Screen>(i);
    } else if (s.needs_setup()) {
        screen = Screen::Setup;
    }
    // Graphics preset "auto": with nothing chosen yet, take the detected preset.
    if (io && game && game->quality_apply && io->quality_preset == 0 && game->quality_detected >= 1 &&
        game->quality_detected <= 4) {
        const char* v = surf.value("graphics.preset");
        if (!v || !std::strcmp(v, "auto")) s.quality.select(game->quality_detected, io);
    }
    if (mode == Mode::Launcher && s.primary_disc().empty() && !std::getenv("R4L_NO_AUTOSCAN") &&
        (S("disc.autoscan") || surf.automatic("disc.autoscan")))
        autoscan_disc();
#ifndef R4L_DEFAULT_LANGUAGE
#define R4L_DEFAULT_LANGUAGE ""
#endif
    load_language(s.assets_dir, std::getenv("R4L_LANGUAGE") ? std::getenv("R4L_LANGUAGE") : R4L_DEFAULT_LANGUAGE);
    if (game && game->netplay && game->netplay->player_name) {
        const char* n = game->netplay->player_name(game->netplay->ctx);
        if (n && *n && io && !io->netplay_player_name[0])
            std::snprintf(io->netplay_player_name, sizeof(io->netplay_player_name), "%s", n);
    }
}

void App::init_skin() {
    {
        std::string want;
        if (const char* e = std::getenv("R4L_SKIN")) want = e;
        if (want.empty()) {
            std::ifstream f(sibling_path(s.launcher_prefs_path(), "launcher-skin.txt"));
            std::getline(f, want);
        }
        if (want.empty()) want = title->id;  // the title's own skin, then the console theme, then "default"
        if (!load_skin_named(*this, want) && !(s.game && s.game->theme && load_skin_named(*this, s.game->theme)))
            load_skin_named(*this, "default");
    }
}

void App::request_launch() {
    if (!s.media_ready()) {
        screen = Screen::Setup;
        s.status = "Choose your R4 disc first.";
        return;
    }
    std::string err;
    if (!s.persist_media(&err)) s.status = err.empty() ? "Could not save disc setup." : err;
    if (!mods.commit(s.primary_disc(), &err)) {
        s.status = "Mods: " + err;
        screen = Screen::Mods;
        return;
    }
    s.commit_files(&err);
    s.outcome = Outcome::Launch;
}

void App::request_quit() {
    std::string err;
    s.commit_files(&err);
    s.outcome = Outcome::Quit;
}

void App::set_pad_source(const std::string& src) {
    // Primary column replaces the first source; Alt keeps it and adds a second.
    std::string& v = s.pads.for_guid("", false)->source[capture.input];
    if (!capture.alt) {
        const size_t c = v.find(',');
        v = src + (c == std::string::npos ? "" : v.substr(c));
    } else {
        const size_t c = v.find(',');
        v = (c == std::string::npos ? v : v.substr(0, c)) + (src.empty() ? "" : ", " + src);
    }
}

bool App::handle_event(const SDL_Event& e) {
    if (capture.kind == Capture::None) return false;
    auto finish = [&]() {
        s.binds_dirty = true;
        // Map All walks every PlayStation input in order.
        if (capture.map_all && (capture.kind == Capture::Key || capture.kind == Capture::PadSource) &&
            capture.input + 1 < kPsxInputCount) {
            ++capture.input;
            capture.started = time;
            return;
        }
        capture.kind = Capture::None;
        capture.map_all = false;
    };
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
            capture.kind = Capture::None;
            capture.map_all = false;
            return true;
        }
        if (capture.kind == Capture::HostKey) {
            if (capture.input >= 0 && capture.input < static_cast<int>(s.hotkeys.size())) {
                std::string v;
                if (e.key.mod & SDL_KMOD_CTRL) v += "Ctrl+";
                if (e.key.mod & SDL_KMOD_ALT) v += "Alt+";
                if (e.key.mod & SDL_KMOD_SHIFT) v += "Shift+";
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LALT || k == SDLK_RALT || k == SDLK_LSHIFT ||
                    k == SDLK_RSHIFT)
                    return true;  // wait for the key, not the modifier
                v += SDL_GetKeyName(k);
                s.hotkeys[capture.input].second = e.key.scancode == SDL_SCANCODE_BACKSPACE ? "None" : v;
            }
            finish();
            return true;
        }
        if (capture.kind == Capture::Key) {
            KeyBind& b = s.keys.player[capture.player][capture.input];
            const char* name = SDL_GetScancodeName(e.key.scancode);
            if (e.key.scancode == SDL_SCANCODE_BACKSPACE) name = "";
            (capture.alt ? b.alt : b.primary) = name ? name : "";
            finish();
        }
        return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const SDL_GamepadButton btn = static_cast<SDL_GamepadButton>(e.gbutton.button);
        if (capture.kind == Capture::PadSource) {
            const char* n = SDL_GetGamepadStringForButton(btn);
            set_pad_source(n ? n : "");
            finish();
        } else if (capture.kind == Capture::PadValue) {
            capture.combo |= 1u << btn;
        }
        return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP && capture.kind == Capture::PadValue && capture.combo) {
        const unsigned m = capture.combo;
        int value;
        if ((m & (m - 1)) == 0) {
            int b = 0;
            while (!((m >> b) & 1)) ++b;
            const bool direct = s.game && s.game->assist_direct_pad_bind_action == capture.input + 1;
            value = direct ? RECOMP_LAUNCHER_PAD_BUTTON_COMBO(m) : RECOMP_LAUNCHER_PAD_BUTTON(b);
        } else {
            value = RECOMP_LAUNCHER_PAD_BUTTON_COMBO(m);
        }
        if (s.io) s.io->assist_pad_bind[capture.input] = value;
        capture.combo = 0;
        finish();
        return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && capture.kind == Capture::PadSource &&
        std::abs(static_cast<int>(e.gaxis.value)) > 24000) {
        const char* n = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(e.gaxis.axis));
        std::string src = n ? n : "";
        const bool trigger = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                             e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        if (!trigger) src += e.gaxis.value > 0 ? "+" : "-";
        set_pad_source(src);
        finish();
        return true;
    }
    return e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION || e.type == SDL_EVENT_KEY_UP ||
           e.type == SDL_EVENT_GAMEPAD_BUTTON_UP;
}

void App::draw_capture_modal() {
    if (capture.kind != Capture::None && !ImGui::IsPopupOpen("Rebind")) ImGui::OpenPopup("Rebind");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rebind", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoMove)) {
        if (capture.kind == Capture::None) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::PushFont(theme().bold, theme().body_size * 1.2f);
            ImGui::TextUnformatted(capture.kind == Capture::Key ? "Press a key" :
                                   capture.kind == Capture::PadValue ? "Hold a button or combo, then release" :
                                   "Press a button or move a stick");
            ImGui::PopFont();
            if (capture.kind == Capture::Key || capture.kind == Capture::PadSource)
                ImGui::Text("%s", kPsxInputs[capture.input].label);
            ImGui::TextDisabled("%s", tr("Esc cancels. Backspace clears a key."));
            const float t = static_cast<float>(std::fmod(time - capture.started, 1.0));
            ImGui::ProgressBar(t, ImVec2(360, 6), "");
        }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- Home

void App::draw_home() {
    const TitleLayer& t = *title;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    Skin& sk = skin();
    const float hero_h = sk.loaded() ? sk.metric("hero_height", 300)
                                     : std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.46f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    const bool hero_art = !sk.loaded() || sk.model().metric("hero_art", 1) != 0;
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + hero_h), true);
    if (hero_art && t.draw_hero) t.draw_hero(dl, p.x, p.y, p.x + w, p.y + hero_h, time);
    if (hero_art)
        dl->AddRectFilledMultiColor(ImVec2(p.x, p.y + hero_h * 0.45f), ImVec2(p.x + w, p.y + hero_h),
                                    IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), u32(sk.color("hero_fade", theme().bg)),
                                    u32(sk.color("hero_fade", theme().bg)));
    dl->PopClipRect();
    const std::string name = sk.label("home.title", t.display_name);
    std::string tag = sk.label("home.tagline", t.tagline);
    if (s.game && s.game->platform && tag.find(s.game->platform) == std::string::npos && !sk.model().text.count("home.tagline"))
        tag = std::string(s.game->platform) + " · " + tag;
    if (sk.loaded()) {
        const float th = sk.text_size("display", name).y, lh = sk.text_size("label", tag).y;
        sk.text(dl, "display", ImVec2(p.x + 28, p.y + hero_h - th - lh - 24), name);
        sk.text(dl, "label", ImVec2(p.x + 30, p.y + hero_h - lh - 18), tag);
    } else {
        dl->AddText(theme().bold, theme().body_size * 2.3f, ImVec2(p.x + 28, p.y + hero_h - 100), u32(theme().text), name.c_str());
        dl->AddText(ImVec2(p.x + 30, p.y + hero_h - 40), u32(theme().text_dim), tag.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hero_h + 18));

    // Disc status card
    const DiscPick& d = s.discs[0];
    ImGui::BeginChild("##disc", ImVec2(w, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    if (s.game && s.game->boxart_path && skin().loaded()) {  // the build's box art
        int bw = 0, bh = 0;
        const std::string bp = s.game->boxart_path[0] == '/' ? s.game->boxart_path : join_path(s.exe_dir, s.game->boxart_path);
        if (ImTextureID t = skin().image(bp, &bw, &bh); t && bh > 0) {
            ImGui::Image(t, ImVec2(64.0f * bw / bh, 64));
            ImGui::SameLine();
        }
    }
    ImGui::BeginGroup();
    ImGui::PushFont(theme().bold, 0.0f);
    ImGui::TextUnformatted(tr("Disc"));
    ImGui::PopFont();
    ImGui::SameLine();
    if (d.path.empty()) chip("Not set up", theme().warn);
    else if (!d.verified) chip("Unverified", theme().text_dim);
    else if (d.verify.verdict == 1) chip("Verified", theme().ok);
    else if (d.verify.verdict == 2) chip("Plays, with warnings", theme().warn);
    else if (d.verify.verdict == 3) chip("Bad dump", theme().bad);
    else chip("Unknown", theme().text_dim);
    if (d.verified && d.verify.serial[0]) {
        ImGui::SameLine();
        chip(d.verify.serial, theme().accent2);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
    ImGui::TextUnformatted(d.path.empty() ? "Choose your R4 disc image (.cue/.bin/.chd) to play." : d.path.c_str());
    ImGui::PopStyleColor();
    if (S("disc.setup") && ImGui::Button(tr("Change disc"))) screen = Screen::Setup;
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 8));

    // Quick tiles
    const float tw = (w - 24) / 3.0f;
    auto tile = [&](const char* id, const char* head, const std::string& value, Screen go) {
        ImGui::PushID(id);
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        const float k = ImGui::GetFontSize() / 17.0f;
        if (ImGui::Button("##tile", ImVec2(tw, 96 * k))) screen = go;
        ImDrawList* l = ImGui::GetWindowDrawList();
        l->AddText(theme().body, ImGui::GetFontSize() * 0.8f, ImVec2(tp.x + 18 * k, tp.y + 16 * k),
                   u32(theme().text_dim), head);
        l->AddText(theme().bold, ImGui::GetFontSize() * 1.25f, ImVec2(tp.x + 18 * k, tp.y + 44 * k),
                   u32(theme().text), value.c_str());
        ImGui::PopID();
    };
    std::string q = "Off";
    if (s.io && s.quality.offered()) q = quality_name(s.io->quality_preset);
    else if (s.io && s.game && s.game->quality_offered_mask) q = quality_name(s.io->quality_preset);
    tile("q", "GRAPHICS PRESET", q, Screen::Graphics);
    ImGui::SameLine();
    std::string scheme = "Classic";
    if (ModFeature* f = mods.find(t.modern_controls.package_id, t.modern_controls.feature_id)) {
        scheme = f->info.enabled ? "Classic" : "Classic";
        for (auto& o : f->options)
            if (t.modern_option && !std::strcmp(o.info.id, t.modern_option) && f->info.enabled)
                scheme = (t.modern_value && !std::strcmp(o.info.value, t.modern_value)) ? "Modern" : "Classic";
    }
    tile("c", "CONTROLS", scheme, Screen::Controls);
    ImGui::SameLine();
    int on = 0;
    for (auto& f : mods.features) on += f.info.enabled ? 1 : 0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d of %d on", on, static_cast<int>(mods.features.size()));
    tile("m", "MODS", mods.available() ? buf : "Unavailable", Screen::Mods);
}

// ---------------------------------------------------------------- Disc setup

namespace {
struct DialogCtx {
    App* app;
    int slot;  // >=0 disc slot, -1 BIOS, -2 SBI
};
void SDLCALL on_file_chosen(void* user, const char* const* files, int) {
    DialogCtx* c = static_cast<DialogCtx*>(user);
    if (files && files[0]) {
        if (c->slot >= 0) c->app->s.set_disc(c->slot, files[0]);
        else if (c->slot == -1) c->app->s.set_bios(files[0]);
        else if (c->slot == -2 && c->app->s.game && c->app->s.game->import_sbi) {
            char out[1024] = {0}, err[256] = {0};
            const std::string disc = c->app->s.primary_disc();
            if (c->app->s.game->import_sbi(disc.c_str(), files[0], out, sizeof(out), err, sizeof(err)))
                c->app->s.set_disc(0, out);
            else
                c->app->s.status = err[0] ? err : "SBI import failed";
        }
    }
    delete c;
}
void SDLCALL job_progress(void* ctx, float pct, const char* msg) {
    Job* j = static_cast<Job*>(ctx);
    std::lock_guard<std::mutex> lk(j->mu);
    j->pct = pct;
    j->message = msg ? msg : "";
}
}  // namespace

void App::draw_setup() {
    const RecompLauncherCGameInfo* g = s.game;
    screen_title("Disc setup");
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
    ImGui::TextWrapped("Point the game at your own copy of %s (%s). Nothing is downloaded; the disc stays "
                       "where it is.", title->display_name, title->serial);
    ImGui::PopStyleColor();

    // In-game, multi-disc: swap the disc in the drive through the runtime's
    // tray policy (the same one the runtime's own disc menu and hotkeys use).
    if (mode == Mode::Overlay && host && host->abi_version >= 2 && host->disc_count &&
        host->disc_count(host->ctx) > 1) {
        section(tr("Disc in the drive"));
        const int n = host->disc_count(host->ctx);
        const int mounted = host->disc_mounted ? host->disc_mounted(host->ctx) : 0;
        for (int d = 1; d <= n; ++d) {
            ImGui::PushID(d);
            char label[64];
            std::snprintf(label, sizeof label, "%s %d%s", tr("Disc"), d, d == mounted ? tr(" (in the drive)") : "");
            if (big_button(label, ImVec2(260, 44), d == mounted) && host->disc_insert) {
                const int rc = host->disc_insert(host->ctx, d);
                s.status = rc == 0 ? std::string(tr("Disc changed.")) : std::string(tr("The disc could not be changed now."));
                if (rc == 0) overlay_open = false;  // back to the game for the lid cycle
            }
            char mark[32];
            std::snprintf(mark, sizeof mark, "disc:%d", d);
            script_mark(mark);
            ImGui::PopID();
        }
    }

    section("1  Disc image");
    for (size_t i = 0; i < s.discs.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        DiscPick& d = s.discs[i];
        static char buf[1024];
        std::snprintf(buf, sizeof(buf), "%s", d.path.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
        if (ImGui::InputTextWithHint("##path", "/path/to/R4 - Ridge Racer Type 4 (USA).cue", buf, sizeof(buf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            s.set_disc(static_cast<int>(i), buf);
        script_mark(i == 0 ? "field:disc_path" : "field:disc_path_more");
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
            static std::string pat, desc;
            pat = patterns_of(g ? g->rom_patterns : nullptr, g ? g->num_rom_patterns : 0, "cue;bin;chd;iso");
            desc = (g && g->rom_filter_desc) ? g->rom_filter_desc : std::string("PlayStation ") + (g && g->rom_noun ? g->rom_noun : "disc");
            static SDL_DialogFileFilter filters[1];
            filters[0] = SDL_DialogFileFilter{desc.c_str(), pat.c_str()};
            show_open_file(on_file_chosen, new DialogCtx{this, static_cast<int>(i)},
                                   SDL_GL_GetCurrentWindow(), filters, 1, nullptr, false);
        }
        if (!d.path.empty()) {
            if (!d.verified) chip("Could not read this disc", theme().warn);
            else {
                const char* v[] = {"Unknown", "Verified dump", "Plays, with warnings", "Bad dump"};
                const ImVec4 col[] = {theme().text_dim, theme().ok, theme().warn, theme().bad};
                const int vi = (d.verify.verdict >= 0 && d.verify.verdict <= 3) ? d.verify.verdict : 0;
                chip(v[vi], col[vi]);
                ImGui::SameLine();
                ImGui::TextDisabled("%s  %s  %d track(s)", d.verify.serial, d.verify.region, d.verify.track_count);
                if (d.verify.netplay_detail[0]) ImGui::TextDisabled("%s", d.verify.netplay_detail);
                if (d.verify.sbi_status == RECOMP_SBI_MISSING && g && g->import_sbi) {
                    ImGui::TextColored(theme().warn, "This disc needs its SBI subchannel file.");
                    ImGui::SameLine();
                    if (ImGui::Button("Import .sbi...")) {
                        static const SDL_DialogFileFilter f[] = {{"SBI file", "sbi"}};
                        show_open_file(on_file_chosen, new DialogCtx{this, -2},
                                               SDL_GL_GetCurrentWindow(), f, 1, nullptr, false);
                    }
                }
            }
        }
        ImGui::PopID();
    }

    if (g && g->has_bios) {
        section("2  BIOS (optional)");
        ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
        ImGui::TextWrapped("The bundled OpenBIOS is used when none is set. A Sony BIOS is only needed for "
                           "exact boot behaviour.");
        ImGui::PopStyleColor();
        static char bbuf[512];
        std::snprintf(bbuf, sizeof(bbuf), "%s", s.bios_path.c_str());
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
        if (ImGui::InputTextWithHint("##bios", "OpenBIOS (bundled)", bbuf, sizeof(bbuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
            s.set_bios(bbuf);
        ImGui::SameLine();
        if (ImGui::Button("Browse...##b", ImVec2(-1, 0))) {
            static std::string bpat, bdesc;
            bpat = patterns_of(g->bios_patterns, g->num_bios_patterns, "bin;rom");
            bdesc = g->bios_filter_desc ? g->bios_filter_desc : (g->bios_name ? g->bios_name : "BIOS image");
            static SDL_DialogFileFilter f[1];
            f[0] = SDL_DialogFileFilter{bdesc.c_str(), bpat.c_str()};
            show_open_file(on_file_chosen, new DialogCtx{this, -1}, SDL_GL_GetCurrentWindow(), f, 1,
                                   nullptr, false);
        }
        if (!s.bios_path.empty())
            chip(s.bios_verify.detail[0] ? s.bios_verify.detail : (s.bios_verify.ok ? "BIOS ok" : "Unrecognised BIOS"),
                 s.bios_verify.ok ? theme().ok : theme().warn);
    }

    if (S("disc.toolchain") && g && g->setup_needs_toolchain && g->toolchain_is_ready && !g->toolchain_is_ready()) {
        section(tr("Build tools"));
        ImGui::TextWrapped("%s", tr("Building the game from your disc needs a small set of tools. They are downloaded once."));
        if (g->toolchain_repair_note) {
            const char* note = g->toolchain_repair_note();
            if (note && *note) ImGui::TextWrapped("%s", note);
        }
        ImGui::BeginDisabled(job.running);
        if (g->ensure_toolchain_with_progress && big_button(tr("Download build tools"), ImVec2(260, 44), false)) start_job(2);
        ImGui::EndDisabled();
    }
    if (S("disc.multi") && g && g->num_discs > 1 && g->discs && s.io) {
        section(tr("Disc selection"));
        std::vector<std::string> labels;
        for (int i = 0; i < g->num_discs; ++i) {
            const RecompLauncherCDisc& d = g->discs[i];
            labels.push_back(d.label && *d.label ? d.label : std::string(tr("Disc ")) + std::to_string(d.number ? d.number : i + 1));
        }
        std::vector<const char*> c;
        for (auto& l : labels) c.push_back(l.c_str());
        int cur = s.io->disc_index > 0 ? s.io->disc_index - 1 : 0;
        if (row_combo(tr("Boot from"), &cur, c.data(), static_cast<int>(c.size()))) s.io->disc_index = cur + 1;
    }
    const bool can_prepare = S("disc.prepare") && g && (g->prepare_with_progress || g->prepare_disc) &&
                             (g->setup_wizard_supported || g->prepare_required_before_continue);
    if (can_prepare) {
        section(g->prepare_section_title ? g->prepare_section_title : "3  Build the game");
        ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
        ImGui::TextWrapped("%s", g->prepare_disc_note ? g->prepare_disc_note
                                                      : "Generates the recompiled game from your disc, then rebuilds.");
        ImGui::PopStyleColor();
        if (job.running) {
            std::lock_guard<std::mutex> lk(job.mu);
            if (g->prepare_busy_status) ImGui::TextDisabled("%s", g->prepare_busy_status);
            ImGui::ProgressBar(job.pct / 100.0f, ImVec2(-1, 0), job.message.c_str());
        } else {
            if (job.done) {
                job.join();
                job.done = false;
                if (job.rc && !job.out_path.empty()) {
                    s.relaunch_exe = job.out_path;
                    s.outcome = Outcome::Relaunch;
                } else if (!job.rc) {
                    s.status = job.error.empty() ? "Prepare failed." : job.error;
                } else {
                    s.status = g->rebuild_after_prepare ? (g->rebuild_success_status ? g->rebuild_success_status : tr("Build complete."))
                                                        : (g->prepare_success_status ? g->prepare_success_status : tr("Ready."));
                }
            }
            ImGui::BeginDisabled(!s.media_ready());
            if (big_button(g->prepare_disc_label ? g->prepare_disc_label : "Generate and build", ImVec2(260, 48), false)) {
                job.running = true;
                job.done = false;
                job.worker = std::thread([this, g]() {
                    char out[1024] = {0}, err[512] = {0}, exe[1024] = {0};
                    int ok = 1;
                    // prepare_use_selected_rom: the wizard's chosen disc is the source.
                    const std::string src = g->prepare_use_selected_rom ? s.primary_disc() : std::string();
                    if (g->prepare_with_progress)
                        ok = g->prepare_with_progress(src.c_str(), out, sizeof(out), err, sizeof(err), job_progress, &job);
                    else if (g->prepare_disc)
                        ok = g->prepare_disc(src.c_str(), out, sizeof(out), err, sizeof(err));
                    if (ok && g->rebuild_after_prepare && g->rebuild_with_progress)
                        ok = g->rebuild_with_progress(src.c_str(), exe, sizeof(exe), err, sizeof(err), job_progress, &job);
                    {
                        std::lock_guard<std::mutex> lk(job.mu);
                        job.error = err;
                        job.out_path = (ok && g->relaunch_after_rebuild) ? exe : "";
                    }
                    job.rc = ok;
                    job.running = false;
                    job.done = true;
                });
            }
            ImGui::EndDisabled();
        }
    }

    ImGui::Dummy(ImVec2(0, 16));
    const bool blocked = g && g->prepare_required_before_continue && can_prepare;
    // Say why Continue is off.
    if (!s.media_ready()) ImGui::TextColored(theme().warn, "%s", s.primary_disc().empty() ? tr("Choose your disc to continue.")
                                                                                          : tr("This disc image cannot be used."));
    else if (blocked) ImGui::TextColored(theme().warn, "%s", tr("Build the game from your disc first."));
    ImGui::BeginDisabled(!s.media_ready() || blocked || job.running);
    if (big_button("Continue", ImVec2(220, 52), true)) {
        std::string err;
        if (s.persist_media(&err)) {
            s.status = "Disc saved.";
            screen = Screen::Home;
        } else {
            s.status = err.empty() ? "Could not save the disc setup." : err;
        }
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------- About

void App::draw_about() {
    const TitleLayer& t = *title;
    screen_title("About");
    ImGui::PushStyleColor(ImGuiCol_Text, theme().text_dim);
    ImGui::TextWrapped("%s", t.about);
    ImGui::PopStyleColor();
    section("Credits and notices");
    // Cards are drawn into this (scrolling) body, not as child windows: a
    // wheel over a child window is the child's first and only reaches the
    // body when ImGui decides to forward it, which it did not do reliably
    // over these auto-sized cards. Plain groups have no window of their own,
    // so every wheel tick lands on the body.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pad = ImGui::GetStyle().WindowPadding;
    const float card_w = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < t.notice_count; ++i) {
        const TitleNotice& n = t.notices[i];
        ImGui::PushID(i);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        ImGui::SetCursorScreenPos(ImVec2(top.x + pad.x, top.y + pad.y));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(top.x + card_w - pad.x);
        ImGui::PushFont(theme().bold, 0.0f);
        ImGui::TextUnformatted(n.heading);
        ImGui::PopFont();
        ImGui::TextWrapped("%s", n.body);
        if (n.url) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme().accent2);
            if (ImGui::Selectable(n.url, false, 0, ImGui::CalcTextSize(n.url))) SDL_OpenURL(n.url);
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const ImVec2 bottom(top.x + card_w, ImGui::GetItemRectMax().y + pad.y);
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled(top, bottom, u32(theme().surface), theme().radius);
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(top.x, bottom.y));
        ImGui::Dummy(ImVec2(card_w, 0));
        ImGui::PopID();
    }
    if (s.game && s.game->credits_text) {
        section("Game credits");
        ImGui::TextWrapped("%s", s.game->credits_text);
    }
    if (surf.shown("about.version")) {
        section(tr("Version"));
        ImGui::TextDisabled("recomp-launcher %s · Dear ImGui %s · SDL %d.%d.%d", R4L_VERSION, IMGUI_VERSION,
                            SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                            SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
        if (s.game && s.game->name) ImGui::TextDisabled("%s %s", s.game->name, s.game->region ? s.game->region : "");
    }
    const RecompLauncherCGameInfo* g = s.game;
    if (surf.shown("about.updates") && g && g->toolchain_update_available) {
        section(tr("Updates"));
        char local[64] = {0}, remote[64] = {0};
        if (g->toolchain_update_available(local, sizeof local, remote, sizeof remote)) {
            ImGui::Text("%s %s -> %s", tr("Build tools update available:"), local, remote);
            if (g->ensure_toolchain_with_progress && ImGui::Button(tr("Update build tools"))) start_job(2);
        } else {
            ImGui::TextDisabled("%s %s", tr("Build tools are current"), local);
        }
        if (g->toolchain_repair_note) {
            const char* note = g->toolchain_repair_note();
            if (note && *note) ImGui::TextWrapped("%s", note);
        }
    }
    if (surf.shown("about.logs")) {
        section(tr("Logs"));
        if (ImGui::Button(tr("Open the game folder"))) SDL_OpenURL(("file://" + s.exe_dir).c_str());
    }
}

}  // namespace r4l
