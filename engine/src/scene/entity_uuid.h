#pragma once

#include "common/uuid.h"

namespace Comet {
    using EntityUuid = Uuid;

    inline constexpr EntityUuid INVALID_ENTITY_UUID{};
}
