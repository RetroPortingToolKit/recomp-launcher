// overlay_entry.cpp — recomp_launcher_overlay.h: the launcher's menus over a
// running game, on the runtime's own window and GL context.
#include "recomp_launcher_overlay.h"

#include "r4l/ui/platform.h"
#include "r4l/ui/script.h"
#include "r4l/ui/skin.h"
#include "r4l/ui/ui.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace {
using namespace r4l;

// Threads: recomp_overlay_handle_event / set_open / is_open run on the host's
// main thread; recomp_overlay_render runs on whichever thread owns the GL
// context (psxrecomp's render thread when it is on). The App, ImGui and every
// GL object are touched only from render(); the main thread only flips atomics
// and queues raw SDL events.
struct Overlay {
    std::unique_ptr<App> app;
    ImGuiContext* ctx = nullptr;
    SDL_Window* window = nullptr;
    const RecompOverlayHost* host = nullptr;
    std::string assets;
    bool gl_ready = false;
    std::string disc_at_open;           // render thread
    std::unique_ptr<Script> script;     // R4L_OVERLAY_SCRIPT (tests): drives the in-game menu
    Platform* script_plat = nullptr;    // the host's window, for the script's events
    bool script_done = false;
    std::atomic<bool> open{false};
    std::atomic<bool> opening{false};   // main asked to open; render applies
    std::atomic<bool> closing{false};   // render asked to close; main applies
    unsigned pad_held = 0;              // main thread
    unsigned combo = (1u << SDL_GAMEPAD_BUTTON_START) | (1u << SDL_GAMEPAD_BUTTON_BACK);
    std::mutex mu;
    std::vector<SDL_Event> events;      // main -> render
    uint64_t last_ns = 0;
};
Overlay* g_ov = nullptr;
std::unique_ptr<Script> g_carry_script;  // R4L_OVERLAY_SCRIPT across a provider detach/attach (tests)

ImGuiKey imgui_key(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP: return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN: return ImGuiKey_DownArrow;
    case SDL_SCANCODE_LEFT: return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT: return ImGuiKey_RightArrow;
    case SDL_SCANCODE_RETURN: return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_SPACE: return ImGuiKey_Space;
    case SDL_SCANCODE_ESCAPE: return ImGuiKey_Escape;
    case SDL_SCANCODE_TAB: return ImGuiKey_Tab;
    case SDL_SCANCODE_BACKSPACE: return ImGuiKey_Backspace;
    case SDL_SCANCODE_DELETE: return ImGuiKey_Delete;
    case SDL_SCANCODE_HOME: return ImGuiKey_Home;
    case SDL_SCANCODE_END: return ImGuiKey_End;
    case SDL_SCANCODE_PAGEUP: return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN: return ImGuiKey_PageDown;
    case SDL_SCANCODE_LSHIFT: return ImGuiKey_LeftShift;
    case SDL_SCANCODE_RSHIFT: return ImGuiKey_RightShift;
    case SDL_SCANCODE_LCTRL: return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_RCTRL: return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_Q: return ImGuiKey_Q;
    case SDL_SCANCODE_E: return ImGuiKey_E;
    default: return ImGuiKey_None;
    }
}

ImGuiKey imgui_pad(Uint8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

// Render thread: feed one queued event to the app (rebind capture) or ImGui.
void feed(App& a, const SDL_Event& e) {
    if (a.handle_event(e)) return;
    ImGuiIO& io = ImGui::GetIO();
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = e.type == SDL_EVENT_KEY_DOWN;
        io.AddKeyEvent(ImGuiMod_Shift, (e.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Ctrl, (e.key.mod & SDL_KMOD_CTRL) != 0);
        const ImGuiKey k = imgui_key(e.key.scancode);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
        break;
    }
    case SDL_EVENT_TEXT_INPUT: io.AddInputCharactersUTF8(e.text.text); break;
    case SDL_EVENT_MOUSE_MOTION: io.AddMousePosEvent(e.motion.x, e.motion.y); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        io.AddMousePosEvent(e.button.x, e.button.y);
        if (e.button.button >= 1 && e.button.button <= 3)
            io.AddMouseButtonEvent(e.button.button == 1 ? 0 : e.button.button == 3 ? 1 : 2,
                                   e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    case SDL_EVENT_MOUSE_WHEEL: io.AddMouseWheelEvent(e.wheel.x, e.wheel.y); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const ImGuiKey k = imgui_pad(e.gbutton.button);
        if (k != ImGuiKey_None) io.AddKeyEvent(k, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp, e.gaxis.value < -16000, 1.0f);
            io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown, e.gaxis.value > 16000, 1.0f);
        }
        break;
    default: break;
    }
}

// Main thread.
void request_open(bool open) {
    Overlay& o = *g_ov;
    if (open == o.open.load()) return;
    if (open) {
        const bool netplay = o.host && o.host->netplay_active && o.host->netplay_active(o.host->ctx);
        if (!netplay && o.host && o.host->set_paused) o.host->set_paused(o.host->ctx, 1);
        o.opening = true;
        o.open = true;
    } else {
        o.closing = true;
    }
}

// Render thread, inside the overlay's ImGui context.
void apply_transitions(Overlay& o) {
    App& a = *o.app;
    if (o.opening.exchange(false)) {
        a.netplay_locked = o.host && o.host->netplay_active && o.host->netplay_active(o.host->ctx);
        a.overlay_open = true;
        a.screen = Screen::Graphics;
        a.focus_request = Screen::Graphics;
        if (a.s.io) a.opened = a.applied = *a.s.io;
        o.disc_at_open = a.s.primary_disc();
        a.exit_requested = false;
        skin().play("confirm");
    }
    if (o.closing.exchange(false) || !a.overlay_open) {
        std::string err;
        a.s.commit_files(&err);
        if (a.s.binds_dirty == false && o.host && o.host->reload_bindings) o.host->reload_bindings(o.host->ctx);
        if (o.host && o.host->set_paused) o.host->set_paused(o.host->ctx, 0);
        a.overlay_open = false;
        o.open = false;
        skin().play("back");
        // In-game launcher: edits that only take effect at start-up are
        // applied by a quick relaunch when the menu closes.
        if (a.in_game && !a.netplay_locked && !a.exit_requested && o.host && o.host->request_exit) {
            const bool disc = a.s.primary_disc() != o.disc_at_open;
            if (disc) a.s.persist_media(&err);
            if (a.applied_bits || a.mods_restart || disc) {
                std::fprintf(stdout, "r4l: relaunching for restart-only changes\n");
                a.exit_requested = true;
                o.host->request_exit(o.host->ctx, 2);
            }
        }
    }
}

bool gl_init(Overlay& o) {
    if (o.gl_ready) return true;
    ImGui_ImplOpenGL3_Init("#version 330");
    App& a = *o.app;
    Theme& th = const_cast<Theme&>(theme());
    th.body = ImGui::GetIO().Fonts->AddFontDefault();
    th.bold = th.body;
    apply_theme(*a.title, 1.0f);
    a.init_skin();  // textures: needs the GL context, so here, not in init
    o.gl_ready = true;
    return true;
}
}  // namespace

extern "C" int recomp_overlay_init(void* sdl_window, void*, const RecompLauncherCGameInfo* game,
                                   RecompLauncherCSettings* io, const RecompOverlayHost* host,
                                   const char* assets_dir) {
    if (g_ov) return 1;
    g_ov = new Overlay();
    g_ov->window = static_cast<SDL_Window*>(sdl_window);
    g_ov->host = host;
    g_ov->assets = resolve_assets_dir(assets_dir);
    g_ov->app = std::make_unique<App>();
    App& a = *g_ov->app;
    a.mode = Mode::Overlay;
    a.host = host;
    a.begin(io, game, g_ov->assets.c_str(), nullptr);
    if (host && host->mod_set_live)
        a.mods.live = [host, &a](const char* p, const char* f, const char* o, const char* v) {
            const int now = host->mod_set_live(host->ctx, p, f, o, v);
            if (std::getenv("R4L_SCRIPT_DEBUG"))
                std::fprintf(stderr, "[r4l] mod %s/%s %s=%s -> %s\n", p, f, o ? o : "(enabled)", v, now ? "live" : "restart");
            if (!now) a.mods_restart = true;  // saved; applies on the next start
            return now;
        };
    if (g_carry_script) {  // a session ended mid-script: continue on the new window
        g_ov->script = std::move(g_carry_script);
        g_ov->script_plat = new Platform();
        g_ov->script_plat->window = g_ov->window;
    } else if (const char* sp = std::getenv("R4L_OVERLAY_SCRIPT")) {
        g_ov->script = std::make_unique<Script>();
        if (g_ov->script->load(sp)) {
            g_ov->script_plat = new Platform();  // never closed: the window is the host's
            g_ov->script_plat->window = g_ov->window;
#if defined(_WIN32)
            _putenv_s("R4L_OVERLAY_SCRIPT", "");  // a quick relaunch must not run it again
#else
            unsetenv("R4L_OVERLAY_SCRIPT");
#endif
        } else {
            g_ov->script.reset();
        }
    }
    if (const char* c = std::getenv("R4L_OVERLAY_COMBO")) g_ov->combo = static_cast<unsigned>(std::strtoul(c, nullptr, 0));
    ImGuiContext* prev = ImGui::GetCurrentContext();
    g_ov->ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_ov->ctx);
    ImGuiIO& im = ImGui::GetIO();
    im.IniFilename = nullptr;
    im.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    im.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    ImGui::SetCurrentContext(prev);
    return 1;
}

extern "C" int recomp_overlay_handle_event(const void* ev) {
    if (!g_ov || !ev) return 0;
    const SDL_Event& e = *static_cast<const SDL_Event*>(ev);
    Overlay& o = *g_ov;
    const bool is_open = o.open.load();
    bool toggle = false;
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !e.key.repeat && !is_open)
        toggle = true;  // inside the menu Esc is "back"; the frame resumes from the top level
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        o.pad_held |= 1u << e.gbutton.button;
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) toggle = true;
        if ((o.pad_held & o.combo) == o.combo) toggle = true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) o.pad_held &= ~(1u << e.gbutton.button);
    if (toggle) {
        request_open(!is_open);
        return 1;
    }
    if (!is_open) return 0;
    switch (e.type) {  // the game sees no input while the menu is open
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL: case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        SDL_Event q = e;
        // ImGui works in framebuffer pixels here; mouse events are in points.
        int ww = 0, wh = 0, pw = 0, ph = 0;
        if (o.window && SDL_GetWindowSize(o.window, &ww, &wh) && SDL_GetWindowSizeInPixels(o.window, &pw, &ph) &&
            ww > 0 && wh > 0) {
            const float sx = static_cast<float>(pw) / ww, sy = static_cast<float>(ph) / wh;
            if (q.type == SDL_EVENT_MOUSE_MOTION) { q.motion.x *= sx; q.motion.y *= sy; }
            if (q.type == SDL_EVENT_MOUSE_BUTTON_DOWN || q.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                q.button.x *= sx; q.button.y *= sy;
            }
        }
        std::lock_guard<std::mutex> lk(o.mu);
        o.events.push_back(q);
        return 1;
    }
    default: return 0;  // quit, window and device events stay with the host
    }
}

extern "C" int recomp_overlay_is_open(void) { return g_ov && g_ov->open.load(); }
extern "C" void recomp_overlay_set_open(int open) {
    if (g_ov) request_open(open != 0);
}

extern "C" void recomp_overlay_render(int fb_w, int fb_h) {
    if (g_ov && g_ov->script && !g_ov->script_done) {
        ImGuiContext* p = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(g_ov->ctx);
        if (g_ov->script->active()) {
            g_ov->script->step(*g_ov->app, *g_ov->script_plat);
        } else {
            g_ov->script_done = true;
            std::fprintf(stderr, "[r4l-script] overlay script done, failures=%d\n", g_ov->script->failures());
        }
        ImGui::SetCurrentContext(p);
    }
    if (!g_ov || !g_ov->open.load()) return;
    Overlay& o = *g_ov;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(o.ctx);
    gl_init(o);
    std::vector<SDL_Event> evs;
    {
        std::lock_guard<std::mutex> lk(o.mu);
        evs.swap(o.events);
    }
    apply_transitions(o);
    ImGui_ImplOpenGL3_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    for (const SDL_Event& e : evs) feed(*o.app, e);
    const uint64_t now = SDL_GetTicksNS();
    io.DeltaTime = o.last_ns ? std::max(1e-4f, (now - o.last_ns) / 1e9f) : 1.0f / 60.0f;
    o.last_ns = now;
    io.DisplaySize = ImVec2(static_cast<float>(fb_w), static_cast<float>(fb_h));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    if (o.script && o.window) {
        // The runtime hands mouse events over in pixels; the script pushes
        // window points, so scale its coordinates down by the density.
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(o.window, &win_w, &win_h);
        if (win_w > 0) o.script->coord_scale = static_cast<float>(win_w) / static_cast<float>(fb_w);
    }
    if (o.script) {
        script_frame_begin();
        ImGui::GetCurrentContext()->TestEngineHookItems = true;
    }
    ImGui::NewFrame();
    if (std::getenv("R4L_SCRIPT_DEBUG") && ImGui::GetIO().MouseClicked[0]) {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        std::fprintf(stderr, "[r4l] click at %.0f,%.0f over %s\n", g.IO.MousePos.x, g.IO.MousePos.y,
                     g.HoveredWindow ? g.HoveredWindow->Name : "(none)");
    }
    o.app->frame();
    ImGui::Render();
    glViewport(0, 0, fb_w, fb_h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!o.app->overlay_open) apply_transitions(o);
    ImGui::SetCurrentContext(prev);
}

extern "C" void recomp_overlay_shutdown(void) {
    if (!g_ov) return;
    if (g_ov->script && !g_ov->script_done) g_carry_script = std::move(g_ov->script);
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_ov->ctx);
    if (g_ov->gl_ready) {
        skin().unload();
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui::DestroyContext(g_ov->ctx);
    ImGui::SetCurrentContext(prev == g_ov->ctx ? nullptr : prev);
    delete g_ov;
    g_ov = nullptr;
}

// Test/screenshot helper (not ABI): renders a stand-in game frame with the
// overlay open, into a hidden window's FBO, and writes a PNG.
extern "C" int r4l_render_overlay_png(const char* path, int w, int h, const RecompLauncherCGameInfo* game,
                                      RecompLauncherCSettings* io, const RecompOverlayHost* host,
                                      const char* assets_dir, const char* screen) {
    Platform plat;
    if (!plat.open("overlay", w, h, true, nullptr, assets_dir ? assets_dir : "assets")) return 0;
    // Platform::open made its own ImGui context; the overlay brings its own.
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    recomp_overlay_init(plat.window, plat.gl, game, io, host, assets_dir);
    recomp_overlay_set_open(1);
    if (screen)
        for (int i = 0; i < static_cast<int>(Screen::Count); ++i)
            if (!std::strcmp(screen, screen_name(static_cast<Screen>(i)))) g_ov->app->screen = static_cast<Screen>(i);
    // Bind an FBO and paint a fake game frame (sky, road, horizon bands).
    plat.width = w;
    plat.height = h;
    for (int frame = 0; frame < 4; ++frame) {
        ImGuiContext* none = nullptr;
        ImGui::SetCurrentContext(none);
        // Reuse Platform's FBO path by rendering an empty ImGui-less clear.
        auto fbo_gen = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
        auto fbo_bind = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
        auto fbo_tex = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
        if (!plat.fbo) {
            glGenTextures(1, &plat.fbo_tex);
            glBindTexture(GL_TEXTURE_2D, plat.fbo_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            fbo_gen(1, &plat.fbo);
            fbo_bind(GL_FRAMEBUFFER, plat.fbo);
            fbo_tex(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, plat.fbo_tex, 0);
        }
        fbo_bind(GL_FRAMEBUFFER, plat.fbo);
        glViewport(0, 0, w, h);
        glEnable(GL_SCISSOR_TEST);
        const float bands[][4] = {{0.20f, 0.42f, 0.78f, 1}, {0.95f, 0.62f, 0.35f, 1}, {0.16f, 0.30f, 0.14f, 1}, {0.22f, 0.22f, 0.24f, 1}};
        for (int b = 0; b < 4; ++b) {
            glScissor(0, h - (b + 1) * h / 4, w, h / 4);
            glClearColor(bands[b][0], bands[b][1], bands[b][2], 1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glScissor(w * 2 / 5, 0, w / 5, h / 4);
        glClearColor(0.9f, 0.9f, 0.9f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        recomp_overlay_render(w, h);
        glFinish();
    }
    const bool ok = plat.capture_png(path);
    recomp_overlay_shutdown();
    plat.close(false);
    return ok;
}
