#pragma once
// Ctrl+C / Ctrl+Break / console-close handling shared by streaming commands and the
// dashboard. The handler only sets a flag and signals an event; the main loop exits
// cleanly and restores console state itself.
namespace sm {

void installInterruptHandler();
bool interrupted();
// Sleeps up to ms milliseconds; returns true early if interrupted.
bool sleepInterruptible(unsigned ms);
// Event handle that becomes signaled on interrupt (for WaitForMultipleObjects).
void* interruptEvent();

}  // namespace sm
