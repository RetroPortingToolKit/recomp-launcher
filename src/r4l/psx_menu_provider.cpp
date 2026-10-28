// psx_menu_provider.cpp — recomp-launcher as a psxrecomp in-game menu provider.
//
// psxrecomp owns the contract (runtime/include/psx_menu_host.h): the runtime
// keeps every policy (pause, live settings, mod changes, disc/tray, netplay
// session start and exit); a provider only draws and asks. This file adapts
// the launcher's overlay (recomp_launcher_overlay.h) to PsxMenuProvider and
// the runtime's PsxMenuHost back to RecompOverlayHost. It is optional: a
// title selects it with
//     psx_menu_set_provider(recomp_launcher_psx_menu_provider());
// and nothing is registered otherwise. Built only when the runtime's include
// path has psx_menu_host.h (inside a psxrecomp title build).
#if defined(__has_include)
#if __has_include("psx_menu_host.h")
#define R4L_HAS_PSX_MENU_HOST 1
#endif
#endif

#if defined(R4L_HAS_PSX_MENU_HOST)
#include "psx_menu_host.h"
#include "recomp_launcher_overlay.h"

#include <cstring>

namespace {

const PsxMenuHost* g_host = nullptr;
RecompLauncherCGameInfo g_game{};
RecompLauncherCSettings g_settings{};

int oh_set_paused(void*, int p) { return g_host && g_host->set_paused ? g_host->set_paused(p) : 0; }
int oh_netplay_active(void*) { return g_host && g_host->netplay_active ? g_host->netplay_active() : 0; }
unsigned oh_apply_settings(void*, const RecompLauncherCSettings* s) {
    if (!g_host || !g_host->launcher_settings_apply || !s) return 0;
    const unsigned b = g_host->launcher_settings_apply(s);
    unsigned out = 0;
    if (b & PSX_VIDEO_RESTART_RENDERER) out |= RECOMP_OVERLAY_RESTART_RENDERER;
    if (b & PSX_VIDEO_RESTART_RESOLUTION) out |= RECOMP_OVERLAY_RESTART_INTERNAL_RESOLUTION;
    if (b & PSX_VIDEO_RESTART_THREADS) out |= RECOMP_OVERLAY_RESTART_THREADS;
    if (b & PSX_MENU_RESTART_AFTER_MATCH) out |= RECOMP_OVERLAY_RESTART_OTHER;
    return out;
}
int oh_mod_set(void*, const char* p, const char* f, const char* o, const char* v) {
    return g_host && g_host->mod_set ? g_host->mod_set(p, f, o, v) : 0;
}
void oh_reload_bindings(void*) { if (g_host && g_host->reload_bindings) g_host->reload_bindings(); }
void oh_request_exit(void*, int how) {
    if (!g_host || !g_host->request_exit) return;
    g_host->request_exit(how == 1 ? PSX_MENU_EXIT_TO_FRONTEND : how == 2 ? PSX_MENU_EXIT_RELAUNCH : PSX_MENU_EXIT_QUIT);
}
int oh_disc_count(void*) { return g_host && g_host->disc_count ? g_host->disc_count() : 0; }
int oh_disc_mounted(void*) { return g_host && g_host->disc_mounted ? g_host->disc_mounted() : 0; }
const char* oh_disc_path(void*, int n) { return g_host && g_host->disc_path ? g_host->disc_path(n) : nullptr; }
int oh_disc_insert(void*, int n) { return g_host && g_host->disc_insert ? g_host->disc_insert(n) : -1; }

#if defined(PSX_MENU_HOST_HAS_SAVE_STATES)
int oh_state_slot_count(void*) { return g_host && g_host->state_slot_count ? g_host->state_slot_count() : 0; }
int oh_state_available(void*) { return g_host && g_host->state_available ? g_host->state_available() : 0; }
int oh_state_info(void*, int n, long long* t) { return g_host && g_host->state_info ? g_host->state_info(n, t) : 0; }
int oh_state_thumb(void*, int n, unsigned* px, int w, int h) {
    return g_host && g_host->state_thumb ? g_host->state_thumb(n, px, w, h) : 0;
}
int oh_state_save(void*, int n) { return g_host && g_host->state_save ? g_host->state_save(n) : 0; }
int oh_state_load(void*, int n) { return g_host && g_host->state_load ? g_host->state_load(n) : 0; }
#endif

const RecompOverlayHost g_overlay_host = {
    RECOMP_OVERLAY_ABI_VERSION, nullptr, oh_set_paused, oh_netplay_active, oh_apply_settings,
    oh_mod_set, oh_reload_bindings, oh_request_exit,
    oh_disc_count, oh_disc_mounted, oh_disc_path, oh_disc_insert,
#if defined(PSX_MENU_HOST_HAS_SAVE_STATES)
    oh_state_slot_count, oh_state_available, oh_state_info, oh_state_thumb, oh_state_save, oh_state_load,
#else
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
#endif
};

int p_attach(void*, void* window, void* gl, const PsxMenuHost* host) {
    if (!host || host->abi_version != PSX_MENU_ABI_VERSION || !host->launcher_game_info ||
        !host->launcher_settings_get)
        return 0;  // this provider speaks the shared launcher ABI
    g_host = host;
    std::memset(&g_game, 0, sizeof g_game);
    std::memset(&g_settings, 0, sizeof g_settings);
    host->launcher_game_info(&g_game);
    host->launcher_settings_get(&g_settings);
    return recomp_overlay_init(window, gl, &g_game, &g_settings, &g_overlay_host, host->assets_dir);
}
int p_event(void*, const void* ev) { return recomp_overlay_handle_event(ev); }
int p_is_open(void*) { return recomp_overlay_is_open(); }
void p_set_open(void*, int open) { recomp_overlay_set_open(open); }
void p_draw(void*, int w, int h) { recomp_overlay_render(w, h); }
void p_detach(void*) {
    recomp_overlay_shutdown();
    g_host = nullptr;
}

const PsxMenuProvider g_provider = {
    PSX_MENU_ABI_VERSION, "recomp-launcher", nullptr,
    p_attach, p_event, p_is_open, p_set_open, p_draw, p_detach,
};

}  // namespace

extern "C" const PsxMenuProvider* recomp_launcher_psx_menu_provider(void) { return &g_provider; }

#endif  // R4L_HAS_PSX_MENU_HOST
