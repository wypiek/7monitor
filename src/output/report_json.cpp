#include "output/report_json.h"

#include <cstdio>

#include "util/format.h"

namespace sm {

std::string hex4(unsigned v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%04X", v);
  return buf;
}

static JValue strings(const std::vector<std::string>& v) {
  JValue a = JValue::array();
  for (const auto& s : v) a.push(s);
  return a;
}

static JValue pct(const std::optional<double>& v) { return JValue::number(v, 1); }

static JValue optStr(const std::optional<std::string>& v) { return v ? JValue(*v) : JValue(); }

JValue envelope(const std::string& type, const std::string& timestamp, JValue data) {
  JValue o = JValue::object();
  o.set("schema_version", kSchemaVersion);
  o.set("tool", "sevenmon");
  o.set("tool_version", SEVENMON_VERSION);
  o.set("type", type);
  o.set("timestamp", timestamp);
  o.set("data", std::move(data));
  return o;
}

Totals computeTotals(const Snapshot& s) {
  Totals t;
  bool anyDisk = false, anyNet = false;
  double r = 0, w = 0, rx = 0, tx = 0;
  for (const auto& d : s.disks) {
    if (d.readBps && d.writeBps) {
      r += *d.readBps;
      w += *d.writeBps;
      anyDisk = true;
    }
  }
  for (const auto& n : s.network) {
    if (n.connected) t.interfacesUp++;
    if (n.rxBytesPerSec && n.txBytesPerSec) {
      rx += *n.rxBytesPerSec;
      tx += *n.txBytesPerSec;
      anyNet = true;
    }
  }
  if (anyDisk) {
    t.diskReadBps = r;
    t.diskWriteBps = w;
  }
  if (anyNet) {
    t.netRxBps = rx;
    t.netTxBps = tx;
  }
  return t;
}

JValue jsonSystem(const SystemInfo& s) {
  JValue o = JValue::object();
  JValue os = JValue::object();
  os.set("name", s.productName);
  os.set("edition", s.edition.empty() ? JValue() : JValue(s.edition));
  os.set("service_pack", s.servicePack.empty() ? JValue() : JValue(s.servicePack));
  os.set("version", s.displayVersion);
  os.set("build", s.build);
  os.set("architecture", s.osArch);
  os.set("is_windows_7", s.isWindows7);
  os.set("out_of_support", s.outOfSupport);
  o.set("os", os);
  o.set("process_architecture", s.processArch);
  o.set("wow64", s.wow64);
  o.set("computer_name", s.computerName);
  o.set("uptime_seconds", static_cast<unsigned long long>(s.uptimeSeconds));
  o.set("boot_time_utc", s.bootTimeUtc);
  o.set("boot_time_source", "derived from GetTickCount64 (now - uptime)");
  o.set("elevated", s.elevated);
  return o;
}

JValue jsonCpu(const Snapshot& s) {
  const auto& i = s.cpuInfo;
  const auto& u = s.cpu;
  JValue o = JValue::object();
  o.set("vendor", i.vendor);
  o.set("brand", i.brand);
  o.set("logical_processors", i.logicalProcessors);
  o.set("physical_cores", i.physicalCores);
  o.set("packages", i.packages);
  o.set("l2_cache_kib", i.l2CacheKiB);
  o.set("l3_cache_kib", i.l3CacheKiB);
  o.set("max_mhz", u.maxMhz ? u.maxMhz : i.maxMhz);
  o.set("current_mhz", u.currentMhz);
  o.set("current_mhz_source", "CallNtPowerInformation (may not reflect turbo boost)");
  JValue usage = JValue::object();
  usage.set("total_pct", pct(u.totalPct));
  usage.set("user_pct", pct(u.userPct));
  usage.set("system_pct", pct(u.systemPct));
  usage.set("idle_pct", pct(u.idlePct));
  o.set("usage", usage);
  JValue cores = JValue::array();
  for (const auto& c : u.corePct) cores.push(pct(c));
  o.set("per_core_pct", cores);
  o.set("temperature_c", JValue());
  o.set("temperature_note", "not available: no documented Windows 7 API (see docs/hardware-support.md)");
  std::vector<std::string> notes = i.notes;
  notes.insert(notes.end(), u.notes.begin(), u.notes.end());
  o.set("notes", strings(notes));
  return o;
}

JValue jsonMemory(const Snapshot& s) {
  const auto& m = s.memory;
  JValue o = JValue::object();
  JValue phys = JValue::object();
  phys.set("total_bytes", m.ok ? JValue(static_cast<unsigned long long>(m.totalPhys)) : JValue());
  phys.set("available_bytes", m.ok ? JValue(static_cast<unsigned long long>(m.availPhys)) : JValue());
  phys.set("used_bytes", m.ok ? JValue(static_cast<unsigned long long>(m.usedPhys)) : JValue());
  phys.set("used_pct", m.ok ? JValue(m.loadPct, 1) : JValue());
  o.set("physical", phys);
  JValue commit = JValue::object();
  commit.set("total_bytes", m.commitTotal);
  commit.set("limit_bytes", m.commitLimit);
  commit.set("peak_bytes", m.commitPeak);
  std::optional<double> cpct;
  if (m.commitTotal && m.commitLimit && *m.commitLimit)
    cpct = 100.0 * static_cast<double>(*m.commitTotal) / static_cast<double>(*m.commitLimit);
  commit.set("used_pct", pct(cpct));
  o.set("commit", commit);
  JValue k = JValue::object();
  k.set("paged_pool_bytes", m.kernelPaged);
  k.set("nonpaged_pool_bytes", m.kernelNonpaged);
  o.set("kernel", k);
  o.set("system_cache_bytes", m.systemCache);
  JValue pfs = JValue::array();
  for (const auto& p : m.pageFiles) {
    JValue e = JValue::object();
    e.set("path", p.path);
    e.set("total_bytes", static_cast<unsigned long long>(p.totalBytes));
    e.set("in_use_bytes", static_cast<unsigned long long>(p.inUseBytes));
    e.set("peak_bytes", static_cast<unsigned long long>(p.peakBytes));
    pfs.push(e);
  }
  o.set("page_files", m.pageFilesKnown ? pfs : JValue());
  JValue pr = JValue::object();
  pr.set("count", m.processCount);
  pr.set("threads", m.threadCount);
  pr.set("handles", m.handleCount);
  pr.set("working_set_total_bytes", m.processWorkingSetTotal);
  pr.set("private_total_bytes", m.processPrivateTotal);
  o.set("processes", pr);
  o.set("notes", strings(m.notes));
  return o;
}

static JValue gpuAdapter(const GpuAdapter& g) {
  JValue o = JValue::object();
  o.set("name", g.name);
  o.set("vendor", g.vendor);
  o.set("vendor_id", hex4(g.vendorId));
  o.set("device_id", hex4(g.deviceId));
  o.set("software_adapter", g.software);
  o.set("detection", g.detectionSource);
  o.set("driver_version", optStr(g.driverVersion));
  o.set("driver_date", optStr(g.driverDate));
  o.set("driver_provider", optStr(g.driverProvider));
  o.set("dedicated_video_memory_bytes", g.dedicatedVideoMemory);
  o.set("shared_system_memory_bytes", g.sharedSystemMemory);
  o.set("sensor_backend", g.sensorBackend);
  o.set("utilization_pct", pct(g.utilizationPct));
  o.set("memory_controller_pct", pct(g.memoryControllerPct));
  o.set("vram_used_bytes", g.vramUsed);
  o.set("vram_total_bytes", g.vramTotal);
  o.set("temperature_c", JValue::number(g.temperatureC, 1));
  o.set("core_clock_mhz", g.coreClockMhz);
  o.set("memory_clock_mhz", g.memoryClockMhz);
  o.set("power_w", JValue::number(g.powerWatts, 1));
  o.set("notes", strings(g.notes));
  return o;
}

JValue jsonGpu(const Snapshot& s) {
  JValue o = JValue::object();
  JValue a = JValue::array();
  for (const auto& g : s.gpus) a.push(gpuAdapter(g));
  o.set("adapters", a);
  JValue b = JValue::array();
  for (const auto& st : s.gpuBackends) {
    JValue e = JValue::object();
    e.set("name", st.name);
    e.set("available", st.available);
    e.set("status", st.status);
    b.push(e);
  }
  o.set("backends", b);
  o.set("notes", strings(s.gpuNotes));
  return o;
}

static JValue volumeJson(const VolumeInfo& v) {
  JValue o = JValue::object();
  o.set("kind", "volume");
  o.set("root", v.root);
  o.set("label", v.label);
  o.set("file_system", v.fileSystem.empty() ? JValue() : JValue(v.fileSystem));
  o.set("drive_type", v.driveType);
  o.set("ready", v.ready);
  o.set("total_bytes", v.totalBytes);
  o.set("free_bytes", v.freeBytes);
  o.set("available_to_user_bytes", v.availableToUserBytes);
  o.set("used_pct", pct(v.usedPct));
  JValue disks = JValue::array();
  for (unsigned d : v.diskNumbers) disks.push(d);
  o.set("physical_disks", disks);
  o.set("note", v.note.empty() ? JValue() : JValue(v.note));
  return o;
}

static JValue diskJson(const PhysicalDiskInfo& d) {
  JValue o = JValue::object();
  o.set("kind", "physical_disk");
  o.set("number", d.number);
  o.set("model", d.model);
  o.set("bus_type", d.busType.empty() ? JValue() : JValue(d.busType));
  o.set("removable", d.removable);
  o.set("size_bytes", d.sizeBytes);
  o.set("media_type", d.mediaType);
  o.set("media_type_source", d.mediaTypeSource);
  o.set("read_bytes_per_sec", JValue::number(d.readBps, 0));
  o.set("write_bytes_per_sec", JValue::number(d.writeBps, 0));
  o.set("reads_per_sec", JValue::number(d.readIops, 1));
  o.set("writes_per_sec", JValue::number(d.writeIops, 1));
  o.set("active_pct", pct(d.activePct));
  o.set("smart_predict_failure", d.smartPredictFailure);
  o.set("smart_temperature_c", JValue::number(d.smartTemperatureC, 0));
  o.set("smart_note", d.smartNote.empty() ? JValue() : JValue(d.smartNote));
  o.set("perf_note", d.perfNote.empty() ? JValue() : JValue(d.perfNote));
  return o;
}

JValue jsonDisk(const Snapshot& s) {
  JValue o = JValue::object();
  JValue vols = JValue::array();
  for (const auto& v : s.volumes) vols.push(volumeJson(v));
  o.set("volumes", vols);
  JValue disks = JValue::array();
  for (const auto& d : s.disks) disks.push(diskJson(d));
  o.set("physical_disks", disks);
  o.set("smart_status", s.smart.status.empty() ? JValue() : JValue(s.smart.status));
  return o;
}

JValue jsonNetwork(const Snapshot& s) {
  JValue o = JValue::object();
  JValue arr = JValue::array();
  for (const auto& n : s.network) {
    JValue e = JValue::object();
    e.set("name", n.name);
    e.set("description", n.description);
    e.set("type", n.type);
    e.set("status", n.status);
    e.set("connected", n.connected);
    e.set("hardware", n.hardware);
    e.set("mac", n.mac.empty() ? JValue() : JValue(n.mac));
    e.set("ipv4", strings(n.ipv4));
    e.set("ipv6", strings(n.ipv6));
    e.set("link_speed_rx_bps", n.linkSpeedRxBps);
    e.set("link_speed_tx_bps", n.linkSpeedTxBps);
    e.set("rx_bytes_total", static_cast<unsigned long long>(n.rxBytes));
    e.set("tx_bytes_total", static_cast<unsigned long long>(n.txBytes));
    e.set("rx_bytes_per_sec", JValue::number(n.rxBytesPerSec, 0));
    e.set("tx_bytes_per_sec", JValue::number(n.txBytesPerSec, 0));
    e.set("rate_note", n.rateNote.empty() ? JValue() : JValue(n.rateNote));
    arr.push(e);
  }
  o.set("interfaces", arr);
  Totals t = computeTotals(s);
  JValue tot = JValue::object();
  tot.set("rx_bytes_per_sec", JValue::number(t.netRxBps, 0));
  tot.set("tx_bytes_per_sec", JValue::number(t.netTxBps, 0));
  tot.set("interfaces_up", t.interfacesUp);
  o.set("totals", tot);
  return o;
}

JValue jsonBattery(const BatteryInfo& b) {
  JValue o = JValue::object();
  o.set("present", b.present);
  o.set("ac_online", b.acOnline);
  o.set("charging", b.charging);
  o.set("charge_pct", b.percent);
  o.set("estimated_seconds_remaining", b.estimatedSecondsRemaining);
  o.set("estimate_source", "Windows estimate (GetSystemPowerStatus); not a measurement");
  o.set("design_capacity_mwh", b.designCapacityMWh);
  o.set("full_charge_capacity_mwh", b.fullChargeCapacityMWh);
  o.set("health_pct", pct(b.healthPct));
  o.set("health_note", b.healthNote.empty() ? JValue() : JValue(b.healthNote));
  o.set("notes", strings(b.notes));
  return o;
}

JValue jsonProcess(const ProcessInfo& p) {
  JValue o = JValue::object();
  o.set("pid", p.pid);
  o.set("parent_pid", p.parentPid);
  o.set("name", p.name);
  o.set("cpu_pct", pct(p.cpuPct));
  o.set("cpu_time_seconds", p.cpuTime100ns ? JValue(static_cast<double>(*p.cpuTime100ns) / 1e7, 2) : JValue());
  o.set("working_set_bytes", p.workingSet);
  o.set("peak_working_set_bytes", p.peakWorkingSet);
  o.set("private_bytes", p.privateBytes);
  o.set("threads", p.threads);
  o.set("handles", p.handles);
  o.set("priority_class", optStr(p.priorityClass));
  o.set("base_priority", p.basePriority);
  o.set("start_time_utc", p.createTime ? JValue(fileTimeToIsoUtc(*p.createTime)) : JValue());
  o.set("io_read_bytes", p.ioReadBytes);
  o.set("io_write_bytes", p.ioWriteBytes);
  o.set("path", optStr(p.path));
  o.set("access_denied", p.accessDenied);
  return o;
}

JValue jsonProcesses(const std::vector<ProcessInfo>& procs, ProcessSort sort, size_t total) {
  JValue o = JValue::object();
  o.set("total_processes", static_cast<unsigned long long>(total));
  o.set("shown", static_cast<unsigned long long>(procs.size()));
  o.set("sort", processSortName(sort));
  o.set("cpu_pct_basis", "percent of total capacity of all logical processors");
  JValue arr = JValue::array();
  for (const auto& p : procs) arr.push(jsonProcess(p));
  o.set("processes", arr);
  return o;
}

JValue jsonSensors(const Snapshot& s) {
  JValue o = JValue::object();
  JValue arr = JValue::array();
  for (const auto& r : s.sensors) {
    JValue e = JValue::object();
    e.set("name", r.name);
    e.set("kind", r.kind);
    e.set("value", JValue::number(r.value, 1));
    e.set("unit", r.unit);
    e.set("source", r.source);
    e.set("note", r.note.empty() ? JValue() : JValue(r.note));
    arr.push(e);
  }
  o.set("sensors", arr);
  return o;
}

JValue jsonHardware(const Snapshot& s) {
  JValue o = jsonSystem(s.system);
  const auto& b = s.system.smbios;
  JValue fw = JValue::object();
  fw.set("smbios_version", b.ok ? JValue(b.version) : JValue());
  fw.set("system_manufacturer", optStr(b.systemManufacturer));
  fw.set("system_product", optStr(b.systemProduct));
  fw.set("system_version", optStr(b.systemVersion));
  fw.set("board_manufacturer", optStr(b.boardManufacturer));
  fw.set("board_product", optStr(b.boardProduct));
  fw.set("board_version", optStr(b.boardVersion));
  fw.set("bios_vendor", optStr(b.biosVendor));
  fw.set("bios_version", optStr(b.biosVersion));
  fw.set("bios_date", optStr(b.biosDate));
  fw.set("note", b.note.empty() ? JValue() : JValue(b.note));
  o.set("firmware", fw);
  JValue cpu = JValue::object();
  cpu.set("brand", s.cpuInfo.brand);
  cpu.set("vendor", s.cpuInfo.vendor);
  cpu.set("logical_processors", s.cpuInfo.logicalProcessors);
  cpu.set("physical_cores", s.cpuInfo.physicalCores);
  cpu.set("packages", s.cpuInfo.packages);
  cpu.set("max_mhz", s.cpuInfo.maxMhz);
  o.set("cpu", cpu);
  o.set("memory_total_bytes", s.memory.ok ? JValue(static_cast<unsigned long long>(s.memory.totalPhys)) : JValue());
  JValue gpus = JValue::array();
  for (const auto& g : s.gpus) {
    JValue e = JValue::object();
    e.set("name", g.name);
    e.set("vendor", g.vendor);
    e.set("driver_version", optStr(g.driverVersion));
    e.set("dedicated_video_memory_bytes", g.dedicatedVideoMemory);
    gpus.push(e);
  }
  o.set("gpus", gpus);
  JValue disks = JValue::array();
  for (const auto& d : s.disks) {
    JValue e = JValue::object();
    e.set("number", d.number);
    e.set("model", d.model);
    e.set("bus_type", d.busType);
    e.set("size_bytes", d.sizeBytes);
    e.set("media_type", d.mediaType);
    disks.push(e);
  }
  o.set("disks", disks);
  return o;
}

JValue jsonSummary(const Snapshot& s) {
  JValue o = JValue::object();
  o.set("system", jsonSystem(s.system));
  JValue cpu = JValue::object();
  cpu.set("brand", s.cpuInfo.brand);
  cpu.set("logical_processors", s.cpuInfo.logicalProcessors);
  cpu.set("physical_cores", s.cpuInfo.physicalCores);
  cpu.set("usage_pct", pct(s.cpu.totalPct));
  cpu.set("current_mhz", s.cpu.currentMhz);
  o.set("cpu", cpu);
  JValue mem = JValue::object();
  mem.set("total_bytes", s.memory.ok ? JValue(static_cast<unsigned long long>(s.memory.totalPhys)) : JValue());
  mem.set("used_bytes", s.memory.ok ? JValue(static_cast<unsigned long long>(s.memory.usedPhys)) : JValue());
  mem.set("used_pct", s.memory.ok ? JValue(s.memory.loadPct, 1) : JValue());
  mem.set("commit_total_bytes", s.memory.commitTotal);
  mem.set("commit_limit_bytes", s.memory.commitLimit);
  o.set("memory", mem);
  JValue gpus = JValue::array();
  for (const auto& g : s.gpus) {
    JValue e = JValue::object();
    e.set("name", g.name);
    e.set("utilization_pct", pct(g.utilizationPct));
    e.set("temperature_c", JValue::number(g.temperatureC, 1));
    gpus.push(e);
  }
  o.set("gpus", gpus);
  JValue vols = JValue::array();
  for (const auto& v : s.volumes) {
    if (v.driveType != "fixed" || !v.ready) continue;
    JValue e = JValue::object();
    e.set("root", v.root);
    e.set("total_bytes", v.totalBytes);
    e.set("free_bytes", v.freeBytes);
    e.set("used_pct", pct(v.usedPct));
    vols.push(e);
  }
  o.set("volumes", vols);
  Totals t = computeTotals(s);
  JValue io = JValue::object();
  io.set("disk_read_bytes_per_sec", JValue::number(t.diskReadBps, 0));
  io.set("disk_write_bytes_per_sec", JValue::number(t.diskWriteBps, 0));
  io.set("net_rx_bytes_per_sec", JValue::number(t.netRxBps, 0));
  io.set("net_tx_bytes_per_sec", JValue::number(t.netTxBps, 0));
  io.set("interfaces_up", t.interfacesUp);
  o.set("io", io);
  JValue bat = JValue::object();
  bat.set("present", s.battery.present);
  bat.set("charge_pct", s.battery.percent);
  bat.set("ac_online", s.battery.acOnline);
  o.set("battery", bat);
  o.set("process_count", s.processesCollected ? JValue(static_cast<unsigned long long>(s.processes.size())) : JValue());
  return o;
}

JValue jsonSample(const Snapshot& s) {
  Totals t = computeTotals(s);
  std::optional<double> commitPct;
  if (s.memory.commitTotal && s.memory.commitLimit && *s.memory.commitLimit)
    commitPct = 100.0 * static_cast<double>(*s.memory.commitTotal) / static_cast<double>(*s.memory.commitLimit);
  std::optional<double> gpuUtil, gpuTemp;
  for (const auto& g : s.gpus) {
    if (!gpuUtil && g.utilizationPct) gpuUtil = g.utilizationPct;
    if (!gpuTemp && g.temperatureC) gpuTemp = g.temperatureC;
  }
  JValue o = JValue::object();
  o.set("timestamp", s.timestamp);
  o.set("cpu_total_pct", pct(s.cpu.totalPct));
  o.set("cpu_user_pct", pct(s.cpu.userPct));
  o.set("cpu_system_pct", pct(s.cpu.systemPct));
  o.set("cpu_mhz", s.cpu.currentMhz);
  o.set("mem_total_bytes", s.memory.ok ? JValue(static_cast<unsigned long long>(s.memory.totalPhys)) : JValue());
  o.set("mem_used_bytes", s.memory.ok ? JValue(static_cast<unsigned long long>(s.memory.usedPhys)) : JValue());
  o.set("mem_used_pct", s.memory.ok ? JValue(s.memory.loadPct, 1) : JValue());
  o.set("commit_total_bytes", s.memory.commitTotal);
  o.set("commit_limit_bytes", s.memory.commitLimit);
  o.set("commit_used_pct", pct(commitPct));
  o.set("disk_read_bytes_per_sec", JValue::number(t.diskReadBps, 0));
  o.set("disk_write_bytes_per_sec", JValue::number(t.diskWriteBps, 0));
  o.set("net_rx_bytes_per_sec", JValue::number(t.netRxBps, 0));
  o.set("net_tx_bytes_per_sec", JValue::number(t.netTxBps, 0));
  o.set("gpu0_utilization_pct", pct(gpuUtil));
  o.set("gpu0_temperature_c", JValue::number(gpuTemp, 1));
  o.set("process_count", s.processesCollected ? JValue(static_cast<unsigned long long>(s.processes.size())) : JValue());
  o.set("battery_charge_pct", s.battery.percent);
  o.set("ac_online", s.battery.acOnline);
  o.set("uptime_seconds", static_cast<unsigned long long>(s.system.uptimeSeconds));
  return o;
}

}  // namespace sm
