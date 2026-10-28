#include "output/report_text.h"

#include <cstdio>

#include "output/report_json.h"
#include "output/table.h"
#include "util/format.h"
#include "util/strings.h"

namespace sm {

namespace {

void section(Out& out, const std::string& title) { out.colored(Color::Title, title + "\r\n"); }

void kv(Out& out, const std::string& key, const std::string& value, int width = 18) {
  std::string k = key;
  if (static_cast<int>(k.size()) < width) k += std::string(static_cast<size_t>(width - static_cast<int>(k.size())), ' ');
  out.colored(Color::Accent, k);
  out.write(value + "\r\n");
}

std::string pctBar(const std::optional<double>& v, int width = 20) {
  if (!v) return std::string(static_cast<size_t>(width + 2), ' ') + "  N/A";
  char buf[16];
  std::snprintf(buf, sizeof(buf), " %5.1f%%", *v);
  return asciiBar(*v, width) + buf;
}

std::string orNA(const std::string& s) { return s.empty() ? kNA : s; }
std::string orNA(const std::optional<std::string>& s) { return s && !s->empty() ? *s : kNA; }

void notes(Out& out, const std::vector<std::string>& ns, bool verbose) {
  if (!verbose) return;
  for (const auto& n : ns) out.colored(Color::Dim, "  note: " + n + "\r\n");
}

std::string osLine(const SystemInfo& s) {
  std::string l = s.productName;
  if (!s.servicePack.empty()) l += " " + s.servicePack;
  l += " (" + s.displayVersion + ", " + s.osArch + ")";
  return l;
}

}  // namespace

void printSummary(Out& out, const Snapshot& s, const CliOptions& o) {
  out.colored(Color::Title, "SevenMonitor " SEVENMON_VERSION);
  out.write("  " + s.system.computerName + "  " + osLine(s.system) + "\r\n");
  kv(out, "Uptime", fmtDuration(s.system.uptimeSeconds) + "  (booted " + s.system.bootTimeUtc + ")");
  out.line();
  std::string topo = std::to_string(s.cpuInfo.logicalProcessors) + " logical";
  if (s.cpuInfo.physicalCores) topo = std::to_string(*s.cpuInfo.physicalCores) + " cores / " + topo;
  kv(out, "CPU", s.cpuInfo.brand + "  [" + topo + ", " + fmtMhz(s.cpu.currentMhz) + "]");
  kv(out, "CPU usage", pctBar(s.cpu.totalPct) + "   user " + fmtPercent(s.cpu.userPct) + "  system " +
                           fmtPercent(s.cpu.systemPct));
  if (s.memory.ok) {
    kv(out, "Memory", pctBar(s.memory.loadPct) + "   " + fmtBytes(s.memory.usedPhys) + " used of " +
                          fmtBytes(s.memory.totalPhys));
  }
  if (s.memory.commitTotal && s.memory.commitLimit && *s.memory.commitLimit) {
    double c = 100.0 * static_cast<double>(*s.memory.commitTotal) / static_cast<double>(*s.memory.commitLimit);
    kv(out, "Commit", pctBar(c) + "   " + fmtBytes(*s.memory.commitTotal) + " of " + fmtBytes(*s.memory.commitLimit));
  }
  for (const auto& g : s.gpus) {
    if (g.software && !o.verbose) continue;
    std::string l = g.name;
    if (g.driverVersion) l += "  (driver " + *g.driverVersion + ")";
    l += "  load " + fmtPercent(g.utilizationPct, 0) + "  temp " + fmtCelsius(g.temperatureC);
    kv(out, "GPU", l);
  }
  for (const auto& v : s.volumes) {
    if (v.driveType != "fixed" || !v.ready) continue;
    std::string l = pctBar(v.usedPct) + "   " + fmtBytes(v.freeBytes) + " free of " + fmtBytes(v.totalBytes);
    if (!v.fileSystem.empty()) l += " (" + v.fileSystem + ")";
    kv(out, "Disk " + v.root, l);
  }
  Totals t = computeTotals(s);
  kv(out, "Disk I/O", "read " + fmtRate(t.diskReadBps) + "   write " + fmtRate(t.diskWriteBps));
  kv(out, "Network", "down " + fmtRate(t.netRxBps) + "   up " + fmtRate(t.netTxBps) + "   (" +
                         std::to_string(t.interfacesUp) + " interface(s) connected)");
  std::string bat;
  if (s.battery.present && !*s.battery.present) bat = "no battery";
  else if (s.battery.present) {
    bat = (s.battery.percent ? std::to_string(*s.battery.percent) + "%" : std::string(kNA));
    if (s.battery.acOnline) bat += *s.battery.acOnline ? ", on AC power" : ", on battery";
    if (s.battery.charging && *s.battery.charging) bat += ", charging";
  } else bat = "unknown";
  kv(out, "Battery", bat);
  if (s.processesCollected) {
    std::string pl = std::to_string(s.processes.size());
    if (s.memory.threadCount) pl += "  (" + fmtCount(*s.memory.threadCount) + " threads)";
    kv(out, "Processes", pl);
  }
  if (!s.ratesValid) out.colored(Color::Dim, "\r\n(rates need two samples; values shown as N/A)\r\n");
}

void printCpu(Out& out, const Snapshot& s, const CliOptions& o) {
  const auto& i = s.cpuInfo;
  section(out, "Processor");
  kv(out, "Model", i.brand);
  kv(out, "Vendor", orNA(i.vendor));
  kv(out, "Logical CPUs", std::to_string(i.logicalProcessors));
  kv(out, "Physical cores", i.physicalCores ? std::to_string(*i.physicalCores) : kNA);
  kv(out, "Packages", i.packages ? std::to_string(*i.packages) : kNA);
  kv(out, "L2 / L3 cache", (i.l2CacheKiB ? std::to_string(*i.l2CacheKiB) + " KiB" : std::string(kNA)) + " / " +
                               (i.l3CacheKiB ? std::to_string(*i.l3CacheKiB) + " KiB" : std::string(kNA)));
  kv(out, "Frequency", fmtMhz(s.cpu.currentMhz) + " current, " + fmtMhz(s.cpu.maxMhz ? s.cpu.maxMhz : i.maxMhz) +
                           " max (power manager; turbo not reflected)");
  kv(out, "Temperature", "N/A (no documented Windows 7 API)");
  out.line();
  section(out, "Utilization (measured over " +
                   std::to_string(o.intervalMs.value_or(kDefaultOneShotIntervalMs)) + " ms)");
  kv(out, "Total", pctBar(s.cpu.totalPct));
  kv(out, "User", pctBar(s.cpu.userPct));
  kv(out, "System", pctBar(s.cpu.systemPct));
  kv(out, "Idle", pctBar(s.cpu.idlePct));
  if (!s.cpu.corePct.empty()) {
    out.line();
    Table t({{"CORE", true}, {"USAGE", false}});
    for (size_t c = 0; c < s.cpu.corePct.size(); ++c) t.add({std::to_string(c), pctBar(s.cpu.corePct[c])});
    t.print(out);
  }
  std::vector<std::string> ns = i.notes;
  ns.insert(ns.end(), s.cpu.notes.begin(), s.cpu.notes.end());
  notes(out, ns, o.verbose);
}

void printMemory(Out& out, const Snapshot& s, const CliOptions& o) {
  const auto& m = s.memory;
  section(out, "Physical memory (RAM)");
  if (m.ok) {
    kv(out, "Total", fmtBytes(m.totalPhys));
    kv(out, "Used", fmtBytes(m.usedPhys));
    kv(out, "Available", fmtBytes(m.availPhys));
    kv(out, "Usage", pctBar(m.loadPct));
  } else {
    out.line("  unavailable");
  }
  if (m.systemCache) kv(out, "System cache", fmtBytes(*m.systemCache));
  out.line();
  section(out, "Commit charge (virtual memory backed by RAM + page files)");
  kv(out, "Committed", fmtBytes(m.commitTotal));
  kv(out, "Limit", fmtBytes(m.commitLimit));
  kv(out, "Peak", fmtBytes(m.commitPeak));
  if (m.commitTotal && m.commitLimit && *m.commitLimit)
    kv(out, "Usage", pctBar(100.0 * static_cast<double>(*m.commitTotal) / static_cast<double>(*m.commitLimit)));
  out.line();
  section(out, "Kernel");
  kv(out, "Paged pool", fmtBytes(m.kernelPaged));
  kv(out, "Non-paged pool", fmtBytes(m.kernelNonpaged));
  out.line();
  section(out, "Page files");
  if (!m.pageFilesKnown) out.line("  unavailable");
  else if (m.pageFiles.empty()) out.line("  none configured");
  else {
    Table t({{"PATH"}, {"SIZE", true}, {"IN USE", true}, {"PEAK", true}});
    for (const auto& p : m.pageFiles) t.add({p.path, fmtBytes(p.totalBytes), fmtBytes(p.inUseBytes), fmtBytes(p.peakBytes)});
    t.print(out);
  }
  out.line();
  section(out, "Processes");
  kv(out, "Count", m.processCount ? fmtCount(*m.processCount) : kNA);
  kv(out, "Threads", m.threadCount ? fmtCount(*m.threadCount) : kNA);
  kv(out, "Handles", m.handleCount ? fmtCount(*m.handleCount) : kNA);
  kv(out, "Working sets", fmtBytes(m.processWorkingSetTotal) + " (sum, accessible processes)");
  kv(out, "Private bytes", fmtBytes(m.processPrivateTotal) + " (sum, accessible processes)");
  notes(out, m.notes, o.verbose);
}

void printGpu(Out& out, const Snapshot& s, const CliOptions& o) {
  if (s.gpus.empty()) out.line("No display adapters detected.");
  bool first = true;
  for (const auto& g : s.gpus) {
    if (!first) out.line();
    first = false;
    section(out, g.name);
    kv(out, "Vendor", g.vendor + " (" + hex4(g.vendorId) + ":" + hex4(g.deviceId) + ")");
    kv(out, "Driver", orNA(g.driverVersion) + (g.driverDate ? "  (" + *g.driverDate + ")" : std::string()));
    kv(out, "Dedicated VRAM", fmtBytes(g.dedicatedVideoMemory));
    kv(out, "Shared memory", fmtBytes(g.sharedSystemMemory));
    if (g.software) kv(out, "Type", "software adapter");
    kv(out, "Sensor backend", g.sensorBackend);
    kv(out, "Utilization", fmtPercent(g.utilizationPct, 0));
    std::string vram = kNA;
    if (g.vramUsed && g.vramTotal) vram = fmtBytes(*g.vramUsed) + " / " + fmtBytes(*g.vramTotal);
    kv(out, "VRAM in use", vram);
    kv(out, "Temperature", fmtCelsius(g.temperatureC));
    kv(out, "Core clock", fmtMhz(g.coreClockMhz));
    kv(out, "Memory clock", fmtMhz(g.memoryClockMhz));
    kv(out, "Power", fmtWatts(g.powerWatts));
    for (const auto& n : g.notes) out.colored(Color::Dim, "  note: " + n + "\r\n");
  }
  if (o.verbose) {
    out.line();
    section(out, "Backends");
    for (const auto& b : s.gpuBackends) kv(out, b.name, std::string(b.available ? "available: " : "unavailable: ") + b.status, 24);
    notes(out, s.gpuNotes, true);
  }
}

void printDisk(Out& out, const Snapshot& s, const CliOptions& o) {
  section(out, "Volumes");
  Table v({{"DRIVE"}, {"LABEL", false, 20}, {"FS"}, {"TYPE"}, {"SIZE", true}, {"FREE", true}, {"USED", true}, {"DISK"}});
  for (const auto& vol : s.volumes) {
    if (!vol.ready && !o.verbose && vol.driveType != "fixed") {
      v.add({vol.root, "", "", vol.driveType, "", "", "", vol.note});
      continue;
    }
    std::string disks;
    for (unsigned d : vol.diskNumbers) disks += (disks.empty() ? "" : ",") + std::to_string(d);
    v.add({vol.root, vol.label, vol.fileSystem, vol.driveType, fmtBytes(vol.totalBytes), fmtBytes(vol.freeBytes),
           fmtPercent(vol.usedPct), disks});
  }
  v.print(out);
  out.line();
  section(out, "Physical disks");
  Table p({{"#", true}, {"MODEL", false, 32}, {"BUS"}, {"MEDIA"}, {"SIZE", true}, {"READ", true}, {"WRITE", true},
           {"ACTIVE", true}, {"SMART"}});
  for (const auto& d : s.disks) {
    std::string smart = d.smartPredictFailure ? (*d.smartPredictFailure ? "FAILURE PREDICTED" : "OK") : kNA;
    p.add({std::to_string(d.number), d.model, d.busType, d.mediaType, fmtBytes(d.sizeBytes), fmtRate(d.readBps),
           fmtRate(d.writeBps), fmtPercent(d.activePct), smart});
  }
  p.print(out);
  if (!s.smart.status.empty()) out.colored(Color::Dim, "  SMART: " + s.smart.status + "\r\n");
  if (o.verbose) {
    for (const auto& d : s.disks) {
      out.colored(Color::Dim, "  disk " + std::to_string(d.number) + ": media type from " + d.mediaTypeSource + "\r\n");
      if (!d.perfNote.empty()) out.colored(Color::Dim, "  disk " + std::to_string(d.number) + ": " + d.perfNote + "\r\n");
      if (d.smartTemperatureC)
        out.colored(Color::Dim, "  disk " + std::to_string(d.number) + ": SMART temperature " + fmtCelsius(d.smartTemperatureC) + "\r\n");
    }
  }
}

void printNetwork(Out& out, const Snapshot& s, const CliOptions& o) {
  Table t({{"INTERFACE", false, 28}, {"TYPE"}, {"STATUS"}, {"LINK", true}, {"DOWN", true}, {"UP", true},
           {"RECEIVED", true}, {"SENT", true}, {"IPV4", false, 32}});
  for (const auto& n : s.network) {
    std::string link = n.linkSpeedRxBps ? fmtBitRate(*n.linkSpeedRxBps) : kNA;
    std::string v4;
    for (const auto& a : n.ipv4) v4 += (v4.empty() ? "" : " ") + a;
    t.add({n.name, n.type, n.status, link, fmtRate(n.rxBytesPerSec), fmtRate(n.txBytesPerSec), fmtBytes(n.rxBytes),
           fmtBytes(n.txBytes), v4.empty() ? "-" : v4});
  }
  t.print(out);
  Totals tot = computeTotals(s);
  out.line();
  kv(out, "Total", "down " + fmtRate(tot.netRxBps) + "   up " + fmtRate(tot.netTxBps));
  if (o.verbose) {
    out.line();
    for (const auto& n : s.network) {
      section(out, n.name);
      kv(out, "Description", n.description);
      kv(out, "MAC", orNA(n.mac));
      for (const auto& a : n.ipv4) kv(out, "IPv4", a);
      for (const auto& a : n.ipv6) kv(out, "IPv6", a);
      kv(out, "Hardware", n.hardware ? "yes" : "no");
    }
  } else {
    out.colored(Color::Dim, "(use --verbose for MAC and IPv6 addresses, --all for virtual interfaces)\r\n");
  }
}

void printBattery(Out& out, const Snapshot& s, const CliOptions& o) {
  const auto& b = s.battery;
  kv(out, "AC power", b.acOnline ? (*b.acOnline ? "online" : "offline") : "unknown");
  if (b.present && !*b.present) {
    kv(out, "Battery", "not present");
  } else if (!b.present) {
    kv(out, "Battery", "unknown (Windows reports no battery information)");
  } else {
    kv(out, "Battery", "present");
    kv(out, "Charge", b.percent ? pctBar(static_cast<double>(*b.percent)) : std::string(kNA));
    kv(out, "Charging", b.charging ? (*b.charging ? "yes" : "no") : kNA);
    kv(out, "Time remaining", b.estimatedSecondsRemaining
                                  ? fmtDuration(*b.estimatedSecondsRemaining) + " (Windows estimate)"
                                  : std::string("N/A (no estimate from Windows)"));
    kv(out, "Design capacity", b.designCapacityMWh ? fmtCount(*b.designCapacityMWh) + " mWh" : kNA);
    kv(out, "Full charge cap.", b.fullChargeCapacityMWh ? fmtCount(*b.fullChargeCapacityMWh) + " mWh" : kNA);
    kv(out, "Health", fmtPercent(b.healthPct));
    if (!b.healthNote.empty()) out.colored(Color::Dim, "  note: " + b.healthNote + "\r\n");
  }
  notes(out, b.notes, o.verbose);
}

void printProcesses(Out& out, const std::vector<ProcessInfo>& shown, size_t total, const CliOptions& o) {
  Table t({{"PID", true}, {"NAME", false, 28}, {"CPU%", true}, {"MEMORY", true}, {"PRIVATE", true}, {"THREADS", true},
           {"PRIORITY"}, {"PATH", false, o.verbose ? 0u : 60u}});
  for (const auto& p : shown) {
    t.add({std::to_string(p.pid), p.name, p.cpuPct ? fmtFixed(*p.cpuPct, 1) : kNA, fmtBytes(p.workingSet),
           fmtBytes(p.privateBytes), std::to_string(p.threads), p.priorityClass.value_or(p.accessDenied ? "-" : kNA),
           p.path.value_or(p.accessDenied ? "(access denied)" : "")});
  }
  t.print(out);
  out.colored(Color::Dim, "\r\n" + std::to_string(shown.size()) + " of " + std::to_string(total) +
                              " processes, sorted by " + processSortName(o.sort) +
                              ". CPU% is a share of all logical processors.\r\n");
}

void printProcess(Out& out, const ProcessInfo& p, const CliOptions&) {
  section(out, p.name + " (PID " + std::to_string(p.pid) + ")");
  kv(out, "Path", p.path.value_or(p.accessDenied ? "N/A (access denied)" : "N/A"));
  kv(out, "Parent PID", std::to_string(p.parentPid));
  kv(out, "CPU", fmtPercent(p.cpuPct));
  kv(out, "CPU time", p.cpuTime100ns ? fmtFixed(static_cast<double>(*p.cpuTime100ns) / 1e7, 2) + " s" : std::string(kNA));
  kv(out, "Working set", fmtBytes(p.workingSet));
  kv(out, "Peak working set", fmtBytes(p.peakWorkingSet));
  kv(out, "Private bytes", fmtBytes(p.privateBytes));
  kv(out, "Threads", std::to_string(p.threads));
  kv(out, "Handles", p.handles ? std::to_string(*p.handles) : kNA);
  kv(out, "Priority", p.priorityClass.value_or(kNA) + " (base " + std::to_string(p.basePriority) + ")");
  kv(out, "Started", p.createTime ? fileTimeToLocal(*p.createTime) + " (local)" : std::string(kNA));
  kv(out, "I/O read", fmtBytes(p.ioReadBytes));
  kv(out, "I/O written", fmtBytes(p.ioWriteBytes));
  if (p.accessDenied) out.colored(Color::Warn, "Some details are unavailable: access denied (try an elevated prompt).\r\n");
}

void printSensors(Out& out, const Snapshot& s, const CliOptions& o) {
  Table t({{"SENSOR", false, 44}, {"VALUE", true}, {"SOURCE"}, {"NOTE", false, o.verbose ? 0u : 70u}});
  for (const auto& r : s.sensors) {
    std::string val = kNA;
    if (r.value) val = fmtFixed(*r.value, r.unit == "MHz" ? 0 : 1) + " " + r.unit;
    t.add({r.name, val, r.source, r.note});
  }
  t.print(out);
}

void printHardware(Out& out, const Snapshot& s, const CliOptions&) {
  const auto& sys = s.system;
  section(out, "Operating system");
  kv(out, "Windows", osLine(sys));
  kv(out, "Edition", orNA(sys.edition));
  kv(out, "Computer name", sys.computerName);
  kv(out, "Uptime", fmtDuration(sys.uptimeSeconds));
  kv(out, "Boot time", sys.bootTimeUtc + " (derived from uptime)");
  kv(out, "sevenmon build", sys.processArch + (sys.wow64 ? " (running under WOW64)" : ""));
  kv(out, "Elevated", sys.elevated ? "yes" : "no");
  if (sys.outOfSupport)
    out.colored(Color::Warn, "  This Windows version no longer receives security updates from Microsoft.\r\n");
  out.line();
  const auto& b = sys.smbios;
  section(out, "Firmware (SMBIOS " + (b.ok ? b.version : std::string(kNA)) + ")");
  kv(out, "System", orNA(b.systemManufacturer) + " " + orNA(b.systemProduct));
  kv(out, "Motherboard", orNA(b.boardManufacturer) + " " + orNA(b.boardProduct));
  kv(out, "BIOS", orNA(b.biosVendor) + " " + orNA(b.biosVersion) + " (" + orNA(b.biosDate) + ")");
  if (!b.note.empty()) out.colored(Color::Dim, "  note: " + b.note + "\r\n");
  out.line();
  section(out, "Hardware");
  std::string topo = std::to_string(s.cpuInfo.logicalProcessors) + " logical processors";
  if (s.cpuInfo.physicalCores) topo = std::to_string(*s.cpuInfo.physicalCores) + " cores, " + topo;
  kv(out, "CPU", s.cpuInfo.brand + " (" + topo + ")");
  kv(out, "Memory", s.memory.ok ? fmtBytes(s.memory.totalPhys) : std::string(kNA));
  for (const auto& g : s.gpus)
    kv(out, "GPU", g.name + (g.dedicatedVideoMemory ? " (" + fmtBytes(*g.dedicatedVideoMemory) + ")" : std::string()));
  for (const auto& d : s.disks)
    kv(out, "Disk " + std::to_string(d.number), d.model + " (" + fmtBytes(d.sizeBytes) + ", " + d.busType + ", " + d.mediaType + ")");
}

std::string sampleLine(const Snapshot& s) {
  Totals t = computeTotals(s);
  std::string l = s.timestamp + "  cpu " + fmtPercent(s.cpu.totalPct);
  if (s.memory.ok) l += "  mem " + fmtFixed(s.memory.loadPct, 1) + "% (" + fmtBytes(s.memory.usedPhys) + ")";
  l += "  disk r " + fmtRate(t.diskReadBps) + " w " + fmtRate(t.diskWriteBps);
  l += "  net down " + fmtRate(t.netRxBps) + " up " + fmtRate(t.netTxBps);
  if (s.processesCollected) l += "  procs " + std::to_string(s.processes.size());
  return l;
}

}  // namespace sm
