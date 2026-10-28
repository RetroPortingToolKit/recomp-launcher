// fake_host.cpp — a host that drives the launcher through the C ABI only,
// the way psxrecomp's main.cpp does. Used for the ABI self-test and for
// headless screenshots (never opens a visible window: R4L_HIDDEN is forced).
//
//   r4l-fake-host --abi-selftest
//   r4l-fake-host --shots <out-dir>
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "launcher_boot_timing.h"
#include "recomp_launcher_overlay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "posix_compat.h"

extern "C" int r4l_abi_symbols_present(void);
extern "C" int r4l_run_overlay_script(int w, int h, const RecompLauncherCGameInfo* game, RecompLauncherCSettings* io,
                                      const RecompOverlayHost* host, const char* assets_dir, int max_frames);
extern "C" int r4l_render_overlay_png(const char* path, int w, int h, const RecompLauncherCGameInfo* game,
                                      RecompLauncherCSettings* io, const RecompOverlayHost* host,
                                      const char* assets_dir, const char* screen);

namespace {

// ---- mod provider: a slice of R4's real catalog
struct FOpt { const char* id; const char* label; const char* group; int type; std::string value;
              std::vector<std::pair<const char*, const char*>> choices; };
struct FFeat { const char* pkg; const char* pkg_name; const char* id; const char* name; const char* group;
               const char* desc; int enabled; std::vector<FOpt> opts; };

std::vector<FFeat> g_feats = {
    {"r4.enhancement.widescreen", "R4 Custom Renderer", "widescreen", "Custom Renderer", "Display",
     "Native widescreen with HUD re-anchoring.", 1,
     {{"aspect", "View", "Display", RECOMP_MOD_OPTION_CHOICE, "16:9",
       {{"Fit", "Fit to Window"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}}}}},
    {"r4.enhancement.hide-rear-view-mirror", "Hide Rear-view Mirror", "hide-mirror", "Hide Rear-view Mirror",
     "Display", "Removes the in-car mirror overlay.", 0, {}},
    {"r4.enhancement.max-detail", "R4 Max Detail", "max-detail", "Max Detail", "Detail",
     "Full course and car detail at any distance.", 1,
     {{"draw_distance", "Draw distance", "Detail", RECOMP_MOD_OPTION_CHOICE, "maximum",
       {{"maximum", "Maximum (Recommended)"}, {"extended", "Extended"}, {"stock", "Stock"}}}}},
    {"psx.enhancement.pgxp", "PGXP Precision", "pgxp", "PGXP Precision", "Visual",
     "Sub-pixel geometry and perspective-correct textures.", 1,
     {{"culling", "Precise culling", "Visual", RECOMP_MOD_OPTION_BOOLEAN, "true", {}}}},
    {"r4.enhancement.ui-fonts", "R4 HD HUD", "ui-glyphs", "HD HUD", "Visual",
     "HD HUD by Kuid0us (T4HDHUD).", 1, {}},
    {"r4.modern-controls", "R4 Controls", "modern-controls", "Controls", "Controllers",
     "Modern analog steering, or the original layout.", 1,
     {{"scheme", "Control scheme", "Controllers", RECOMP_MOD_OPTION_CHOICE, "modern",
       {{"modern", "Modern (Recommended)"}, {"classic", "Classic"}}}}},
    {"r4.compat.jogcon-input", "R4 JogCon Input", "jogcon-input", "JogCon Input", "Controllers",
     "Namco JogCon dial support.", 1, {}},
    {"r4.enhancement.camera-lookaround", "R4 Camera Look-Around", "camera-lookaround", "Camera Look-Around",
     "Controls", "Look around the car with the right stick.", 1,
     {{"sensitivity", "Look sensitivity", "Controls", RECOMP_MOD_OPTION_CHOICE, "100",
       {{"70", "70%"}, {"100", "100% (Recommended)"}, {"130", "130%"}}}}},
};

FFeat* feat(const char* p, const char* f) {
    for (auto& x : g_feats) if (!std::strcmp(x.pkg, p) && !std::strcmp(x.id, f)) return &x;
    return nullptr;
}
int m_fcount(void*) { return (int)g_feats.size(); }
int m_fget(void*, int i, RecompLauncherCModFeature* o) {
    const FFeat& f = g_feats[i];
    *o = RecompLauncherCModFeature{};
    std::snprintf(o->id, sizeof o->id, "%s", f.id);
    std::snprintf(o->package_id, sizeof o->package_id, "%s", f.pkg);
    std::snprintf(o->package_name, sizeof o->package_name, "%s", f.pkg_name);
    std::snprintf(o->package_version, sizeof o->package_version, "1.0.0");
    std::snprintf(o->name, sizeof o->name, "%s", f.name);
    std::snprintf(o->group, sizeof o->group, "%s", f.group);
    std::snprintf(o->description, sizeof o->description, "%s", f.desc);
    o->enabled = f.enabled;
    o->option_count = (int)f.opts.size();
    return 1;
}
int m_optget(void*, const char* p, const char* fid, int i, RecompLauncherCModOption* o) {
    FFeat* f = feat(p, fid);
    if (!f || i >= (int)f->opts.size()) return 0;
    const FOpt& x = f->opts[i];
    *o = RecompLauncherCModOption{};
    std::snprintf(o->id, sizeof o->id, "%s", x.id);
    std::snprintf(o->label, sizeof o->label, "%s", x.label);
    std::snprintf(o->group, sizeof o->group, "%s", x.group);
    std::snprintf(o->value, sizeof o->value, "%s", x.value.c_str());
    o->type = x.type;
    o->choice_count = (int)x.choices.size();
    return 1;
}
int m_chget(void*, const char* p, const char* fid, const char* oid, int i, RecompLauncherCModChoice* o) {
    FFeat* f = feat(p, fid);
    if (!f) return 0;
    for (auto& x : f->opts)
        if (!std::strcmp(x.id, oid) && i < (int)x.choices.size()) {
            std::snprintf(o->value, sizeof o->value, "%s", x.choices[i].first);
            std::snprintf(o->label, sizeof o->label, "%s", x.choices[i].second);
            return 1;
        }
    return 0;
}
int m_enable(void*, const char* p, const char* f, int on) { if (FFeat* x = feat(p, f)) { x->enabled = on; return 1; } return 0; }
int m_setopt(void*, const char* p, const char* f, const char* o, const char* v) {
    if (FFeat* x = feat(p, f)) for (auto& op : x->opts) if (!std::strcmp(op.id, o)) { op.value = v; return 1; }
    return 0;
}
int m_commit(void*, const char*) { return 1; }

// ---- netplay: offline by default, a 3-of-4 lobby with R4L_FAKE_LOBBY=1
bool lobby() { const char* e = std::getenv("R4L_FAKE_LOBBY"); return e && *e == '1'; }
int n_connected(void*) { return 1; }
void n_pump(void*) {}
int n_in_lobby(void*) { return lobby(); }
int n_is_host(void*) { return 1; }
int n_max(void*) { return 4; }
int n_mcount(void*) { return 3; }
int n_mget(void*, int i, RecompLauncherCNetplayMember* m) {
    static const char* names[] = {"Shokunin", "Reiko", "Hitomi"};
    *m = RecompLauncherCNetplayMember{};
    m->slot = i == 2 ? 3 : i;
    std::snprintf(m->display_name, sizeof m->display_name, "%s", names[i]);
    m->is_host = i == 0; m->is_local = i == 0; m->ready = i != 2; m->latency_ms = i ? 18 + 20 * i : 0;
    return 1;
}
int n_all_ready(void*) { return 0; }
int n_lcount(void*) { return 3; }
int n_lget(void*, int i, RecompLauncherCNetplayLobby* l) {
    static const char* names[] = {"Late-night GP", "Phantomile 4P", "Helter Skelter time trials"};
    *l = RecompLauncherCNetplayLobby{};
    std::snprintf(l->lobby_id, sizeof l->lobby_id, "lobby-%d", i);
    std::snprintf(l->name, sizeof l->name, "%s", names[i]);
    l->player_count = 1 + i; l->max_slots = 4; l->latency_ms = 24 + 31 * i; l->has_password = i == 1;
    return 1;
}
int n_chat_count(void*) { return 3; }
int n_chat_get(void*, int i, RecompLauncherCNetplayChatMessage* m) {
    static const char* t[] = {"Reiko joined P2", "gg, one more on Wonderhill?", "Hitomi is choosing a car"};
    *m = RecompLauncherCNetplayChatMessage{};
    std::snprintf(m->from, sizeof m->from, "%s", i == 1 ? "Reiko" : "");
    std::snprintf(m->text, sizeof m->text, "%s", t[i]);
    m->is_system = i != 1;
    return 1;
}
int n_swap_req(void*, int) { return 1; }
int n_swap_in(void*, char* who, size_t cap, int* from) {
    std::snprintf(who, cap, "Reiko");
    if (from) *from = 1;
    return lobby() ? 1 : 0;
}
int n_swap_resp(void*, int) { return 1; }
int n_acc_avail(void*) { return 1; }
int n_acc_state(void*) { return RECOMP_LAUNCHER_ACCOUNT_GUEST; }
int n_am_avail(void*) { return 1; }
int n_am_state(void*) { return RECOMP_LAUNCHER_AUTOMATCH_IDLE; }
int n_am_count(void*) { return 2; }
int n_am_get(void*, int i, RecompLauncherCNetplayRuleset* r) {
    *r = RecompLauncherCNetplayRuleset{};
    std::snprintf(r->id, sizeof r->id, "rs%d", i);
    std::snprintf(r->label, sizeof r->label, "%s", i ? "4-player Grand Prix" : "1v1 Time Trial");
    std::snprintf(r->caps_summary, sizeof r->caps_summary, "%s", i ? "rollback, 4 seats" : "rollback, 2 seats");
    return 1;
}
int n_online_count(void*) { return 2; }
int n_online_get(void*, int i, RecompLauncherCNetplayOnlinePlayer* p) {
    *p = RecompLauncherCNetplayOnlinePlayer{};
    std::snprintf(p->display_name, sizeof p->display_name, "%s", i ? "Hitomi" : "Reiko");
    std::snprintf(p->country, sizeof p->country, "%s", i ? "JP" : "US");
    std::snprintf(p->account, sizeof p->account, "acct%d", i);
    p->in_lobby = i;
    return 1;
}
int n_pred_get(void*) { return 2; }
int n_pred_set(void*, int) { return 0; }
int n_bool_get(void*) { return 0; }
int n_bool_set(void*, int) { return 0; }
int n_addr(void*, int i, RecompLauncherCNetplayLocalAddress* a) {
    if (i) return 0;
    std::snprintf(a->address, sizeof a->address, "192.168.1.20:7777");
    std::snprintf(a->label, sizeof a->label, "LAN");
    return 1;
}

// ---- quality presets
void q_apply(int p, RecompLauncherCSettings* s) {
    s->internal_resolution = p >= 4 ? 4 : p;
    s->antialiasing = p >= 3 ? 3 : 0;
    s->dynamic_resolution = p <= 2;
    s->frame_generation = p >= 3;
}
int q_redetect(void) { return 3; }

int disc_verify(const char*, RecompLauncherCDiscVerify* v) {
    *v = RecompLauncherCDiscVerify{};
    std::snprintf(v->serial, sizeof v->serial, "SLUS-00797");
    std::snprintf(v->region, sizeof v->region, "NTSC-U");
    v->iso_ok = 1; v->verdict = 1; v->track_count = 3; v->netplay_ok = 1;
    return 1;
}

const char* kRenderers[] = {"Software", "OpenGL (Recommended)"};
const char* kIr[] = {"Display (native)", "2x", "3x", "4x (1440p)", "5x", "8x (8K)"};
const int kIrV[] = {0, 2, 3, 4, 5, 8};
const char* kAssist[] = {"Rewind", "Save states", "Fast-forward", "Fast-forward toggle"};

void fill(RecompLauncherCGameInfo* gi, RecompLauncherCModProvider* mp, RecompLauncherCNetplayCallbacks* np) {
    *gi = RecompLauncherCGameInfo{};
    launcher_profile_apply("psx", gi);
    gi->name = "Ridge Racer Type 4";
    gi->region = "USA";
    gi->num_players = 4;
    gi->renderer_labels = kRenderers; gi->num_renderers = 2;
    gi->internal_resolution_labels = kIr; gi->internal_resolution_values = kIrV; gi->num_internal_resolutions = 6;
    gi->has_dynamic_resolution = 1; gi->has_render_pipeline = 1; gi->has_geometry_precision = 1;
    gi->assist_binding_labels = kAssist; gi->assist_binding_count = 4; gi->assist_direct_pad_bind_action = 0;
    gi->disc_verify = disc_verify;
    gi->quality_offered_mask = 0xF; gi->quality_detected = 3;
    gi->quality_summary = "Apple M2 Pro · 12 threads · 32 GB";
    gi->quality_reason = "Apple silicon with 16+ GB: High";
    gi->quality_apply = q_apply; gi->quality_redetect = q_redetect;
    gi->memcard_inspect = [](const char*, RecompLauncherCMemcard* m) {
        *m = RecompLauncherCMemcard{}; m->valid = 1; m->used_blocks = 4;
        for (int i = 0; i < 4; ++i) m->block_used[i] = 1;
        return 1;
    };
    gi->has_player_name = 1;
    *mp = RecompLauncherCModProvider{};
    mp->feature_count = m_fcount; mp->feature_get = m_fget; mp->feature_option_get = m_optget;
    mp->feature_choice_get = m_chget; mp->feature_enable = m_enable; mp->feature_set_option = m_setopt;
    mp->commit = m_commit;
    mp->install_archive = [](void*, const char*) { return 1; };
    gi->mods = mp;
    *np = RecompLauncherCNetplayCallbacks{};
    np->connected = n_connected; np->pump = n_pump; np->in_lobby = n_in_lobby; np->is_host = n_is_host;
    np->lobby_max_slots = n_max; np->member_count = n_mcount; np->member_get = n_mget;
    np->all_ready = n_all_ready; np->list_count = n_lcount; np->list_get = n_lget;
    np->account_available = n_acc_avail; np->account_state = n_acc_state;
    np->automatch_available = n_am_avail; np->automatch_state = n_am_state;
    np->automatch_ruleset_count = n_am_count; np->automatch_ruleset_get = n_am_get;
    np->online_count = n_online_count; np->online_get = n_online_get;
    np->input_prediction_get = n_pred_get; np->input_prediction_set = n_pred_set;
    np->allow_spectators_get = n_bool_get; np->allow_spectators_set = n_bool_set;
    np->multitap_analog_get = n_bool_get; np->multitap_analog_set = n_bool_set;
    np->seat_swap_request = n_swap_req; np->seat_swap_incoming = n_swap_in; np->seat_swap_respond = n_swap_resp;
    np->chat_count = n_chat_count; np->chat_get = n_chat_get; np->local_address_get = n_addr;
    gi->netplay_supported = 1; gi->netplay = np;
}

}  // namespace

int main(int argc, char** argv) {
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    launcher_boot_timing_mark("fake:main");
    RecompLauncherCGameInfo gi; RecompLauncherCModProvider mp; RecompLauncherCNetplayCallbacks np;
    fill(&gi, &mp, &np);
    RecompLauncherCSettings s{};
    s.renderer = 1; s.fullscreen = 0; s.vsync = 1; s.player_src[0] = 2; s.player_src[1] = 1;
    s.pad_mode[0] = 1; s.deadzone[0] = 15; s.rewind_enabled = 1; s.rewind_depth = 50;
    s.assist_pad_bind[0] = RECOMP_LAUNCHER_PAD_BUTTON_COMBO((1 << 4) | (1 << 8));
    s.quality_preset = 3; s.quality_base = 3; q_apply(3, &s);
    std::snprintf(s.netplay_player_name, sizeof s.netplay_player_name, "Shokunin");
    s.memcard_enabled[0] = 1; s.volume = 80; s.audio_freq = 44100;
    std::snprintf(s.memcard_path[0], sizeof s.memcard_path[0], "memcards/card1.mcd");
    char out[1024];

    if (argc > 1 && !std::strcmp(argv[1], "--abi-selftest")) {
        int fail = !r4l_abi_symbols_present();
        setenv("RECOMP_UI_PICKER_SELFTEST", "1", 1);
        fail |= recomp_launcher_run_window("t", &s, &gi, "assets", "", out, sizeof out) != RECOMP_LAUNCHER_RESULT_UNAVAILABLE;
        unsetenv("RECOMP_UI_PICKER_SELFTEST");
        recomp_launcher_set_preserve_sdl(1);
        fail |= recomp_launcher_relaunch_exe(out, sizeof out) != 0;  // nothing built yet
        std::printf("abi selftest: %s\n", fail ? "FAIL" : "ok");
        return fail;
    }
    if (argc > 2 && !std::strcmp(argv[1], "--shots")) {
        setenv("R4L_HIDDEN", "1", 1);
        setenv("R4L_NO_SOUND", "1", 1);
        setenv("R4L_NO_TRANSITION", "1", 1);
        setenv("R4L_NO_AUTOSCAN", "1", 1);  // never scan the machine for screenshots
        const std::string dir = argv[2];
        std::string assets = "assets", only;
        int w = 1280, h = 800;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--assets")) assets = argv[i + 1];
            else if (!std::strcmp(argv[i], "--skin")) setenv("R4L_SKIN", argv[i + 1], 1);
            else if (!std::strcmp(argv[i], "--size")) std::sscanf(argv[i + 1], "%dx%d", &w, &h);
            else if (!std::strcmp(argv[i], "--screens")) only = "," + std::string(argv[i + 1]) + ",";
        }
        char size[32];
        std::snprintf(size, sizeof size, "%dx%d", w, h);
        setenv("R4L_SIZE", size, 1);
        struct Shot { const char* screen; const char* file; bool lobby; bool disc; bool overlay; };
        const Shot shots[] = {
            {"Play", "home", false, true, false},        {"Graphics", "graphics", false, true, false},
            {"Mods", "mods", false, true, false},        {"Controls", "controls", false, true, false},
            {"Netplay", "netplay", false, true, false},  {"Netplay", "netplay-lobby", true, true, false},
            {"Disc setup", "setup", false, false, false}, {"About", "about", false, true, false},
            {"Settings", "settings", false, true, false},
            {"Graphics", "overlay", false, true, true},  {"Controls", "overlay-netplay", true, true, true},
        };
        int fail = 0;
        for (const Shot& sh : shots) {
            if (!only.empty() && only.find("," + std::string(sh.file) + ",") == std::string::npos) continue;
            setenv("R4L_SCREEN", sh.screen, 1);
            setenv("R4L_FAKE_LOBBY", sh.lobby ? "1" : "0", 1);
            const std::string png = dir + "/" + sh.file + ".png";
            std::remove(png.c_str());
            int rc;
            if (sh.overlay) {
                RecompOverlayHost host{};
                host.abi_version = RECOMP_OVERLAY_ABI_VERSION;
                host.set_paused = [](void*, int) { return 1; };
                host.netplay_active = [](void*) { return lobby() ? 1 : 0; };
                RecompLauncherCSettings o = s;
                rc = r4l_render_overlay_png(png.c_str(), w, h, &gi, &o, &host, assets.c_str(), sh.screen);
            } else {
                setenv("R4L_SCREENSHOT", png.c_str(), 1);
                const char* real = std::getenv("R4L_TEST_DISC");  // a local disc enables disc-sourced art
                const char* disc = sh.disc ? (real ? real : "/Games/PS1/R4 - Ridge Racer Type 4 (USA).cue") : "";
                RecompLauncherCSettings copy = s;
                rc = recomp_launcher_run_window("R4", &copy, &gi, assets.c_str(), disc, out, sizeof out);
                unsetenv("R4L_SCREENSHOT");
            }
            std::ifstream f(png);
            std::printf("%-16s rc=%d %s\n", sh.file, rc, f ? "written" : "MISSING");
            fail |= !f;
        }
        return fail;
    }
    if (argc > 1 && !std::strcmp(argv[1], "--run-overlay")) {  // the in-game menu (R4L_OVERLAY_SCRIPT drives it)
        // Save states: 12 slots, slot 1 used; save marks a slot used, load
        // needs a used slot. R4L_FAKE_LOBBY=1 = netplay (states refused).
        static long long slots[12] = {1761600000};
        RecompOverlayHost host{};
        host.abi_version = RECOMP_OVERLAY_ABI_VERSION;
        host.set_paused = [](void*, int) { return lobby() ? 0 : 1; };
        host.netplay_active = [](void*) { return lobby() ? 1 : 0; };
        host.state_slot_count = [](void*) { return 12; };
        host.state_available = [](void*) { return lobby() ? 0 : 1; };
        host.state_info = [](void*, int n, long long* t) {
            if (n < 0 || n >= 12 || !slots[n]) return 0;
            if (t) *t = slots[n];
            return 1;
        };
        host.state_thumb = [](void*, int n, unsigned* px, int w, int h) {
            for (int i = 0; i < w * h; ++i) px[i] = 0xff000000u | (static_cast<unsigned>(n * 20) << 16) | ((i % w) * 2);
            return 1;
        };
        host.state_save = [](void*, int n) {
            if (n < 0 || n >= 12 || lobby()) return 0;
            slots[n] = 1761700000 + n;
            std::fprintf(stderr, "fake-host: save slot %d\n", n + 1);
            return 1;
        };
        host.state_load = [](void*, int n) {
            if (n < 0 || n >= 12 || lobby() || !slots[n]) return 0;
            std::fprintf(stderr, "fake-host: load slot %d\n", n + 1);
            return 1;
        };
        int w = 1280, h = 720;
        if (const char* sz = std::getenv("R4L_SIZE")) std::sscanf(sz, "%dx%d", &w, &h);
        RecompLauncherCSettings o = s;
        const int fails = r4l_run_overlay_script(w, h, &gi, &o, &host, argc > 2 ? argv[2] : "assets", 6000);
        std::printf("overlay run failures=%d\n", fails);
        return fails == 0 ? 0 : 1;
    }
    if (argc > 1 && !std::strcmp(argv[1], "--run")) {  // one launcher session (R4L_SCRIPT drives it)
        const int rc = recomp_launcher_run_window("R4", &s, &gi, argc > 2 ? argv[2] : "assets", "", out, sizeof out);
        std::printf("run_window rc=%d\n", rc);
        return 0;
    }
    std::fprintf(stderr, "usage: %s --abi-selftest | --shots <out-dir> [--assets d] [--skin name|path] [--size WxH] [--screens a,b]\n", argv[0]);
    return 2;
}
