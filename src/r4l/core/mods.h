// mods.h — read model over GameInfo.mods (RecompLauncherCModProvider).
//
// Features are the unit the player toggles; options hang off features;
// "groups" are the free-form group string on each feature/option.
#pragma once

#include "recomp_launcher.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace r4l {

struct ModOption {
    RecompLauncherCModOption info{};
    std::vector<RecompLauncherCModChoice> choices;
};

struct ModFeature {
    RecompLauncherCModFeature info{};
    std::vector<ModOption> options;
    std::vector<RecompLauncherCModDiagnostic> diagnostics;
    std::vector<RecompLauncherCModResource> resources;
};

struct ModPackageView {
    RecompLauncherCModPackage info{};
    std::vector<RecompLauncherCModVersion> versions;
    std::vector<ModOption> options;  // legacy package-level options
};

struct ModGroup {
    std::string name;
    std::vector<int> features;  // indexes into ModCatalog::features
};

bool zip_has_manifest(const std::string& path, std::string* err);

class ModCatalog {
public:
    void bind(const RecompLauncherCModProvider* p) { p_ = p; }
    bool available() const { return p_ && p_->feature_count && p_->feature_get; }
    void refresh(bool include_developer);

    std::vector<ModFeature> features;
    std::vector<ModGroup> groups;
    std::vector<RecompLauncherCModPackage> packages;
    std::vector<ModPackageView> package_views;
    std::vector<RecompLauncherCModDiagnostic> catalog_diagnostics;

    ModFeature* find(const char* package_id, const char* feature_id);
    bool set_enabled(ModFeature& f, bool on);
    bool set_option(ModFeature& f, const ModOption& o, const std::string& value);
    bool commit(const std::string& disc_path, std::string* err);
    std::string last_error() const;

    // Install / remove packages (launcher only; not while a game runs).
    bool can_install() const { return p_ && p_->install_archive; }
    // Checks the file is a zip whose entries include a manifest.toml before
    // handing it to the provider, which does the real validation.
    bool install(const std::string& archive_path, std::string* err);
    bool remove(const RecompLauncherCModPackage& pkg, std::string* err);
    std::string archive_patterns() const;  // "psxmod;zip"
    bool select_version(const RecompLauncherCModPackage& pkg, const char* version);
    bool set_package_enabled(ModPackageView& pkg, bool on);
    bool set_package_option(ModPackageView& pkg, ModOption& o, const std::string& value);
    bool set_resource(ModFeature& f, const RecompLauncherCModResource& r, const std::string& path);
    int disable_all();
    bool has_versions() const { return p_ && p_->version_count; }
    bool dirty() const { return dirty_; }
    // Overlay: the host records, saves (mods/state.toml) and applies every
    // edit; returns 1 if live, 0 if it waits for a restart (needs_restart).
    std::function<int(const char* pkg, const char* feature, const char* option, const char* value)> live;
    std::set<std::string> needs_restart;  // "pkg/feature"

private:
    const RecompLauncherCModProvider* p_ = nullptr;
    bool dirty_ = false;
    unsigned long long prepared_revision_ = ~0ull;
};

}  // namespace r4l
