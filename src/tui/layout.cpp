#include "tui/layout.h"

#include <algorithm>
#include <cstring>
#include <functional>

#include "output/report_json.h"
#include "util/format.h"
#include "util/strings.h"

namespace sm {

namespace {

using LineFn = std::function<void(Canvas&, int x, int y, int w)>;

struct Panel {
  std::string title;
  std::vector<LineFn> lines;
  int height() const { return static_cast<int>(lines.size()) + 2; }
};

// Draws a panel clipped to maxH rows. Returns rows used.
int drawPanel(Canvas& c, const Theme& t, const Panel& p, int x, int y, int w, int maxH) {
  int h = std::min(p.height(), maxH);
  if (h < 3 || w < 6) return 0;
  c.box(x, y, w, h, t, p.title);
  for (int i = 0; i < h - 2 && i < static_cast<int>(p.lines.size()); ++i) p.lines[static_cast<size_t>(i)](c, x + 2, y + 1 + i, w - 4);
  return h;
}

std::string pctText(const std::optional<double>& v) {
  if (!v) return "  N/A";
  std::string s = fmtFixed(*v, 1) + "%";
  return std::string(s.size() < 6 ? 6 - s.size() : 0, ' ') + s;
}

// "Label  [bar] 42.0%  suffix"
void meter(Canvas& c, const Theme& t, int x, int y, int w, const std::string& label, int labelW,
           const std::optional<double>& pct, const std::string& suffix) {
  int used = c.text(x, y, label, t.label, std::min(labelW, w));
  int pos = x + std::max(used, labelW) + 1;
  int remaining = x + w - pos;
  if (remaining <= 8) return;
  int suffixW = static_cast<int>(displayWidth(suffix));
  int barW = std::min(30, remaining - 7 - (suffix.empty() ? 0 : suffixW + 2));
  if (barW < 5) {  // not enough room for the suffix: drop it
    barW = std::min(30, remaining - 7);
    suffixW = -1;
  }
  if (barW < 3) {
    c.text(pos, y, pctText(pct), pct ? levelAttr(t, *pct) : t.dim, remaining);
    return;
  }
  c.bar(pos, y, barW, pct, t);
  pos += barW;
  c.text(pos, y, " " + pctText(pct), pct ? t.value : t.dim, x + w - pos);
  pos += 7;
  if (suffixW > 0 && pos + 2 < x + w) c.text(pos + 2, y, suffix, t.normal, x + w - pos - 2);
}

void kvLine(Canvas& c, const Theme& t, int x, int y, int w, const std::string& k, const std::string& v, int kw = 16) {
  int used = c.text(x, y, k, t.label, std::min(kw, w));
  int pos = x + std::max(used, kw) + 1;
  if (pos < x + w) c.text(pos, y, v, v == kNA ? t.dim : t.value, x + w - pos);
}

LineFn kvFn(const Theme& t, std::string k, std::string v, int kw = 16) {
  return [&t, k, v, kw](Canvas& c, int x, int y, int w) { kvLine(c, t, x, y, w, k, v, kw); };
}

LineFn textFn(std::string s, uint16_t attr) {
  return [s, attr](Canvas& c, int x, int y, int w) { c.text(x, y, s, attr, w); };
}

std::string osShort(const SystemInfo& s) {
  std::string n = s.productName;
  if (!s.servicePack.empty()) n += " " + (startsWith(s.servicePack, "Service Pack ") ? "SP" + s.servicePack.substr(13) : s.servicePack);
  return n;
}

// ---------------------------------------------------------------------------------------
// Panels

Panel cpuPanel(const Snapshot& s, const Theme& t, int width, int maxCoreRows) {
  Panel p;
  p.title = "CPU";
  std::string topo = std::to_string(s.cpuInfo.logicalProcessors) + "T";
  if (s.cpuInfo.physicalCores) topo = std::to_string(*s.cpuInfo.physicalCores) + "C/" + topo;
  std::string info = s.cpuInfo.brand;
  p.lines.push_back(textFn(info, t.normal));
  std::string sub = topo + "  " + fmtMhz(s.cpu.currentMhz) + "  user " + fmtPercent(s.cpu.userPct) + "  sys " +
                    fmtPercent(s.cpu.systemPct);
  p.lines.push_back(textFn(sub, t.dim));
  auto total = s.cpu.totalPct;
  p.lines.push_back([&t, total](Canvas& c, int x, int y, int w) { meter(c, t, x, y, w, "Total", 6, total, ""); });
  const auto& cores = s.cpu.corePct;
  if (!cores.empty() && maxCoreRows > 0) {
    const int cellW = 17;
    int cols = std::max(1, (width - 4 + 1) / (cellW + 1));
    int rows = static_cast<int>((cores.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
    rows = std::min(rows, maxCoreRows);
    for (int r = 0; r < rows; ++r) {
      p.lines.push_back([&t, &cores, r, rows, cols, cellW](Canvas& c, int x, int y, int w) {
        for (int k = 0; k < cols; ++k) {
          size_t idx = static_cast<size_t>(r + k * rows);  // column-major so cores read top-down
          if (idx >= cores.size()) continue;
          int cx = x + k * (cellW + 1);
          if (cx + cellW > x + w) break;
          std::string lab = std::to_string(idx);
          c.text(cx, y, lab, t.dim, 3);
          c.bar(cx + 3, y, 7, cores[idx], t);
          c.text(cx + 10, y, pctText(cores[idx]), cores[idx] ? t.value : t.dim, 7);
        }
      });
    }
    size_t shown = static_cast<size_t>(rows * cols);
    if (shown < cores.size())
      p.lines.push_back(textFn("(" + std::to_string(cores.size() - shown) + " more cores: press C)", t.dim));
  }
  return p;
}

Panel memoryPanel(const Snapshot& s, const Theme& t, bool compact) {
  Panel p;
  p.title = "Memory";
  const auto& m = s.memory;
  if (!m.ok) {
    p.lines.push_back(textFn("unavailable", t.dim));
    return p;
  }
  std::optional<double> load = m.loadPct;
  std::string ram = fmtBytes(m.usedPhys) + " / " + fmtBytes(m.totalPhys);
  p.lines.push_back([&t, load, ram](Canvas& c, int x, int y, int w) { meter(c, t, x, y, w, "RAM", 6, load, ram); });
  if (m.commitTotal && m.commitLimit && *m.commitLimit) {
    std::optional<double> cp = 100.0 * static_cast<double>(*m.commitTotal) / static_cast<double>(*m.commitLimit);
    std::string txt = fmtBytes(*m.commitTotal) + " / " + fmtBytes(*m.commitLimit);
    p.lines.push_back([&t, cp, txt](Canvas& c, int x, int y, int w) { meter(c, t, x, y, w, "Commit", 6, cp, txt); });
  }
  if (!compact)
    p.lines.push_back(textFn("Available " + fmtBytes(m.availPhys) + "  Cache " + fmtBytes(m.systemCache) +
                                 "  Paged " + fmtBytes(m.kernelPaged) + "  Nonpaged " + fmtBytes(m.kernelNonpaged),
                             t.dim));
  return p;
}

Panel gpuPanel(const Snapshot& s, const Theme& t) {
  Panel p;
  p.title = "GPU";
  if (!s.slowReady) {
    p.lines.push_back(textFn("detecting adapters...", t.dim));
    return p;
  }
  int shown = 0;
  bool onlySoftware = std::all_of(s.gpus.begin(), s.gpus.end(), [](const GpuAdapter& g) { return g.software; });
  for (const auto& g : s.gpus) {
    if (g.software && !onlySoftware) continue;
    if (++shown > 3) break;
    p.lines.push_back(textFn(g.name + (g.driverVersion ? "  (driver " + *g.driverVersion + ")" : std::string()), t.normal));
    std::string vram = kNA;
    if (g.vramUsed && g.vramTotal) vram = fmtBytes(*g.vramUsed) + "/" + fmtBytes(*g.vramTotal);
    else if (g.dedicatedVideoMemory) vram = fmtBytes(*g.dedicatedVideoMemory) + " total";
    std::string line = "Temp " + fmtCelsius(g.temperatureC) + "  VRAM " + vram + "  Clock " + fmtMhz(g.coreClockMhz);
    auto util = g.utilizationPct;
    p.lines.push_back([&t, util](Canvas& c, int x, int y, int w) { meter(c, t, x, y, w, "Load", 6, util, ""); });
    p.lines.push_back(textFn(line + "  [" + g.sensorBackend + "]", t.dim));
  }
  if (s.gpus.empty()) p.lines.push_back(textFn("no display adapters detected", t.dim));
  return p;
}

Panel diskPanel(const Snapshot& s, const Theme& t) {
  Panel p;
  p.title = "Disk";
  int shown = 0;
  for (const auto& v : s.volumes) {
    if (v.driveType != "fixed" || !v.ready) continue;
    if (++shown > 4) break;
    std::string suffix = fmtBytes(v.freeBytes) + " free of " + fmtBytes(v.totalBytes);
    auto used = v.usedPct;
    std::string root = v.root.substr(0, 2);
    p.lines.push_back([&t, used, suffix, root](Canvas& c, int x, int y, int w) { meter(c, t, x, y, w, root, 6, used, suffix); });
  }
  if (!s.slowReady) p.lines.push_back(textFn("reading volumes...", t.dim));
  Totals tot = computeTotals(s);
  std::optional<double> active;
  for (const auto& d : s.disks)
    if (d.activePct && (!active || *d.activePct > *active)) active = d.activePct;
  p.lines.push_back(textFn("Read " + fmtRate(tot.diskReadBps) + "  Write " + fmtRate(tot.diskWriteBps) + "  Busiest " +
                               fmtPercent(active),
                           t.normal));
  return p;
}

Panel networkPanel(const Snapshot& s, const Theme& t, int maxIfs) {
  Panel p;
  p.title = "Network";
  Totals tot = computeTotals(s);
  p.lines.push_back([&t, tot](Canvas& c, int x, int y, int w) {
    int n = c.text(x, y, "Down ", t.label, w);
    n += c.text(x + n, y, fmtRate(tot.netRxBps), t.value, w - n);
    n += c.text(x + n, y, "   Up ", t.label, w - n);
    c.text(x + n, y, fmtRate(tot.netTxBps), t.value, w - n);
  });
  int shown = 0;
  for (const auto& n : s.network) {
    if (!n.connected) continue;
    if (++shown > maxIfs) break;
    std::string addr = n.ipv4.empty() ? std::string() : "  " + n.ipv4.front();
    std::string rates = fmtRate(n.rxBytesPerSec) + " / " + fmtRate(n.txBytesPerSec);
    std::string name = n.name;
    p.lines.push_back([&t, name, rates, addr](Canvas& c, int x, int y, int w) {
      int nameW = std::max(8, std::min(24, w / 3));
      c.text(x, y, truncateDisplay(name, static_cast<size_t>(nameW)), t.normal, nameW);
      int pos = x + nameW + 1;
      int k = c.text(pos, y, rates, t.value, x + w - pos);
      c.text(pos + k, y, addr, t.dim, x + w - pos - k);
    });
  }
  if (shown == 0) p.lines.push_back(textFn("no connected interfaces", t.dim));
  return p;
}

// ---------------------------------------------------------------------------------------
// Process table

struct ColSpec {
  std::string title;
  int width;  // 0 = flexible
  bool right;
};

std::vector<ColSpec> processColumns(int w) {
  std::vector<ColSpec> cols = {{"PID", 7, true}, {"NAME", 0, false}, {"CPU%", 6, true}, {"MEMORY", 10, true}};
  if (w >= 70) cols.push_back({"PRIVATE", 10, true});
  if (w >= 60) cols.push_back({"THR", 5, true});
  if (w >= 84) cols.push_back({"PRIORITY", 12, false});
  if (w >= 110) cols.push_back({"PATH", -1, false});
  return cols;
}

std::vector<std::string> processCells(const ProcessInfo& p, const std::vector<ColSpec>& cols) {
  std::vector<std::string> out;
  for (const auto& c : cols) {
    if (c.title == "PID") out.push_back(std::to_string(p.pid));
    else if (c.title == "NAME") out.push_back(p.name);
    else if (c.title == "CPU%") out.push_back(p.cpuPct ? fmtFixed(*p.cpuPct, 1) : "-");
    else if (c.title == "MEMORY") out.push_back(p.workingSet ? fmtBytes(*p.workingSet) : "-");
    else if (c.title == "PRIVATE") out.push_back(p.privateBytes ? fmtBytes(*p.privateBytes) : "-");
    else if (c.title == "THR") out.push_back(std::to_string(p.threads));
    else if (c.title == "PRIORITY") out.push_back(p.priorityClass.value_or("-"));
    else if (c.title == "PATH") out.push_back(p.path.value_or(p.accessDenied ? "(access denied)" : ""));
  }
  return out;
}

// Resolves flexible widths: NAME gets up to 28 columns, PATH takes the rest.
std::vector<int> resolveWidths(const std::vector<ColSpec>& cols, int w) {
  std::vector<int> widths;
  int fixed = 0;
  for (const auto& c : cols) fixed += (c.width > 0 ? c.width : 0) + 1;
  int remaining = std::max(4, w - fixed);
  bool hasPath = std::any_of(cols.begin(), cols.end(), [](const ColSpec& c) { return c.width < 0; });
  int nameW = hasPath ? std::min(28, remaining / 2) : remaining;
  for (const auto& c : cols) {
    if (c.width > 0) widths.push_back(c.width);
    else if (c.width == 0) widths.push_back(std::max(4, nameW));
    else widths.push_back(std::max(4, remaining - nameW - 1));
  }
  return widths;
}

void drawRow(Canvas& c, int x, int y, int w, const std::vector<ColSpec>& cols, const std::vector<int>& widths,
             const std::vector<std::string>& cells, uint16_t attr, bool fillRow) {
  if (fillRow) c.fill(x, y, w, 1, L' ', attr);
  int pos = x;
  for (size_t i = 0; i < cols.size() && i < cells.size(); ++i) {
    int cw = std::min(widths[i], x + w - pos);
    if (cw <= 0) break;
    std::string v = truncateDisplay(cells[i], static_cast<size_t>(cw));
    int len = static_cast<int>(displayWidth(v));
    int off = cols[i].right ? cw - len : 0;
    c.text(pos + off, y, v, attr, cw - off);
    pos += widths[i] + 1;
  }
}

void drawProcessTable(Canvas& c, const Theme& t, const std::vector<ProcessInfo>& procs, int x, int y, int w, int h,
                      UiState* ui) {
  if (h < 2) return;
  auto cols = processColumns(w);
  auto widths = resolveWidths(cols, w);
  std::vector<std::string> titles;
  for (const auto& col : cols) titles.push_back(col.title);
  drawRow(c, x, y, w, cols, widths, titles, t.tableHeader, false);
  int rows = h - 1;
  int start = 0;
  if (ui) {
    ui->pageRows = rows;
    syncSelection(*ui, procs);
    start = ui->scroll;
  }
  for (int r = 0; r < rows; ++r) {
    size_t idx = static_cast<size_t>(start + r);
    if (idx >= procs.size()) break;
    bool sel = ui && static_cast<int>(idx) == ui->selected;
    uint16_t attr = sel ? t.selected : (procs[idx].accessDenied ? t.dim : t.normal);
    drawRow(c, x, y + 1 + r, w, cols, widths, processCells(procs[idx], cols), attr, sel);
  }
}

// ---------------------------------------------------------------------------------------
// Views

void viewOverview(Canvas& c, const Snapshot& s, UiState& ui, const Theme& t, int top, int h) {
  int w = c.width();
  int y = top;
  int bottom = top + h;
  auto procs = visibleProcesses(s.processes, ui);
  if (w >= 100) {
    int lw = w / 2, rw = w - lw;
    int coreRows = h >= 34 ? 4 : (h >= 26 ? 2 : (h >= 20 ? 1 : 0));
    Panel cpu = cpuPanel(s, t, lw, coreRows);
    Panel mem = memoryPanel(s, t, h < 24);
    Panel gpu = gpuPanel(s, t);
    Panel disk = diskPanel(s, t);
    Panel net = networkPanel(s, t, h >= 30 ? 3 : 1);
    int ly = y, ry = y;
    ly += drawPanel(c, t, cpu, 0, ly, lw, bottom - ly);
    ly += drawPanel(c, t, mem, 0, ly, lw, bottom - ly);
    ry += drawPanel(c, t, gpu, lw, ry, rw, bottom - ry);
    ry += drawPanel(c, t, disk, lw, ry, rw, bottom - ry);
    ry += drawPanel(c, t, net, lw, ry, rw, bottom - ry);
    // Balance: fill the shorter column's gap is left empty; processes start below both.
    y = std::max(ly, ry);
  } else {
    bool roomy = h >= 30;
    Panel cpu = cpuPanel(s, t, w, roomy ? 2 : 0);
    Panel mem = memoryPanel(s, t, true);
    y += drawPanel(c, t, cpu, 0, y, w, bottom - y);
    y += drawPanel(c, t, mem, 0, y, w, bottom - y);
    if (bottom - y >= 7) {
      Panel net = networkPanel(s, t, 1);
      y += drawPanel(c, t, net, 0, y, w, bottom - y);
    }
    if (roomy && bottom - y >= 10) y += drawPanel(c, t, diskPanel(s, t), 0, y, w, bottom - y);
  }
  int remaining = bottom - y;
  if (remaining >= 4) {
    c.box(0, y, w, remaining, t, std::string("Top processes by ") + processSortName(ui.sort));
    drawProcessTable(c, t, procs, 2, y + 1, w - 4, remaining - 2, nullptr);
  }
}

void viewProcesses(Canvas& c, const Snapshot& s, UiState& ui, const Theme& t, int top, int h) {
  int w = c.width();
  auto procs = visibleProcesses(s.processes, ui);
  std::string status = std::to_string(procs.size()) + " of " + std::to_string(s.processes.size()) +
                       " processes   Sort: " + processSortName(ui.sort) + " (S)   Filter: " +
                       (ui.filter.empty() ? std::string("none") : "\"" + ui.filter + "\"") +
                       " (/)   Enter details   K kill";
  c.text(1, top, status, t.dim, w - 2);
  drawProcessTable(c, t, procs, 1, top + 1, w - 2, h - 1, &ui);
}

void viewProcessDetail(Canvas& c, const Snapshot& s, UiState& ui, const Theme& t, int top, int h) {
  int w = c.width();
  const ProcessInfo* p = nullptr;
  for (const auto& q : s.processes)
    if (q.pid == ui.detailPid) p = &q;
  Panel panel;
  if (!p) {
    panel.title = "PID " + std::to_string(ui.detailPid);
    panel.lines.push_back(textFn("The process has exited.", t.warn));
  } else {
    panel.title = p->name + " (PID " + std::to_string(p->pid) + ")";
    panel.lines.push_back(kvFn(t, "Path", p->path.value_or(p->accessDenied ? "N/A (access denied)" : kNA)));
    panel.lines.push_back(kvFn(t, "Parent PID", std::to_string(p->parentPid)));
    panel.lines.push_back(kvFn(t, "CPU", fmtPercent(p->cpuPct)));
    panel.lines.push_back(kvFn(t, "CPU time", p->cpuTime100ns ? fmtFixed(static_cast<double>(*p->cpuTime100ns) / 1e7, 2) + " s" : std::string(kNA)));
    panel.lines.push_back(kvFn(t, "Working set", fmtBytes(p->workingSet)));
    panel.lines.push_back(kvFn(t, "Peak working set", fmtBytes(p->peakWorkingSet)));
    panel.lines.push_back(kvFn(t, "Private bytes", fmtBytes(p->privateBytes)));
    panel.lines.push_back(kvFn(t, "Threads", std::to_string(p->threads)));
    panel.lines.push_back(kvFn(t, "Handles", p->handles ? std::to_string(*p->handles) : kNA));
    panel.lines.push_back(kvFn(t, "Priority", p->priorityClass.value_or(kNA) + " (base " + std::to_string(p->basePriority) + ")"));
    panel.lines.push_back(kvFn(t, "Started", p->createTime ? fileTimeToLocal(*p->createTime) : std::string(kNA)));
    panel.lines.push_back(kvFn(t, "I/O read", fmtBytes(p->ioReadBytes)));
    panel.lines.push_back(kvFn(t, "I/O written", fmtBytes(p->ioWriteBytes)));
    if (p->accessDenied) panel.lines.push_back(textFn("Some fields unavailable: access denied.", t.warn));
  }
  panel.lines.push_back(textFn("", t.normal));
  panel.lines.push_back(textFn("Esc back   K terminate (asks for confirmation)", t.dim));
  drawPanel(c, t, panel, 0, top, w, h);
}

void viewCpu(Canvas& c, const Snapshot& s, UiState&, const Theme& t, int top, int h) {
  int w = c.width();
  Panel p;
  p.title = "Processor";
  const auto& i = s.cpuInfo;
  p.lines.push_back(kvFn(t, "Model", i.brand));
  p.lines.push_back(kvFn(t, "Vendor", i.vendor.empty() ? kNA : i.vendor));
  std::string topo = std::to_string(i.logicalProcessors) + " logical";
  if (i.physicalCores) topo += ", " + std::to_string(*i.physicalCores) + " cores";
  if (i.packages) topo += ", " + std::to_string(*i.packages) + " package(s)";
  p.lines.push_back(kvFn(t, "Topology", topo));
  p.lines.push_back(kvFn(t, "Frequency", fmtMhz(s.cpu.currentMhz) + " current / " + fmtMhz(s.cpu.maxMhz ? s.cpu.maxMhz : i.maxMhz) + " max"));
  p.lines.push_back(kvFn(t, "Temperature", "N/A (no Windows 7 API)"));
  auto tot = s.cpu.totalPct, usr = s.cpu.userPct, sys = s.cpu.systemPct;
  p.lines.push_back([&t, tot](Canvas& cv, int x, int y, int ww) { meter(cv, t, x, y, ww, "Total", 16, tot, ""); });
  p.lines.push_back([&t, usr](Canvas& cv, int x, int y, int ww) { meter(cv, t, x, y, ww, "User", 16, usr, ""); });
  p.lines.push_back([&t, sys](Canvas& cv, int x, int y, int ww) { meter(cv, t, x, y, ww, "System", 16, sys, ""); });
  int used = drawPanel(c, t, p, 0, top, w, h);
  int remaining = h - used;
  if (remaining >= 3 && !s.cpu.corePct.empty()) {
    Panel cores = cpuPanel(s, t, w, remaining - 5);
    cores.title = "Per-core utilization";
    cores.lines.erase(cores.lines.begin(), cores.lines.begin() + 3);
    drawPanel(c, t, cores, 0, top + used, w, remaining);
  }
}

void viewMemory(Canvas& c, const Snapshot& s, UiState&, const Theme& t, int top, int h) {
  int w = c.width();
  Panel p = memoryPanel(s, t, true);
  p.title = "Physical memory and commit";
  const auto& m = s.memory;
  p.lines.push_back(kvFn(t, "Total RAM", m.ok ? fmtBytes(m.totalPhys) : std::string(kNA)));
  p.lines.push_back(kvFn(t, "Available", m.ok ? fmtBytes(m.availPhys) : std::string(kNA)));
  p.lines.push_back(kvFn(t, "Committed", fmtBytes(m.commitTotal) + " (peak " + fmtBytes(m.commitPeak) + ")"));
  p.lines.push_back(kvFn(t, "Commit limit", fmtBytes(m.commitLimit)));
  p.lines.push_back(kvFn(t, "System cache", fmtBytes(m.systemCache)));
  p.lines.push_back(kvFn(t, "Paged pool", fmtBytes(m.kernelPaged)));
  p.lines.push_back(kvFn(t, "Nonpaged pool", fmtBytes(m.kernelNonpaged)));
  p.lines.push_back(kvFn(t, "Process WS sum", fmtBytes(m.processWorkingSetTotal)));
  p.lines.push_back(kvFn(t, "Process private", fmtBytes(m.processPrivateTotal)));
  p.lines.push_back(kvFn(t, "Processes", m.processCount ? std::to_string(*m.processCount) : kNA));
  p.lines.push_back(kvFn(t, "Threads", m.threadCount ? fmtCount(*m.threadCount) : kNA));
  p.lines.push_back(kvFn(t, "Handles", m.handleCount ? fmtCount(*m.handleCount) : kNA));
  int used = drawPanel(c, t, p, 0, top, w, h);
  Panel pf;
  pf.title = "Page files";
  if (!m.pageFilesKnown) pf.lines.push_back(textFn("unavailable", t.dim));
  else if (m.pageFiles.empty()) pf.lines.push_back(textFn("none configured", t.dim));
  for (const auto& f : m.pageFiles)
    pf.lines.push_back(textFn(f.path + "  size " + fmtBytes(f.totalBytes) + "  in use " + fmtBytes(f.inUseBytes) +
                                  "  peak " + fmtBytes(f.peakBytes),
                              t.normal));
  drawPanel(c, t, pf, 0, top + used, w, h - used);
}

void viewGpu(Canvas& c, const Snapshot& s, UiState&, const Theme& t, int top, int h) {
  int w = c.width();
  int y = top;
  if (!s.slowReady) {
    c.text(1, y, "Detecting adapters...", t.dim, w - 2);
    return;
  }
  if (s.gpus.empty()) c.text(1, y, "No display adapters detected.", t.dim, w - 2);
  for (const auto& g : s.gpus) {
    Panel p;
    p.title = g.name;
    p.lines.push_back(kvFn(t, "Vendor", g.vendor + " (" + hex4(g.vendorId) + ":" + hex4(g.deviceId) + ")"));
    p.lines.push_back(kvFn(t, "Driver", (g.driverVersion ? *g.driverVersion : std::string(kNA)) +
                                            (g.driverDate ? "  " + *g.driverDate : std::string())));
    p.lines.push_back(kvFn(t, "Dedicated VRAM", fmtBytes(g.dedicatedVideoMemory)));
    auto util = g.utilizationPct;
    p.lines.push_back([&t, util](Canvas& cv, int x, int yy, int ww) { meter(cv, t, x, yy, ww, "Utilization", 16, util, ""); });
    std::string vram = kNA;
    if (g.vramUsed && g.vramTotal) vram = fmtBytes(*g.vramUsed) + " / " + fmtBytes(*g.vramTotal);
    p.lines.push_back(kvFn(t, "VRAM in use", vram));
    p.lines.push_back(kvFn(t, "Temperature", fmtCelsius(g.temperatureC)));
    p.lines.push_back(kvFn(t, "Clocks", fmtMhz(g.coreClockMhz) + " core / " + fmtMhz(g.memoryClockMhz) + " memory"));
    p.lines.push_back(kvFn(t, "Power", fmtWatts(g.powerWatts)));
    p.lines.push_back(kvFn(t, "Sensor backend", g.sensorBackend));
    for (const auto& n : g.notes) p.lines.push_back(textFn("note: " + n, t.dim));
    int used = drawPanel(c, t, p, 0, y, w, top + h - y);
    if (used == 0) break;
    y += used;
  }
}

void viewDisk(Canvas& c, const Snapshot& s, UiState&, const Theme& t, int top, int h) {
  int w = c.width();
  Panel v;
  v.title = "Volumes";
  for (const auto& vol : s.volumes) {
    if (!vol.ready) {
      v.lines.push_back(textFn(vol.root.substr(0, 2) + "    " + vol.driveType + "  " + vol.note, t.dim));
      continue;
    }
    auto used = vol.usedPct;
    std::string suffix = fmtBytes(vol.freeBytes) + " free of " + fmtBytes(vol.totalBytes) + "  " + vol.fileSystem +
                         "  " + vol.driveType + (vol.label.empty() ? "" : "  \"" + vol.label + "\"");
    std::string root = vol.root.substr(0, 2);
    v.lines.push_back([&t, used, suffix, root](Canvas& cv, int x, int y, int ww) { meter(cv, t, x, y, ww, root, 6, used, suffix); });
  }
  if (!s.slowReady) v.lines.push_back(textFn("reading volumes...", t.dim));
  int usedRows = drawPanel(c, t, v, 0, top, w, h);
  Panel d;
  d.title = "Physical disks";
  for (const auto& disk : s.disks) {
    d.lines.push_back(textFn(std::to_string(disk.number) + ": " + disk.model + "  " + fmtBytes(disk.sizeBytes) + "  " +
                                 disk.busType + "  " + disk.mediaType,
                             t.normal));
    std::string smart = disk.smartPredictFailure ? (*disk.smartPredictFailure ? "FAILURE PREDICTED" : "OK") : "N/A";
    d.lines.push_back(textFn("   read " + fmtRate(disk.readBps) + "  write " + fmtRate(disk.writeBps) + "  active " +
                                 fmtPercent(disk.activePct) + "  SMART " + smart + "  temp " + fmtCelsius(disk.smartTemperatureC),
                             disk.smartPredictFailure && *disk.smartPredictFailure ? t.bad : t.dim));
  }
  if (!s.smart.status.empty()) d.lines.push_back(textFn("SMART: " + s.smart.status, t.dim));
  drawPanel(c, t, d, 0, top + usedRows, w, h - usedRows);
}

void viewNetwork(Canvas& c, const Snapshot& s, UiState&, const Theme& t, int top, int h) {
  int w = c.width();
  std::vector<ColSpec> cols = {{"INTERFACE", 0, false}, {"STATUS", 12, false}, {"DOWN", 11, true}, {"UP", 11, true}};
  if (w >= 70) cols.push_back({"LINK", 9, true});
  if (w >= 90) cols.push_back({"IPV4", 16, false});
  if (w >= 110) cols.push_back({"TYPE", 10, false});
  auto widths = resolveWidths(cols, w - 2);
  std::vector<std::string> titles;
  for (const auto& col : cols) titles.push_back(col.title);
  drawRow(c, 1, top, w - 2, cols, widths, titles, t.tableHeader, false);
  int y = top + 1;
  for (const auto& n : s.network) {
    if (y >= top + h - 1) break;
    std::vector<std::string> cells;
    for (const auto& col : cols) {
      if (col.title == "INTERFACE") cells.push_back(n.name);
      else if (col.title == "STATUS") cells.push_back(n.status);
      else if (col.title == "DOWN") cells.push_back(fmtRate(n.rxBytesPerSec));
      else if (col.title == "UP") cells.push_back(fmtRate(n.txBytesPerSec));
      else if (col.title == "LINK") cells.push_back(n.linkSpeedRxBps ? fmtBitRate(*n.linkSpeedRxBps) : "-");
      else if (col.title == "IPV4") cells.push_back(n.ipv4.empty() ? "-" : n.ipv4.front());
      else if (col.title == "TYPE") cells.push_back(n.type);
    }
    drawRow(c, 1, y++, w - 2, cols, widths, cells, n.connected ? t.normal : t.dim, false);
  }
  Totals tot = computeTotals(s);
  c.text(1, top + h - 1, "Total down " + fmtRate(tot.netRxBps) + "   up " + fmtRate(tot.netTxBps), t.value, w - 2);
}

void viewHelp(Canvas& c, const Snapshot&, UiState&, const Theme& t, int top, int h) {
  Panel p;
  p.title = "Keyboard";
  const char* const lines[][2] = {
      {"Q / Ctrl+C", "Quit (console is restored)"},
      {"R", "Refresh now"},
      {"O", "Overview"},
      {"P", "Processes"},
      {"C / M / G", "CPU / Memory / GPU"},
      {"D / N", "Disk / Network"},
      {"Up/Down PgUp/PgDn", "Select process"},
      {"Enter", "Process details"},
      {"S", "Cycle sort: cpu, memory, pid, name"},
      {"/", "Filter processes by name or PID"},
      {"K", "Terminate selected process (asks Y/N)"},
      {"+ / -", "Slower / faster refresh"},
      {"Space", "Pause / resume sampling"},
      {"Esc", "Back"},
  };
  for (const auto& l : lines) p.lines.push_back(kvFn(t, l[0], l[1], 20));
  p.lines.push_back(textFn("", t.normal));
  p.lines.push_back(textFn("N/A means the value is not available from a trustworthy source on this system.", t.dim));
  p.lines.push_back(textFn("Run 'sevenmon doctor' for details about missing sensors and permissions.", t.dim));
  drawPanel(c, t, p, 0, top, c.width(), h);
}

void drawKillConfirm(Canvas& c, const UiState& ui, const Theme& t) {
  int w = std::min(c.width() - 4, 64);
  int h = 6;
  if (w < 20 || c.height() < h + 2) return;
  int x = (c.width() - w) / 2, y = (c.height() - h) / 2;
  c.fill(x, y, w, h, L' ', t.normal);
  c.box(x, y, w, h, t, "Terminate process");
  c.text(x + 2, y + 1, "Terminate " + ui.killName + " (PID " + std::to_string(ui.killPid) + ")?", t.value, w - 4);
  c.text(x + 2, y + 2, "Unsaved data in that program will be lost.", t.warn, w - 4);
  c.text(x + 2, y + 4, "Y = terminate    N / Esc = cancel", t.label, w - 4);
}

}  // namespace

void renderDashboard(Canvas& c, const Snapshot& s, UiState& ui, const Theme& t, const std::string& clock) {
  c.clear(t.normal);
  int w = c.width(), h = c.height();
  if (w < kMinDashboardWidth || h < kMinDashboardHeight) {
    std::string msg = "Window too small (" + std::to_string(w) + "x" + std::to_string(h) + "); need " +
                      std::to_string(kMinDashboardWidth) + "x" + std::to_string(kMinDashboardHeight);
    for (int y = 0; y < h; ++y) {
      int off = static_cast<int>(y * w);
      std::string part = msg.size() > static_cast<size_t>(off) ? msg.substr(static_cast<size_t>(off)) : std::string();
      c.text(0, y, part, t.warn, w);
    }
    return;
  }
  // Header
  c.fill(0, 0, w, 1, L' ', t.header);
  std::string left = " SevenMonitor " SEVENMON_VERSION " | " + osShort(s.system) + " | " + s.system.osArch + " | " +
                     s.system.computerName + " | up " + fmtDuration(s.system.uptimeSeconds);
  std::string right = std::string(viewName(ui.view)) + (ui.paused ? " [paused]" : "") + "  " + clock + " ";
  int rw = static_cast<int>(displayWidth(right));
  c.text(0, 0, left, t.header, std::max(0, w - rw - 1));
  if (rw < w) c.text(w - rw, 0, right, t.header, rw);

  int top = 1, bodyH = h - 2;
  switch (ui.view) {
    case View::Overview: viewOverview(c, s, ui, t, top, bodyH); break;
    case View::Processes: viewProcesses(c, s, ui, t, top, bodyH); break;
    case View::ProcessDetail: viewProcessDetail(c, s, ui, t, top, bodyH); break;
    case View::Cpu: viewCpu(c, s, ui, t, top, bodyH); break;
    case View::Memory: viewMemory(c, s, ui, t, top, bodyH); break;
    case View::Gpu: viewGpu(c, s, ui, t, top, bodyH); break;
    case View::Disk: viewDisk(c, s, ui, t, top, bodyH); break;
    case View::Network: viewNetwork(c, s, ui, t, top, bodyH); break;
    case View::Help: viewHelp(c, s, ui, t, top, bodyH); break;
  }

  // Footer
  c.fill(0, h - 1, w, 1, L' ', t.footer);
  if (ui.editingFilter) {
    c.text(1, h - 1, "Filter: " + ui.filterDraft + "_   (Enter apply, Esc cancel)", t.footerKey, w - 2);
  } else {
    struct Hint {
      const char* key;
      const char* label;
    };
    static const Hint hints[] = {{"Q", "Quit"}, {"R", "Refresh"}, {"O", "Overview"}, {"P", "Procs"}, {"C", "CPU"},
                                 {"M", "Mem"},  {"G", "GPU"},     {"D", "Disk"},     {"N", "Net"},   {"H", "Help"}};
    int x = 1;
    int msgW = ui.message.empty() ? 0 : static_cast<int>(displayWidth(ui.message)) + 2;
    for (const auto& hint : hints) {
      int need = static_cast<int>(strlen(hint.key) + strlen(hint.label)) + 3;
      if (x + need > w - msgW) break;
      x += c.text(x, h - 1, hint.key, t.footerKey);
      x += c.text(x, h - 1, std::string(" ") + hint.label + "  ", t.footer);
    }
    if (!ui.message.empty() && msgW < w) c.text(w - msgW + 1, h - 1, ui.message, t.footerKey, msgW - 1);
  }
  if (ui.confirmKill) drawKillConfirm(c, ui, t);
}

}  // namespace sm
