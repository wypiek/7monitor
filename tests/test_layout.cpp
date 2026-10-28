#include "fake_snapshot.h"
#include "test.h"
#include "tui/layout.h"
#include "util/strings.h"

using namespace sm;

static const View kViews[] = {View::Overview, View::Processes, View::ProcessDetail, View::Cpu, View::Memory,
                              View::Gpu,      View::Disk,      View::Network,       View::Help};

static std::string render(const Snapshot& s, UiState& ui, int w, int h, bool color, bool unicode) {
  Theme t = Theme::make(color, unicode);
  Canvas c(w, h, t.normal, !unicode);
  renderDashboard(c, s, ui, t, "12:34:56");
  return c.allText();
}

TEST(layout_all_views_all_sizes) {
  Snapshot s = fakeSnapshot(false);
  const int sizes[][2] = {{40, 10}, {60, 15}, {80, 25}, {100, 30}, {120, 40}, {200, 60}, {250, 100}};
  for (View v : kViews) {
    for (const auto& sz : sizes) {
      for (int mode = 0; mode < 4; ++mode) {
        UiState ui;
        ui.view = v;
        ui.detailPid = 104;
        std::string txt = render(s, ui, sz[0], sz[1], mode & 1, (mode & 2) != 0);
        CHECK(txt.find("SevenMonitor") != std::string::npos);
        CHECK(txt.find("Q Quit") != std::string::npos);
        // every row is exactly the canvas width
        size_t rows = 0, pos = 0;
        while ((pos = txt.find('\n', pos)) != std::string::npos) {
          ++pos;
          ++rows;
        }
        CHECK_EQ(rows, static_cast<size_t>(sz[1]));
      }
    }
  }
}

TEST(layout_too_small_window) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  std::string txt = render(s, ui, 39, 9, true, true);
  CHECK(txt.find("too small") != std::string::npos);
  std::string tiny = render(s, ui, 5, 2, true, true);  // must not crash
  CHECK(!tiny.empty());
  Canvas empty(0, 0);
  Theme t = Theme::make(true, true);
  renderDashboard(empty, s, ui, t, "x");  // zero-size canvas is safe
}

TEST(layout_ascii_mode_is_pure_ascii) {
  Snapshot s = fakeSnapshot(false);
  for (View v : kViews) {
    UiState ui;
    ui.view = v;
    ui.detailPid = 120;  // the process with a non-ASCII name
    std::string txt = render(s, ui, 120, 40, false, false);
    for (unsigned char ch : txt) CHECK(ch < 0x80);
  }
}

TEST(layout_missing_sensors_show_na) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  ui.view = View::Gpu;
  std::string txt = render(s, ui, 100, 40, true, true);
  CHECK(txt.find("N/A") != std::string::npos);
  CHECK(txt.find("NVML not available") != std::string::npos);
  Snapshot s2 = fakeSnapshot(true);
  std::string txt2 = render(s2, ui, 100, 40, true, true);
  CHECK(txt2.find("51.0 C") != std::string::npos);
  CHECK(txt2.find("17.0%") != std::string::npos);
}

TEST(layout_overview_shows_real_values) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  std::string txt = render(s, ui, 120, 40, true, true);
  CHECK(txt.find("42.5%") != std::string::npos);  // CPU total
  CHECK(txt.find("62.5%") != std::string::npos);  // RAM
  CHECK(txt.find("Windows 7 Professional SP1") != std::string::npos);
  CHECK(txt.find("TESTBOX") != std::string::npos);
  CHECK(txt.find("122 KiB/s") != std::string::npos);  // network down 125000 B/s
}

TEST(layout_first_sample_rates_na) {
  Snapshot s = fakeSnapshot(false);
  s.cpu = CpuUsage();  // no second sample yet
  for (auto& n : s.network) n.rxBytesPerSec.reset(), n.txBytesPerSec.reset();
  UiState ui;
  std::string txt = render(s, ui, 120, 40, true, true);
  CHECK(txt.find("N/A") != std::string::npos);
  CHECK(txt.find("42.5%") == std::string::npos);
}

TEST(ui_selection_and_resize) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  ui.view = View::Processes;
  ui.sort = ProcessSort::Pid;
  render(s, ui, 80, 25, true, true);
  auto vis = visibleProcesses(s.processes, ui);
  for (int i = 0; i < 40; ++i) handleKey(ui, {Key::Down, 0}, vis);
  CHECK_EQ(ui.selected, 40);
  CHECK_EQ(ui.selectedPid, vis[40].pid);
  CHECK(ui.scroll > 0);
  // Shrink the console: selection must stay visible and anchored to the same PID.
  render(s, ui, 60, 12, true, true);
  CHECK_EQ(ui.selectedPid, vis[40].pid);
  CHECK(ui.selected >= ui.scroll && ui.selected < ui.scroll + ui.pageRows);
  // Grow it a lot: scroll clamps so the list is not scrolled past its end.
  render(s, ui, 200, 100, true, true);
  CHECK(ui.scroll == 0);
  handleKey(ui, {Key::End, 0}, vis);
  CHECK_EQ(ui.selected, static_cast<int>(vis.size()) - 1);
  handleKey(ui, {Key::Home, 0}, vis);
  CHECK_EQ(ui.selected, 0);
}

TEST(ui_selection_follows_pid_after_resort) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  ui.view = View::Processes;
  ui.sort = ProcessSort::Pid;
  auto vis = visibleProcesses(s.processes, ui);
  handleKey(ui, {Key::Down, 0}, vis);
  handleKey(ui, {Key::Down, 0}, vis);
  uint32_t pid = ui.selectedPid;
  handleKey(ui, {Key::Char, L's'}, vis);  // pid -> name
  CHECK(ui.sort == ProcessSort::Name);
  auto vis2 = visibleProcesses(s.processes, ui);
  syncSelection(ui, vis2);
  CHECK_EQ(ui.selectedPid, pid);
  CHECK_EQ(vis2[static_cast<size_t>(ui.selected)].pid, pid);
}

TEST(ui_filter_editing) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  std::vector<ProcessInfo> vis = visibleProcesses(s.processes, ui);
  handleKey(ui, {Key::Char, L'/'}, vis);
  CHECK(ui.editingFilter);
  CHECK(ui.view == View::Processes);
  for (wchar_t c : std::wstring(L"proc1x")) handleKey(ui, {Key::Char, c}, vis);
  handleKey(ui, {Key::Backspace, 0}, vis);
  handleKey(ui, {Key::Enter, 0}, vis);
  CHECK(!ui.editingFilter);
  CHECK_EQ(ui.filter, std::string("proc1"));
  auto filtered = visibleProcesses(s.processes, ui);
  CHECK_EQ(filtered.size(), static_cast<size_t>(11));  // proc1, proc10..proc19
  handleKey(ui, {Key::Escape, 0}, filtered);           // clears filter
  CHECK(ui.filter.empty());
  // Unicode filter matches the non-ASCII process name
  handleKey(ui, {Key::Char, L'/'}, vis);
  handleKey(ui, {Key::Char, 0x00FC}, vis);  // ü
  handleKey(ui, {Key::Enter, 0}, vis);
  CHECK_EQ(visibleProcesses(s.processes, ui).size(), static_cast<size_t>(1));
}

TEST(ui_kill_requires_confirmation) {
  Snapshot s = fakeSnapshot(false);
  UiState ui;
  ui.view = View::Processes;
  auto vis = visibleProcesses(s.processes, ui);
  syncSelection(ui, vis);
  CHECK(handleKey(ui, {Key::Char, L'k'}, vis) == Action::Redraw);
  CHECK(ui.confirmKill);
  CHECK_EQ(ui.killPid, vis[0].pid);
  // Other keys are ignored while the dialog is open
  CHECK(handleKey(ui, {Key::Char, L'q'}, vis) == Action::None);
  CHECK(handleKey(ui, {Key::Char, L'n'}, vis) == Action::Redraw);
  CHECK(!ui.confirmKill);
  handleKey(ui, {Key::Char, L'K'}, vis);
  std::string txt = render(s, ui, 80, 25, true, true);
  CHECK(txt.find("Terminate") != std::string::npos);
  CHECK(handleKey(ui, {Key::Char, L'Y'}, vis) == Action::Kill);
  // Overview has no selection: K only explains
  UiState ov;
  CHECK(handleKey(ov, {Key::Char, L'k'}, vis) == Action::Redraw);
  CHECK(!ov.confirmKill);
}

TEST(ui_navigation_keys) {
  UiState ui;
  std::vector<ProcessInfo> none;
  CHECK(handleKey(ui, {Key::Char, L'q'}, none) == Action::Quit);
  CHECK(handleKey(ui, {Key::Char, L'R'}, none) == Action::Refresh);
  handleKey(ui, {Key::Char, L'g'}, none);
  CHECK(ui.view == View::Gpu);
  handleKey(ui, {Key::Char, L'h'}, none);
  CHECK(ui.view == View::Help);
  handleKey(ui, {Key::Escape, 0}, none);
  CHECK(ui.view == View::Overview);
  handleKey(ui, {Key::Enter, 0}, none);  // nothing selected: stays
  CHECK(ui.view == View::Overview);
  unsigned before = ui.intervalMs;
  handleKey(ui, {Key::Char, L'+'}, none);
  CHECK(ui.intervalMs > before);
  for (int i = 0; i < 100; ++i) handleKey(ui, {Key::Char, L'-'}, none);
  CHECK_EQ(ui.intervalMs, 250u);
}
