// Long-running stability: many sampling iterations must not leak handles or memory.
#include <windows.h>
#include <psapi.h>

#include <cstdio>

#include "sampler/sampler.h"
#include "fake_snapshot.h"
#include "test.h"
#include "tui/layout.h"

using namespace sm;

static uint64_t privateBytes() {
  PROCESS_MEMORY_COUNTERS_EX pmc;
  ZeroMemory(&pmc, sizeof(pmc));
  pmc.cb = sizeof(pmc);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
    std::printf("       GetProcessMemoryInfo failed: %lu\n", GetLastError());
  // Wine leaves PrivateUsage at 0; PagefileUsage (private commit) is equivalent on Windows.
  return pmc.PrivateUsage ? pmc.PrivateUsage : pmc.PagefileUsage;
}

static DWORD handleCount() {
  DWORD n = 0;
  GetProcessHandleCount(GetCurrentProcess(), &n);
  return n;
}

LIVE_TEST(live_stability_sampling_no_leaks) {
  Sampler::Options o;
  o.slowIntervalMs = 100;  // exercise the slow tier often
  Sampler s(o);
  s.startBackground();
  for (int i = 0; i < 20; ++i) s.sampleFast();  // warm-up (caches, lazy DLL loads)
  Sleep(300);
  DWORD h0 = handleCount();
  uint64_t m0 = privateBytes();
  const int iterations = 400;
  for (int i = 0; i < iterations; ++i) {
    s.sampleFast();
    Snapshot snap = s.snapshot();
    UiState ui;
    Theme t = Theme::make(true, true);
    Canvas c(120, 40, t.normal);
    renderDashboard(c, snap, ui, t, "00:00:00");
  }
  s.stopBackground();
  DWORD h1 = handleCount();
  uint64_t m1 = privateBytes();
  std::printf("       %d iterations: handles %lu -> %lu, private bytes %llu KiB -> %llu KiB\n", iterations,
              static_cast<unsigned long>(h0), static_cast<unsigned long>(h1),
              static_cast<unsigned long long>(m0 / 1024), static_cast<unsigned long long>(m1 / 1024));
  CHECK(h1 <= h0 + 16);
  CHECK(m1 <= m0 + 4ull * 1024 * 1024);
}

TEST(stability_render_many_frames_constant_memory) {
  Snapshot snap = fakeSnapshot(true);
  Theme t = Theme::make(true, true);
  UiState ui;
  for (int i = 0; i < 50; ++i) {
    Canvas c(160, 50, t.normal);
    renderDashboard(c, snap, ui, t, "00:00:00");
  }
  uint64_t m0 = privateBytes();
  for (int i = 0; i < 2000; ++i) {
    ui.view = static_cast<View>(i % 9);
    Canvas c(40 + i % 160, 10 + i % 60, t.normal);
    renderDashboard(c, snap, ui, t, "00:00:00");
  }
  CHECK(privateBytes() <= m0 + 2ull * 1024 * 1024);
}
