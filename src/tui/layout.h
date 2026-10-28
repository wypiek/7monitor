#pragma once
// Pure dashboard rendering: Snapshot + UiState -> Canvas. No console access, so every
// view can be rendered at arbitrary sizes in tests.
#include <string>

#include "sampler/sampler.h"
#include "tui/canvas.h"
#include "tui/ui_state.h"

namespace sm {

constexpr int kMinDashboardWidth = 40;
constexpr int kMinDashboardHeight = 10;

// clock: preformatted local time shown in the header (passed in for determinism).
void renderDashboard(Canvas& c, const Snapshot& s, UiState& ui, const Theme& t, const std::string& clock);

}  // namespace sm
