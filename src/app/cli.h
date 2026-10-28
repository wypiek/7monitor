#pragma once
// Command-line parsing and validation. Pure (no I/O) so it is unit-testable.
#include <optional>
#include <string>
#include <vector>

#include "collectors/process.h"

namespace sm {

enum class Format { Text, Json, Csv };

// Process exit codes (documented in docs/commands.md).
enum ExitCode {
  kExitOk = 0,
  kExitError = 1,     // runtime failure
  kExitUsage = 2,     // invalid command line
  kExitNotFound = 3,  // e.g. PID does not exist
  kExitIo = 4,        // output file could not be written
  kExitInterrupted = 130,
};

struct CliOptions {
  std::string command = "summary";
  std::vector<std::string> positional;
  Format format = Format::Text;
  bool noColor = false;
  bool plain = false;
  bool verbose = false;
  bool append = false;
  bool help = false;
  bool version = false;
  bool all = false;  // include virtual/filter network interfaces and idle volumes
  std::optional<unsigned> intervalMs;
  std::optional<unsigned long> count;
  std::optional<unsigned long> durationSec;
  ProcessSort sort = ProcessSort::Cpu;
  bool sortGiven = false;
  std::optional<unsigned> top;
  std::string filter;
  std::string output;  // UTF-8 path
  std::optional<unsigned> pid;  // for "process"
};

struct ParseResult {
  bool ok = false;
  std::string error;
  CliOptions opts;
};

ParseResult parseCli(const std::vector<std::string>& args);

bool isKnownCommand(const std::string& cmd);
std::string usageText();
std::string commandHelp(const std::string& cmd);  // empty if unknown

constexpr unsigned kMinIntervalMs = 100;
constexpr unsigned kMaxIntervalMs = 3600000;
constexpr unsigned kDefaultOneShotIntervalMs = 500;
constexpr unsigned kDefaultWatchIntervalMs = 1000;

}  // namespace sm
