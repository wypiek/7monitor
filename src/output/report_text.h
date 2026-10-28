#pragma once
// Human-readable reports for one-shot commands. Rendering only: all data comes from a
// Snapshot produced by the collectors.
#include <vector>

#include "app/cli.h"
#include "platform/console.h"
#include "sampler/sampler.h"

namespace sm {

void printSummary(Out& out, const Snapshot& s, const CliOptions& o);
void printCpu(Out& out, const Snapshot& s, const CliOptions& o);
void printMemory(Out& out, const Snapshot& s, const CliOptions& o);
void printGpu(Out& out, const Snapshot& s, const CliOptions& o);
void printDisk(Out& out, const Snapshot& s, const CliOptions& o);
void printNetwork(Out& out, const Snapshot& s, const CliOptions& o);
void printBattery(Out& out, const Snapshot& s, const CliOptions& o);
void printProcesses(Out& out, const std::vector<ProcessInfo>& shown, size_t total, const CliOptions& o);
void printProcess(Out& out, const ProcessInfo& p, const CliOptions& o);
void printSensors(Out& out, const Snapshot& s, const CliOptions& o);
void printHardware(Out& out, const Snapshot& s, const CliOptions& o);
// One line per sample for "watch" when stdout is not a console.
std::string sampleLine(const Snapshot& s);

}  // namespace sm
