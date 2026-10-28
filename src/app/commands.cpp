#include "app/commands.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <memory>

#include "output/report_json.h"
#include "output/report_text.h"
#include "platform/console.h"
#include "platform/interrupt.h"
#include "platform/win.h"
#include "sampler/sampler.h"
#include "tui/console_tui.h"
#include "util/csv.h"
#include "util/format.h"
#include "util/strings.h"

namespace sm {

namespace {

// Output destination: stdout (console or redirected) or a file from --output.
class Sink {
 public:
  bool open(const CliOptions& o, std::string& error) {
    bool color = !o.noColor && !o.plain && o.format == Format::Text;
    if (o.output.empty()) {
      out_.reset(new Out(STD_OUTPUT_HANDLE, color));
      return true;
    }
    std::wstring path = toWide(o.output);
    DWORD disp = o.append ? OPEN_ALWAYS : CREATE_ALWAYS;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      error = "cannot open " + o.output + ": " + win::errorText(GetLastError());
      return false;
    }
    file_.reset(h);
    LARGE_INTEGER size;
    size.QuadPart = 0;
    GetFileSizeEx(h, &size);
    existingBytes_ = static_cast<uint64_t>(size.QuadPart);
    if (o.append) SetFilePointer(h, 0, nullptr, FILE_END);
    out_.reset(new Out(h));
    return true;
  }
  Out& out() { return *out_; }
  uint64_t existingBytes() const { return existingBytes_; }

 private:
  win::Handle file_;
  std::unique_ptr<Out> out_;
  uint64_t existingBytes_ = 0;
};

Sampler::Options samplerOptionsFor(const std::string& cmd, const CliOptions& o) {
  Sampler::Options so;
  so.includeAllInterfaces = o.all;
  so.processes = cmd == "summary" || cmd == "processes" || cmd == "process" || cmd == "memory" || cmd == "watch" ||
                 cmd == "export";
  so.smart = cmd == "disk" || cmd == "sensors";
  so.acpi = cmd == "sensors";
  so.gpuSensors = cmd == "gpu" || cmd == "sensors" || cmd == "summary" || cmd == "watch" || cmd == "export";
  return so;
}

bool needsSlow(const std::string& cmd) {
  return cmd == "summary" || cmd == "gpu" || cmd == "disk" || cmd == "battery" || cmd == "sensors" ||
         cmd == "hardware" || cmd == "export" || cmd == "watch";
}

// Two fast samples interval ms apart (rates are measured, never guessed), with the slow
// tier collected in between.
Snapshot oneShot(Sampler& sampler, const std::string& cmd, const CliOptions& o) {
  unsigned interval = o.intervalMs.value_or(kDefaultOneShotIntervalMs);
  double start = win::monotonicSeconds();
  sampler.sampleFast();
  if (needsSlow(cmd)) sampler.collectSlow();
  double elapsedMs = (win::monotonicSeconds() - start) * 1000.0;
  if (elapsedMs < interval) sleepInterruptible(static_cast<unsigned>(interval - elapsedMs));
  sampler.sampleFast();
  return sampler.snapshot();
}

void emit(Sink& sink, const CliOptions& o, const std::string& type, const std::string& ts, JValue data,
          const std::vector<JValue>* csvRows) {
  if (o.format == Format::Json) {
    sink.out().write(toJson(envelope(type, ts, std::move(data)), true) + "\r\n");
  } else if (o.format == Format::Csv) {
    std::vector<JValue> single;
    if (!csvRows) {
      single.push_back(data);
      csvRows = &single;
    }
    sink.out().write(csvFromObjects(*csvRows).toString(true));
  }
}

std::vector<JValue> items(const JValue& v, const char* key) {
  const JValue* a = v.get(key);
  return a ? a->items() : std::vector<JValue>();
}

std::vector<ProcessInfo> selectProcesses(const Snapshot& s, const CliOptions& o) {
  std::vector<ProcessInfo> out;
  for (const auto& p : s.processes)
    if (o.filter.empty() || icontains(p.name, o.filter)) out.push_back(p);
  sortProcesses(out, o.sort);
  if (o.top && out.size() > *o.top) out.resize(*o.top);
  return out;
}

std::string timestampForFile() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                st.wSecond);
  return buf;
}

bool isDirectory(const std::string& utf8) {
  DWORD a = GetFileAttributesW(toWide(utf8).c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int runStream(const CliOptions& o, bool exportMode) {
  CliOptions eff = o;
  if (exportMode && eff.format == Format::Text) eff.format = Format::Csv;
  unsigned interval = o.intervalMs.value_or(kDefaultWatchIntervalMs);
  unsigned long count = 0;  // 0 = until interrupted
  if (o.count) count = *o.count;
  else if (o.durationSec) count = static_cast<unsigned long>((*o.durationSec * 1000ull + interval - 1) / interval);
  else if (exportMode) count = 1;

  if (exportMode) {
    std::string ext = eff.format == Format::Json ? ".jsonl" : ".csv";
    std::string name = "sevenmon-" + timestampForFile() + ext;
    if (eff.output.empty()) eff.output = name;
    else if (isDirectory(eff.output) || eff.output.back() == '\\' || eff.output.back() == '/') {
      std::string dir = eff.output;
      if (dir.back() != '\\' && dir.back() != '/') dir += "\\";
      eff.output = dir + name;
    }
  }
  Sink sink;
  std::string err;
  if (!sink.open(eff, err)) {
    Out e(STD_ERROR_HANDLE, false);
    e.line("sevenmon: " + err);
    return kExitIo;
  }
  Out progress(STD_ERROR_HANDLE, false);
  bool showProgress = exportMode && progress.isConsole();

  Sampler sampler(samplerOptionsFor(exportMode ? "export" : "watch", o));
  sampler.collectSlow();
  sampler.startBackground();
  sampler.sampleFast();
  bool headerWritten = eff.format == Format::Csv && eff.append && sink.existingBytes() > 0;
  unsigned long written = 0;
  bool stoppedEarly = false;
  while (count == 0 || written < count) {
    if (sleepInterruptible(interval)) {
      stoppedEarly = true;
      break;
    }
    sampler.sampleFast();
    Snapshot s = sampler.snapshot();
    if (eff.format == Format::Json) {
      JValue rec = jsonSample(s);
      JValue env = JValue::object();
      env.set("schema_version", kSchemaVersion);
      env.set("type", "sample");
      for (const auto& m : rec.members()) env.set(m.first, m.second);
      sink.out().write(toJson(env, false) + "\n");
    } else if (eff.format == Format::Csv) {
      CsvTable t = csvFromObjects({jsonSample(s)});
      sink.out().write(t.toString(!headerWritten));
      headerWritten = true;
    } else {
      sink.out().line(sampleLine(s));
    }
    if (sink.out().failed()) break;  // e.g. reader of the pipe went away
    ++written;
    if (showProgress) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "\rRecorded %lu%s%s samples", written, count ? " of " : "",
                    count ? std::to_string(count).c_str() : "");
      progress.write(buf);
    }
  }
  sampler.stopBackground();
  if (exportMode) {
    Out e(STD_ERROR_HANDLE, false);
    if (showProgress) e.write("\r\n");
    e.line("Wrote " + std::to_string(written) + " sample(s) to " + eff.output +
           (stoppedEarly ? " (interrupted)" : ""));
    if (sink.out().failed()) return kExitIo;
    return stoppedEarly && count ? kExitInterrupted : kExitOk;
  }
  return kExitOk;
}

}  // namespace

int runCommand(const CliOptions& o) {
  const std::string& cmd = o.command;
  Out err(STD_ERROR_HANDLE, !o.noColor && !o.plain);

  if (o.version || cmd == "version") {
    Out out(STD_OUTPUT_HANDLE, false);
    if (o.format == Format::Json) {
      JValue v = JValue::object();
      v.set("tool", "sevenmon");
      v.set("version", SEVENMON_VERSION);
      v.set("schema_version", kSchemaVersion);
#if defined(_WIN64)
      v.set("architecture", "x64");
#else
      v.set("architecture", "x86");
#endif
      out.write(toJson(v, true) + "\r\n");
    } else {
      out.line("sevenmon " SEVENMON_VERSION);
    }
    return kExitOk;
  }
  if (o.help || cmd == "help") {
    Out out(STD_OUTPUT_HANDLE, false);
    std::string topic = cmd == "help" && !o.positional.empty() ? toLower(o.positional[0]) : (o.help && cmd != "help" && cmd != "summary" ? cmd : "");
    if (!topic.empty()) out.write(commandHelp(topic));
    else out.write(usageText());
    return kExitOk;
  }
  if (cmd == "doctor") return runDoctor(o);
  if (cmd == "export") return runStream(o, true);
  if (cmd == "watch") {
    bool console = handleIsConsole(GetStdHandle(STD_OUTPUT_HANDLE));
    if (console && o.format == Format::Text && o.output.empty()) return runDashboard(o);
    return runStream(o, false);
  }

  Sink sink;
  std::string e;
  if (!sink.open(o, e)) {
    err.line("sevenmon: " + e);
    return kExitIo;
  }
  Out& out = sink.out();

  if (cmd == "process") {
    if (!queryProcess(*o.pid)) {
      err.line("sevenmon: no process with PID " + std::to_string(*o.pid));
      return kExitNotFound;
    }
  }

  Sampler sampler(samplerOptionsFor(cmd, o));
  Snapshot s = oneShot(sampler, cmd, o);
  if (interrupted()) return kExitInterrupted;
  const std::string& ts = s.timestamp;

  if (cmd == "summary") {
    if (o.format == Format::Text) printSummary(out, s, o);
    else if (o.format == Format::Json) emit(sink, o, "summary", ts, jsonSummary(s), nullptr);
    else emit(sink, o, "summary", ts, jsonSample(s), nullptr);
  } else if (cmd == "cpu") {
    if (o.format == Format::Text) printCpu(out, s, o);
    else if (o.format == Format::Json) emit(sink, o, "cpu", ts, jsonCpu(s), nullptr);
    else {
      std::vector<JValue> rows;
      JValue total = JValue::object();
      total.set("core", "total");
      total.set("usage_pct", JValue::number(s.cpu.totalPct, 1));
      rows.push_back(total);
      for (size_t i = 0; i < s.cpu.corePct.size(); ++i) {
        JValue r = JValue::object();
        r.set("core", std::to_string(i));
        r.set("usage_pct", JValue::number(s.cpu.corePct[i], 1));
        rows.push_back(r);
      }
      emit(sink, o, "cpu", ts, JValue(), &rows);
    }
  } else if (cmd == "memory") {
    if (o.format == Format::Text) printMemory(out, s, o);
    else emit(sink, o, "memory", ts, jsonMemory(s), nullptr);
  } else if (cmd == "gpu") {
    if (o.format == Format::Text) printGpu(out, s, o);
    else {
      JValue d = jsonGpu(s);
      auto rows = items(d, "adapters");
      emit(sink, o, "gpu", ts, d, &rows);
    }
  } else if (cmd == "disk") {
    if (o.format == Format::Text) printDisk(out, s, o);
    else {
      JValue d = jsonDisk(s);
      auto rows = items(d, "volumes");
      auto pd = items(d, "physical_disks");
      rows.insert(rows.end(), pd.begin(), pd.end());
      emit(sink, o, "disk", ts, d, &rows);
    }
  } else if (cmd == "network") {
    if (o.format == Format::Text) printNetwork(out, s, o);
    else {
      JValue d = jsonNetwork(s);
      auto rows = items(d, "interfaces");
      emit(sink, o, "network", ts, d, &rows);
    }
  } else if (cmd == "battery") {
    if (o.format == Format::Text) printBattery(out, s, o);
    else emit(sink, o, "battery", ts, jsonBattery(s.battery), nullptr);
  } else if (cmd == "processes") {
    auto shown = selectProcesses(s, o);
    if (o.format == Format::Text) printProcesses(out, shown, s.processes.size(), o);
    else {
      JValue d = jsonProcesses(shown, o.sort, s.processes.size());
      auto rows = items(d, "processes");
      emit(sink, o, "processes", ts, d, &rows);
    }
  } else if (cmd == "process") {
    std::optional<ProcessInfo> found;
    for (const auto& p : s.processes)
      if (p.pid == *o.pid) found = p;
    if (!found) found = queryProcess(*o.pid);
    if (!found) {
      err.line("sevenmon: process " + std::to_string(*o.pid) + " exited during sampling");
      return kExitNotFound;
    }
    if (o.format == Format::Text) printProcess(out, *found, o);
    else emit(sink, o, "process", ts, jsonProcess(*found), nullptr);
  } else if (cmd == "sensors") {
    if (o.format == Format::Text) printSensors(out, s, o);
    else {
      JValue d = jsonSensors(s);
      auto rows = items(d, "sensors");
      emit(sink, o, "sensors", ts, d, &rows);
    }
  } else if (cmd == "hardware") {
    if (o.format == Format::Text) printHardware(out, s, o);
    else emit(sink, o, "hardware", ts, jsonHardware(s), nullptr);
  } else {
    err.line("sevenmon: command not implemented: " + cmd);
    return kExitUsage;
  }
  return out.failed() ? kExitIo : kExitOk;
}

}  // namespace sm
