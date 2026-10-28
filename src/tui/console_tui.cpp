// Interactive dashboard on the classic Win32 console API.
//
// A private screen buffer (CreateConsoleScreenBuffer) is made active for the session so
// the user's scrollback is untouched; every frame is rendered into an in-memory Canvas
// and copied with one WriteConsoleOutputW call into the visible window rectangle. Only
// changed frames are written. Input comes from ReadConsoleInputW; window and buffer size
// changes are detected each tick. All console state is restored on exit, including after
// Ctrl+C.
#include "tui/console_tui.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "platform/console.h"
#include "platform/interrupt.h"
#include "platform/win.h"
#include "platform/wmi.h"
#include "sampler/sampler.h"
#include "tui/layout.h"
#include "tui/ui_state.h"

namespace sm {

namespace {

class ConsoleSession {
 public:
  ConsoleSession() = default;
  ~ConsoleSession() { restore(); }

  bool open(std::string& error) {
    out_ = GetStdHandle(STD_OUTPUT_HANDLE);
    in_ = GetStdHandle(STD_INPUT_HANDLE);
    if (!handleIsConsole(out_)) {
      error = "stdout is not a console";
      return false;
    }
    if (!handleIsConsole(in_)) {
      // stdin redirected: read keys from the console directly.
      ownIn_ = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
      if (ownIn_ == INVALID_HANDLE_VALUE) {
        ownIn_ = nullptr;
        error = "no console input available";
        return false;
      }
      in_ = ownIn_;
    }
    if (!GetConsoleMode(in_, &origInMode_)) {
      error = "cannot read console input mode";
      return false;
    }
    haveInMode_ = true;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out_, &info)) {
      error = "cannot query console screen buffer";
      return false;
    }
    SHORT w = static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1);
    SHORT h = static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1);
    buf_ = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     CONSOLE_TEXTMODE_BUFFER, nullptr);
    if (buf_ == INVALID_HANDLE_VALUE) {
      buf_ = nullptr;
      error = "CreateConsoleScreenBuffer failed: " + win::errorText(GetLastError());
      return false;
    }
    // Make the private buffer exactly the window size so there are no scrollbars.
    COORD size = {w, h};
    SMALL_RECT win = {0, 0, static_cast<SHORT>(w - 1), static_cast<SHORT>(h - 1)};
    CONSOLE_SCREEN_BUFFER_INFO nb;
    if (GetConsoleScreenBufferInfo(buf_, &nb) && (nb.dwSize.X < w || nb.dwSize.Y < h)) {
      COORD bigger = {std::max(nb.dwSize.X, w), std::max(nb.dwSize.Y, h)};
      SetConsoleScreenBufferSize(buf_, bigger);
    }
    SetConsoleWindowInfo(buf_, TRUE, &win);
    SetConsoleScreenBufferSize(buf_, size);
    CONSOLE_CURSOR_INFO ci = {1, FALSE};
    SetConsoleCursorInfo(buf_, &ci);
    if (!SetConsoleActiveScreenBuffer(buf_)) {
      error = "SetConsoleActiveScreenBuffer failed";
      return false;
    }
    active_ = true;
    DWORD mode = (origInMode_ & ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_MOUSE_INPUT)) |
                 ENABLE_WINDOW_INPUT | ENABLE_PROCESSED_INPUT;
    SetConsoleMode(in_, mode);
    FlushConsoleInputBuffer(in_);
    return true;
  }

  void restore() {
    if (active_) {
      SetConsoleActiveScreenBuffer(out_);
      active_ = false;
    }
    if (buf_) {
      CloseHandle(buf_);
      buf_ = nullptr;
    }
    if (haveInMode_) {
      SetConsoleMode(in_, origInMode_);
      haveInMode_ = false;
    }
    if (ownIn_) {
      CloseHandle(ownIn_);
      ownIn_ = nullptr;
    }
  }

  // Current visible window of the private buffer; shrinks the buffer to the window if
  // the user made the window smaller (removes scrollbars).
  bool windowRect(SMALL_RECT& rect) {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(buf_, &info)) return false;
    rect = info.srWindow;
    SHORT w = static_cast<SHORT>(rect.Right - rect.Left + 1), h = static_cast<SHORT>(rect.Bottom - rect.Top + 1);
    if (info.dwSize.X != w || info.dwSize.Y != h) {
      if (rect.Left != 0 || rect.Top != 0) {
        SMALL_RECT origin = {0, 0, static_cast<SHORT>(w - 1), static_cast<SHORT>(h - 1)};
        SetConsoleWindowInfo(buf_, TRUE, &origin);
      }
      COORD size = {w, h};
      if (SetConsoleScreenBufferSize(buf_, size)) {
        rect = {0, 0, static_cast<SHORT>(w - 1), static_cast<SHORT>(h - 1)};
      } else if (GetConsoleScreenBufferInfo(buf_, &info)) {
        rect = info.srWindow;
      }
      CONSOLE_CURSOR_INFO ci = {1, FALSE};
      SetConsoleCursorInfo(buf_, &ci);
    }
    return true;
  }

  void present(const Canvas& c, const SMALL_RECT& rect) {
    std::vector<CHAR_INFO> cells(static_cast<size_t>(c.width()) * c.height());
    for (size_t i = 0; i < cells.size(); ++i) {
      cells[i].Char.UnicodeChar = c.cells()[i].ch;
      cells[i].Attributes = c.cells()[i].attr;
    }
    COORD size = {static_cast<SHORT>(c.width()), static_cast<SHORT>(c.height())};
    COORD origin = {0, 0};
    SMALL_RECT r = rect;
    WriteConsoleOutputW(buf_, cells.data(), size, origin, &r);
  }

  HANDLE input() const { return in_; }

 private:
  HANDLE out_ = nullptr;
  HANDLE in_ = nullptr;
  HANDLE ownIn_ = nullptr;
  HANDLE buf_ = nullptr;
  DWORD origInMode_ = 0;
  bool haveInMode_ = false;
  bool active_ = false;
};

KeyEvent translate(const KEY_EVENT_RECORD& k) {
  KeyEvent ev;
  switch (k.wVirtualKeyCode) {
    case VK_UP: ev.key = Key::Up; return ev;
    case VK_DOWN: ev.key = Key::Down; return ev;
    case VK_PRIOR: ev.key = Key::PageUp; return ev;
    case VK_NEXT: ev.key = Key::PageDown; return ev;
    case VK_HOME: ev.key = Key::Home; return ev;
    case VK_END: ev.key = Key::End; return ev;
    case VK_LEFT: ev.key = Key::Left; return ev;
    case VK_RIGHT: ev.key = Key::Right; return ev;
    case VK_RETURN: ev.key = Key::Enter; return ev;
    case VK_ESCAPE: ev.key = Key::Escape; return ev;
    case VK_BACK: ev.key = Key::Backspace; return ev;
    case VK_TAB: ev.key = Key::Tab; return ev;
    default: break;
  }
  if (k.uChar.UnicodeChar >= 0x20) {
    ev.key = Key::Char;
    ev.ch = k.uChar.UnicodeChar;
  }
  return ev;
}

std::string localClock() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
  return buf;
}

}  // namespace

int runDashboard(const CliOptions& opts) {
  ConsoleCaps caps = detectConsoleCaps(GetStdHandle(STD_OUTPUT_HANDLE));
  bool unicode = caps.unicodeBlocks && !opts.plain;
  bool color = !opts.noColor && !opts.plain;
  Theme theme = Theme::make(color, unicode);

  Sampler::Options so;
  Sampler sampler(so);
  sampler.startBackground();  // slow tier (WMI, GPU sensors, volumes) on its own thread

  ConsoleSession console;
  std::string err;
  if (!console.open(err)) {
    sampler.stopBackground();
    Out e(STD_ERROR_HANDLE, false);
    e.line("sevenmon: cannot start the dashboard: " + err);
    return kExitError;
  }

  UiState ui;
  ui.intervalMs = opts.intervalMs.value_or(kDefaultWatchIntervalMs);
  ui.sort = opts.sort;
  ui.filter = opts.filter;

  sampler.sampleFast();
  // Take the second sample sooner so real rates appear quickly.
  double nextSample = win::monotonicSeconds() + std::min(ui.intervalMs, 500u) / 1000.0;
  double messageUntil = 0;
  std::string lastMessage;
  unsigned long frames = 0;
  unsigned long maxFrames = opts.count.value_or(0);
  if (opts.durationSec) maxFrames = static_cast<unsigned long>((*opts.durationSec * 1000ull + ui.intervalMs - 1) / ui.intervalMs);
  Snapshot snap = sampler.snapshot();
  Canvas prev(0, 0);
  bool dirty = true;
  int exitCode = kExitOk;
  bool quit = false;
  HANDLE waits[2] = {console.input(), static_cast<HANDLE>(interruptEvent())};

  while (!quit && !interrupted()) {
    double now = win::monotonicSeconds();
    if (now >= nextSample) {
      if (!ui.paused) {
        sampler.sampleFast();
        ++frames;
      }
      snap = sampler.snapshot();
      nextSample = now + ui.intervalMs / 1000.0;
      dirty = true;
      if (maxFrames && frames >= maxFrames) quit = true;
    }
    if (!ui.message.empty() && ui.message != lastMessage) {
      lastMessage = ui.message;
      messageUntil = now + 3.0;
    }
    if (!ui.message.empty() && now > messageUntil) {
      ui.message.clear();
      lastMessage.clear();
      dirty = true;
    }

    SMALL_RECT rect;
    if (console.windowRect(rect)) {
      int w = rect.Right - rect.Left + 1, h = rect.Bottom - rect.Top + 1;
      Canvas canvas(w, h, theme.normal, !unicode);
      renderDashboard(canvas, snap, ui, theme, localClock());
      bool changed = dirty || canvas.width() != prev.width() || canvas.height() != prev.height() ||
                     canvas.cells() != prev.cells();
      if (changed) {
        console.present(canvas, rect);
        prev = canvas;
      }
      dirty = false;
    }
    if (quit) break;

    double wait = std::max(0.0, std::min(nextSample - win::monotonicSeconds(), 0.25));
    DWORD r = WaitForMultipleObjects(waits[1] ? 2 : 1, waits, FALSE, static_cast<DWORD>(wait * 1000.0));
    if (r != WAIT_OBJECT_0) continue;
    DWORD pending = 0;
    if (!GetNumberOfConsoleInputEvents(console.input(), &pending) || pending == 0) continue;
    INPUT_RECORD recs[32];
    DWORD got = 0;
    if (!ReadConsoleInputW(console.input(), recs, 32, &got)) continue;
    auto visible = visibleProcesses(snap.processes, ui);
    for (DWORD i = 0; i < got; ++i) {
      if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
        dirty = true;
        continue;
      }
      if (recs[i].EventType != KEY_EVENT || !recs[i].Event.KeyEvent.bKeyDown) continue;
      KeyEvent ev = translate(recs[i].Event.KeyEvent);
      if (ev.key == Key::None) continue;
      Action a = handleKey(ui, ev, visible);
      dirty = true;
      if (a == Action::Quit) quit = true;
      else if (a == Action::Refresh) {
        sampler.requestSlowRefresh();
        nextSample = 0;
      } else if (a == Action::Kill) {
        std::string e = terminateProcessById(ui.killPid);
        ui.message = e.empty() ? "Terminated " + ui.killName + " (PID " + std::to_string(ui.killPid) + ")"
                               : "Could not terminate PID " + std::to_string(ui.killPid) + ": " + e;
        nextSample = 0;
      }
    }
  }

  console.restore();
  sampler.stopBackground();
  if (interrupted()) exitCode = kExitOk;  // Ctrl+C is the normal way to leave
  return exitCode;
}

}  // namespace sm
