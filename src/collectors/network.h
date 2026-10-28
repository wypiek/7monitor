#pragma once
// Network interfaces via the IP Helper API (GetIfTable2 / GetAdaptersAddresses, both
// available since Windows Vista). Throughput is computed from 64-bit octet counters.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sm {

struct NetInterface {
  uint64_t luid = 0;
  unsigned ifIndex = 0;
  std::string name;         // friendly name, e.g. "Local Area Connection"
  std::string description;  // driver description
  std::string type;         // ethernet, wifi, loopback, tunnel, ppp, mobile broadband, other
  std::string status;       // up, down, disconnected, ...
  bool connected = false;
  bool hardware = false;
  std::string mac;
  std::vector<std::string> ipv4, ipv6;
  std::optional<uint64_t> linkSpeedRxBps, linkSpeedTxBps;  // bits per second
  uint64_t rxBytes = 0, txBytes = 0;                        // totals since adapter start
  std::optional<double> rxBytesPerSec, txBytesPerSec;
  std::string rateNote;  // e.g. "counter reset", "first sample"
};

class NetworkCollector {
 public:
  // includeAll=false hides filter/loopback interfaces and non-hardware interfaces that are down.
  std::vector<NetInterface> sample(bool includeAll);

 private:
  struct Prev {
    uint64_t rx, tx;
    double t;
  };
  std::map<uint64_t, Prev> prev_;
};

std::string ifTypeName(unsigned type);

// Pure: computes rates for one interface; updates prev in place. Handles counter
// resets (decrease) and first appearance.
void computeNetRates(bool hadPrev, uint64_t prevRx, uint64_t prevTx, double prevT, NetInterface& cur, double now);

}  // namespace sm
