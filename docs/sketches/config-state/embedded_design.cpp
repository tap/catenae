// Copyright 2026 Timothy Place and the catenae contributors
// The designer, in a file of its own so that what it pulls in can be seen apart from the audio
// path. Throwaway; built by measure.sh.
#include "sketch.h"

using namespace catenae;

void set_design(const lowpass_design<float>& design) noexcept;

/// Called from a control path, never from the audio callback.
bool set_cutoff(double cutoff) noexcept {
    const auto designed = lowpass_design<float>::make(cutoff);
    if (!designed) {
        return false;
    }
    set_design(*designed);
    return true;
}
