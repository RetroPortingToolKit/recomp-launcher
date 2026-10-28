// title.h — the title seam.
//
// Everything game-specific lives behind TitleLayer: names, accent colours,
// credits and notices, which mod features stand for "Modern controls" or
// "JogCon", which netplay seats exist, and the hero art. The launcher core
// and screens only talk to this interface. R4 is titles/r4/; another
// psxrecomp game adds titles/<id>/ and selects it with -DR4L_TITLE=<id>.
#pragma once

#include "r4l/core/surface.h"

#include <cstddef>
#include <cstdint>

namespace r4l {

struct Rgb {
    uint8_t r, g, b;
};

struct TitleNotice {
    const char* heading;  // "HD HUD (T4HDHUD)"
    const char* body;     // credit / licence line(s)
    const char* url;      // may be null
};

// A mod feature the title surfaces outside the Mods screen.
struct FeatureRef {
    const char* package_id;
    const char* feature_id;
};

struct ControllerProfile {
    const char* id;          // "dualshock", "negcon", "jogcon"
    const char* label;       // "DualShock (analog)"
    const char* detail;      // one line under the label
    int pad_mode;            // Settings.pad_mode value it implies (1 analog, 2 digital)
    FeatureRef feature;      // feature that must be on for it; {nullptr,nullptr} = none
};

struct TitleLayer {
    const char* id;              // "r4"
    const char* display_name;    // "R4: Ridge Racer Type 4"
    const char* tagline;         // under the name on Home
    const char* serial;          // "SLUS-00797"
    Rgb accent;                  // primary accent
    Rgb accent2;                 // secondary accent (gradients, focus ring)

    int max_players;             // local seats shown on Controls
    int netplay_seats;           // P1..Pn shown in the lobby

    FeatureRef modern_controls;  // Controls: Modern / Classic switch
    const char* modern_option;   // choice option on that feature ("scheme")
    const char* modern_value;    // "modern"
    const char* classic_value;   // "classic"
    const FeatureRef* graphics_features;  // mod features shown inline on Graphics
    int graphics_feature_count;
    const ControllerProfile* controllers;
    int controller_count;

    const TitleNotice* notices;  // About screen
    int notice_count;
    const char* about;           // paragraph on About

    // What the launcher shows, hides, locks or automates (docs/SUPPORTED.md).
    const SurfaceRule* surface;
    int surface_count;

    // Disc-sourced skin assets: extract art from the player's own disc into
    // out_dir (a local cache, never shipped). Skins reference the results as
    // "$disc/<id>/<file>". Returns false and fills err on failure.
    bool (*extract_disc_assets)(const char* disc_path, const char* out_dir, char* err, size_t err_cap);

    // Optional: draw a title-specific hero banner into the Home card (UI
    // layer supplies an ImDrawList via void* to keep this header ImGui-free).
    void (*draw_hero)(void* draw_list, float x0, float y0, float x1, float y1, double time);

    // Optional: the Steam library name for "Add to Steam" (controls.steam);
    // null uses display_name.
    const char* steam_name;
};

// Implemented by exactly one titles/<id>/ translation unit.
const TitleLayer& active_title();

}  // namespace r4l
