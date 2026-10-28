// "sevenmon doctor": probes every data source and explains what works, what does not and
// why. Read-only; it never changes system configuration.
#include <windows.h>

#include <vector>

#include "app/commands.h"
#include "collectors/battery.h"
#include "collectors/cpu.h"
#include "collectors/disk.h"
#include "collectors/gpu.h"
#include "collectors/memory.h"
#include "collectors/network.h"
#include "collectors/process.h"
#include "collectors/sensors.h"
#include "collectors/smart.h"
#include "collectors/system.h"
#include "output/report_json.h"
#include "output/table.h"
#include "platform/console.h"
#include "platform/win.h"
#include "platform/wmi.h"
#include "util/csv.h"
#include "util/format.h"
#include "util/strings.h"

namespace sm {

namespace {

struct Check {
  std::string area, name, status, detail;  // status: ok, limited, unavailable, warning, info
};

}  // namespace

int runDoctor(const CliOptions& o) {
  std::vector<Check> c;
  auto add = [&](const std::string& a, const std::string& n, const std::string& s, const std::string& d) {
    c.push_back({a, n, s, d});
  };

  SystemInfo sys = querySystemInfo();
  add("system", "Windows version", "info", sys.productName + (sys.servicePack.empty() ? "" : " " + sys.servicePack) + " (" + sys.displayVersion + ", " + sys.osArch + ")");
  if (sys.isWindows7 && sys.build < 7601)
    add("system", "Service Pack 1", "warning", "Windows 7 SP1 is required (build 7601); this is build " + std::to_string(sys.build));
  else if (sys.isWindows7)
    add("system", "Service Pack 1", "ok", "installed");
  if (sys.outOfSupport)
    add("system", "Security support", "warning",
        "this Windows version no longer receives security updates from Microsoft; sevenmon does not change that");
  if (sys.major < 6 || (sys.major == 6 && sys.minor < 1))
    add("system", "Supported OS", "warning", "Windows 7 SP1 or later is required");
  add("system", "Architecture", "info", "sevenmon " + sys.processArch + " build" + (sys.wow64 ? " under WOW64 (use the x64 build for full detail)" : ""));
  add("privileges", "Elevation", sys.elevated ? "ok" : "info",
      sys.elevated ? "running elevated" : "standard user: SMART and ACPI thermal zones usually need 'Run as administrator'");

  ConsoleCaps caps = detectConsoleCaps(GetStdHandle(STD_OUTPUT_HANDLE));
  if (caps.isConsole) {
    add("console", "Console", "ok", std::to_string(caps.width) + "x" + std::to_string(caps.height) + ", code page " +
                                       std::to_string(caps.outputCodePage) + ", font " + (caps.fontKnown ? caps.fontFace : "unknown"));
    add("console", "Box drawing", caps.unicodeBlocks ? "ok" : "limited",
        caps.unicodeBlocks ? caps.reason : caps.reason + " (choose Lucida Console/Consolas in the window properties)");
  } else {
    add("console", "Console", "info", "stdout is redirected; the dashboard needs a console window");
  }

  CpuRaw r1 = sampleCpu();
  add("cpu", "GetSystemTimes", r1.totalOk ? "ok" : "unavailable", r1.totalOk ? "total utilization available" : "call failed");
  add("cpu", "Per-core counters", r1.coresOk ? "ok" : "unavailable",
      r1.coresOk ? std::to_string(r1.cores.size()) + " processors (NtQuerySystemInformation)" : "NtQuerySystemInformation failed");
  CpuInfo ci = queryCpuInfo();
  add("cpu", "Topology", ci.physicalCores ? "ok" : "limited",
      ci.brand + ", " + std::to_string(ci.logicalProcessors) + " logical" +
          (ci.physicalCores ? ", " + std::to_string(*ci.physicalCores) + " cores" : std::string()));
  unsigned curMhz = 0, maxMhz = 0;
  bool freq = queryCpuFrequency(curMhz, maxMhz);
  add("cpu", "Frequency", freq ? "ok" : "unavailable",
      freq ? std::to_string(curMhz) + " MHz current, " + std::to_string(maxMhz) + " MHz max (power manager; turbo not reflected)"
           : "CallNtPowerInformation failed");
  add("cpu", "Temperature", "unavailable", "no documented Windows 7 API; reading on-die sensors needs a kernel driver (not installed by sevenmon)");

  MemoryInfo mi = queryMemory();
  add("memory", "Physical memory", mi.ok ? "ok" : "unavailable", mi.ok ? fmtBytes(mi.totalPhys) + " total" : "GlobalMemoryStatusEx failed");
  add("memory", "Commit charge", mi.commitLimit ? "ok" : "unavailable", mi.commitLimit ? "GetPerformanceInfo" : "GetPerformanceInfo failed");
  add("memory", "Page files", mi.pageFilesKnown ? "ok" : "unavailable",
      mi.pageFilesKnown ? std::to_string(mi.pageFiles.size()) + " page file(s)" : "NtQuerySystemInformation failed");

  ProcessCollector pc(ci.logicalProcessors);
  auto procs = pc.sample();
  size_t denied = 0;
  for (const auto& p : procs) denied += p.accessDenied ? 1 : 0;
  add("processes", "Enumeration", procs.empty() ? "unavailable" : "ok", std::to_string(procs.size()) + " processes");
  add("processes", "Details", denied ? "limited" : "ok",
      denied ? std::to_string(denied) + " process(es) deny access (protected/system processes; elevation helps for some)"
             : "all processes accessible");

  std::vector<std::string> gnotes;
  auto gpus = detectGpus(&gnotes);
  add("gpu", "Adapter detection", gpus.empty() ? "unavailable" : "ok",
      std::to_string(gpus.size()) + " adapter(s)" + (gnotes.empty() ? std::string(" via DXGI 1.1") : ": " + gnotes.front()));
  for (const auto& g : gpus) add("gpu", g.name, "info", g.vendor + ", driver " + g.driverVersion.value_or("unknown"));
  GpuMonitor gm;
  gm.update(gpus);
  for (const auto& b : gm.backendStatus()) add("gpu", b.name, b.available ? "ok" : "unavailable", b.status);
  add("gpu", "Generic GPU counters", "unavailable", "Windows 7 has no WDDM 2.x GPU performance counters; utilization needs NVML/ADL");

  auto disks = queryPhysicalDisks();
  add("disk", "Physical disks", disks.empty() ? "unavailable" : "ok", std::to_string(disks.size()) + " disk(s) opened without write access");
  for (const auto& d : disks) {
    add("disk", "Disk " + std::to_string(d.number) + " throughput", d.raw.ok ? "ok" : "unavailable",
        d.raw.ok ? d.model + " (IOCTL_DISK_PERFORMANCE)" : d.perfNote);
    add("disk", "Disk " + std::to_string(d.number) + " SSD/HDD", d.mediaType == "unknown" ? "limited" : "ok",
        d.mediaType + ": " + d.mediaTypeSource);
  }
  auto vols = queryVolumes();
  add("disk", "Volumes", vols.empty() ? "unavailable" : "ok", std::to_string(vols.size()) + " drive letter(s)");

  NetworkCollector nc;
  auto ifs = nc.sample(true);
  add("network", "GetIfTable2", ifs.empty() ? "unavailable" : "ok", std::to_string(ifs.size()) + " interface(s) incl. virtual");

  BatteryInfo bi = queryBatteryStatus();
  add("battery", "Power status", bi.statusOk ? "ok" : "unavailable",
      !bi.present ? "unknown" : (*bi.present ? "battery present" : "no battery (desktop or battery removed)"));

  SmbiosInfo sb = querySmbios();
  add("firmware", "SMBIOS", sb.ok ? "ok" : "unavailable", sb.ok ? "version " + sb.version : sb.note);

  {
    WmiSession cimv2;
    std::string err;
    bool ok = cimv2.connect(L"ROOT\\CIMV2", &err);
    add("wmi", "ROOT\\CIMV2", ok ? "ok" : "unavailable", ok ? "connected" : err + " (is the Winmgmt service running?)");
    WmiSession wmi;
    ok = wmi.connect(L"ROOT\\WMI", &err);
    add("wmi", "ROOT\\WMI", ok ? "ok" : "unavailable", ok ? "connected" : err);
    if (ok) {
      SmartResult smart = querySmart(wmi);
      add("disk", "SMART (WMI)", smart.queried && !smart.entries.empty() ? "ok" : "unavailable",
          smart.queried ? std::to_string(smart.entries.size()) + " disk(s) report SMART" + (smart.status.empty() ? "" : "; " + smart.status)
                        : smart.status);
      AcpiZones z = queryAcpiThermalZones(wmi);
      add("sensors", "ACPI thermal zones", z.queried && !z.zones.empty() ? "ok" : "unavailable",
          z.queried ? std::to_string(z.zones.size()) + " zone(s); these are NOT CPU core temperatures" : z.status);
      if (bi.present && *bi.present) {
        queryBatteryHealth(wmi, bi);
        add("battery", "Health", bi.healthPct ? "ok" : "unavailable", bi.healthNote);
      }
    }
  }
  add("privacy", "Network / telemetry", "info", "sevenmon makes no network connections and sends no telemetry");

  if (o.format == Format::Text) {
    Out out(STD_OUTPUT_HANDLE, !o.noColor && !o.plain);
    out.colored(Color::Title, "SevenMonitor " SEVENMON_VERSION " doctor\r\n\r\n");
    std::vector<std::string> lines;
    Table t({{"AREA"}, {"CHECK", false, 30}, {"STATUS"}, {"DETAIL"}});
    for (const auto& x : c) t.add({x.area, x.name, x.status, x.detail});
    auto ls = t.lines();
    for (size_t i = 0; i < ls.size(); ++i) {
      Color col = Color::Default;
      if (i == 0) col = Color::Header;
      else {
        const std::string& st = c[i - 1].status;
        col = st == "ok" ? Color::Good : st == "warning" ? Color::Bad : st == "unavailable" || st == "limited" ? Color::Warn : Color::Default;
      }
      out.colored(col, ls[i]);
      out.write("\r\n");
    }
    size_t okCount = 0;
    for (const auto& x : c) okCount += x.status == "ok";
    out.write("\r\n" + std::to_string(okCount) + " of " + std::to_string(c.size()) +
              " checks OK. Unavailable sources are shown as N/A elsewhere; nothing is estimated.\r\n");
  } else {
    std::vector<JValue> rows;
    for (const auto& x : c) {
      JValue r = JValue::object();
      r.set("area", x.area);
      r.set("check", x.name);
      r.set("status", x.status);
      r.set("detail", x.detail);
      rows.push_back(r);
    }
    Out out(STD_OUTPUT_HANDLE, false);
    if (o.format == Format::Json) {
      JValue arr = JValue::array();
      for (auto& r : rows) arr.push(r);
      JValue d = JValue::object();
      d.set("checks", arr);
      out.write(toJson(envelope("doctor", nowIsoUtc(), d), true) + "\r\n");
    } else {
      out.write(csvFromObjects(rows).toString(true));
    }
  }
  return kExitOk;
}

}  // namespace sm
