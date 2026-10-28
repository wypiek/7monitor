#pragma once
#include "app/cli.h"

namespace sm {

// Runs the interactive dashboard on the console. Requires a real console for both
// input and output. Returns a process exit code.
int runDashboard(const CliOptions& opts);

}  // namespace sm
