#include "tui/ui_state.h"

#include <algorithm>

#include "util/strings.h"

namespace sm {

const char* viewName(View v) {
  switch (v) {
    case View::Overview: return "Overview";
    case View::Processes: return "Processes";
    case View::ProcessDetail: return "Process";
    case View::Cpu: return "CPU";
    case View::Memory: return "Memory";
    case View::Gpu: return "GPU";
    case View::Disk: return "Disk";
    case View::Network: return "Network";
    case View::Help: return "Help";
  }
  return "";
}

std::vector<ProcessInfo> visibleProcesses(const std::vector<ProcessInfo>& all, const UiState& ui) {
  std::vector<ProcessInfo> out;
  out.reserve(all.size());
  for (const auto& p : all) {
    if (ui.filter.empty() || icontains(p.name, ui.filter) || std::to_string(p.pid) == ui.filter) out.push_back(p);
  }
  sortProcesses(out, ui.sort);
  return out;
}

void syncSelection(UiState& ui, const std::vector<ProcessInfo>& visible) {
  int n = static_cast<int>(visible.size());
  if (ui.selectedPid != 0) {
    for (int i = 0; i < n; ++i) {
      if (visible[static_cast<size_t>(i)].pid == ui.selectedPid) {
        ui.selected = i;
        break;
      }
    }
  }
  if (n == 0) {
    ui.selected = 0;
    ui.scroll = 0;
    ui.selectedPid = 0;
    return;
  }
  ui.selected = std::max(0, std::min(ui.selected, n - 1));
  ui.selectedPid = visible[static_cast<size_t>(ui.selected)].pid;
  int rows = std::max(1, ui.pageRows);
  if (ui.selected < ui.scroll) ui.scroll = ui.selected;
  if (ui.selected >= ui.scroll + rows) ui.scroll = ui.selected - rows + 1;
  ui.scroll = std::max(0, std::min(ui.scroll, std::max(0, n - rows)));
}

static void move(UiState& ui, int delta, const std::vector<ProcessInfo>& visible) {
  int n = static_cast<int>(visible.size());
  if (n == 0) return;
  ui.selected = std::max(0, std::min(n - 1, ui.selected + delta));
  ui.selectedPid = visible[static_cast<size_t>(ui.selected)].pid;
  syncSelection(ui, visible);
}

Action handleKey(UiState& ui, const KeyEvent& ev, const std::vector<ProcessInfo>& visible) {
  // Modal: kill confirmation accepts only Y / N / Esc.
  if (ui.confirmKill) {
    if (ev.key == Key::Char && (ev.ch == L'y' || ev.ch == L'Y')) {
      ui.confirmKill = false;
      return Action::Kill;
    }
    if (ev.key == Key::Escape || (ev.key == Key::Char && (ev.ch == L'n' || ev.ch == L'N'))) {
      ui.confirmKill = false;
      ui.message = "Termination cancelled.";
      return Action::Redraw;
    }
    return Action::None;
  }
  // Modal: filter editing.
  if (ui.editingFilter) {
    if (ev.key == Key::Enter) {
      ui.editingFilter = false;
      ui.filter = ui.filterDraft;
      ui.selected = 0;
      ui.scroll = 0;
      ui.selectedPid = 0;
    } else if (ev.key == Key::Escape) {
      ui.editingFilter = false;
    } else if (ev.key == Key::Backspace) {
      if (!ui.filterDraft.empty()) {
        // Remove one UTF-8 code point.
        size_t i = ui.filterDraft.size() - 1;
        while (i > 0 && (static_cast<unsigned char>(ui.filterDraft[i]) & 0xC0) == 0x80) --i;
        ui.filterDraft.erase(i);
      }
    } else if (ev.key == Key::Char && ev.ch >= 0x20 && ui.filterDraft.size() < 64) {
      ui.filterDraft += toUtf8(std::wstring(1, ev.ch));
    }
    return Action::Redraw;
  }

  bool listView = ui.view == View::Processes;
  switch (ev.key) {
    case Key::Up: if (listView) move(ui, -1, visible); return Action::Redraw;
    case Key::Down: if (listView) move(ui, 1, visible); return Action::Redraw;
    case Key::PageUp: if (listView) move(ui, -std::max(1, ui.pageRows - 1), visible); return Action::Redraw;
    case Key::PageDown: if (listView) move(ui, std::max(1, ui.pageRows - 1), visible); return Action::Redraw;
    case Key::Home: if (listView) move(ui, -1000000, visible); return Action::Redraw;
    case Key::End: if (listView) move(ui, 1000000, visible); return Action::Redraw;
    case Key::Enter:
      if (listView && !visible.empty()) {
        ui.detailPid = visible[static_cast<size_t>(ui.selected)].pid;
        ui.returnView = View::Processes;
        ui.view = View::ProcessDetail;
      }
      return Action::Redraw;
    case Key::Escape:
      if (ui.view == View::ProcessDetail) ui.view = ui.returnView;
      else if (!ui.filter.empty() && listView) ui.filter.clear();
      else ui.view = View::Overview;
      return Action::Redraw;
    case Key::Char: break;
    default: return Action::None;
  }
  wchar_t c = ev.ch;
  if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
  switch (c) {
    case L'q': return Action::Quit;
    case L'r': return Action::Refresh;
    case L'o': case L'1': ui.view = View::Overview; return Action::Redraw;
    case L'p': ui.view = View::Processes; return Action::Redraw;
    case L'c': ui.view = View::Cpu; return Action::Redraw;
    case L'm': ui.view = View::Memory; return Action::Redraw;
    case L'g': ui.view = View::Gpu; return Action::Redraw;
    case L'd': ui.view = View::Disk; return Action::Redraw;
    case L'n': ui.view = View::Network; return Action::Redraw;
    case L'h': case L'?': ui.view = View::Help; return Action::Redraw;
    case L' ':
      ui.paused = !ui.paused;
      ui.message = ui.paused ? "Paused (space to resume)" : "Resumed";
      return Action::Redraw;
    case L's':
      ui.sort = ui.sort == ProcessSort::Cpu      ? ProcessSort::Memory
                : ui.sort == ProcessSort::Memory ? ProcessSort::Pid
                : ui.sort == ProcessSort::Pid    ? ProcessSort::Name
                                                 : ProcessSort::Cpu;
      ui.message = std::string("Sorted by ") + processSortName(ui.sort);
      return Action::Redraw;
    case L'/':
      if (ui.view != View::Processes) ui.view = View::Processes;
      ui.editingFilter = true;
      ui.filterDraft = ui.filter;
      return Action::Redraw;
    case L'+':
      ui.intervalMs = std::min(60000u, ui.intervalMs + (ui.intervalMs >= 2000 ? 1000u : 250u));
      ui.message = "Refresh interval " + std::to_string(ui.intervalMs) + " ms";
      return Action::Redraw;
    case L'-':
      ui.intervalMs = std::max(250u, ui.intervalMs - (ui.intervalMs > 2000 ? 1000u : 250u));
      ui.message = "Refresh interval " + std::to_string(ui.intervalMs) + " ms";
      return Action::Redraw;
    case L'k': {
      uint32_t pid = 0;
      std::string name;
      if (ui.view == View::ProcessDetail) {
        pid = ui.detailPid;
        for (const auto& p : visible)
          if (p.pid == pid) name = p.name;
      } else if (listView && !visible.empty()) {
        pid = visible[static_cast<size_t>(ui.selected)].pid;
        name = visible[static_cast<size_t>(ui.selected)].name;
      }
      if (pid == 0) {
        ui.message = "Select a process first (P, then Up/Down).";
        return Action::Redraw;
      }
      ui.confirmKill = true;
      ui.killPid = pid;
      ui.killName = name.empty() ? "PID " + std::to_string(pid) : name;
      return Action::Redraw;
    }
    default: return Action::None;
  }
}

}  // namespace sm
