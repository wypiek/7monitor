#include "platform/interrupt.h"

#include <windows.h>

#include <atomic>

namespace sm {

namespace {
std::atomic<bool> g_interrupted{false};
HANDLE g_event = nullptr;

BOOL WINAPI ctrlHandler(DWORD type) {
  switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      g_interrupted = true;
      if (g_event) SetEvent(g_event);
      if (type == CTRL_CLOSE_EVENT) Sleep(500);  // give the main loop a moment to restore the console
      return TRUE;
    default:
      return FALSE;
  }
}
}  // namespace

void installInterruptHandler() {
  if (!g_event) g_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  SetConsoleCtrlHandler(ctrlHandler, TRUE);
}

bool interrupted() { return g_interrupted.load(); }

bool sleepInterruptible(unsigned ms) {
  if (g_interrupted) return true;
  if (g_event) return WaitForSingleObject(g_event, ms) == WAIT_OBJECT_0 || g_interrupted;
  Sleep(ms);
  return g_interrupted;
}

void* interruptEvent() { return g_event; }

}  // namespace sm
