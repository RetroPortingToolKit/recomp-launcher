// recomp_launcher_overlay.h — in-game pause/options overlay (proposed ABI).
//
// The same menus as the launcher, summoned over a running game. The runtime
// owns the window, the GL context and the game loop; the overlay owns an
// ImGui context on that GL context and draws after the game frame on the
// thread that presents (docs/DESIGN.md, "In-game overlay").
//
// psxrecomp reaches it through its provider-neutral in-game menu contract
// (psx_menu_host.h): src/r4l/psx_menu_provider.cpp adapts this overlay to
// PsxMenuProvider, and a title opts in with psx_menu_set_provider().
#ifndef RECOMP_LAUNCHER_OVERLAY_H
#define RECOMP_LAUNCHER_OVERLAY_H

#include "recomp_launcher.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RECOMP_OVERLAY_ABI_VERSION 2  /* 2: disc_* (the runtime tray policy) */

/* Bits returned by apply_settings: which edits only take effect after a
 * restart (the overlay labels those rows). */
#define RECOMP_OVERLAY_RESTART_RENDERER            (1u << 0)
#define RECOMP_OVERLAY_RESTART_INTERNAL_RESOLUTION (1u << 1)
#define RECOMP_OVERLAY_RESTART_THREADS             (1u << 2)
#define RECOMP_OVERLAY_RESTART_OTHER               (1u << 31)

typedef struct RecompOverlayHost {
    int   abi_version;   /* RECOMP_OVERLAY_ABI_VERSION */
    void* ctx;
    /* Pause (1) / resume (0) emulation and audio. Returns 0 when refused,
     * e.g. during netplay; the overlay then stays non-pausing. */
    int  (*set_paused)(void* ctx, int paused);
    int  (*netplay_active)(void* ctx);
    /* Live-apply the [video]/[audio]/input fields of *s. Returns the
     * RECOMP_OVERLAY_RESTART_* bits for edits that need a restart. Also
     * persists settings.toml the same way the launcher's QUIT path does. */
    unsigned (*apply_settings)(void* ctx, const RecompLauncherCSettings* s);
    /* Live mod toggle/option. Returns 1 if applied now, 0 if it needs a
     * restart (the choice is still saved through the mod provider). */
    int  (*mod_set_live)(void* ctx, const char* package_id, const char* feature_id,
                         const char* option_id /* NULL = enable flag */, const char* value);
    /* Reload keybinds.ini/input.ini/config.ini after the overlay wrote them. */
    void (*reload_bindings)(void* ctx);
    /* Leave the running game. how: 0 quit; 1 end the session and return to
     * the launcher loop in-process (recomp_launcher_run_window runs again,
     * e.g. to start a match the in-game lobby set up); 2 relaunch this
     * process with the same arguments (restart-only settings). */
    void (*request_exit)(void* ctx, int how);
    /* v2: multi-disc swap through the runtime's tray policy (lid cycle,
     * netplay refusal, persistence). count is 0 for single-disc titles or
     * runtimes without the policy; insert returns 0 on success. */
    int  (*disc_count)(void* ctx);
    int  (*disc_mounted)(void* ctx);              /* 1-based, 0 = none */
    const char* (*disc_path)(void* ctx, int disc_1based);
    int  (*disc_insert)(void* ctx, int disc_1based);
} RecompOverlayHost;

/* Call once on the GL thread with the runtime's window and current context. */
int  recomp_overlay_init(void* sdl_window, void* gl_context,
                         const RecompLauncherCGameInfo* game, RecompLauncherCSettings* io,
                         const RecompOverlayHost* host, const char* assets_dir);
/* Feed every SDL event (main thread). Returns 1 when the overlay consumed it
 * (the runtime must then not pass it to the game). Opens the overlay on Esc,
 * Guide, or the configured pad combo (default Start+Select). */
int  recomp_overlay_handle_event(const void* sdl_event);
int  recomp_overlay_is_open(void);
void recomp_overlay_set_open(int open);
/* Draw over the game frame into the currently bound framebuffer. */
void recomp_overlay_render(int framebuffer_w, int framebuffer_h);
void recomp_overlay_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
