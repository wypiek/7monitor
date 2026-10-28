#pragma once
#include "app/cli.h"

namespace sm {

// Executes a parsed command line. Returns the process exit code.
int runCommand(const CliOptions& opts);

int runDoctor(const CliOptions& opts);

}  // namespace sm
