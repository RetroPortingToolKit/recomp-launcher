// quality.h — Graphics preset tracking (Low/Medium/High/Ultra/Custom).
//
// The host owns detection and what each preset sets (GameInfo.quality_apply).
// The launcher only has to know which Settings bytes a preset governs, so it
// can flip the preset to Custom when the player edits one of them. It learns
// that without a field list: the preset is applied to an all-zero and an
// all-ones scratch copy, and every byte that comes out equal in both was
// written by the preset.
#pragma once

#include "recomp_launcher.h"

#include <cstdint>
#include <vector>

namespace r4l {

enum QualityPreset : int {
    kQualityUnset = 0,
    kQualityLow = 1,
    kQualityMedium = 2,
    kQualityHigh = 3,
    kQualityUltra = 4,
    kQualityCustom = 5,
};

const char* quality_name(int preset);

class QualityTracker {
public:
    // Host hooks; null apply means presets are not offered. apply must be a
    // pure preset -> rows mapping: the in-game menu calls it while drawing.
    using ApplyFn = void (*)(int preset, RecompLauncherCSettings* s);

    void bind(ApplyFn apply, int offered_mask);
    bool offered() const { return apply_ != nullptr && offered_mask_ != 0; }
    bool preset_offered(int preset) const;

    // Player picked a preset: apply it to *s and remember the governed bytes.
    void select(int preset, RecompLauncherCSettings* s);

    // On open with a preset already in force: learn its mask without
    // changing *s (the host's apply is idempotent for the preset in force).
    void adopt(const RecompLauncherCSettings& s);

    // Call once per frame after UI edits. Returns true when it switched the
    // preset to Custom (quality_base keeps the preset it started from).
    bool observe(RecompLauncherCSettings* s);

    // Bytes the current preset governs (testing / diagnostics).
    size_t governed_bytes() const;

private:
    void learn(int preset);

    ApplyFn apply_ = nullptr;
    int offered_mask_ = 0;
    int learned_for_ = 0;
    std::vector<uint8_t> mask_;      // 1 = governed byte
    std::vector<uint8_t> snapshot_;  // settings right after the preset applied
};

}  // namespace r4l
