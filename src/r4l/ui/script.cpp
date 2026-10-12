#include "script.h"

#include "platform.h"
#include "ui.h"

#include "r4l/core/ini.h"

#include <algorithm>
#include <cstdlib>

#include <SDL3/SDL.h>

#include "imgui_internal.h"

#include <cstdarg>
#include <map>

#include <cstdio>
#include <fstream>
#include <sstream>

// ---- item registry (Dear ImGui test-engine hooks) ---------------------------
// Every labelled item ImGui adds this frame is recorded by its visible label,
// so scripts can say `tap Restore defaults` instead of guessing coordinates.
namespace {
std::map<ImGuiID, ImRect> g_bb;
std::map<std::string, ImRect> g_items, g_prev;
std::string g_scroll_to;  // `tap`/`find`: bring this item into view while it is drawn
void scroll_into_view(const std::string& l, const ImRect& bb) {
    if (g_scroll_to.empty() || l != g_scroll_to) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w = g.CurrentWindow; w; w = w->ParentWindow) {
        if (w->ScrollMax.y > 0 || w->Scroll.y > 0) {  // the nearest scrolling window
            ImGui::SetScrollFromPosY(w, bb.GetCenter().y - w->Pos.y, 0.5f);
            break;
        }
    }
    g_scroll_to.clear();
}
std::string visible(const char* label) {
    std::string l = label ? label : "";
    const size_t h = l.find("##");
    if (h != std::string::npos) l.resize(h);
    return l;
}
}  // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext*, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) { g_bb[id] = bb; }
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    const std::string l = visible(label);
    auto it = g_bb.find(id);
    if (!l.empty() && it != g_bb.end() && !r4l_script_items_has(l)) {
        g_items[l] = it->second;
        scroll_into_view(l, it->second);
    }
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

bool r4l_script_items_has(const std::string& l) { return g_items.count(l) != 0; }

namespace r4l {

void script_mark(const char* label) {  // row widgets: the row's own label
    if (!ImGui::GetCurrentContext() || !ImGui::GetCurrentContext()->TestEngineHookItems) return;
    const std::string l = visible(label);
    if (!l.empty() && !g_items.count(l)) {
        g_items[l] = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        scroll_into_view(l, g_items[l]);
    }
}
void script_frame_begin() {
    g_prev.swap(g_items);
    g_items.clear();
    g_bb.clear();
}

bool Script::load(const std::string& path) {
    std::ifstream f(path);
    std::string l;
    while (std::getline(f, l)) {
        l = trim(l);
        if (!l.empty() && l[0] != '#') lines_.push_back(l);
    }
    return !lines_.empty();
}

void Script::step(App& app, Platform& plat) {
    if (sleep_until_ && SDL_GetTicks() < sleep_until_) return;
    sleep_until_ = 0;
    if (wait_ > 0) {
        --wait_;
        return;
    }
    const Uint32 win = plat.window ? SDL_GetWindowID(plat.window) : 0;
    while (pc_ < lines_.size()) {
        const std::string line = lines_[pc_++];
        std::istringstream in(line);
        std::string op;
        in >> op;
        std::string rest;
        std::getline(in, rest);
        rest = trim(rest);
        std::fprintf(stderr, "[r4l-script] %s\n", line.c_str());
        SDL_Event e;
        if (op == "wait") {
            wait_ = std::max(1, std::atoi(rest.c_str()));
            return;
        } else if (op == "sleep") {  // wall-clock seconds (for multi-process tests)
            sleep_until_ = SDL_GetTicks() + static_cast<Uint64>(std::atof(rest.c_str()) * 1000.0);
            return;
        } else if (op == "screen") {
            for (int i = 0; i < static_cast<int>(Screen::Count); ++i)
                if (rest == screen_name(static_cast<Screen>(i))) app.screen = static_cast<Screen>(i);
        } else if (op == "click" || op == "wheel") {
            float x = 0, y = 0;
            int steps = 0;
            std::istringstream a(rest);
            a >> x >> y >> steps;
            x *= coord_scale;
            y *= coord_scale;
            SDL_zero(e);
            e.type = SDL_EVENT_MOUSE_MOTION;
            e.motion.windowID = win;
            e.motion.x = x;
            e.motion.y = y;
            SDL_PushEvent(&e);
            if (op == "wheel") {
                SDL_zero(e);
                e.type = SDL_EVENT_MOUSE_WHEEL;
                e.wheel.windowID = win;
                e.wheel.y = static_cast<float>(steps);
                e.wheel.mouse_x = x;
                e.wheel.mouse_y = y;
                SDL_PushEvent(&e);
            } else {
                for (int down = 1; down >= 0; --down) {
                    SDL_zero(e);
                    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    e.button.windowID = win;
                    e.button.button = SDL_BUTTON_LEFT;
                    e.button.down = down != 0;
                    e.button.clicks = 1;
                    e.button.x = x;
                    e.button.y = y;
                    SDL_PushEvent(&e);
                }
            }
            wait_ = 2;  // let ImGui see down and up on separate frames
            return;
        } else if (op == "key") {
            const SDL_Scancode sc = SDL_GetScancodeFromName(rest.c_str());
            for (int down = 1; down >= 0; --down) {
                SDL_zero(e);
                e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                e.key.windowID = win;
                e.key.scancode = sc;
                e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
                e.key.down = down != 0;
                SDL_PushEvent(&e);
            }
            wait_ = 2;
            return;
        } else if (op == "text") {
            SDL_zero(e);
            e.type = SDL_EVENT_TEXT_INPUT;
            e.text.windowID = win;
            static std::string keep;
            keep = rest;
            e.text.text = keep.c_str();
            SDL_PushEvent(&e);
            wait_ = 2;
            return;
        } else if (op == "tap") {  // click the item with this label (last frame)
            auto it = g_prev.find(rest);
            const float vh = ImGui::GetIO().DisplaySize.y;
            const bool visible = it != g_prev.end() && it->second.Min.y >= 0 && it->second.Max.y <= vh * 0.86f;
            if (!visible && it != g_prev.end() && scrolls_ == 0) {
                // Drawn but off screen: scroll its window to it, then tap.
                ++scrolls_;
                g_scroll_to = rest;
                lines_.insert(lines_.begin() + static_cast<long>(pc_), {"wait 3", "tap " + rest});
                continue;
            }
            if (!visible) {
                // Off screen? Scroll the content pane towards it and look again.
                const bool up = it != g_prev.end() && it->second.Min.y < 0;
                if (scrolls_ == 0 && it == g_prev.end()) {  // unknown position: start from the top
                    ++scrolls_;
                    lines_.insert(lines_.begin() + static_cast<long>(pc_),
                                  {"wheel 300 420 60", "wheel " + std::to_string(content_x_) + " 420 60", "wait 2", "tap " + rest});
                    continue;
                }
                if (scrolls_++ < 16) {
                    // Alternate between the left and right halves of the content (two-pane pages).
                    const int x = (scrolls_ & 1) ? 300 : content_x_;
                    lines_.insert(lines_.begin() + static_cast<long>(pc_), {"wheel " + std::to_string(x) + " 420 " + (up ? "3" : "-3"),
                                                                            "wait 2", "tap " + rest});
                    continue;
                }
                scrolls_ = 0;
                std::fprintf(stderr, "[r4l-script] FAIL no item \"%s\"\n", rest.c_str());
                ++failures_;
                continue;
            }
            scrolls_ = 0;
            const ImVec2 c = it->second.GetCenter();
            lines_.insert(lines_.begin() + static_cast<long>(pc_), "click " + std::to_string(c.x) + " " + std::to_string(c.y));
        } else if (op == "pad") {  // gamepad button by SDL name (a, b, back, start, leftshoulder...)
            const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(rest.c_str());
            for (int down = 1; down >= 0; --down) {
                SDL_zero(e);
                e.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
                e.gbutton.button = static_cast<Uint8>(b);
                e.gbutton.down = down != 0;
                SDL_PushEvent(&e);
            }
            wait_ = 2;
            return;
        } else if (op == "file") {  // assert a file exists and contains text: file <path> <text>
            std::istringstream a(rest);
            std::string path, want;
            a >> path;
            std::getline(a, want);
            want = trim(want);
            std::ifstream f(path, std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            const bool ok = f && ss.str().find(want) != std::string::npos;
            std::fprintf(stderr, "[r4l-script] %s file %s contains \"%s\"\n", ok ? "PASS" : "FAIL", path.c_str(), want.c_str());
            if (!ok) ++failures_;
        } else if (op == "top") {  // scroll the content pane back to the top
            for (int i = 0; i < 3; ++i) lines_.insert(lines_.begin() + static_cast<long>(pc_), "wheel " + std::to_string(content_x_) + " 420 40");
        } else if (op == "find") {  // scroll until an item with this label is on screen
            auto it = g_prev.find(rest);
            const float vh = ImGui::GetIO().DisplaySize.y;
            if (it == g_prev.end() || it->second.Min.y < 0 || it->second.Max.y > vh * 0.86f) {
                if (scrolls_ == 0 && (it == g_prev.end() || it->second.Min.y < 0)) {
                    ++scrolls_;
                    lines_.insert(lines_.begin() + static_cast<long>(pc_),
                                  {"wheel 300 420 60", "wheel " + std::to_string(content_x_) + " 420 60", "wait 2", "find " + rest});
                    continue;
                }
                if (scrolls_++ < 16) {
                    const int x = (scrolls_ & 1) ? 300 : content_x_;
                    lines_.insert(lines_.begin() + static_cast<long>(pc_),
                                  {"wheel " + std::to_string(x) + " 420 -3", "wait 2", "find " + rest});
                    continue;
                }
            }
            scrolls_ = 0;
            const bool ok = it != g_prev.end();
            std::fprintf(stderr, "[r4l-script] %s find \"%s\"\n", ok ? "PASS" : "FAIL", rest.c_str());
            if (!ok) ++failures_;
        } else if (op == "seen") {  // an item with this label was drawn last frame
            const bool ok = g_prev.count(rest) != 0;
            if (!ok && std::getenv("R4L_SCRIPT_DEBUG")) for (auto& kv : g_prev) std::fprintf(stderr, "  item: %s\n", kv.first.c_str());
            std::fprintf(stderr, "[r4l-script] %s seen \"%s\"\n", ok ? "PASS" : "FAIL", rest.c_str());
            if (!ok) ++failures_;
        } else if (op == "shot") {
            plat.capture_png(rest);
        } else if (op == "status") {
            const bool ok = app.s.status.find(rest) != std::string::npos || app.np_status.find(rest) != std::string::npos;
            std::fprintf(stderr, "[r4l-script] %s status \"%s\" (now \"%s\")\n", ok ? "PASS" : "FAIL", rest.c_str(),
                         app.s.status.c_str());
            if (!ok) ++failures_;
        } else if (op == "play") {
            app.request_launch();
        } else if (op == "quit") {
            app.request_quit();
        }
    }
}

}  // namespace r4l
