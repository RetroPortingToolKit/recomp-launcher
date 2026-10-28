// layout.cpp — the skinned frame: background, rail, content, footer, and the
// overlay variant. Everything positional comes from the active skin's layout
// rects, with built-in fallbacks when a skin leaves one out.
#include "recomp_launcher_overlay.h"
#include "skin.h"
#include "ui.h"
#include "script.h"
#include "imgui_internal.h"  // NavMoveRequestCancel

#include <SDL3/SDL.h>

#include <cmath>
#include <cstring>

namespace r4l {

namespace {
ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }


const char* skin_key(Screen s) {
    switch (s) {
    case Screen::Home: return "Home";
    case Screen::Graphics: return "Graphics";
    case Screen::Mods: return "Mods";
    case Screen::Controls: return "Controls";
    case Screen::Netplay: return "Netplay";
    case Screen::Setup: return "Setup";
    case Screen::About: return "About";
    case Screen::Resume: return "Resume";
    case Screen::QuitGame: return "Quit";
    case Screen::System: return "System";
    case Screen::Main: return "Main";
    case Screen::Options: return "Options";
    case Screen::Back: return "Back";
    case Screen::SaveState: return "SaveState";
    case Screen::LoadState: return "LoadState";
    case Screen::Steam: return "Steam";
    default: return "";
    }
}

ImVec2 iv(float x, float y) { return ImVec2(x, y); }
}  // namespace

void apply_skin_theme() {
    Skin& sk = skin();
    if (!sk.loaded()) return;
    Theme& th = const_cast<Theme&>(theme());
    th.bg = sk.color("bg", th.bg);
    th.surface = sk.color("surface", th.surface);
    th.surface_hi = sk.color("surface_hi", th.surface_hi);
    th.line = sk.color("line", th.line);
    th.text = sk.color("text", th.text);
    th.text_dim = sk.color("text_dim", th.text_dim);
    th.accent = sk.color("accent", th.accent);
    th.accent2 = sk.color("accent2", th.accent2);
    th.ok = sk.color("ok", th.ok);
    th.warn = sk.color("warn", th.warn);
    th.bad = sk.color("bad", th.bad);
    th.radius = sk.model().metric("radius", 10);
    if (ImFont* f = sk.imfont("body")) th.body = f;
    if (ImFont* f = sk.imfont("heading")) th.bold = f;
    if (th.body) ImGui::GetIO().FontDefault = th.body;
    th.body_size = sk.model().fonts.count("body") ? sk.model().fonts.at("body").size : th.body_size;
    ImGui::GetStyle().FontSizeBase = th.body_size;
    ImGuiStyle& st = ImGui::GetStyle();
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = th.text;
    c[ImGuiCol_TextDisabled] = th.text_dim;
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ChildBg] = th.surface;
    c[ImGuiCol_PopupBg] = sk.color("popup", th.surface_hi);
    c[ImGuiCol_FrameBg] = sk.color("control", th.surface_hi);
    c[ImGuiCol_FrameBgHovered] = sk.color("control_hover", c[ImGuiCol_FrameBgHovered]);
    c[ImGuiCol_Button] = sk.color("control", th.surface_hi);
    c[ImGuiCol_ButtonHovered] = sk.color("control_hover", c[ImGuiCol_ButtonHovered]);
    c[ImGuiCol_ButtonActive] = th.accent;
    c[ImGuiCol_Header] = sk.color("control", c[ImGuiCol_Header]);
    c[ImGuiCol_HeaderHovered] = sk.color("control_hover", c[ImGuiCol_HeaderHovered]);
    c[ImGuiCol_CheckMark] = th.accent;
    c[ImGuiCol_SliderGrab] = th.accent;
    c[ImGuiCol_SliderGrabActive] = th.accent;
    c[ImGuiCol_NavCursor] = sk.color("focus", th.accent);
    c[ImGuiCol_TableRowBgAlt] = sk.color("row_alt", ImVec4(1, 1, 1, 0.03f));
    st.FrameRounding = sk.model().metric("control_radius", 8);
    st.ChildRounding = th.radius;
    st.GrabRounding = st.FrameRounding;
}

void App::frame() {
    time = ImGui::GetTime();
    s.tick();
    if (job.done && !job.running && screen != Screen::Setup) {  // jobs started outside the wizard
        job.join();
        job.done = false;
        std::lock_guard<std::mutex> lk(job.mu);
        if (job.rc && !job.out_path.empty()) {
            s.relaunch_exe = job.out_path;
            s.outcome = Outcome::Relaunch;
        } else {
            const RecompLauncherCGameInfo* g = s.game;
            const char* ok = !g ? nullptr : job_kind == 3 ? g->pgo_success_status : job_kind == 4 ? g->fmv_timing_success_status
                           : job_kind == 5 ? g->bios_prepare_success_status : nullptr;
            s.status = job.rc ? (ok ? ok : tr("Done.")) : (job.error.empty() ? tr("That did not work.") : job.error);
        }
    }
    {   // Disc-sourced skin art: (re)extract when the chosen disc changes.
        static std::string last_disc;
        if (last_disc != s.primary_disc()) {
            last_disc = s.primary_disc();
            assets_checked = false;
        }
        if (!assets_checked) ensure_disc_assets();
    }
    Skin& sk = skin();
    std::string serr;
    if (sk.maybe_reload(time, &serr)) {
        s.status = serr.empty() ? "Skin reloaded: " + sk.model().name : "Skin error (kept previous): " + serr;
        apply_theme(*title, 1.0f);
        apply_skin_theme();
    }
    const bool overlay = mode == Mode::Overlay;
    if (!overlay && s.game && s.game->netplay && s.game->netplay->pump) s.game->netplay->pump(s.game->netplay->ctx);
    if (overlay) overlay_tick();

    // launcher.main_screen: the in-game menu opens on a main screen (Start or
    // Resume Game, save states, Options, Quit); Options shows the pages
    // behind a Back entry. A first run with no disc offers Select Disc only.
    main_screen = surf.automatic("launcher.main_screen");
    const bool main_rail = main_screen && overlay && !options_open;
    if (main_screen && overlay && !options_open && screen != Screen::Main && screen != Screen::SaveState &&
        screen != Screen::LoadState && screen != Screen::Steam && !(setup_only && screen == Screen::Setup))
        options_open = true;  // a page was asked for directly (script, deep link)
    if (main_rail && !states_allowed() && (screen == Screen::SaveState || screen == Screen::LoadState))
        screen = Screen::Main;
    // The rail lists only pages the title's surface manifest leaves something on.
    std::vector<Screen> navv;
    if (main_rail) {
        // The main screen holds only these; every page lives under Options.
        if (setup_only) {
            navv.push_back(Screen::Setup);  // shown as Select Disc
        } else {
            navv.push_back(Screen::Resume);
            if (states_allowed()) {
                if (game_started) navv.push_back(Screen::SaveState);
                navv.push_back(Screen::LoadState);
            }
            navv.push_back(Screen::Options);
            if (steam_offered()) navv.push_back(Screen::Steam);
        }
        navv.push_back(Screen::QuitGame);
    } else {
        if (main_screen && overlay) navv.push_back(Screen::Back);
        else if (overlay) navv.push_back(Screen::Resume);
        else if (setup_only) navv.push_back(Screen::Setup);  // in-game first run: setup, then play
        else navv.push_back(Screen::Home);
        if (setup_only) {
            // The game starts as soon as the disc checks out.
            if (s.media_ready() && !job.running) request_launch();
        } else {
            if (surf.any_shown("graphics.")) navv.push_back(Screen::Graphics);
            if (surf.any_shown("controls.")) navv.push_back(Screen::Controls);
            if (surf.any_shown("mods.") && mods.available()) navv.push_back(Screen::Mods);
            if ((!overlay || in_game) && surf.any_shown("netplay.") && s.game && s.game->netplay_supported && s.game->netplay)
                navv.push_back(Screen::Netplay);
            if (surf.any_shown("audio.") || surf.any_shown("system.") || S("bios.select")) navv.push_back(Screen::System);
        }
        if (overlay && in_game && !netplay_locked && S("disc.setup")) navv.push_back(Screen::Setup);
        if (setup_only && main_screen) navv.resize(1);  // Select Disc is the only entry
        else if (surf.any_shown("about.")) navv.push_back(Screen::About);
        if (!main_screen && !setup_only && steam_offered()) navv.push_back(Screen::Steam);
        if (overlay && !main_screen) navv.push_back(Screen::QuitGame);
    }
    const Screen* nav = navv.data();
    const int nav_n = static_cast<int>(navv.size());
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float W = vp->WorkSize.x, H = vp->WorkSize.y;
    sk.begin_frame(W, H, std::string(overlay ? "Overlay." : "") + skin_key(screen), time);
    if (screen != last_focus_screen) {  // per-screen palette overrides feed ImGui colours
        last_focus_screen = screen;
        apply_theme(*title, ui_scale > 0 ? ui_scale : 1.0f);
        apply_skin_theme();
    }
    const float u = sk.loaded() ? sk.s() : H / 720.0f;
    // Resolution independence for ordinary widgets: rescale the style when
    // the window height changes (the skin's own elements scale themselves).
    if (std::fabs(u - ui_scale) > 0.01f) {
        ui_scale = u;
        apply_theme(*title, u);
        apply_skin_theme();
    }
    ImGui::GetStyle().FontScaleMain = u;

    // Paging and global buttons.
    if (capture.kind == Capture::None && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
        int idx = 0;
        for (int i = 0; i < nav_n; ++i)
            if (nav[i] == screen) idx = i;
        auto action = [](Screen sc) {  // rail entries that act instead of showing a page
            return sc == Screen::Resume || sc == Screen::QuitGame || sc == Screen::Options || sc == Screen::Back;
        };
        int pages = 0;
        for (int i = 0; i < nav_n; ++i) pages += action(nav[i]) ? 0 : 1;
        auto go = [&](int d) {
            if (!pages) return;
            do idx = (idx + nav_n + d) % nav_n; while (action(nav[idx]));
            screen = nav[idx];
            focus_request = screen;
            sk.play("move");
        };
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) go(-1);
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) go(+1);
        if (!overlay && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) && screen != Screen::Setup) {
            sk.play("confirm");
            request_launch();
        }
        if (overlay && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                        (pad_back_closes && ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))) &&
            !ImGui::IsAnyItemActive()) {
            sk.play("back");
            if (main_screen && options_open) open_options(false);  // Options: back to the main screen
            else overlay_open = false;
        }
    }

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if (overlay) {
        bg->AddRectFilled(iv(0, 0), iv(W, H), u32(sk.color("overlay_dim", ImVec4(0, 0, 0, 0.62f))));
    } else if (sk.loaded()) {
        sk.draw_background(bg);
    } else {
        bg->AddRectFilled(iv(0, 0), iv(W, H), u32(theme().bg));
    }

    // Regions (skin layout, fallback = built-in proportions).
    const std::string pre = overlay ? "overlay." : "";
    const Rect full{0, 0, W, H};
    const Rect panel = overlay ? sk.rect("overlay.panel", Rect{W * 0.08f, H * 0.08f, W * 0.84f, H * 0.84f}) : full;
    if (overlay)
        ImGui::GetBackgroundDrawList()->AddRectFilled(iv(panel.x, panel.y), iv(panel.x + panel.w, panel.y + panel.h),
                                                      u32(sk.color("overlay_panel", ImVec4(0.05f, 0.05f, 0.09f, 0.94f))),
                                                      theme().radius * u);
    const float footer_h = 46 * u;
    const Rect footer = sk.rect(pre + "footer", Rect{panel.x, panel.y + panel.h - footer_h, panel.w, footer_h}, &panel);
    const float rail_w = std::max(200.0f * u, panel.w * 0.19f);
    const Rect rail = sk.rect(pre + "rail", Rect{panel.x, panel.y, rail_w, panel.h - footer_h}, &panel);
    const Rect content = sk.rect(pre + "content", Rect{rail.x + rail.w, panel.y, panel.x + panel.w - (rail.x + rail.w), panel.h - footer_h}, &panel);
    if (sk.loaded() && !overlay) sk.draw_sprites(ImGui::GetBackgroundDrawList());

    // ---------------- Rail
    ImGui::SetCursorScreenPos(iv(rail.x, rail.y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sk.color("rail", theme().surface));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * u, 24 * u));
    ImGui::BeginChild("##rail", ImVec2(rail.w, rail.h), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        const Rect brand = sk.rect(pre + "brand", Rect{rail.x + 16 * u, rail.y + 20 * u, rail.w - 32 * u, 60 * u});
        if (sk.loaded() && brand.h > 0) {
            sk.text(dl, "logo", iv(brand.x, brand.y), sk.label("brand.title", "R4"));
            const ImVec2 ls = sk.text_size("logo", sk.label("brand.title", "R4"));
            sk.text(dl, "label", iv(brand.x, brand.y + ls.y), sk.label("brand.subtitle", overlay ? "PAUSED" : "RIDGE RACER TYPE 4"));
        }
        ImGui::SetCursorScreenPos(iv(rail.x + 16 * u, brand.y + brand.h + 16 * u));
    }
    const float item_h = sk.loaded() ? sk.metric("nav_item_height", 46) : 46 * u;
    const float item_gap = sk.loaded() ? sk.metric("nav_item_gap", 4) : 4 * u;
    const bool setup_mode = !overlay && s.needs_setup();
    for (int i = 0; i < nav_n; ++i) {
        const Screen sc = nav[i];
        if (sc == Screen::Netplay && netplay_locked) continue;
        const bool sel = screen == sc || (sc == Screen::Home && screen == Screen::Setup);
        ImGui::PushID(i);
        const ImVec2 rp = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (focus_request == sc) {
            ImGui::SetKeyboardFocusHere();
            focus_request = Screen::Count;
        }
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        if (ImGui::Selectable("##nav", sel, 0, ImVec2(w, item_h))) {
            sk.play("confirm");
            if (sc == Screen::Resume) overlay_open = false;
            else if (sc == Screen::QuitGame) quit_confirm = true;
            else if (sc == Screen::Options) open_options(true);
            else if (sc == Screen::Back) open_options(false);
            else
                screen = (sc == Screen::Home && setup_mode) ? Screen::Setup : sc;
        }
        ImGui::PopStyleColor(3);
        // Right from any rail item enters the page. ImGui's own scoring only
        // finds a candidate that overlaps the rail item vertically, so from a
        // lower item Right did nothing; hand the focus to the page's first
        // control instead (SetKeyboardFocusHere after the content child opens).
        if (ImGui::IsItemFocused() && sc != Screen::Resume && sc != Screen::QuitGame && sc != Screen::Options &&
            sc != Screen::Back &&
            (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
             ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, false))) {
            ImGui::NavMoveRequestCancel();
            if (!sel) screen = (sc == Screen::Home && setup_mode) ? Screen::Setup : sc;
            content_focus = true;
        }
        const bool hov = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
        std::string base = tr((sc == Screen::Home && setup_mode) ? "Disc setup" : screen_name(sc));
        if (main_screen && sc == Screen::Resume) base = tr(game_started ? "Resume Game" : "Start Game");
        if (main_screen && setup_only && sc == Screen::Setup) base = tr("Select Disc");
        script_mark(("nav:" + base).c_str());
        if (hov && last_nav_hover != i) {
            if (last_nav_hover >= 0) sk.play("move");
            last_nav_hover = i;
        }
        const ImVec2 a = rp, b(rp.x + w, rp.y + item_h);
        if (sk.loaded() && sk.model().palette.count("nav_item"))
            dl->AddRectFilled(a, b, u32(sk.color("nav_item", ImVec4(0, 0, 0, 0))), sk.model().metric("nav_item_radius", 0) * u);
        if (sk.loaded() && sk.model().palette.count("nav_item_border"))
            dl->AddRect(a, b, u32(sk.color("nav_item_border", ImVec4(0, 0, 0, 0))), sk.model().metric("nav_item_radius", 0) * u,
                        0, std::max(1.0f, sk.model().metric("nav_item_border_px", 1) * u));
        if (sel || hov) {
            if (sk.loaded()) sk.draw_highlight(dl, a, b);
            else dl->AddRectFilled(a, b, u32(theme().surface_hi), 8);
        }
        if (sel && sk.loaded()) sk.draw_cursor(dl, a, b);
        std::string label = base;
        if (main_screen && sc == Screen::Resume) label = sk.label(game_started ? "nav.ResumeGame" : "nav.StartGame", label);
        else if (main_screen && setup_only && sc == Screen::Setup) label = sk.label("nav.SelectDisc", label);
        else label = sk.label(std::string("nav.") + skin_key(sc), label);
        const ImVec4 col = (sel || hov) ? sk.color("nav_selected", theme().text) : sk.color("nav", theme().text_dim);
        if (sk.loaded()) {
            const ImVec2 ts = sk.text_size("nav", label);
            sk.text(dl, "nav", iv(a.x + 18 * u, a.y + (item_h - ts.y) * 0.5f), label, 0, &col);
        } else {
            const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText(iv(a.x + 18, a.y + (item_h - ts.y) * 0.5f), u32(col), label.c_str());
        }
        if (sc == Screen::Netplay && s.game && s.game->netplay && s.game->netplay->in_lobby &&
            s.game->netplay->in_lobby(s.game->netplay->ctx))
            dl->AddCircleFilled(iv(b.x - 16 * u, a.y + item_h * 0.5f), 4 * u, u32(theme().ok));
        ImGui::SetCursorScreenPos(iv(rp.x, rp.y + item_h + item_gap));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (!overlay && !(main_screen && setup_only)) {
        const Rect pb = sk.rect("play_button", Rect{rail.x + 16 * u, rail.y + rail.h - 84 * u, rail.w - 32 * u, 56 * u});
        ImGui::SetCursorScreenPos(iv(pb.x, pb.y));
        const bool ready = s.media_ready();
        const bool resume = s.game && s.game->in_session;  // reopened from a running game
        const std::string lbl = resume ? sk.label("play.resume", tr("RESUME"))
                                       : sk.label(ready ? "play" : "play.setup", ready ? tr("PLAY") : tr("SET UP DISC"));
        if (ImGui::InvisibleButton("##play", ImVec2(pb.w, pb.h))) {
            sk.play("confirm");
            if (ready) request_launch();
            else screen = Screen::Setup;
        }
        const bool hov = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        const ImVec4 pc = sk.color(hov ? "play_hover" : "play", theme().accent);
        if (sk.model().metric("play_slant", 0) > 0) {
            const float sl = sk.model().metric("play_slant", 0) * u;
            const ImVec2 q[4] = {iv(pb.x + sl, pb.y), iv(pb.x + pb.w, pb.y), iv(pb.x + pb.w - sl, pb.y + pb.h), iv(pb.x, pb.y + pb.h)};
            dl->AddConvexPolyFilled(q, 4, u32(pc));
        } else {
            dl->AddRectFilled(iv(pb.x, pb.y), iv(pb.x + pb.w, pb.y + pb.h), u32(pc), theme().radius * u);
        }
        if (hov) dl->AddRect(iv(pb.x - 2, pb.y - 2), iv(pb.x + pb.w + 2, pb.y + pb.h + 2), u32(sk.color("focus", theme().text)), theme().radius * u, 0, 2.0f);
        const ImVec4 tc = sk.color("play_text", ImVec4(1, 1, 1, 1));
        if (sk.loaded()) {
            const ImVec2 ts = sk.text_size("button", lbl);
            sk.text(dl, "button", iv(pb.x + pb.w * 0.5f, pb.y + (pb.h - ts.y) * 0.5f), lbl, 0.5f, &tc);
        } else {
            const ImVec2 ts = ImGui::CalcTextSize(lbl.c_str());
            dl->AddText(iv(pb.x + (pb.w - ts.x) * 0.5f, pb.y + (pb.h - ts.y) * 0.5f), u32(tc), lbl.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ---------------- Content (with the skin's screen transition)
    const ImVec2 off = sk.loaded() ? sk.transition_offset() : ImVec2(0, 0);
    ImGui::SetCursorScreenPos(iv(content.x + off.x, content.y + off.y));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sk.loaded() ? sk.transition_alpha() : 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sk.color("content", ImVec4(0, 0, 0, 0)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36 * u, 28 * u));
    ImGui::BeginChild("##content", ImVec2(content.w, content.h), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    if (content_focus) {  // Right from the rail: the page's first control
        ImGui::SetKeyboardFocusHere();
        content_focus = false;
    }
    switch (screen) {
    case Screen::Home: draw_home(); break;
    case Screen::Graphics: draw_graphics(); break;
    case Screen::Mods: draw_mods(); break;
    case Screen::Controls: draw_controls(); break;
    case Screen::Netplay: draw_netplay(); break;
    case Screen::Setup: draw_setup(); break;
    case Screen::About: draw_about(); break;
    case Screen::System: draw_system(); break;
    case Screen::Main: draw_main(); break;
    case Screen::SaveState: draw_states(true); break;
    case Screen::LoadState: draw_states(false); break;
    case Screen::Steam: draw_steam_section(); break;
    default: break;
    }
    screen_body_end();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // ---------------- Footer
    ImGui::SetCursorScreenPos(iv(footer.x, footer.y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sk.color("footer", theme().surface));
    ImGui::BeginChild("##footer", ImVec2(footer.w, footer.h), 0, ImGuiWindowFlags_NoNav);
    ImDrawList* fl = ImGui::GetWindowDrawList();
    float fx = footer.x + 20 * u;
    const float fy = footer.y + (footer.h - (sk.loaded() ? sk.font_px("label") : 14)) * 0.5f;
    auto ftxt = [&](const std::string& t, ImVec4 c) {
        if (sk.loaded()) {
            sk.text(fl, "label", iv(fx, fy), t, 0, &c);
            fx += sk.text_size("label", t).x + 28 * u;
        } else {
            fl->AddText(iv(fx, fy), u32(c), t.c_str());
            fx += ImGui::CalcTextSize(t.c_str()).x + 28;
        }
    };
    if (!s.status.empty()) ftxt(s.status, theme().warn);
    ftxt(sk.label(overlay ? "footer.overlay" : "footer.hints",
                  overlay ? (main_screen && options_open ? "A Select    B Back    LB/RB Switch page" : "A Select    B Resume    LB/RB Switch page") : "A Select    B Back    LB/RB Switch page    START Play"),
         theme().text_dim);
    if (overlay && netplay_locked) ftxt("Netplay: the race keeps running", theme().warn);
    if (!overlay && game_mode) ftxt("Game Mode: next launch boots straight into the race", theme().text_dim);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    draw_capture_modal();
    draw_quit_confirm();
    ImGui::End();
}

// Quit game asks first. Yes/No are plain buttons, so A/Enter picks and B/Esc
// (the popup's own cancel) closes it; No has the focus until the player moves.
void App::draw_quit_confirm() {
    if (quit_confirm && !ImGui::IsPopupOpen("Quit game?")) ImGui::OpenPopup("Quit game?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Quit game?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
        return;
    ImGui::PushFont(theme().bold, theme().body_size * 1.2f);
    ImGui::TextUnformatted(tr("Quit the game?"));
    ImGui::PopFont();
    ImGui::TextDisabled("%s", tr("Unsaved race progress is lost."));
    ImGui::Dummy(ImVec2(0, 6));
    const float bw = 140 * (ui_scale > 0 ? ui_scale : 1.0f);
    const bool yes = big_button(tr("Yes"), ImVec2(bw, 0), false);
    script_mark("quit:yes");
    ImGui::SameLine();
    if (quit_confirm) ImGui::SetKeyboardFocusHere();  // first frame: land on No
    const bool no = big_button(tr("No"), ImVec2(bw, 0), true);
    script_mark("quit:no");
    quit_confirm = false;
    if (yes) {
        if (host && host->request_exit) host->request_exit(host->ctx, 0);
        else request_quit();
        ImGui::CloseCurrentPopup();
    }
    if (no || ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
        skin().play("back");
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool App::restart_needed(size_t off, size_t size) const {
    if (mode != Mode::Overlay || !s.io) return false;
    return std::memcmp(reinterpret_cast<const char*>(s.io) + off, reinterpret_cast<const char*>(&opened) + off, size) != 0;
}

void App::restart_chip(size_t off, size_t size) {
    if (!restart_needed(off, size)) return;
    chip("Applies after restart", theme().warn);
}

void App::overlay_tick() {
    if (!s.io || !host) return;
    if (std::memcmp(s.io, &applied, sizeof(applied)) != 0) {
        if (host->apply_settings) applied_bits = host->apply_settings(host->ctx, s.io);
        applied = *s.io;
    }
    if (s.binds_dirty) {
        std::string err;
        if (s.commit_files(&err) && host->reload_bindings) host->reload_bindings(host->ctx);
    }
}

}  // namespace r4l
