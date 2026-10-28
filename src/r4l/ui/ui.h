// ui.h — shared UI state and widgets for the launcher screens.
#pragma once

#include "r4l/core/mods.h"
#include "r4l/core/session.h"
#include "r4l/title.h"

#include "imgui.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct SDL_Window;
struct RecompOverlayHost;
union SDL_Event;

namespace r4l {

struct App;

enum class Screen { Home, Graphics, Mods, Controls, Netplay, Setup, About, Resume, QuitGame, System, Count };
enum class Mode { Launcher, Overlay };
const char* screen_name(Screen s);

// Design tokens (see docs/DESIGN.md, "Visual design system").
struct Theme {
    ImVec4 bg, surface, surface_hi, line, text, text_dim, accent, accent2, ok, warn, bad;
    float radius = 10.0f;
    float row_h = 44.0f;  // gamepad-sized rows
    ImFont* body = nullptr;
    ImFont* bold = nullptr;
    float body_size = 18.0f;
};
const Theme& theme();
void apply_theme(const TitleLayer& t, float ui_scale);
void apply_skin_theme();
std::vector<std::string> list_skins(const std::string& assets_dir);
bool load_skin_named(App& app, const std::string& name_or_path);

// Input capture for rebinding (keyboard, gamepad button, axis or combo).
struct Capture {
    enum Kind { None, Key, PadSource, PadValue, HostKey } kind = None;
    bool map_all = false;  // walk every input in order (Map All)
    int player = 0;
    int input = 0;      // kPsxInputs index or assist slot
    bool alt = false;   // keyboard alternate column
    unsigned combo = 0; // held gamepad buttons (PadValue)
    double started = 0;
};

// Async host job (prepare / rebuild) with progress.
struct Job {
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> done{false};
    std::atomic<int> rc{0};
    std::mutex mu;
    float pct = 0;
    std::string message, error, out_path;
    void join() {
        if (worker.joinable()) worker.join();
    }
};

struct App {
    Session s;
    ModCatalog mods;
    const TitleLayer* title = nullptr;
    Screen screen = Screen::Home;
    Mode mode = Mode::Launcher;
    const RecompOverlayHost* host = nullptr;   // overlay mode only
    RecompLauncherCSettings opened{};          // settings when the overlay opened
    RecompLauncherCSettings applied{};         // last state pushed to the host
    unsigned applied_bits = 0;                 // host's answer: RECOMP_OVERLAY_RESTART_*
    bool overlay_open = false;
    bool netplay_locked = false;               // overlay during netplay: no pause, local-view only
    bool in_game = false;                      // launcher.in_game: no pre-game launcher
    bool setup_only = false;                   // in-game first run: disc setup, then play
    bool mods_restart = false;                 // overlay: a mod change waits for a restart
    bool exit_requested = false;               // overlay: request_exit already sent
    Screen last_focus_screen = Screen::Count;
    int last_nav_hover = -1;
    std::string skin_dir;
    std::string pending_install;  // set by the file dialog (any thread), under job.mu
    float ui_scale = 0;
    Screen focus_request = Screen::Count;  // move nav focus once
    bool content_focus = false;                // Right from the rail: focus the page's first control
    bool quit_confirm = false;                 // Quit game pressed; the Yes/No dialog opens
    Capture capture;
    Job job;
    double time = 0;
    bool game_mode = false;      // gamescope / Steam Deck session
    bool want_quit = false;
    int controls_player = 0;
    int selected_feature = -1;
    // Netplay screen state
    char np_lobby_name[64] = "R4 Grand Prix";
    char np_password[32] = "";
    char np_join_code[64] = "";
    char np_address[96] = "";
    char np_chat[200] = "";
    int np_seats = 2;
    bool np_lan_only = false;
    bool np_connected_once = false;
    std::string np_status;
    bool np_login_required = false;  // the lobby server refused a guest (DISCORD_REQUIRED)
    char np_url[160] = "";
    char np_handle[64] = "";
    char np_server_chat[200] = "";
    char np_report_note[200] = "";
    int np_list_scope = 0;
    int np_report_reason = 0;
    std::string np_report_target, np_join_lobby;  // lobby needing a password
    bool np_show_password = false, np_show_report = false, np_show_network = false, np_show_automatch = false;
    std::vector<std::string> np_blocked;          // moderation.ini accounts
    char mods_search[64] = "";
    bool mods_packages_view = false;
    int selected_package = -1;
    // Surface manifest (title.h): what this title shows, hides, locks.
    Surface surf;
    bool S(const char* key) const { return surf.shown(key); }
    bool L(const char* key) const { return surf.locked(key); }
    RecompLauncherCSettings defaults{};          // for Restore defaults
    bool have_defaults = false;
    std::string toolchain_note;
    int job_kind = 0;
    bool assets_checked = false;

    // Lifecycle
    void begin(RecompLauncherCSettings* io, const RecompLauncherCGameInfo* game,
               const char* assets_dir, const char* initial_rom);
    void init_skin();                      // after the ImGui context exists
    void frame();                          // build one ImGui frame
    bool handle_event(const SDL_Event& e); // true = swallowed (capture)
    void set_pad_source(const std::string& src);
    void request_launch();
    void request_quit();

    // Screens
    void draw_nav();
    void draw_home();
    void draw_graphics();
    void draw_mods();
    void draw_controls();
    void draw_netplay();
    void draw_setup();
    void draw_about();
    void draw_system();     // audio, system, memory cards, BIOS, hotkeys
    void draw_memcards();
    void ensure_disc_assets();
    void autoscan_disc();
    void start_job(int kind);  // 0 prepare, 1 rebuild, 2 toolchain, 3 pgo, 4 fmv timing, 5 bios
    void restore_defaults();
    void draw_footer();
    void draw_capture_modal();
    void draw_quit_confirm();              // Quit game: Yes / No
    void overlay_tick();                   // live-apply + resume handling
    bool restart_needed(size_t offset, size_t size) const;
    void restart_chip(size_t offset, size_t size);
};

// Translation (assets/i18n/<lang>.json); falls back to the English text.
const char* tr(const char* english);
void load_language(const std::string& assets_dir, const std::string& lang);

std::string patterns_of(const char* const* pats, int n, const char* fallback);

// Widgets
void screen_body_end();  // closes the scrolling body screen_title opened
void screen_title(const char* title, const char* sub = nullptr);
bool section(const char* label);
// Menu column: while active, the row_* widgets draw as centred "Label: Value"
// boxes (x, w, h in pixels) instead of label-left / control-right rows.
void kv_column_begin(float x, float w, float h);
void kv_column_end();
bool kv_column_active();
bool row_combo(const char* label, int* v, const char* const* items, int count, const char* help = nullptr);
bool row_toggle(const char* label, int* v, const char* help = nullptr);
bool row_slider(const char* label, int* v, int lo, int hi, const char* fmt, const char* help = nullptr);
void row_text(const char* label, const char* value, ImVec4 color);
bool big_button(const char* label, ImVec2 size, bool primary);
void chip(const char* text, ImVec4 color);
void help_marker(const char* text);

}  // namespace r4l
