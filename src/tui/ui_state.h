#pragma once
// Dashboard state and keyboard handling. Pure logic (no console calls) so navigation,
// selection, filtering and the kill confirmation flow are unit-testable.
#include <cstdint>
#include <string>
#include <vector>

#include "collectors/process.h"

namespace sm {

struct Snapshot;

enum class View { Overview, Processes, ProcessDetail, Cpu, Memory, Gpu, Disk, Network, Help };

enum class Key { None, Char, Up, Down, PageUp, PageDown, Home, End, Enter, Escape, Backspace, Left, Right, Tab };

struct KeyEvent {
  Key key = Key::None;
  wchar_t ch = 0;
};

enum class Action { None, Redraw, Refresh, Quit, Kill };

struct UiState {
  View view = View::Overview;
  View returnView = View::Processes;  // where Esc goes from the detail view
  ProcessSort sort = ProcessSort::Cpu;
  std::string filter;
  bool editingFilter = false;
  std::string filterDraft;
  int selected = 0;
  int scroll = 0;
  int pageRows = 10;  // set by the layout each frame
  uint32_t selectedPid = 0;
  uint32_t detailPid = 0;
  bool confirmKill = false;
  uint32_t killPid = 0;
  std::string killName;
  std::string message;
  unsigned intervalMs = 1000;
  bool paused = false;
};

// Processes visible in the process view (filtered + sorted).
std::vector<ProcessInfo> visibleProcesses(const std::vector<ProcessInfo>& all, const UiState& ui);

// Re-anchors the selection on selectedPid after the list changed and clamps scroll.
void syncSelection(UiState& ui, const std::vector<ProcessInfo>& visible);

Action handleKey(UiState& ui, const KeyEvent& ev, const std::vector<ProcessInfo>& visible);

const char* viewName(View v);

}  // namespace sm
