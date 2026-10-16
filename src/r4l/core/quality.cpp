#include "quality.h"

#include <cstddef>
#include <cstring>

namespace r4l {

namespace {
// Never treat these as governed: they are launcher bookkeeping or transient
// outputs, and a preset that happens to touch them must not flip Custom.
bool excluded_offset(size_t off) {
    const size_t ql = offsetof(RecompLauncherCSettings, quality_preset);
    const size_t qb = offsetof(RecompLauncherCSettings, quality_base);
    const size_t nl = offsetof(RecompLauncherCSettings, netplay_launch);
    if (off >= ql && off < ql + sizeof(int)) return true;
    if (off >= qb && off < qb + sizeof(int)) return true;
    if (off >= nl && off < nl + sizeof(RecompLauncherCNetplayLaunch)) return true;
    return false;
}
}  // namespace

const char* quality_name(int preset) {
    switch (preset) {
    case kQualityLow: return "Low";
    case kQualityMedium: return "Medium";
    case kQualityHigh: return "High";
    case kQualityUltra: return "Ultra";
    case kQualityCustom: return "Custom";
    default: return "Not set";
    }
}

void QualityTracker::bind(ApplyFn apply, int offered_mask) {
    apply_ = apply;
    offered_mask_ = offered_mask;
    learned_for_ = 0;
    mask_.clear();
    snapshot_.clear();
}

bool QualityTracker::preset_offered(int preset) const {
    if (preset < kQualityLow || preset > kQualityUltra) return false;
    return (offered_mask_ >> (preset - 1)) & 1;
}

void QualityTracker::learn(int preset) {
    if (learned_for_ == preset && !mask_.empty()) return;
    const size_t n = sizeof(RecompLauncherCSettings);
    std::vector<uint8_t> lo(n, 0x00), hi(n, 0xFF);
    apply_(preset, reinterpret_cast<RecompLauncherCSettings*>(lo.data()));
    apply_(preset, reinterpret_cast<RecompLauncherCSettings*>(hi.data()));
    mask_.assign(n, 0);
    for (size_t i = 0; i < n; ++i)
        mask_[i] = (lo[i] == hi[i] && !excluded_offset(i)) ? 1 : 0;
    learned_for_ = preset;
}

void QualityTracker::select(int preset, RecompLauncherCSettings* s) {
    if (!offered() || !preset_offered(preset) || !s) return;
    learn(preset);
    apply_(preset, s);  // the host maps the preset to rows (pure: safe in-game)
    s->quality_preset = preset;
    s->quality_base = preset;
    snapshot_.assign(reinterpret_cast<const uint8_t*>(s),
                     reinterpret_cast<const uint8_t*>(s) + sizeof(*s));
}

void QualityTracker::adopt(const RecompLauncherCSettings& s) {
    if (!offered()) return;
    if (s.quality_preset < kQualityLow || s.quality_preset > kQualityUltra) return;
    RecompLauncherCSettings probe = s;
    learn(s.quality_preset);
    apply_(s.quality_preset, &probe);
    // Snapshot what the preset says the governed rows should hold. If the
    // saved settings already differ there, observe() reports Custom at once.
    snapshot_.assign(reinterpret_cast<const uint8_t*>(&probe),
                     reinterpret_cast<const uint8_t*>(&probe) + sizeof(probe));
}

bool QualityTracker::observe(RecompLauncherCSettings* s) {
    if (!s || mask_.empty() || snapshot_.size() != sizeof(*s)) return false;
    if (s->quality_preset < kQualityLow || s->quality_preset > kQualityUltra) return false;
    const uint8_t* cur = reinterpret_cast<const uint8_t*>(s);
    for (size_t i = 0; i < sizeof(*s); ++i) {
        if (mask_[i] && cur[i] != snapshot_[i]) {
            s->quality_base = s->quality_preset;
            s->quality_preset = kQualityCustom;
            return true;
        }
    }
    return false;
}

size_t QualityTracker::governed_bytes() const {
    size_t n = 0;
    for (uint8_t b : mask_) n += b;
    return n;
}

}  // namespace r4l
