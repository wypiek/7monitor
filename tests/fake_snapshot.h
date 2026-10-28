#pragma once
// Deterministic synthetic snapshot for rendering tests.
#include "sampler/sampler.h"

inline sm::Snapshot fakeSnapshot(bool withSensors) {
  using namespace sm;
  Snapshot s;
  s.fastSeq = 3;
  s.ratesValid = true;
  s.slowReady = true;
  s.timestamp = "2026-01-01T00:00:00Z";
  s.system.productName = "Windows 7 Professional";
  s.system.servicePack = "Service Pack 1";
  s.system.displayVersion = "6.1.7601";
  s.system.osArch = "x64";
  s.system.computerName = "TESTBOX";
  s.system.uptimeSeconds = 3 * 86400 + 3600;
  s.cpuInfo.brand = "Intel(R) Core(TM) i5-2500 CPU @ 3.30GHz";
  s.cpuInfo.vendor = "GenuineIntel";
  s.cpuInfo.logicalProcessors = 8;
  s.cpuInfo.physicalCores = 4u;
  s.cpu.totalPct = 42.5;
  s.cpu.userPct = 30.0;
  s.cpu.systemPct = 12.5;
  s.cpu.idlePct = 57.5;
  s.cpu.currentMhz = 3300u;
  for (int i = 0; i < 8; ++i) s.cpu.corePct.push_back(i == 3 ? std::optional<double>() : std::optional<double>(i * 12.0));
  s.memory.ok = true;
  s.memory.totalPhys = 8ull << 30;
  s.memory.availPhys = 3ull << 30;
  s.memory.usedPhys = 5ull << 30;
  s.memory.loadPct = 62.5;
  s.memory.commitTotal = 6ull << 30;
  s.memory.commitLimit = 16ull << 30;
  s.memory.pageFilesKnown = true;
  s.memory.pageFiles.push_back({"C:\\pagefile.sys", 8ull << 30, 1ull << 30, 2ull << 30});
  GpuAdapter g;
  g.name = "NVIDIA GeForce GTX 1650";
  g.vendor = "NVIDIA";
  g.vendorId = 0x10DE;
  g.deviceId = 0x1F82;
  g.driverVersion = "26.21.14.4250";
  g.dedicatedVideoMemory = 4ull << 30;
  if (withSensors) {
    g.sensorBackend = "NVML";
    g.utilizationPct = 17.0;
    g.temperatureC = 51.0;
    g.vramUsed = 1ull << 30;
    g.vramTotal = 4ull << 30;
  } else {
    g.notes.push_back("NVML not available from the installed driver");
  }
  s.gpus.push_back(g);
  VolumeInfo v;
  v.root = "C:\\";
  v.label = "System";
  v.fileSystem = "NTFS";
  v.driveType = "fixed";
  v.ready = true;
  v.totalBytes = 250ull << 30;
  v.freeBytes = 100ull << 30;
  v.usedPct = 60.0;
  s.volumes.push_back(v);
  VolumeInfo cd;
  cd.root = "D:\\";
  cd.driveType = "cdrom";
  cd.note = "no media";
  s.volumes.push_back(cd);
  PhysicalDiskInfo d;
  d.number = 0;
  d.model = "ST500DM002";
  d.busType = "SATA";
  d.mediaType = "HDD";
  d.readBps = 1048576.0;
  d.writeBps = 2048.0;
  d.activePct = 5.0;
  s.disks.push_back(d);
  NetInterface n;
  n.name = "Local Area Connection";
  n.type = "ethernet";
  n.status = "up";
  n.connected = true;
  n.hardware = true;
  n.ipv4.push_back("192.168.1.10");
  n.linkSpeedRxBps = 1000000000ull;
  n.rxBytesPerSec = 125000.0;
  n.txBytesPerSec = 4000.0;
  s.network.push_back(n);
  s.battery.statusOk = true;
  s.battery.present = false;
  s.processesCollected = true;
  for (uint32_t i = 0; i < 60; ++i) {
    ProcessInfo p;
    p.pid = 100 + i * 4;
    p.name = i == 5 ? "M\xC3\xBCller-\xE6\x97\xA5.exe" : "proc" + std::to_string(i) + ".exe";
    p.threads = 3 + i;
    p.cpuPct = static_cast<double>(i % 7);
    p.workingSet = (i + 1) * 1048576ull;
    p.privateBytes = (i + 2) * 1048576ull;
    p.priorityClass = "Normal";
    p.path = "C:\\Program Files\\App " + std::to_string(i) + "\\proc.exe";
    if (i == 7) {
      p.accessDenied = true;
      p.cpuPct.reset();
      p.path.reset();
    }
    s.processes.push_back(p);
  }
  return s;
}
