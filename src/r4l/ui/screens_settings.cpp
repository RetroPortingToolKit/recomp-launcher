// screens_settings.cpp — Graphics, Mods and Controls.
#include "skin.h"
#include "ui.h"
#include "script.h"
#include "imgui_internal.h"
#include "dialogs.h"

#include "r4l/core/ini.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cfloat>
#include <cstddef>
#include <functional>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace r4l {

void draw_skin_picker(App& app);

namespace {

void title_text(const char* t, const char* sub) { screen_title(t, sub); }

// Option row for a mod feature option (choice / boolean / integer / text).
void mod_option_row(ModCatalog& mods, ModFeature& f, ModOption& o) {
    ImGui::BeginDisabled(o.info.disabled || !f.info.enabled);
    const char* help = o.info.description[0] ? o.info.description : nullptr;
    if (o.info.type == RECOMP_MOD_OPTION_CHOICE && !o.choices.empty()) {
        std::vector<const char*> labels;
        int cur = -1;
        for (size_t i = 0; i < o.choices.size(); ++i) {
            labels.push_back(o.choices[i].label[0] ? o.choices[i].label : o.choices[i].value);
            if (!std::strcmp(o.choices[i].value, o.info.value)) cur = static_cast<int>(i);
        }
        if (row_combo(o.info.label, &cur, labels.data(), static_cast<int>(labels.size()), help))
            mods.set_option(f, o, o.choices[cur].value);
    } else if (o.info.type == RECOMP_MOD_OPTION_BOOLEAN) {
        int v = !std::strcmp(o.info.value, "true") ? 1 : 0;
        if (row_toggle(o.info.label, &v, help)) mods.set_option(f, o, v ? "true" : "false");
    } else if (o.info.type == RECOMP_MOD_OPTION_INTEGER) {
        int v = std::atoi(o.info.value);
        if (row_slider(o.info.label, &v, static_cast<int>(o.info.min_value), static_cast<int>(o.info.max_value),
                       "%d", help))
            mods.set_option(f, o, std::to_string(v));
    } else {
        row_text(o.info.label, o.info.value, theme().text_dim);
    }
    ImGui::EndDisabled();
}

}  // namespace

// ---------------------------------------------------------------- Graphics

void App::draw_graphics() {
    // In a netplay match the host applies only presentation live; it says so
    // with RECOMP_OVERLAY_RESTART_OTHER (bit 31), and the rest waits.
    if (mode == Mode::Overlay && netplay_locked && (applied_bits & (1u << 31)))
        chip(tr("Some changes apply after this match"), theme().warn);
    RecompLauncherCSettings* io = s.io;
    const RecompLauncherCGameInfo* g = s.game;
    title_text(tr("Graphics"), tr("Pick a preset, or change any row. Changing a row a preset sets makes it Custom."));
    if (!io || !g) return;
    auto lock = [&](const char* key) { ImGui::BeginDisabled(L(key)); };
    auto unlock = [&]() { ImGui::EndDisabled(); };

    if (g->quality_offered_mask && S("graphics.preset")) {
        const bool stacked = skin().loaded() && skin().model().metric("preset_vertical@Graphics",
                                                                      skin().model().metric("preset_vertical", 0)) != 0;
        if (!stacked) section(tr("Preset"));
        lock("graphics.preset");
        if (!stacked)
            ImGui::BeginChild("##preset", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        const bool auto_mode = !surf.value("graphics.preset") || !std::strcmp(surf.value("graphics.preset"), "auto");
        const int n = auto_mode ? 5 : 4;
        float bw = (ImGui::GetContentRegionAvail().x - (n - 1) * 10) / n;
        // Skins may stack the presets as a centred menu column (preset_vertical).
        Skin& sk = skin();
        const bool vertical = sk.loaded() && sk.model().metric("preset_vertical@" + std::string("Graphics"),
                                                             sk.model().metric("preset_vertical", 0)) != 0;
        float vy = 0, vh = 52, vpitch = 62, vx = 0;
        if (vertical) {
            bw = sk.metric("preset_box_w", 384);
            vh = sk.metric("preset_box_h", 42);
            vpitch = sk.metric("preset_pitch", 60);
            // preset_top is a viewport position on an unscrolled page; follow
            // the page's scroll so the column and every row after it scroll
            // with it (an absolute column pins the rows below it off screen).
            vy = std::max(sk.metric("preset_top", 147) - ImGui::GetScrollY(), ImGui::GetCursorScreenPos().y);
            vx = (ImGui::GetMainViewport()->WorkSize.x - bw) * 0.5f;
        }
        int pushed = 0;
        if (vertical && sk.has_color("preset_box")) {
            ImGui::PushStyleColor(ImGuiCol_Button, sk.color("preset_box", theme().surface_hi));
            ImGui::PushStyleColor(ImGuiCol_Text, sk.color("preset_text", theme().text));
            pushed = 2;
        }
        int slot = 0;
        auto place = [&]() {
            if (!vertical) return;
            ImGui::SetCursorScreenPos(ImVec2(vx, vy + vpitch * slot));
            ++slot;
        };
        if (vertical) {
            // The menu column: "Graphics: Ultra" first (Left/Right walk the
            // presets, Custom reads as itself), then the list of presets
            // below it, then every other row in the same box style.
            kv_column_begin(vx, bw, vh);
            place();
            std::vector<const char*> names;
            std::vector<int> values;
            if (auto_mode) {
                names.push_back(tr("Auto"));
                values.push_back(0);
            }
            for (int p = kQualityLow; p <= kQualityUltra; ++p)
                if ((g->quality_offered_mask >> (p - 1)) & 1) {
                    names.push_back(tr(quality_name(p)));
                    values.push_back(p);
                }
            int cur = -1;
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i] == io->quality_preset && !(auto_mode && values[i] == g->quality_detected)) cur = static_cast<int>(i);
            if (cur < 0 && auto_mode && io->quality_preset == g->quality_detected && io->quality_preset >= 1) cur = 0;
            if (io->quality_preset == kQualityCustom) {
                names.push_back(tr("Custom"));
                values.push_back(kQualityCustom);
                cur = static_cast<int>(values.size()) - 1;
            }
            if (cur < 0) cur = 0;
            if (row_combo(tr("Graphics"), &cur, names.data(), static_cast<int>(names.size()))) {
                const int want = values[static_cast<size_t>(cur)];
                if (want == 0) {
                    const int p = g->quality_redetect ? g->quality_redetect() : g->quality_detected;
                    if (p >= kQualityLow && p <= kQualityUltra) s.quality.select(p, io);
                } else if (want != kQualityCustom) {
                    s.quality.select(want, io);
                }
            }
        }
        if (auto_mode) {
            // Auto: whatever the hardware detection picks (Re-detect refreshes it).
            const bool is_auto = io->quality_preset == g->quality_detected && io->quality_preset >= 1 &&
                                 io->quality_preset <= 4;
            char lbl[48];
            std::snprintf(lbl, sizeof lbl, "%s (%s)", tr("Auto"), quality_name(g->quality_detected));
            place();
            if (big_button(lbl, ImVec2(bw, vh), is_auto)) {
                const int p = g->quality_redetect ? g->quality_redetect() : g->quality_detected;
                if (p >= kQualityLow && p <= kQualityUltra) s.quality.select(p, io);
            }
            if (is_auto) {
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(theme().accent), 8.0f, 0, 2.0f);
            }
            if (!vertical) ImGui::SameLine(0, 10);
        }
        for (int p = kQualityLow; p <= kQualityUltra; ++p) {
            if (p > kQualityLow && !vertical) ImGui::SameLine(0, 10);
            place();
            const bool offered = (g->quality_offered_mask >> (p - 1)) & 1;
            const bool sel = io->quality_preset == p && !(auto_mode && p == g->quality_detected);
            const bool base = io->quality_preset == kQualityCustom && io->quality_base == p;
            std::string label = tr(quality_name(p));
            if (!auto_mode && g->quality_detected == p) label += std::string("  ") + tr("(detected)");
            ImGui::BeginDisabled(!offered);
            ImGui::PushID(p);
            if (big_button(label.c_str(), ImVec2(bw, vh), sel)) s.quality.select(p, io);
            if (vertical && sk.model().palette.count("nav_item_border")) {
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(sk.color("nav_item_border", theme().line)));
            }
            if (sel || base) {
                // The chosen preset must read as chosen in every skin, even one
                // whose primary and plain buttons share a colour.
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(a, b, ImGui::ColorConvertFloat4ToU32(theme().accent), 8.0f, 0, 2.0f);
            }
            ImGui::PopID();
            ImGui::EndDisabled();
        }
        if (pushed) ImGui::PopStyleColor(pushed);
        if (vertical) ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, vy + vpitch * slot + 8));
        if (vertical) {  // the rest of the page sits in the column, below the presets
            ImGui::Dummy(ImVec2(0, 4));
        }
        if (io->quality_preset == kQualityCustom) {
            chip(tr("Custom"), theme().accent);
            ImGui::SameLine();
            ImGui::TextDisabled("%s %s", tr("started from"), tr(quality_name(io->quality_base)));
        }
        ImGui::TextDisabled("%s", g->quality_summary ? g->quality_summary : tr("Hardware not detected"));
        if (g->quality_reason && *g->quality_reason) ImGui::TextDisabled("%s", g->quality_reason);
        if (S("graphics.redetect") && g->quality_redetect && ImGui::Button(tr("Re-detect"))) {
            const int p = g->quality_redetect();
            if (p >= kQualityLow && p <= kQualityUltra) s.quality.select(p, io);
        }
        if (!stacked) ImGui::EndChild();
        unlock();
    }

    // Essentials a title may keep next to the preset.
    if (S("graphics.frame_generation") && g->has_render_pipeline) {
        lock("graphics.frame_generation");
        row_toggle(tr("Smooth motion"), &io->frame_generation, tr("Generated in-between frames for high refresh displays."));
        unlock();
    }
    if (S("graphics.fullscreen") && (g->has_fullscreen_toggle || g->has_window_size)) {
        // 1 is borderless fullscreen (the desktop's mode, Alt-Tab friendly),
        // 2 the exclusive mode change.
        static const char* kLayout[] = {"Windowed", "Borderless", "Exclusive fullscreen"};
        const char* l[3] = {tr(kLayout[0]), tr(kLayout[1]), tr(kLayout[2])};
        int v = io->fullscreen >= 0 && io->fullscreen <= 2 ? io->fullscreen : 0;
        lock("graphics.fullscreen");
        if (row_combo(tr("Window"), &v, l, 3)) io->fullscreen = v;
        unlock();
    }

    if (S("graphics.renderer") || S("graphics.vsync") || S("graphics.window_size")) section(tr("Display"));
    if (S("graphics.renderer") && g->has_renderer && g->num_renderers > 0 && g->renderer_labels) {
        lock("graphics.renderer");
        if (row_combo(tr("Renderer"), &io->renderer, g->renderer_labels, g->num_renderers, g->renderer_note) &&
            g->renderer_ids && io->renderer >= 0 && io->renderer < g->num_renderers)
            std::snprintf(io->renderer_id, sizeof io->renderer_id, "%s", g->renderer_ids[io->renderer]);
        unlock();
        restart_chip(offsetof(RecompLauncherCSettings, renderer), sizeof(int));
    }
    if (S("graphics.window_size") && g->has_window_size) {
        lock("graphics.window_size");
        // 0 is the host's "fit the display" (psxrecomp g_video_win_w); show it
        // as such instead of "0 px", and keep dragged values in the real range.
        if (row_slider(tr("Window width"), &io->window_width, 0, 3840,
                       io->window_width <= 0 ? tr("Fit display") : "%d px") &&
            io->window_width > 0 && io->window_width < 640)
            io->window_width = 640;
        unlock();
    }
    if (S("graphics.vsync") && g->has_vsync) {
        static const char* kVsync[] = {"On", "Off (lowest latency)", "Adaptive"};
        const char* l[3] = {tr(kVsync[0]), tr(kVsync[1]), tr(kVsync[2])};
        int v = io->vsync >= 1 && io->vsync <= 3 ? io->vsync - 1 : 0;
        lock("graphics.vsync");
        if (row_combo(tr("V-Sync"), &v, l, 3)) io->vsync = v + 1;
        unlock();
    }

    if (S("graphics.internal_resolution") || S("graphics.supersampling") || S("graphics.dynamic_resolution"))
        section(tr("Resolution"));
    if (S("graphics.internal_resolution") && g->num_internal_resolutions > 0 && g->internal_resolution_labels &&
        g->internal_resolution_values) {
        int idx = 0;
        for (int i = 0; i < g->num_internal_resolutions; ++i)
            if (g->internal_resolution_values[i] == io->internal_resolution) idx = i;
        lock("graphics.internal_resolution");
        if (row_combo(tr("Internal resolution"), &idx, g->internal_resolution_labels, g->num_internal_resolutions,
                      g->internal_resolution_note))
            io->internal_resolution = g->internal_resolution_values[idx];
        unlock();
        restart_chip(offsetof(RecompLauncherCSettings, internal_resolution), sizeof(int));
        if (g->has_supersample && S("graphics.supersampling")) {
            static const char* const kSsLabels[] = {"Off", "1.25x", "1.5x", "2x"};
            static const int kSsMilli[] = {1000, 1250, 1500, 2000};
            int ss = 0;
            for (int i = 0; i < 4; ++i)
                if (kSsMilli[i] == io->supersample_milli) ss = i;
            lock("graphics.supersampling");
            if (row_combo(tr("Supersampling"), &ss, kSsLabels, 4,
                          tr("Renders above the internal resolution and scales down for smoother edges.")))
                io->supersample_milli = kSsMilli[ss];
            unlock();
            restart_chip(offsetof(RecompLauncherCSettings, supersample_milli), sizeof(int));
        }
    } else if (S("graphics.supersampling") && g->has_supersampling) {
        lock("graphics.supersampling");
        row_slider(tr("Supersampling"), &io->supersampling, 1, 8, "%dx");
        unlock();
    }
    if (S("graphics.dynamic_resolution") && g->has_dynamic_resolution) {
        lock("graphics.dynamic_resolution");
        row_toggle(tr("Dynamic resolution"), &io->dynamic_resolution,
                   tr("Drops the internal resolution under load to hold the frame rate."));
        if (io->dynamic_resolution && g->num_internal_resolutions > 0 && g->internal_resolution_labels &&
            g->internal_resolution_values) {
            int idx = 0;
            for (int i = 0; i < g->num_internal_resolutions; ++i)
                if (g->internal_resolution_values[i] == io->dynamic_resolution_min) idx = i;
            if (row_combo(tr("Lowest resolution"), &idx, g->internal_resolution_labels, g->num_internal_resolutions))
                io->dynamic_resolution_min = g->internal_resolution_values[idx];
        }
        unlock();
    }

    if (surf.any_shown("graphics.antialiasing") || S("graphics.texture_filter") || S("graphics.fmv_filter") ||
        S("graphics.scanlines") || S("graphics.screen_kind") || S("graphics.geometry"))
        section(tr("Image"));
    if (S("graphics.antialiasing") && g->has_antialiasing) {
        lock("graphics.antialiasing");
        row_toggle(tr("Smooth scaling"), &io->antialiasing, tr("Linear filtering when the image is scaled to the window."));
        unlock();
    }
    if (S("graphics.texture_filter") && g->has_texture_filter) {
        // The host's vocabulary when it hands one over (the same catalog every
        // frontend uses), else Nearest/Bilinear.
        static const char* kLegacy[] = {"Nearest (original)", "Bilinear"};
        static const int kLegacyValue[] = {0, 1};
        const char* const* labels = kLegacy;
        const int* values = kLegacyValue;
        int n = 2;
        if (g->texture_filter_labels && g->texture_filter_values && g->num_texture_filters > 0) {
            labels = g->texture_filter_labels;
            values = g->texture_filter_values;
            n = g->num_texture_filters < 8 ? g->num_texture_filters : 8;
        }
        const char* l[8];
        int idx = 0;
        for (int i = 0; i < n; ++i) {
            l[i] = tr(labels[i]);
            if (io->texture_filter == values[i]) idx = i;
        }
        lock("graphics.texture_filter");
        if (row_combo(tr("Texture filtering"), &idx, l, n)) io->texture_filter = values[idx];
        unlock();
    }
    if (S("graphics.fmv_filter") && g->has_fmv_filter) {
        static const char* kFmv[] = {"Nearest", "Bilinear", "Sharp", "Bicubic"};
        const char* l[4] = {tr(kFmv[0]), tr(kFmv[1]), tr(kFmv[2]), tr(kFmv[3])};
        int v = io->fmv_filter >= 1 && io->fmv_filter <= 4 ? io->fmv_filter - 1 : 1;
        lock("graphics.fmv_filter");
        if (row_combo(tr("Movie filter"), &v, l, 4)) io->fmv_filter = v + 1;
        unlock();
    }
    if (S("graphics.screen_kind") && g->has_screen_kind) {
        static const char* kScreen[] = {"Raw", "CRT", "Composite", "Trinitron"};
        const char* l[4] = {tr(kScreen[0]), tr(kScreen[1]), tr(kScreen[2]), tr(kScreen[3])};
        lock("graphics.screen_kind");
        row_combo(tr("Screen model"), &io->screen_kind, l, 4);
        unlock();
    }
    if (S("graphics.scanlines") && g->has_scanlines) {
        lock("graphics.scanlines");
        row_toggle(tr("CRT scanlines"), &io->scanlines);
        if (io->scanlines) row_slider(tr("Scanline strength"), &io->scanline_strength_pct, 0, 100, "%d%%");
        unlock();
    }
    if (S("graphics.geometry") && g->has_geometry_precision) {
        lock("graphics.geometry");
        row_toggle(tr("Geometry correction"), &io->geometry_correction, tr("Sub-pixel vertex precision (PGXP)."));
        row_toggle(tr("Perspective-correct textures"), &io->perspective_texturing);
        unlock();
    }
    if (S("graphics.integer_scale") && g->has_integer_scale) row_toggle(tr("Integer scaling"), &io->integer_scale);
    if (S("graphics.frame_blend") && g->has_frame_blend) row_toggle(tr("Frame blending"), &io->frame_blend);
    if (S("graphics.run_ahead") && g->has_run_ahead) row_slider(tr("Run-ahead"), &io->run_ahead, 0, RECOMP_LAUNCHER_RUN_AHEAD_MAX, "%d");
    if (S("graphics.frame_interp") && g->has_frame_interp) row_toggle(tr("Frame interpolation"), &io->frame_interp);
    if (S("graphics.aspect") && (g->widescreen_supported || g->aspect_mask) && g->aspect_labels && g->num_aspect_labels > 0) {
        row_combo(g->aspect_setting_label ? g->aspect_setting_label : tr("Aspect ratio"), &io->aspect_index,
                  g->aspect_labels, g->num_aspect_labels, g->aspect_setting_help);
        if (g->aspect_experimental && io->aspect_index > 0) chip(tr("Experimental"), theme().warn);
        if (g->adaptive_view_supported) row_toggle(tr("Fit the view to the window"), &io->adaptive_view);
    }
    if (S("graphics.shader") && g->has_shader) {
        ImGui::SetNextItemWidth(360);
        ImGui::InputTextWithHint(tr("Post shader"), "shaders/crt.glsl", io->shader_path, sizeof io->shader_path);
    }

    if ((S("graphics.render_thread") || S("graphics.present_thread")) && g->has_render_pipeline) {
        section(tr("Pipeline"));
        if (S("graphics.render_thread")) row_toggle(tr("Render thread"), &io->render_thread);
        if (S("graphics.present_thread")) row_toggle(tr("Present thread"), &io->present_thread);
        restart_chip(offsetof(RecompLauncherCSettings, render_thread), 2 * sizeof(int));
        if (S("graphics.frame_generation") == false && g->has_render_pipeline && S("graphics.render_thread"))
            row_toggle(tr("Smooth motion"), &io->frame_generation);
    }

    const TitleLayer& t = *title;
    if (S("graphics.title_features") && t.graphics_feature_count && mods.available()) {
        section(tr("Enhancements"));
        for (int i = 0; i < t.graphics_feature_count; ++i) {
            ModFeature* f = mods.find(t.graphics_features[i].package_id, t.graphics_features[i].feature_id);
            if (!f) continue;
            ImGui::PushID(i);
            int on = f->info.enabled;
            if (row_toggle(f->info.name, &on, f->info.description[0] ? f->info.description : nullptr))
                mods.set_enabled(*f, on != 0);
            for (auto& o : f->options) mod_option_row(mods, *f, o);
            ImGui::PopID();
        }
    }
    if (S("graphics.skin")) draw_skin_picker(*this);
    kv_column_end();
}

void draw_skin_picker(App& app) {
    const std::vector<std::string> skins = list_skins(app.s.assets_dir);
    if (skins.empty()) return;
    section("Menu skin");
    int cur = 0;
    std::vector<const char*> c;
    for (size_t i = 0; i < skins.size(); ++i) {
        c.push_back(skins[i].c_str());
        if (app.skin_dir.size() >= skins[i].size() &&
            app.skin_dir.compare(app.skin_dir.size() - skins[i].size(), skins[i].size(), skins[i]) == 0)
            cur = static_cast<int>(i);
    }
    if (row_combo("Skin", &cur, c.data(), static_cast<int>(c.size()), "Edits to skin.json reload live."))
        load_skin_named(app, skins[cur]);
}

// ---------------------------------------------------------------- Mods

void App::draw_mods() {
    title_text(tr("Mods"), mode == Mode::Overlay
                               ? tr("Changes are saved now. Options a mod can change on a running game apply at once; the rest apply on the next start.")
                               : tr("Features from the bundled and installed mod packages. Changes apply when you press Play."));
    if (!mods.available()) {
        ImGui::TextDisabled("%s", tr("This build has no mod support."));
        return;
    }
    // Mods change the game, so a savestate made with other mods may not work.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme().warn, "%s",
                       tr("Changing mods will probably break your existing save states. Don't change mods if you "
                          "want to keep using previous save states."));
    ImGui::PopTextWrapPos();
    script_mark("mods:savestate_warning");
    // Toolbar: search, install, packages view, disable all.
    ImGui::SetNextItemWidth(240);
    ImGui::InputTextWithHint("##search", tr("Search mods"), mods_search, sizeof mods_search);
    script_mark("Search mods");
    if (mode == Mode::Launcher && S("mods.install") && mods.can_install()) {
        ImGui::SameLine();
        if (ImGui::Button(tr("Install mod..."))) {
            static std::string patterns;
            patterns = mods.archive_patterns();
            static SDL_DialogFileFilter filter;
            static std::string desc;
            desc = (s.game && s.game->mods && s.game->mods->archive_description) ? s.game->mods->archive_description : "Mod archive";
            filter = SDL_DialogFileFilter{desc.c_str(), patterns.c_str()};
            show_open_file(
                [](void* user, const char* const* files, int) {
                    App* a = static_cast<App*>(user);
                    if (!files || !files[0]) return;
                    std::lock_guard<std::mutex> lk(a->job.mu);
                    a->pending_install = files[0];
                },
                this, SDL_GL_GetCurrentWindow(), &filter, 1, nullptr, false);
        }
    }
    if (mode == Mode::Launcher && S("mods.versions")) {
        ImGui::SameLine();
        if (ImGui::Button(mods_packages_view ? tr("Features") : tr("Packages"))) {
            mods_packages_view = !mods_packages_view;
            mods.refresh(S("mods.experimental"));  // removable / in-use state follows feature switches
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Turn all off"))) ImGui::OpenPopup("disable_all");
    if (ImGui::BeginPopupModal("disable_all", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("Turn every mod feature off?"));
        if (ImGui::Button(tr("Turn off"))) {
            s.status = std::to_string(mods.disable_all()) + " " + tr("features turned off");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    {
        std::string path;
        {
            std::lock_guard<std::mutex> lk(job.mu);
            path.swap(pending_install);
        }
        if (!path.empty()) {
            std::string err;
            if (mods.install(path, &err)) {
                s.status = std::string(tr("Installed ")) + path.substr(path.find_last_of("/\\") + 1);
                mods.refresh(S("mods.experimental"));
            } else {
                s.status = std::string(tr("Mod not installed: ")) + err;
            }
        }
    }
    if (S("mods.diagnostics"))
        for (const auto& d : mods.catalog_diagnostics)
            ImGui::TextColored(d.severity == RECOMP_MOD_DIAGNOSTIC_ERROR ? theme().bad : theme().warn, "%s%s%s",
                               d.resource, d.resource[0] ? ": " : "", d.message);

    const float w = ImGui::GetContentRegionAvail().x;
    const std::string q = mods_search;
    auto matches = [&](const char* a, const char* b) {
        if (q.empty()) return true;
        auto has = [&](const char* t) {
            std::string x = t, y = q;
            for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : y) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return x.find(y) != std::string::npos;
        };
        return has(a) || has(b);
    };

    if (mods_packages_view) {
        // ---- Packages: versions, legacy switch/options, remove.
        ImGui::BeginChild("##pkglist", ImVec2(w * 0.5f, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
        for (int i = 0; i < static_cast<int>(mods.package_views.size()); ++i) {
            const auto& p = mods.package_views[i].info;
            if (!matches(p.name, p.id)) continue;
            ImGui::PushID(i);
            char row[300];
            std::snprintf(row, sizeof row, "%s  %s", p.name, p.version);
            if (ImGui::Selectable(row, selected_package == i, 0, ImVec2(0, theme().row_h))) selected_package = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##pkgdetail", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (selected_package >= 0 && selected_package < static_cast<int>(mods.package_views.size())) {
            ModPackageView& pv = mods.package_views[selected_package];
            ImGui::PushFont(theme().bold, theme().body_size * 1.3f);
            ImGui::TextWrapped("%s", pv.info.name);
            ImGui::PopFont();
            ImGui::TextDisabled("%s · %s · %s", pv.info.id, pv.info.author, pv.info.license);
            if (pv.info.description[0]) ImGui::TextWrapped("%s", pv.info.description);
            if (pv.info.status[0]) ImGui::TextColored(pv.info.has_error ? theme().bad : theme().text_dim, "%s", pv.info.status);
            for (int l = 0; l < pv.info.author_link_count && l < RECOMP_LAUNCHER_MOD_AUTHOR_LINK_MAX; ++l)
                if (ImGui::SmallButton(pv.info.author_links[l].name[0] ? pv.info.author_links[l].name : pv.info.author_links[l].url))
                    SDL_OpenURL(pv.info.author_links[l].url);
            if (pv.info.option_count == 0 && mods.features.empty()) {
                int on = pv.info.enabled;
                if (row_toggle(tr("Enabled"), &on)) mods.set_package_enabled(pv, on != 0);
            }
            for (auto& o : pv.options) {
                ImGui::PushID(o.info.id);
                if (o.info.type == RECOMP_MOD_OPTION_CHOICE && !o.choices.empty()) {
                    std::vector<const char*> lbl;
                    int cur = 0;
                    for (size_t c = 0; c < o.choices.size(); ++c) {
                        lbl.push_back(o.choices[c].label);
                        if (!std::strcmp(o.choices[c].value, o.info.value)) cur = static_cast<int>(c);
                    }
                    if (row_combo(o.info.label, &cur, lbl.data(), static_cast<int>(lbl.size())))
                        mods.set_package_option(pv, o, o.choices[cur].value);
                } else if (o.info.type == RECOMP_MOD_OPTION_BOOLEAN) {
                    int v = !std::strcmp(o.info.value, "true");
                    if (row_toggle(o.info.label, &v)) mods.set_package_option(pv, o, v ? "true" : "false");
                }
                ImGui::PopID();
            }
            if (S("mods.versions") && pv.versions.size() > 1) {
                section(tr("Version"));
                for (const auto& v : pv.versions)
                    if (ImGui::RadioButton(v.version, v.selected != 0) && mods.select_version(pv.info, v.version))
                        mods.refresh(S("mods.experimental"));
            }
            if (!pv.info.removable && S("mods.install"))
                ImGui::TextDisabled("%s", tr("Bundled mods, and mods with features turned on, cannot be removed."));
            if (pv.info.removable && S("mods.install")) {
                section(tr("Package"));
                if (ImGui::Button(tr("Remove this mod"))) {
                    std::string err;
                    const auto copy = pv.info;
                    if (mods.remove(copy, &err)) {
                        s.status = std::string(tr("Removed ")) + copy.name;
                        selected_package = -1;
                        mods.refresh(S("mods.experimental"));
                    } else {
                        s.status = err;
                    }
                }
            }
        }
        ImGui::EndChild();
        return;
    }

    ImGui::BeginChild("##modlist", ImVec2(w * 0.5f, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    for (const ModGroup& grp : mods.groups) {
        bool any = false;
        for (int idx : grp.features)
            if (matches(mods.features[idx].info.name, mods.features[idx].info.package_name)) any = true;
        if (!any) continue;
        section(grp.name.c_str());
        for (int idx : grp.features) {
            ModFeature& f = mods.features[idx];
            if (!matches(f.info.name, f.info.package_name)) continue;
            if (f.info.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL && !S("mods.experimental") && !f.info.enabled) continue;
            ImGui::PushID(idx);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float rw = ImGui::GetContentRegionAvail().x;
            if (ImGui::Selectable("##f", selected_feature == idx, ImGuiSelectableFlags_AllowOverlap, ImVec2(rw - 70, theme().row_h)))
                selected_feature = idx;
            script_mark(f.info.name);
            if (ImGui::IsItemFocused()) selected_feature = idx;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float fs = ImGui::GetFontSize();
            dl->PushClipRect(p, ImVec2(p.x + rw - 96, p.y + theme().row_h), true);
            dl->AddText(theme().body, fs, ImVec2(p.x + 8, p.y + theme().row_h * 0.1f),
                        ImGui::ColorConvertFloat4ToU32(theme().text), f.info.name);
            dl->AddText(theme().body, fs * 0.75f, ImVec2(p.x + 8, p.y + theme().row_h * 0.58f),
                        ImGui::ColorConvertFloat4ToU32(theme().text_dim), f.info.package_name);
            dl->PopClipRect();
            if (f.info.has_error || !f.diagnostics.empty())
                dl->AddCircleFilled(ImVec2(p.x + rw - 90, p.y + theme().row_h * 0.5f), 4,
                                    ImGui::ColorConvertFloat4ToU32(theme().warn));
            ImGui::SameLine(rw - 62);
            int on = f.info.enabled;
            const ImVec2 tp = ImGui::GetCursorScreenPos();
            const float h = 26;
            ImGui::SetCursorScreenPos(ImVec2(tp.x, p.y + (theme().row_h - h) * 0.5f));
            if (ImGui::InvisibleButton("##tog", ImVec2(50, h))) {
                on = !on;
                mods.set_enabled(f, on != 0);
            }
            script_mark((std::string("mod:") + f.info.name).c_str());
            const ImVec2 a = ImGui::GetItemRectMin();
            dl->AddRectFilled(a, ImVec2(a.x + 50, a.y + h),
                              ImGui::ColorConvertFloat4ToU32(on ? theme().accent : theme().surface_hi), h * 0.5f);
            dl->AddCircleFilled(ImVec2(on ? a.x + 50 - h * 0.5f : a.x + h * 0.5f, a.y + h * 0.5f), h * 0.5f - 3,
                                ImGui::ColorConvertFloat4ToU32(theme().text));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##moddetail", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (selected_feature < 0 && !mods.features.empty()) selected_feature = 0;
    if (selected_feature >= 0 && selected_feature < static_cast<int>(mods.features.size())) {
        ModFeature& f = mods.features[selected_feature];
        ImGui::PushFont(theme().bold, theme().body_size * 1.35f);
        ImGui::TextWrapped("%s", f.info.name);
        ImGui::PopFont();
        ImGui::TextDisabled("%s %s%s%s", f.info.package_name, f.info.package_version, f.info.author[0] ? " · " : "",
                            f.info.author);
        if (f.info.channel == RECOMP_MOD_CHANNEL_EXPERIMENTAL) chip(tr("Experimental"), theme().warn);
        if (f.info.channel == RECOMP_MOD_CHANNEL_DEVELOPER) chip(tr("Developer"), theme().bad);
        if (mods.needs_restart.count(std::string(f.info.package_id) + "/" + f.info.id))
            chip(tr("Applies after restart"), theme().warn);
        if (f.info.description[0]) ImGui::TextWrapped("%s", f.info.description);
        if (f.info.status[0]) ImGui::TextColored(theme().text_dim, "%s", f.info.status);
        if (!f.options.empty()) {
            section(tr("Options"));
            for (auto& o : f.options) {
                ImGui::PushID(o.info.id);
                mod_option_row(mods, f, o);
                ImGui::PopID();
            }
        }
        if (S("mods.resources") && !f.resources.empty()) {
            section(tr("Files"));
            for (const auto& r : f.resources) {
                ImGui::PushID(r.id);
                ImGui::TextUnformatted(r.label[0] ? r.label : r.id);
                if (r.required && !r.verified) {
                    ImGui::SameLine();
                    chip(tr("Required"), theme().warn);
                }
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("%s", r.path[0] ? r.path : tr("Not set"));
                ImGui::PopTextWrapPos();
                if (mode == Mode::Launcher && ImGui::SmallButton(tr("Choose..."))) {
                    struct Ctx { App* a; int feature; std::string res; };
                    auto* c = new Ctx{this, selected_feature, r.id};
                    show_open_folder(
                        [](void* u, const char* const* files, int) {
                            Ctx* c = static_cast<Ctx*>(u);
                            if (files && files[0] && c->feature < static_cast<int>(c->a->mods.features.size())) {
                                ModFeature& ff = c->a->mods.features[c->feature];
                                for (auto& rr : ff.resources)
                                    if (c->res == rr.id) c->a->mods.set_resource(ff, rr, files[0]);
                            }
                            delete c;
                        },
                        c, SDL_GL_GetCurrentWindow(), nullptr, false);
                }
                ImGui::PopID();
            }
        }
        if (S("mods.diagnostics") && !f.diagnostics.empty()) {
            section(tr("Problems"));
            for (auto& d : f.diagnostics)
                ImGui::TextColored(d.severity == RECOMP_MOD_DIAGNOSTIC_ERROR ? theme().bad : theme().warn, "%s", d.message);
        }
        for (int l = 0; l < f.info.author_link_count && l < RECOMP_LAUNCHER_MOD_AUTHOR_LINK_MAX; ++l)
            if (ImGui::SmallButton(f.info.author_links[l].name[0] ? f.info.author_links[l].name : f.info.author_links[l].url))
                SDL_OpenURL(f.info.author_links[l].url);
        if (f.info.source_url[0]) {
            section(tr("Source"));
            ImGui::TextDisabled("%s", f.info.source_url);
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Controls

void App::draw_controls() {
    RecompLauncherCSettings* io = s.io;
    const RecompLauncherCGameInfo* g = s.game;
    const TitleLayer& t = *title;
    title_text(tr("Controls"), nullptr);
    if (!io || !g) return;
    // One scale for every fixed size on the page, so the scheme buttons, the
    // player tabs and the binding cells line up at any window size.
    const float u = std::max(1.0f, ImGui::GetStyle().FontScaleMain);
    const float avail = ImGui::GetContentRegionAvail().x;

    // Modern / Classic (title layer -> mod option)
    if (S("controls.scheme"))
        if (ModFeature* f = mods.find(t.modern_controls.package_id, t.modern_controls.feature_id)) {
            for (auto& o : f->options) {
                if (!t.modern_option || std::strcmp(o.info.id, t.modern_option)) continue;
                section(tr("Control scheme"));
                const bool modern = f->info.enabled && !std::strcmp(o.info.value, t.modern_value);
                const float bw = (avail - 10 * u) * 0.5f;
                ImGui::BeginDisabled(L("controls.scheme"));
                if (big_button(tr("Modern"), ImVec2(bw, 56 * u), modern)) {
                    if (!f->info.enabled) mods.set_enabled(*f, true);
                    mods.set_option(*f, o, t.modern_value);
                }
                ImGui::SameLine(0, 10 * u);
                if (big_button(tr("Classic"), ImVec2(bw, 56 * u), !modern)) mods.set_option(*f, o, t.classic_value);
                ImGui::EndDisabled();
                ImGui::TextDisabled("%s", modern ? tr("Analog steering and triggers, camera on the right stick.")
                                                 : tr("The original 1998 button layout."));
            }
        }

    // Players: P1 is selected until the player picks another; everything
    // below (device, controller type, bindings) is that player's.
    const int players = std::min(std::max(1, t.max_players), g->num_players > 0 ? g->num_players : t.max_players);
    const bool per_player = S("controls.devices") || S("controls.profile") || S("controls.deadzone") || S("controls.bindings");
    if (per_player) {
        section(tr("Players"));
        const float gap = 8 * u;
        const float bw = (avail - gap * (players - 1)) / players;
        for (int p = 0; p < players; ++p) {
            if (p) ImGui::SameLine(0, gap);
            char lbl[8];
            std::snprintf(lbl, sizeof(lbl), "P%d", p + 1);
            if (big_button(lbl, ImVec2(bw, 40 * u), controls_player == p)) controls_player = p;
            script_mark(lbl);
        }
    }
    const int pl = std::min(controls_player, players - 1);
    if (per_player) {
        char head[32];
        std::snprintf(head, sizeof head, "%s %d", tr("Player"), pl + 1);
        section(head);
    }
    if (S("controls.devices")) {
        ImGui::BeginDisabled(L("controls.devices") || g->lock_device);
        static const char* kSrc[] = {"None", "Keyboard", "Gamepad"};
        const char* l[3] = {tr(kSrc[0]), tr(kSrc[1]), tr(kSrc[2])};
        row_combo(tr("Input device"), &io->player_src[pl], l, 3);
        if (io->player_src[pl] == 2) {
            int count = 0;
            SDL_JoystickID* ids = SDL_GetGamepads(&count);
            std::vector<std::string> names{tr("Any connected gamepad")};
            std::vector<std::string> guids{""};
            std::vector<SDL_JoystickID> insts{0};
            for (int i = 0; ids && i < count; ++i) {
                const char* n = SDL_GetGamepadNameForID(ids[i]);
                char gs[40];
                SDL_GUIDToString(SDL_GetGamepadGUIDForID(ids[i]), gs, sizeof(gs));
                names.push_back(n ? n : "Gamepad");
                guids.push_back(gs);
                insts.push_back(ids[i]);
            }
            SDL_free(ids);
            int cur = 0;
            for (size_t i = 0; i < guids.size(); ++i)
                if (guids[i] == io->player_gamepad_guid[pl]) cur = static_cast<int>(i);
            std::vector<const char*> c;
            for (auto& n : names) c.push_back(n.c_str());
            if (row_combo(tr("Gamepad"), &cur, c.data(), static_cast<int>(c.size()))) {
                std::snprintf(io->player_gamepad_guid[pl], sizeof(io->player_gamepad_guid[pl]), "%s", guids[cur].c_str());
                io->player_gamepad_instance[pl] = static_cast<uint32_t>(insts[cur]);
            }
        }
        ImGui::EndDisabled();
    }
    if (S("controls.profile") && g->pad_mode_supported) {
        // Controller type: the title's profiles (DualShock, Digital, NeGcon, JogCon).
        std::vector<const char*> labels;
        int cur = -1;
        for (int i = 0; i < t.controller_count; ++i) {
            labels.push_back(tr(t.controllers[i].label));
            const ControllerProfile& cp = t.controllers[i];
            ModFeature* f = cp.feature.package_id ? mods.find(cp.feature.package_id, cp.feature.feature_id) : nullptr;
            if (cp.pad_mode == io->pad_mode[pl] && (!f || f->info.enabled) && cur < 0) cur = i;
        }
        if (cur < 0) cur = 0;
        ImGui::BeginDisabled(L("controls.profile") || !g->pad_mode_selectable || g->locked_pad_mode);
        if (!labels.empty() &&
            row_combo(tr("Controller"), &cur, labels.data(), static_cast<int>(labels.size()), t.controllers[cur].detail)) {
            const ControllerProfile& cp = t.controllers[cur];
            io->pad_mode[pl] = cp.pad_mode;
            if (cp.feature.package_id)
                if (ModFeature* f = mods.find(cp.feature.package_id, cp.feature.feature_id)) mods.set_enabled(*f, true);
        }
        ImGui::EndDisabled();
    }
    if (S("controls.deadzone") && g->has_deadzone_pct) row_slider(tr("Stick deadzone"), &io->deadzone[pl], 0, 50, "%d%%");
    if (S("controls.multitap") && players > 2) {
        section(tr("Multitap"));
        row_toggle(tr("Multitap (3-4 players)"), &io->multitap_enabled);
        row_toggle(tr("Analog on multitap"), &io->multitap_analog);
    }
    if (S("controls.mouse") && g->has_mouse_controls) {
        section(tr("Mouse"));
        row_toggle(tr("Mouse controls"), &io->mouse_enabled);
    }
    if (S("controls.force_feedback")) {
        section(tr("Force feedback"));
        ImGui::BeginDisabled(L("controls.force_feedback"));
        row_toggle(tr("Wheel force feedback (experimental)"), &io->jogcon_force_feedback,
                   tr("For force-feedback steering wheels used as a JogCon. "
                      "Gamepads are never affected. Off by default."));
        ImGui::EndDisabled();
    }

    if (S("controls.bindings") && !g->hide_rebind) {
        section(tr("Bindings"));
        const int kp = pl < kPsxKeyboardPlayers ? pl : 0;
        const float bh = 36 * u;
        const float tb = (avail - 2 * 8 * u) / 3.0f;
        if (big_button(tr("Map all keys"), ImVec2(tb, bh), false)) {
            capture = Capture{};
            capture.kind = Capture::Key;
            capture.map_all = true;
            capture.player = kp;
            capture.started = time;
        }
        ImGui::SameLine(0, 8 * u);
        if (big_button(tr("Map all gamepad buttons"), ImVec2(tb, bh), false)) {
            capture = Capture{};
            capture.kind = Capture::PadSource;
            capture.map_all = true;
            capture.started = time;
        }
        ImGui::SameLine(0, 8 * u);
        if (big_button(tr("Reset bindings"), ImVec2(tb, bh), false)) {
            s.keys = KeyboardBinds::defaults();
            s.pads.global = PadMapping::defaults();
            s.binds_dirty = true;
        }
        ImGui::Dummy(ImVec2(0, 4 * u));
        // The map: PlayStation input, then its keyboard and gamepad sources
        // for this player. Every cell is a button filling the cell.
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6 * u, 4 * u));
        if (ImGui::BeginTable("##binds", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                                                 ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn(tr("PlayStation"), ImGuiTableColumnFlags_WidthStretch, 1.1f);
            ImGui::TableSetupColumn(tr("Key"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(tr("Alt key"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(tr("Gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(tr("Alt gamepad"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();
            PadMapping* pm = s.pads.for_guid("", false);
            for (int i = 0; i < kPsxInputCount; ++i) {
                ImGui::PushID(i);
                ImGui::TableNextRow(0, theme().row_h);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(tr(kPsxInputs[i].label));
                auto cell = [&](const char* id, const std::string& v, Capture::Kind k, bool alt) {
                    ImGui::TableNextColumn();
                    if (ImGui::Button((std::string(v.empty() ? "-" : v) + "##" + id).c_str(), ImVec2(-FLT_MIN, 0))) {
                        capture = Capture{};
                        capture.kind = k;
                        capture.player = kp;
                        capture.input = i;
                        capture.alt = alt;
                        capture.started = time;
                    }
                };
                const std::string& src = pm->source[i];
                const size_t c = src.find(',');
                cell("k", s.keys.player[kp][i].primary, Capture::Key, false);
                cell("a", s.keys.player[kp][i].alt, Capture::Key, true);
                cell("p", trim(c == std::string::npos ? src : src.substr(0, c)), Capture::PadSource, false);
                cell("q", c == std::string::npos ? "" : trim(src.substr(c + 1)), Capture::PadSource, true);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    // Shortcuts (assist bindings: Rewind, Save states, Fast-forward...)
    if (S("controls.assist") && g->assist_binding_count > 0 && g->assist_binding_labels) {
        section(tr("Shortcut buttons"));
        for (int a = 0; a < g->assist_binding_count && a < RECOMP_LAUNCHER_MAX_ASSIST_BINDINGS; ++a) {
            ImGui::PushID(1000 + a);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(tr(g->assist_binding_labels[a]));
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.52f);
            const std::string v = describe_pad_value(io->assist_pad_bind[a]);
            if (ImGui::Button((v + "##pv").c_str(), ImVec2(240, 0))) {
                capture = Capture{};
                capture.kind = Capture::PadValue;
                capture.input = a;
                capture.started = time;
            }
            script_mark((std::string("assist:") + g->assist_binding_labels[a]).c_str());
            ImGui::SameLine();
            if (ImGui::Button(tr("Clear"))) io->assist_pad_bind[a] = 0;
            if (g->assist_default_key_bind && g->assist_default_key_bind[a] > 0 && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s %s", tr("Keyboard:"), SDL_GetScancodeName(static_cast<SDL_Scancode>(g->assist_default_key_bind[a])));
            if (g->assist_default_pad_bind) {
                ImGui::SameLine();
                if (ImGui::Button(tr("Default"))) io->assist_pad_bind[a] = g->assist_default_pad_bind[a];
            }
            ImGui::PopID();
        }
    }
}

}  // namespace r4l
