// test_core.cpp — settings/config round-trips and the preset tracker.
#include "r4l/core/binds.h"
#include "r4l/core/ini.h"
#include "r4l/core/mods.h"
#include "r4l/core/surface.h"
#include "r4l/core/discscan.h"
#include "r4l/core/discfs.h"
#include "r4l/core/memcard.h"
#include "r4l/core/quality.h"
#include "r4l/core/session.h"
#include "r4l/core/skin_model.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <cmath>

using namespace r4l;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

static std::string tmpdir() {
    char t[] = "/tmp/r4l-test-XXXXXX";
    return mkdtemp(t);
}
static std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static void spit(const std::string& p, const std::string& t) { std::ofstream(p) << t; }

static void test_ini_preserves_foreign_lines() {
    IniDoc d;
    d.parse("; comment\n[KeyMap]\nFullscreen = F11\nCustom = x\n\n[Other]\na=1\n");
    d.set("KeyMap", "Rewind", "Backspace");
    d.set("New", "k", "v");
    const std::string t = d.text();
    CHECK(t.find("; comment") == 0);
    CHECK(t.find("Custom = x") != std::string::npos);
    CHECK(t.find("Rewind = Backspace") < t.find("[Other]"));
    CHECK(d.get("other", "A") == "1");
    CHECK(d.get("New", "k") == "v");
}

static void test_keybinds_roundtrip(const std::string& dir) {
    const std::string p = dir + "/keybinds.ini";
    spit(p, "# runtime header\n[player1]\nup = W\ncross = J, Space\nunknown_key = keep\n");
    KeyboardBinds b;
    CHECK(load_keyboard_binds(p, &b));
    CHECK(b.player[0][0].primary == "W");
    CHECK(b.player[0][4].primary == "J" && b.player[0][4].alt == "Space");
    CHECK(b.player[0][1].primary == "Down");  // default kept for absent key
    b.player[1][5].primary = "K";
    b.player[0][20].primary = "";
    CHECK(save_keyboard_binds(p, b));
    const std::string t = slurp(p);
    CHECK(t.find("# runtime header") == 0);
    CHECK(t.find("unknown_key = keep") != std::string::npos);
    CHECK(t.find("cross = J, Space") != std::string::npos);
    CHECK(t.find("rs_up = None") != std::string::npos);
    KeyboardBinds c;
    CHECK(load_keyboard_binds(p, &c));
    for (int pl = 0; pl < kPsxKeyboardPlayers; ++pl)
        for (int i = 0; i < kPsxInputCount; ++i) {
            CHECK(c.player[pl][i].primary == b.player[pl][i].primary);
            CHECK(c.player[pl][i].alt == b.player[pl][i].alt);
        }
}

static void test_input_ini_roundtrip(const std::string& dir) {
    const std::string p = dir + "/input.ini";
    spit(p, "; PSXRecomp input mapping\n[controller]\nenabled = true\ndevice = 1\ndeadzone = 5000\n"
            "[mapping]\ncross = b\n[mapping.03000000de280000ff11000001000000]\ncross = a\n");
    PadBinds b;
    CHECK(load_pad_binds(p, &b));
    CHECK(b.device == 1 && b.deadzone == 5000);
    CHECK(b.global.source[4] == "b");
    CHECK(b.global.source[0] == "dpup");
    CHECK(b.per_guid.size() == 1 && b.per_guid[0].second.source[4] == "a");
    b.global.source[6] = "x, leftshoulder";
    b.for_guid("abc", true)->source[0] = "lefty-";
    CHECK(save_pad_binds(p, b));
    PadBinds c;
    CHECK(load_pad_binds(p, &c));
    CHECK(c.global.source[6] == "x, leftshoulder");
    CHECK(c.per_guid.size() == 2);
    CHECK(c.for_guid("ABC", false)->source[0] == "lefty-");
    CHECK(slurp(p).find("; PSXRecomp input mapping") == 0);
}

static void test_hotkeys(const std::string& dir) {
    const std::string p = dir + "/config.ini";
    spit(p, "[Video]\nscale=2\n[KeyMap]\nFullscreen = F11\n");
    std::vector<std::pair<std::string, std::string>> k;
    load_hotkeys(p, &k);
    bool found = false;
    for (auto& kv : k)
        if (kv.first == "Rewind") { kv.second = "Backspace"; found = true; }
    CHECK(found);
    CHECK(save_hotkeys(p, k));
    const std::string t = slurp(p);
    CHECK(t.find("scale=2") != std::string::npos || t.find("scale = 2") != std::string::npos);
    CHECK(t.find("Rewind = Backspace") != std::string::npos);
    CHECK(t.find("Fullscreen = F11") != std::string::npos);
}

static void test_sidecars_and_window(const std::string& dir) {
    CHECK(write_sidecars({dir}, {"/d/disc1.cue", "", "/d/disc3.cue"}, "/b/scph.bin"));
    auto d = read_disc_cfg(dir);
    CHECK(d.size() == 3 && d[0] == "/d/disc1.cue" && d[1].empty() && d[2] == "/d/disc3.cue");
    CHECK(slurp(dir + "/rom.cfg") == "/d/disc1.cue\n");
    CHECK(slurp(dir + "/bios.cfg") == "/b/scph.bin\n");
    int w = 0, h = 0;
    CHECK(save_window_size(dir + "/launcher-window.ini", 1366, 768));
    CHECK(load_window_size(dir + "/launcher-window.ini", &w, &h) && w == 1366 && h == 768);
    spit(dir + "/launcher-window.ini", "logical_width=50\nlogical_height=9999999\n");
    CHECK(!load_window_size(dir + "/launcher-window.ini", &w, &h));
}

// A host preset: governs internal resolution, AA and dynamic resolution only.
static void fake_apply(int preset, RecompLauncherCSettings* s) {
    s->internal_resolution = preset * 2;
    s->antialiasing = preset >= 3 ? 2 : 0;
    s->dynamic_resolution = preset <= 2;
}

static void test_quality() {
    QualityTracker q;
    q.bind(fake_apply, 0xF);
    RecompLauncherCSettings s{};
    s.fullscreen = 1;
    q.select(kQualityHigh, &s);
    CHECK(s.quality_preset == kQualityHigh && s.quality_base == kQualityHigh);
    CHECK(s.internal_resolution == 6 && s.antialiasing == 2);
    CHECK(q.governed_bytes() == 3 * sizeof(int));
    CHECK(!q.observe(&s));
    s.fullscreen = 0;  // ungoverned row
    CHECK(!q.observe(&s) && s.quality_preset == kQualityHigh);
    s.antialiasing = 0;  // governed row -> Custom
    CHECK(q.observe(&s));
    CHECK(s.quality_preset == kQualityCustom && s.quality_base == kQualityHigh);
    q.select(kQualityLow, &s);
    CHECK(s.quality_preset == kQualityLow && s.internal_resolution == 2 && s.dynamic_resolution == 1);
    // Re-open: saved settings already deviate from the preset -> Custom.
    RecompLauncherCSettings saved = s;
    saved.internal_resolution = 99;
    QualityTracker q2;
    q2.bind(fake_apply, 0xF);
    q2.adopt(saved);
    CHECK(q2.observe(&saved) && saved.quality_preset == kQualityCustom);
    // Not offered: nothing happens.
    QualityTracker off;
    off.bind(fake_apply, 0);
    RecompLauncherCSettings t{};
    off.select(kQualityUltra, &t);
    CHECK(t.quality_preset == 0);
    CHECK(std::string(quality_name(5)) == "Custom");
}

// Netplay handoff with a fake host.
static int f_ingest(void*, const char* json, char* why, size_t cap) {
    if (std::strstr(json, "\"session\"")) return 1;
    std::snprintf(why, cap, "bad record");
    return 0;
}
static int f_fill(void*, RecompLauncherCNetplayLaunch* l) {
    l->enabled = 1;
    l->local_slot = 1;
    l->max_slots = 4;
    return 1;
}
static void test_handoff(const std::string& dir) {
    RecompLauncherCNetplayCallbacks np{};
    np.ingest_launch = f_ingest;
    np.fill_launch = f_fill;
    RecompLauncherCGameInfo gi{};
    gi.netplay = &np;
    RecompLauncherCSettings s{};
    char out[256] = {0};
    CHECK(run_netplay_handoff(&s, &gi, "/d/r4.cue", out, sizeof(out)) == -1);
    const std::string rec = dir + "/launch.json";
    spit(rec, "{\"session\":1}");
    setenv("RECOMP_NETPLAY_LAUNCH", rec.c_str(), 1);
    CHECK(run_netplay_handoff(&s, &gi, "/d/r4.cue", out, sizeof(out)) == RECOMP_LAUNCHER_RESULT_LAUNCH);
    CHECK(std::getenv("RECOMP_NETPLAY_LAUNCH") == nullptr);
    CHECK(s.netplay_launch.enabled == 1 && s.netplay_launch.local_slot == 1);
    CHECK(std::string(out) == "/d/r4.cue");
    CHECK(slurp(rec + ".status") == "{\"ok\":true}\n");
    spit(rec, "{}");
    setenv("RECOMP_NETPLAY_LAUNCH", rec.c_str(), 1);
    CHECK(run_netplay_handoff(&s, &gi, "/d/r4.cue", out, sizeof(out)) == RECOMP_LAUNCHER_RESULT_QUIT);
    CHECK(slurp(rec + ".status").find("bad record") != std::string::npos);
}

static void test_json_and_skin(const std::string& skins, const std::string& dir) {
    Json j;
    std::string err;
    CHECK(parse_json("{ // c\n \"a\": [1, 2,], /* x */ \"b\": {\"c\": \"#fff\"}, }", &j, &err));
    CHECK(j["a"].size() == 2 && j["a"].at(1).num(0) == 2);
    CHECK(!parse_json("{\n\"a\": }", &j, &err) && err.find("line 2") == 0);
    Color c;
    CHECK(parse_color("#ff000080", &c) && c.r == 1 && c.a > 0.49f && c.a < 0.51f);
    CHECK(!parse_color("red", &c));
    // Resolution independence: the same spec at 4:3, 16:9, 16:10 and Deck.
    RectSpec r;
    r.present = true;
    r.anchor = "bottom-right";
    r.x.v = 10; r.y.v = 20; r.w.v = 100; r.h.v = 50;
    for (auto vpwh : {std::pair<float, float>{960, 720}, {1280, 720}, {1280, 800}, {2560, 1440}}) {
        Viewport vp;
        vp.w = vpwh.first; vp.h = vpwh.second;
        const Rect o = resolve(r, vp);
        const float s = vp.h / 720.0f;
        CHECK(std::fabs(o.w - 100 * s) < 0.01f && std::fabs(o.x + o.w + 10 * s - vp.w) < 0.01f);
        CHECK(std::fabs(o.y + o.h + 20 * s - vp.h) < 0.01f);
    }
    RectSpec f;
    f.present = true;
    f.x.v = 230; f.w.kind = Length::Fill; f.h.kind = Length::Fill; f.h.v = 46;
    Viewport vp; vp.w = 1280; vp.h = 720;
    const Rect fo = resolve(f, vp);
    CHECK(fo.x == 230 && fo.w == 1050 && fo.h == 674);
    // Shipped skins parse, and R4 inherits the default's fonts.
    SkinModel d, r4;
    CHECK(load_skin_model(skins + "/default", &d, &err));
    CHECK(load_skin_model(skins + "/r4", &r4, &err));
    CHECK(r4.name == "R4" && !r4.fonts["nav"].ttf.empty() && r4.fonts["heading"].uppercase);
    CHECK(r4.layout.count("rail") && r4.background.size() >= 3 && r4.sounds.count("move"));
    CHECK(r4.label("nav.Home", "Play") == "Race");
    // A broken skin reports the line; extends cycle depth is bounded.
    std::string bad = dir + "/bad";
    mkdir(bad.c_str(), 0755);
    spit(bad + "/skin.json", "{\n \"name\": \"x\",\n \"palette\": { \"bg\" }\n}");
    SkinModel b;
    CHECK(!load_skin_model(bad, &b, &err) && err.find("line 3") != std::string::npos);
    spit(bad + "/skin.json", "{ \"extends\": \".\" }");
    CHECK(!load_skin_model(bad, &b, &err));
}

static void test_mod_archive(const std::string& dir) {
    std::string err;
    spit(dir + "/notzip.psxmod", "hello");
    CHECK(!zip_has_manifest(dir + "/notzip.psxmod", &err) && err.find("Not a mod archive") == 0);
    CHECK(!zip_has_manifest(dir + "/missing.psxmod", &err));
    // Minimal stored zip with pkg/1.0.0/manifest.toml, then one without.
    auto zip = [&](const std::string& path, const std::string& name) {
        std::string d = "id = \"x\"\n", z;
        auto le16 = [&](unsigned v) { z += char(v & 255); z += char(v >> 8); };
        auto le32 = [&](unsigned long v) { le16(v & 0xffff); le16(v >> 16); };
        z += "PK"; z += char(3); z += char(4);
        le16(10); le16(0); le16(0); le16(0); le16(0); le32(0); le32(d.size()); le32(d.size());
        le16(name.size()); le16(0); z += name; z += d;
        spit(path, z);
    };
    zip(dir + "/good.psxmod", "pkg/1.0.0/manifest.toml");
    CHECK(zip_has_manifest(dir + "/good.psxmod", &err));
    zip(dir + "/bad.psxmod", "pkg/readme.txt");
    CHECK(!zip_has_manifest(dir + "/bad.psxmod", &err) && err == "The archive has no manifest.toml");
}

static void test_surface() {
    // Default rule: essentials shown, advanced hidden.
    Surface none;
    CHECK(none.shown("graphics.preset") && none.shown("disc.setup") && none.shown("netplay.host"));
    CHECK(!none.shown("graphics.renderer") && !none.shown("netplay.automatch") && !none.shown("audio.frequency"));
    CHECK(none.vis("graphics.renderer") == Vis::Hidden && none.vis("controls.bindings") == Vis::Shown);
    // Every catalog key resolves; unknown keys are reported, not crashed on.
    for (const auto& c : capabilities()) CHECK(none.vis(c.key) != Vis::Default);
    static const SurfaceRule rules[] = {
        {"graphics.renderer", Vis::Shown, nullptr},
        {"graphics.preset", Vis::Locked, "3"},
        {"graphics.dynamic_resolution", Vis::Auto, "1"},
        {"graphics.vsync", Vis::Auto, "auto"},
        {"netplay.host", Vis::Hidden, nullptr},
        {"no.such.key", Vis::Shown, nullptr},
    };
    Surface t;
    t.bind(rules, 6);
    CHECK(t.shown("graphics.renderer") && t.locked("graphics.preset") && t.shown("graphics.preset"));
    CHECK(t.automatic("graphics.dynamic_resolution") && !t.shown("graphics.dynamic_resolution"));
    CHECK(!t.shown("netplay.host") && t.any_shown("netplay.") && !t.any_shown("audio.freq"));
    CHECK(t.unknown_keys().size() == 1 && t.unknown_keys()[0] == "no.such.key");
    RecompLauncherCSettings io{};
    io.vsync = 2;
    CHECK(t.apply(&io) == 2);  // preset + dynres; "auto" leaves vsync alone
    CHECK(io.quality_preset == 3 && io.dynamic_resolution == 1 && io.vsync == 2);
    t.set_show_all(true);
    CHECK(t.shown("netplay.host") && !t.locked("graphics.preset"));
}

static void test_discscan(const std::string& dir) {
    const std::string root = dir + "/scan";
    mkdir(root.c_str(), 0755);
    mkdir((root + "/a").c_str(), 0755);
    mkdir((root + "/a/b").c_str(), 0755);
    spit(root + "/a/b/R4.cue", "FILE \"R4.bin\" BINARY\n");
    spit(root + "/a/other.CHD", "x");
    spit(root + "/a/readme.txt", "x");
    auto found = scan_disc_images({root}, 4);
    CHECK(found.size() == 2);
    CHECK(scan_disc_images({root}, 1).size() == 1);  // depth limit
    CHECK(find_disc({root}, [](const std::string& p) { return p.find("R4.cue") != std::string::npos; }) ==
          root + "/a/b/R4.cue");
    CHECK(find_disc({root}, [](const std::string&) { return false; }).empty());
}

static void test_tim_and_font(const std::string& dir) {
    // A 4-bit TIM, 16x2 pixels, CLUT {transparent, white}: two glyph columns.
    std::vector<uint8_t> d;
    auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto le16 = [&](uint16_t v) { d.push_back(v & 255); d.push_back(v >> 8); };
    le32(0x10); le32(0x8);
    le32(12 + 16 * 2); le16(0); le16(0); le16(16); le16(1);
    le16(0); le16(0x7fff); for (int i = 2; i < 16; ++i) le16(0);
    le32(12 + 4 * 2 * 2); le16(0); le16(0); le16(4); le16(2);  // 4 halfwords wide = 16 px
    // row: px 0-1 white, 2-4 clear, 5-6 white, rest clear
    const uint8_t row[8] = {0x11, 0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00};
    for (int r = 0; r < 2; ++r) d.insert(d.end(), row, row + 8);
    Rgba img;
    CHECK(decode_tim(d, 0, &img) && img.w == 16 && img.h == 2);
    CHECK(img.px[3] == 255 && img.px[2 * 4 + 3] == 0 && img.px[5 * 4] == 248);
    CHECK(!decode_tim(d, 4, &img));
    Rgba c = crop(img, 5, 0, 2, 2);
    CHECK(c.w == 2 && c.px[3] == 255);
    tint(&c, 10, 20, 30);
    CHECK(c.px[0] == 10 && c.px[2] == 30 && c.px[3] == 255);
    CHECK(scale_nearest(c, 3).w == 6);
    std::string err;
    CHECK(!write_bitmap_font(img, {{{0, 2}, "AB"}}, dir, "f", 2, &err));  // under 10 glyphs: refused
    Rgba wide;
    wide.w = 60; wide.h = 4; wide.px.assign(60 * 4 * 4, 0);
    for (int g = 0; g < 12; ++g) for (int y = 0; y < 4; ++y) wide.px[(y * 60 + g * 5) * 4 + 3] = 255;
    CHECK(write_bitmap_font(wide, {{{0, 4}, "0123456789AB"}}, dir, "f", 4, &err));
    const std::string fnt = slurp(dir + "/f.fnt");
    CHECK(fnt.find("char id=48 x=0 ") != std::string::npos && fnt.find("char id=66 x=55 ") != std::string::npos);
}

static void test_memcard(const std::string& dir) {
    const std::string p = dir + "/card.mcd";
    CHECK(format_memcard(p));
    const std::string m = slurp(p);
    CHECK(m.size() == 131072 && m[0] == 'M' && m[1] == 'C');
    unsigned char x = 0;
    for (int i = 0; i < 127; ++i) x ^= static_cast<unsigned char>(m[128 + i]);
    CHECK(static_cast<unsigned char>(m[128]) == 0xA0 && x == static_cast<unsigned char>(m[255]));
    CHECK(m.compare(63 * 128, 128, m, 0, 128) == 0);
}

int main(int argc, char** argv) {
    const std::string dir = tmpdir();
    test_ini_preserves_foreign_lines();
    test_keybinds_roundtrip(dir);
    test_input_ini_roundtrip(dir);
    test_hotkeys(dir);
    test_sidecars_and_window(dir);
    test_quality();
    test_handoff(dir);
    test_mod_archive(dir);
    test_surface();
    test_discscan(dir);
    test_tim_and_font(dir);
    test_memcard(dir);
    test_json_and_skin(argc > 1 ? argv[1] : "assets/skins", dir);
    std::printf("r4l-core-tests: %s (%d failures)\n", g_fail ? "FAIL" : "ok", g_fail);
    return g_fail ? 1 : 0;
}
