#include "render/post_process.h"

#include <cmath>

namespace Comet {
    Result<void> PostProcessSettings::validate() const {
        if(!std::isfinite(exposure) || exposure < 0 || exposure > 100)
            return Result<void>::failure("exposure must be finite and between 0 and 100");
        if(!std::isfinite(bloom_strength) || bloom_strength < 0 || bloom_strength > 10)
            return Result<void>::failure("bloom_strength must be finite and between 0 and 10");
        if(!std::isfinite(bloom_threshold) || bloom_threshold < 0 || bloom_threshold > 65504)
            return Result<void>::failure("bloom_threshold must be finite and between 0 and 65504");
        return Result<void>::success();
    }
}
