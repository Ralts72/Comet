#pragma once

#include "runtime/entry.h"

namespace CometApp {
    Comet::Result<void, Comet::Error> run_game(const Comet::LaunchOptions& options);
}
