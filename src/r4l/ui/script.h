// script.h — R4L_SCRIPT: drive the launcher from a text file (tests, the
// recomp-ui LNG_SCRIPT equivalent). One action per line:
//   screen <Name> | click <x> <y> | wheel <x> <y> <steps> | key <Scancode> |
//   text <utf8> | wait <frames> | sleep <seconds> | shot <png> | status <substring> | play | quit
//   seen / unseen <label> | expect slotN <0|1> (overlay: save-state slot N holds a state) | ...
// Coordinates are window points. `status` prints PASS/FAIL against the footer
// status line. Results go to stderr as "[r4l-script] ...".
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <string>
#include <vector>

bool r4l_script_items_has(const std::string& l);

struct SDL_Window;

namespace r4l {

struct App;
struct Platform;

void script_mark(const char* label);   // row widgets register their row label
void script_frame_begin();             // once per frame, before ImGui::NewFrame

class Script {
public:
    bool load(const std::string& path);
    bool active() const { return !lines_.empty() && pc_ < lines_.size(); }
    // Run actions until a wait; returns true when the script asked to stop.
    void step(App& app, Platform& plat);
    int failures() const { return failures_; }
    float coord_scale = 1.0f;  // pushed mouse coordinates = layout coordinates * this

private:
    std::vector<std::string> lines_;
    size_t pc_ = 0;
    int wait_ = 0;
    Uint64 sleep_until_ = 0;
    int failures_ = 0;
    int scrolls_ = 0;
    int content_x_ = 800;  // a point inside the content pane
};

}  // namespace r4l
