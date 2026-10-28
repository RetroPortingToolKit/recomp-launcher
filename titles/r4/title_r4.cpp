// title_r4.cpp — R4: Ridge Racer Type 4 (USA), SLUS-00797.
#include "r4l/title.h"

#include "imgui.h"
#include "r4l/core/discfs.h"
#include "r4l/core/session.h"

#include <cstdio>

#include <cmath>

namespace r4l {
namespace {

const ControllerProfile kControllers[] = {
    {"dualshock", "DualShock", "Analog sticks and triggers. Best with a modern gamepad.", 1,
     {nullptr, nullptr}},
    {"digital", "Digital pad", "The original PlayStation pad, no sticks.", 2, {nullptr, nullptr}},
    {"negcon", "NeGcon", "Twist steering and analog pedals, mapped from a gamepad.", 1,
     {nullptr, nullptr}},
    {"jogcon", "JogCon", "Namco's force-feedback dial. Uses the R4 JogCon input mod.", 1,
     {"r4.compat.jogcon-input", "jogcon-input"}},
};

const FeatureRef kGraphicsFeatures[] = {
    {"r4.enhancement.widescreen", "widescreen"},
    {"r4.enhancement.max-detail", "max-detail"},
    {"psx.enhancement.pgxp", "pgxp"},
};

const TitleNotice kNotices[] = {
    {"HD HUD (T4HDHUD)",
     "The bundled HD HUD pack is HD HUD by Kuid0us, re-keyed for the US disc. "
     "Credit to Kuid0us.",
     "https://github.com/Kuid0us/T4HDHUD"},
    {"Lato",
     "LatoLatin Regular and Bold. Copyright (c) 2010-2015 Lukasz Dziedzic, with Reserved "
     "Font Name \"Lato\". Licensed under the SIL Open Font License 1.1.",
     "https://scripts.sil.org/OFL"},
    {"Dear ImGui", "Copyright (c) 2014-2026 Omar Cornut. MIT License.",
     "https://github.com/ocornut/imgui"},
    {"psxrecomp", "Static recompilation framework for PlayStation titles. MIT License.",
     "https://github.com/RetroPortingToolKit/psxrecomp"},
    {"Ridge Racer Type 4",
     "R4: Ridge Racer Type 4 is a trademark of Bandai Namco Entertainment. This project "
     "ships no game data; you supply your own disc.",
     nullptr},
};

// R4's surfacing decisions (docs/SUPPORTED.md): as few rows as possible,
// everything else automatic. Undeclared keys follow the default rule.
const SurfaceRule kSurface[] = {
    // Graphics: the preset (Auto = detected Low..Ultra), Smooth motion, screen mode.
    {"graphics.preset", Vis::Shown, "auto"},
    {"graphics.redetect", Vis::Hidden, nullptr},          // Auto re-detects
    {"graphics.frame_generation", Vis::Shown, nullptr},   // "Smooth motion"
    {"graphics.fullscreen", Vis::Shown, nullptr},
    {"graphics.dynamic_resolution", Vis::Auto, nullptr}, // the preset decides
    {"graphics.internal_resolution", Vis::Shown, nullptr}, // the preset seeds it; the row stays selectable
    {"graphics.title_features", Vis::Hidden, nullptr},    // widescreen Fit / max detail stay on
    // Controls: Modern / Classic and rebinding.
    {"controls.scheme", Vis::Shown, nullptr},
    {"controls.bindings", Vis::Shown, nullptr},
    {"controls.devices", Vis::Shown, nullptr},
    {"controls.assist", Vis::Hidden, nullptr},
    // Add to Steam (owner, 2026-10-28): a Steam Controller is a gamepad only in
    // a game Steam launched; the button registers R4 as a non-Steam game.
    {"controls.steam", Vis::Shown, nullptr},
    // Disc: found automatically; OpenBIOS is bundled, so no BIOS prompt.
    {"disc.autoscan", Vis::Auto, "auto"},
    // No pre-game launcher window; setup, netplay and options are in the
    // in-game menu (Esc / Guide / Select / Start+Select).
    {"launcher.in_game", Vis::Auto, "auto"},
    // The menu is the first screen; Resume starts the race (owner,
    // 2026-10-27). Select alone opens it: R4 has no use for Select on its
    // own that the menu would take away (owner, 2026-10-28). Select chords
    // (Select + Y rewind and the other shortcuts) still work.
    {"launcher.boot_menu", Vis::Auto, "auto"},
    {"launcher.select_menu", Vis::Auto, "auto"},
    // The menu opens on a main screen: Start Game / Resume Game, Save State,
    // Load State, Options (the other pages, with Back), Quit game; a first
    // run with no disc offers Select Disc only (owner, 2026-10-28).
    {"launcher.main_screen", Vis::Auto, "auto"},
    {"disc.toolchain", Vis::Hidden, nullptr},
    {"bios.select", Vis::Hidden, nullptr},
    // Settings page: volume only; memory cards use the default cards.
    {"system.memcards", Vis::Hidden, nullptr},
    {"system.language", Vis::Hidden, nullptr},
    // Netplay: host and join (lobby, chat, seats are essentials by default).
    {"netplay.host", Vis::Shown, nullptr},
    {"netplay.join", Vis::Shown, nullptr},
    {"netplay.spectators", Vis::Hidden, nullptr},  // owner: few options; spectating still works underneath
    // Mods: shown, simple (no package view, versions or resource pickers).
    {"mods.list", Vis::Shown, nullptr},
    {"mods.install", Vis::Shown, nullptr},
    {"about.updates", Vis::Hidden, nullptr},
};

// Disc-sourced skin art: the "R4 RIDGE RACER TYPE 4" logo and the menu
// font, read from R4.BIN on the player's own disc (offsets for SLUS-00797).
// Written into the local cache; nothing from the disc is ever shipped.
bool extract_r4_assets(const char* disc_path, const char* out_dir, char* err, size_t cap) {
    auto fail = [&](const std::string& m) {
        std::snprintf(err, cap, "%s", m.c_str());
        return false;
    };
    DiscImage disc;
    std::string e;
    if (!disc.open(disc_path, &e)) return fail(e);
    std::vector<uint8_t> bin;
    if (!disc.read_file("R4.BIN", &bin, &e)) return fail(e);
    Rgba logo_tim, font_tim;
    if (!decode_tim(bin, 0x02dcd28c, &logo_tim) || logo_tim.w != 256 || logo_tim.h != 256)
        return fail("logo image not found (not SLUS-00797?)");
    if (!decode_tim(bin, 0x02d59000, &font_tim) || font_tim.w != 256 || font_tim.h != 60)
        return fail("font image not found (not SLUS-00797?)");
    Rgba logo = crop(logo_tim, 34, 151, 188, 82);
    tint(&logo, 0x77, 0x77, 0x77);  // the title menu shows the logo in grey
    logo = scale_nearest(logo, 2);
    const std::string dir = out_dir;
    if (!write_png_rgba(join_path(dir, "logo.png"), logo.w, logo.h, logo.px.data()))
        return fail("cannot write logo.png");
    Rgba font = font_tim;
    tint(&font, 0xff, 0xff, 0xff);  // white glyphs; skins tint them
    // Rows: symbols (only . , : mapped) then 0123 / 4-9 A-Z / a-z.
    std::string row0 = ".,:" + std::string(25, ' ') + "0123";
    if (!write_bitmap_font(font, {{{2, 18}, row0}, {{22, 38}, "456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
                                  {{42, 59}, "abcdefghijklmnopqrstuvwxyz"}},
                           dir, "font", 16, &e))
        return fail(e);
    return true;
}

// Speed lines and a horizon: a cheap, resolution-independent hero.
void draw_hero(void* dl_v, float x0, float y0, float x1, float y1, double t) {
    ImDrawList* dl = static_cast<ImDrawList*>(dl_v);
    const float w = x1 - x0, h = y1 - y0;
    dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(18, 10, 40, 255),
                                IM_COL32(60, 12, 58, 255), IM_COL32(230, 90, 40, 255),
                                IM_COL32(20, 20, 60, 255));
    const float hy = y0 + h * 0.62f;
    dl->AddRectFilled(ImVec2(x0, hy), ImVec2(x1, y1), IM_COL32(8, 8, 18, 235));
    const ImVec2 vp(x0 + w * 0.62f, hy);
    for (int i = -9; i <= 9; ++i) {
        const float fx = vp.x + i * w * 0.12f;
        dl->AddLine(vp, ImVec2(fx, y1), IM_COL32(255, 120, 60, 70), 1.0f);
    }
    for (int i = 1; i <= 7; ++i) {
        float f = static_cast<float>(std::fmod(i / 7.0 + t * 0.25, 1.0));
        f = f * f;
        const float yy = hy + (y1 - hy) * f;
        dl->AddLine(ImVec2(x0, yy), ImVec2(x1, yy), IM_COL32(255, 120, 60, (int)(30 + 90 * f)), 1.0f);
    }
    dl->AddCircleFilled(ImVec2(vp.x, hy - h * 0.02f), h * 0.16f, IM_COL32(255, 170, 70, 200), 48);
    dl->AddRectFilled(ImVec2(x0, hy - 2), ImVec2(x1, hy), IM_COL32(255, 200, 120, 160));
}

const TitleLayer kR4 = {
    "r4",
    "R4: Ridge Racer Type 4",
    "Native PC recompilation · SLUS-00797",
    "SLUS-00797",
    {255, 92, 54},
    {120, 92, 255},
    4,
    4,
    {"r4.modern-controls", "modern-controls"},
    "scheme",
    "modern",
    "classic",
    kGraphicsFeatures,
    static_cast<int>(sizeof(kGraphicsFeatures) / sizeof(kGraphicsFeatures[0])),
    kControllers,
    static_cast<int>(sizeof(kControllers) / sizeof(kControllers[0])),
    kNotices,
    static_cast<int>(sizeof(kNotices) / sizeof(kNotices[0])),
    "A static recompilation of R4: Ridge Racer Type 4 (USA) built on psxrecomp. "
    "It runs natively, with no emulator behind it.",
    kSurface,
    static_cast<int>(sizeof(kSurface) / sizeof(kSurface[0])),
    extract_r4_assets,
    draw_hero,
    "Ridge Racer Type 4 Recompiled",  // Add to Steam: library name
};

}  // namespace

const TitleLayer& active_title() { return kR4; }

}  // namespace r4l
