#pragma once

#include "runtime/entry.h"

namespace CometEditor {
    Comet::Result<void, Comet::Error> run_editor(const Comet::LaunchOptions& options);
}
