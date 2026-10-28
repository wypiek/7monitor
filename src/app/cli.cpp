#include "app/cli.h"

#include <cerrno>
#include <cstdlib>

#include "util/strings.h"

namespace sm {

namespace {

const char* const kCommands[] = {"summary", "help",    "version",   "cpu",     "memory",   "gpu",
                                 "disk",    "network", "battery",   "processes", "process", "sensors",
                                 "hardware", "watch",  "export",    "doctor"};

bool parseUnsigned(const std::string& s, unsigned long long maxValue, unsigned long long& out) {
  if (s.empty() || s.size() > 20) return false;
  for (char c : s)
    if (c < '0' || c > '9') return false;
  errno = 0;
  char* end = nullptr;
  unsigned long long v = std::strtoull(s.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0' || v > maxValue) return false;
  out = v;
  return true;
}

bool takesValue(const std::string& opt) {
  return opt == "--interval" || opt == "--count" || opt == "--sort" || opt == "--top" || opt == "--output" ||
         opt == "-o" || opt == "--filter" || opt == "--duration";
}

}  // namespace

bool isKnownCommand(const std::string& cmd) {
  for (const char* c : kCommands)
    if (cmd == c) return true;
  return false;
}

ParseResult parseCli(const std::vector<std::string>& args) {
  ParseResult r;
  CliOptions& o = r.opts;
  bool commandSet = false;
  auto fail = [&](const std::string& msg) {
    r.ok = false;
    r.error = msg;
    return r;
  };
  for (size_t i = 0; i < args.size(); ++i) {
    std::string a = args[i];
    if (a.size() > 1 && a[0] == '-' && a != "-") {
      std::string name = a, value;
      bool inlineValue = false;
      size_t eq = a.find('=');
      if (a.compare(0, 2, "--") == 0 && eq != std::string::npos) {
        name = a.substr(0, eq);
        value = a.substr(eq + 1);
        inlineValue = true;
      }
      if (takesValue(name)) {
        if (!inlineValue) {
          if (i + 1 >= args.size()) return fail("option " + name + " requires a value");
          value = args[++i];
        }
        unsigned long long v = 0;
        if (name == "--interval") {
          if (!parseUnsigned(value, kMaxIntervalMs, v) || v < kMinIntervalMs)
            return fail("--interval must be a number of milliseconds between " + std::to_string(kMinIntervalMs) +
                        " and " + std::to_string(kMaxIntervalMs));
          o.intervalMs = static_cast<unsigned>(v);
        } else if (name == "--count") {
          if (!parseUnsigned(value, 100000000ull, v) || v == 0) return fail("--count must be a positive integer");
          o.count = static_cast<unsigned long>(v);
        } else if (name == "--duration") {
          if (!parseUnsigned(value, 31ull * 24 * 3600, v) || v == 0)
            return fail("--duration must be a positive number of seconds (at most 31 days)");
          o.durationSec = static_cast<unsigned long>(v);
        } else if (name == "--top") {
          if (!parseUnsigned(value, 1000000ull, v) || v == 0) return fail("--top must be a positive integer");
          o.top = static_cast<unsigned>(v);
        } else if (name == "--sort") {
          if (!parseProcessSort(value, o.sort)) return fail("--sort must be one of: cpu, memory, pid, name");
          o.sortGiven = true;
        } else if (name == "--output" || name == "-o") {
          if (value.empty()) return fail("--output requires a path");
          o.output = value;
        } else if (name == "--filter") {
          o.filter = value;
        }
        continue;
      }
      if (inlineValue) return fail("option " + name + " does not take a value");
      if (a == "--help" || a == "-h" || a == "-?" || a == "/?") o.help = true;
      else if (a == "--version" || a == "-V") o.version = true;
      else if (a == "--json") {
        if (o.format == Format::Csv) return fail("--json and --csv cannot be combined");
        o.format = Format::Json;
      } else if (a == "--csv") {
        if (o.format == Format::Json) return fail("--json and --csv cannot be combined");
        o.format = Format::Csv;
      } else if (a == "--no-color") o.noColor = true;
      else if (a == "--plain") o.plain = true;
      else if (a == "--verbose" || a == "-v") o.verbose = true;
      else if (a == "--append") o.append = true;
      else if (a == "--all") o.all = true;
      else return fail("unknown option: " + a);
      continue;
    }
    if (a == "/?") {
      o.help = true;
      continue;
    }
    if (!commandSet) {
      std::string cmd = toLower(a);
      if (!isKnownCommand(cmd)) return fail("unknown command: " + a + " (run 'sevenmon help')");
      o.command = cmd;
      commandSet = true;
    } else {
      o.positional.push_back(a);
    }
  }

  // Per-command validation.
  const std::string& c = o.command;
  if (c == "process") {
    if (o.positional.size() != 1) return fail("usage: sevenmon process <PID>");
    unsigned long long pid = 0;
    if (!parseUnsigned(o.positional[0], 0xFFFFFFFFull, pid)) return fail("PID must be a non-negative integer");
    o.pid = static_cast<unsigned>(pid);
  } else if (c == "help") {
    if (o.positional.size() > 1) return fail("usage: sevenmon help [command]");
    if (o.positional.size() == 1 && !isKnownCommand(toLower(o.positional[0])))
      return fail("unknown command: " + o.positional[0]);
  } else if (!o.positional.empty()) {
    return fail("unexpected argument: " + o.positional[0]);
  }
  if ((o.count || o.durationSec) && c != "watch" && c != "export")
    return fail("--count and --duration are only valid with 'watch' and 'export'");
  if (o.count && o.durationSec) return fail("use either --count or --duration, not both");
  if (o.append && o.output.empty()) return fail("--append requires --output");
  if ((o.sortGiven || !o.filter.empty()) && c != "processes" && c != "watch")
    return fail("--sort and --filter are only valid with 'processes' and 'watch'");
  if (o.top && c != "processes") return fail("--top is only valid with 'processes'");
  if (c == "watch" && !o.output.empty() && o.format == Format::Text)
    return fail("'watch --output' needs --json or --csv (use 'export' for files)");
  r.ok = true;
  return r;
}

std::string usageText() {
  return
      "SevenMonitor " SEVENMON_VERSION " - native system monitor for Windows 7 and later\r\n"
      "\r\n"
      "Usage: sevenmon [command] [options]\r\n"
      "\r\n"
      "Commands:\r\n"
      "  summary            System overview (default when no command is given)\r\n"
      "  cpu                Processor details and utilization (overall and per core)\r\n"
      "  memory             Physical memory, commit charge and page files\r\n"
      "  gpu                Graphics adapters and vendor sensors (NVML / ADL)\r\n"
      "  disk               Volumes, physical disks, throughput and SMART status\r\n"
      "  network            Network interfaces, addresses and throughput\r\n"
      "  battery            Battery and AC power status\r\n"
      "  processes          Process list (see --sort, --top, --filter)\r\n"
      "  process <PID>      Details for one process\r\n"
      "  sensors            Temperatures and other sensors from trustworthy sources\r\n"
      "  hardware           Operating system, firmware/SMBIOS and hardware inventory\r\n"
      "  watch              Interactive live dashboard (streams --json/--csv when redirected)\r\n"
      "  export             Record samples to a file (--csv or --json lines)\r\n"
      "  doctor             Report available monitoring capabilities and permissions\r\n"
      "  version            Show version\r\n"
      "  help [command]     Show help\r\n"
      "\r\n"
      "Options:\r\n"
      "  --json             Machine-readable JSON (NDJSON for watch/export)\r\n"
      "  --csv              Machine-readable CSV\r\n"
      "  --no-color         Disable console colors\r\n"
      "  --plain            ASCII-only output and dashboard, no colors\r\n"
      "  --interval <ms>    Sampling interval (default 500 one-shot, 1000 watch/export)\r\n"
      "  --count <n>        Number of samples (watch, export)\r\n"
      "  --duration <s>     Sampling duration in seconds (watch, export)\r\n"
      "  --sort <key>       cpu | memory | pid | name (processes, watch)\r\n"
      "  --top <n>          Show only the first n processes (processes)\r\n"
      "  --filter <text>    Only processes whose name contains text\r\n"
      "  --output <path>    Write output to a file (export: file or directory)\r\n"
      "  --append           Append to the --output file instead of replacing it\r\n"
      "  --all              Include virtual/filter network interfaces\r\n"
      "  --verbose          More detail and diagnostic notes\r\n"
      "  --help, --version\r\n"
      "\r\n"
      "Examples:\r\n"
      "  sevenmon summary --json\r\n"
      "  sevenmon processes --sort memory --top 10\r\n"
      "  sevenmon watch --interval 2000\r\n"
      "  sevenmon export --csv --output stats.csv --interval 1000 --count 60\r\n";
}

std::string commandHelp(const std::string& cmd) {
  if (cmd == "summary") return "sevenmon summary [--json|--csv]\r\n  One-shot overview. Takes two samples --interval ms apart (default 500) so rates are real.\r\n";
  if (cmd == "cpu") return "sevenmon cpu [--json|--csv] [--interval ms]\r\n  Processor model, topology, frequency and utilization measured over --interval.\r\n";
  if (cmd == "memory") return "sevenmon memory [--json|--csv]\r\n  Physical RAM, commit charge (RAM + page files), kernel pools and page files.\r\n";
  if (cmd == "gpu") return "sevenmon gpu [--json|--csv]\r\n  Adapters via DXGI 1.1; live sensors only through NVIDIA NVML or AMD ADL when the\r\n  installed driver provides them. Missing values are shown as N/A.\r\n";
  if (cmd == "disk") return "sevenmon disk [--json|--csv] [--interval ms]\r\n  Volumes, physical disks, throughput, SSD/HDD detection and SMART (admin required).\r\n";
  if (cmd == "network") return "sevenmon network [--json|--csv] [--interval ms] [--all]\r\n  Interfaces, addresses, link speed and throughput. --all includes virtual interfaces.\r\n";
  if (cmd == "battery") return "sevenmon battery [--json|--csv]\r\n  Charge, AC status, Windows' remaining-time estimate and health when the driver reports it.\r\n";
  if (cmd == "processes") return "sevenmon processes [--sort cpu|memory|pid|name] [--top n] [--filter text] [--json|--csv]\r\n  Process list with CPU% measured over --interval (default 500 ms).\r\n";
  if (cmd == "process") return "sevenmon process <PID> [--json|--csv]\r\n  Details for one process, including CPU% measured over --interval.\r\n";
  if (cmd == "sensors") return "sevenmon sensors [--json|--csv]\r\n  GPU (NVML/ADL), disk (SMART) and ACPI thermal-zone readings. CPU core temperature is\r\n  not available on Windows 7 without a kernel driver and is reported as N/A.\r\n";
  if (cmd == "hardware") return "sevenmon hardware [--json|--csv]\r\n  Windows version, computer name, uptime, SMBIOS system/board/BIOS data, CPU, GPU, disks.\r\n";
  if (cmd == "watch") return "sevenmon watch [--interval ms] [--sort key] [--count n] [--plain]\r\n  Interactive dashboard. Keys: Q quit, R refresh, O overview, P processes, C cpu, M memory,\r\n  G gpu, D disk, N network, H help, Up/Down select, Enter details, S sort, / filter, K kill.\r\n  When stdout is redirected, or with --json/--csv, streams one record per interval instead\r\n  (NDJSON or CSV) until --count/--duration is reached or Ctrl+C is pressed.\r\n";
  if (cmd == "export") return "sevenmon export [--csv|--json] [--output file|dir] [--interval ms] [--count n|--duration s] [--append]\r\n  Records fixed-column samples (default: CSV, 1 sample, file sevenmon-YYYYMMDD-HHMMSS.csv in\r\n  the current directory). --json writes JSON lines (.jsonl). A directory --output receives a\r\n  generated file name.\r\n";
  if (cmd == "doctor") return "sevenmon doctor [--json]\r\n  Lists every data source, whether it works on this machine and why not.\r\n";
  if (cmd == "version") return "sevenmon version [--json]\r\n";
  if (cmd == "help") return "sevenmon help [command]\r\n";
  return std::string();
}

}  // namespace sm
