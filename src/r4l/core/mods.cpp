#include "mods.h"

#include <cstdio>
#include <cstring>

namespace r4l {

void ModCatalog::refresh(bool include_developer) {
    features.clear();
    groups.clear();
    packages.clear();
    if (!p_) return;
    if (p_->package_count && p_->package_get) {
        const int n = p_->package_count(p_->ctx);
        for (int i = 0; i < n; ++i) {
            RecompLauncherCModPackage pk{};
            if (p_->package_get(p_->ctx, i, &pk)) packages.push_back(pk);
        }
    }
    package_views.clear();
    catalog_diagnostics.clear();
    for (const auto& pk : packages) {
        ModPackageView v;
        v.info = pk;
        if (p_->version_count && p_->version_get)
            for (int i = 0, n = p_->version_count(p_->ctx, pk.id); i < n; ++i) {
                RecompLauncherCModVersion ver{};
                if (p_->version_get(p_->ctx, pk.id, i, &ver)) v.versions.push_back(ver);
            }
        if (p_->option_get)
            for (int i = 0; i < pk.option_count; ++i) {
                ModOption o;
                if (!p_->option_get(p_->ctx, pk.id, i, &o.info)) continue;
                for (int c = 0; c < o.info.choice_count && p_->choice_get; ++c) {
                    RecompLauncherCModChoice ch{};
                    if (p_->choice_get(p_->ctx, pk.id, o.info.id, c, &ch)) o.choices.push_back(ch);
                }
                v.options.push_back(o);
            }
        package_views.push_back(v);
    }
    if (p_->catalog_diagnostic_count && p_->catalog_diagnostic_get)
        for (int i = 0, n = p_->catalog_diagnostic_count(p_->ctx); i < n; ++i) {
            RecompLauncherCModDiagnostic d{};
            if (p_->catalog_diagnostic_get(p_->ctx, i, &d)) catalog_diagnostics.push_back(d);
        }
    if (!available()) return;
    const int n = p_->feature_count(p_->ctx);
    for (int i = 0; i < n; ++i) {
        ModFeature f;
        if (!p_->feature_get(p_->ctx, i, &f.info)) continue;
        if (f.info.hidden && p_->hide_hidden_features) continue;
        if (f.info.channel == RECOMP_MOD_CHANNEL_DEVELOPER && !include_developer) continue;
        for (int k = 0; k < f.info.option_count && p_->feature_option_get; ++k) {
            ModOption o;
            if (!p_->feature_option_get(p_->ctx, f.info.package_id, f.info.id, k, &o.info)) continue;
            for (int c = 0; c < o.info.choice_count && p_->feature_choice_get; ++c) {
                RecompLauncherCModChoice ch{};
                if (p_->feature_choice_get(p_->ctx, f.info.package_id, f.info.id, o.info.id, c, &ch))
                    o.choices.push_back(ch);
            }
            f.options.push_back(o);
        }
        if (p_->diagnostic_count && p_->diagnostic_get) {
            const int d = p_->diagnostic_count(p_->ctx, f.info.package_id, f.info.id);
            for (int k = 0; k < d; ++k) {
                RecompLauncherCModDiagnostic dg{};
                if (p_->diagnostic_get(p_->ctx, f.info.package_id, f.info.id, k, &dg))
                    f.diagnostics.push_back(dg);
            }
        }
        if (p_->feature_resource_count && p_->feature_resource_get)
            for (int k = 0, rn = p_->feature_resource_count(p_->ctx, f.info.package_id, f.info.id); k < rn; ++k) {
                RecompLauncherCModResource r{};
                if (p_->feature_resource_get(p_->ctx, f.info.package_id, f.info.id, k, &r)) f.resources.push_back(r);
            }
        features.push_back(f);
    }
    for (size_t i = 0; i < features.size(); ++i) {
        std::string g = features[i].info.group[0] ? features[i].info.group : "General";
        ModGroup* grp = nullptr;
        for (auto& x : groups)
            if (x.name == g) grp = &x;
        if (!grp) {
            groups.push_back(ModGroup{g, {}});
            grp = &groups.back();
        }
        grp->features.push_back(static_cast<int>(i));
    }
}

ModFeature* ModCatalog::find(const char* package_id, const char* feature_id) {
    if (!package_id || !feature_id) return nullptr;
    for (auto& f : features)
        if (!std::strcmp(f.info.package_id, package_id) && !std::strcmp(f.info.id, feature_id)) return &f;
    return nullptr;
}

bool ModCatalog::set_enabled(ModFeature& f, bool on) {
    // In-game (live set): the host records and saves the change on its own
    // thread; this side only mirrors it. In the launcher, the provider does.
    if (!live) {
        if (!p_ || !p_->feature_enable) return false;
        if (!p_->feature_enable(p_->ctx, f.info.package_id, f.info.id, on ? 1 : 0)) return false;
    }
    f.info.enabled = on ? 1 : 0;
    dirty_ = true;
    if (live && !live(f.info.package_id, f.info.id, nullptr, on ? "true" : "false"))
        needs_restart.insert(std::string(f.info.package_id) + "/" + f.info.id);
    return true;
}

bool ModCatalog::set_option(ModFeature& f, const ModOption& o, const std::string& value) {
    if (!live) {
        if (!p_ || !p_->feature_set_option) return false;
        if (!p_->feature_set_option(p_->ctx, f.info.package_id, f.info.id, o.info.id, value.c_str()))
            return false;
    }
    for (auto& x : f.options)
        if (!std::strcmp(x.info.id, o.info.id))
            std::snprintf(x.info.value, sizeof(x.info.value), "%s", value.c_str());
    dirty_ = true;
    if (live && !live(f.info.package_id, f.info.id, o.info.id, value.c_str()))
        needs_restart.insert(std::string(f.info.package_id) + "/" + f.info.id);
    return true;
}

bool ModCatalog::commit(const std::string& disc_path, std::string* err) {
    if (!p_ || !p_->commit) return true;
    // Fast path: a plan the host already prepared in the background for this
    // selection (preparation_revision unchanged) is committed without redoing it.
    if (p_->try_commit && p_->commit_worker_safe && !dirty_ && p_->preparation_revision &&
        p_->preparation_revision(p_->ctx) == prepared_revision_ && p_->try_commit(p_->ctx, disc_path.c_str()) > 0)
        return true;
    if (!p_->commit(p_->ctx, disc_path.c_str())) {
        if (err) *err = last_error();
        return false;
    }
    dirty_ = false;
    if (p_->preparation_revision) prepared_revision_ = p_->preparation_revision(p_->ctx);
    return true;
}

std::string ModCatalog::last_error() const {
    if (!p_ || !p_->last_error) return "";
    const char* e = p_->last_error(p_->ctx);
    return e ? e : "";
}

}  // namespace r4l

namespace r4l {

bool zip_has_manifest(const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "Cannot open " + path;
        return false;
    }
    unsigned char sig[4] = {0};
    const size_t n = std::fread(sig, 1, 4, f);
    if (n != 4 || sig[0] != 'P' || sig[1] != 'K' || sig[2] != 3 || sig[3] != 4) {
        std::fclose(f);
        if (err) *err = "Not a mod archive (expected a .zip / .psxmod file)";
        return false;
    }
    // Walk local file headers looking for an entry named */manifest.toml.
    std::fseek(f, 0, SEEK_SET);
    bool found = false;
    unsigned char h[30];
    while (std::fread(h, 1, 30, f) == 30 && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4) {
        const unsigned flags = h[6] | (h[7] << 8);
        unsigned long comp = h[18] | (h[19] << 8) | (h[20] << 16) | ((unsigned long)h[21] << 24);
        const unsigned name_len = h[26] | (h[27] << 8), extra = h[28] | (h[29] << 8);
        std::string name(name_len, '\0');
        if (std::fread(&name[0], 1, name_len, f) != name_len) break;
        if (name == "manifest.toml" || (name.size() > 14 && name.compare(name.size() - 14, 14, "/manifest.toml") == 0)) {
            found = true;
            break;
        }
        if (flags & 8) break;  // sizes in a data descriptor: stop scanning, let the provider decide
        std::fseek(f, static_cast<long>(extra + comp), SEEK_CUR);
    }
    std::fclose(f);
    if (!found && err) *err = "The archive has no manifest.toml";
    return found;
}

bool ModCatalog::install(const std::string& path, std::string* err) {
    if (!can_install()) {
        if (err) *err = "This build cannot install mods";
        return false;
    }
    if (!zip_has_manifest(path, err)) return false;
    if (!p_->install_archive(p_->ctx, path.c_str())) {
        if (err) *err = last_error().empty() ? "The mod was rejected" : last_error();
        return false;
    }
    dirty_ = true;
    return true;
}

bool ModCatalog::remove(const RecompLauncherCModPackage& pkg, std::string* err) {
    if (!p_ || !p_->remove_package || !pkg.removable) {
        if (err) *err = "Bundled mods cannot be removed";
        return false;
    }
    if (!p_->remove_package(p_->ctx, pkg.id, pkg.version)) {
        if (err) *err = last_error().empty() ? "Could not remove the mod" : last_error();
        return false;
    }
    dirty_ = true;
    return true;
}

bool ModCatalog::select_version(const RecompLauncherCModPackage& pkg, const char* version) {
    if (!p_ || !p_->select_version || !p_->select_version(p_->ctx, pkg.id, version)) return false;
    dirty_ = true;
    return true;
}

bool ModCatalog::set_package_enabled(ModPackageView& pkg, bool on) {
    if (!p_ || !p_->set_enabled || !p_->set_enabled(p_->ctx, pkg.info.id, on ? 1 : 0)) return false;
    pkg.info.enabled = on ? 1 : 0;
    dirty_ = true;
    return true;
}

bool ModCatalog::set_package_option(ModPackageView& pkg, ModOption& o, const std::string& value) {
    if (!p_ || !p_->set_option || !p_->set_option(p_->ctx, pkg.info.id, o.info.id, value.c_str())) return false;
    std::snprintf(o.info.value, sizeof o.info.value, "%s", value.c_str());
    dirty_ = true;
    return true;
}

bool ModCatalog::set_resource(ModFeature& f, const RecompLauncherCModResource& r, const std::string& path) {
    if (!p_ || !p_->feature_resource_set_path ||
        !p_->feature_resource_set_path(p_->ctx, f.info.package_id, f.info.id, r.id, path.c_str()))
        return false;
    for (auto& x : f.resources)
        if (!std::strcmp(x.id, r.id)) std::snprintf(x.path, sizeof x.path, "%s", path.c_str());
    dirty_ = true;
    return true;
}

int ModCatalog::disable_all() {
    int n = 0;
    for (auto& f : features)
        if (f.info.enabled && set_enabled(f, false)) ++n;
    return n;
}

std::string ModCatalog::archive_patterns() const {
    std::string ext = (p_ && p_->archive_extension && *p_->archive_extension) ? p_->archive_extension : ".psxmod";
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    return ext == "zip" ? "zip" : ext + ";zip";
}

}  // namespace r4l
