#include "collectors/network.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <cstdio>
#include <set>

#include "platform/win.h"
#include "util/rates.h"
#include "util/strings.h"

namespace sm {

std::string ifTypeName(unsigned t) {
  switch (t) {
    case 6: return "ethernet";
    case 71: return "wifi";
    case 24: return "loopback";
    case 131: return "tunnel";
    case 23: return "ppp";
    case 243: case 244: return "mobile broadband";
    case 37: return "atm";
    case 144: return "ieee1394";
    default: return "other";
  }
}

void computeNetRates(bool hadPrev, uint64_t prevRx, uint64_t prevTx, double prevT, NetInterface& cur, double now) {
  cur.rxBytesPerSec.reset();
  cur.txBytesPerSec.reset();
  if (!hadPrev) {
    cur.rateNote = "first sample";
    return;
  }
  auto drx = counterDelta64(prevRx, cur.rxBytes);
  auto dtx = counterDelta64(prevTx, cur.txBytes);
  if (!drx || !dtx) {
    cur.rateNote = "counter reset";
    return;
  }
  cur.rxBytesPerSec = perSecond(drx, now - prevT);
  cur.txBytesPerSec = perSecond(dtx, now - prevT);
  cur.rateNote.clear();
}

namespace {

std::string sockaddrText(const SOCKADDR* sa) {
  wchar_t buf[INET6_ADDRSTRLEN + 8] = {};
  if (sa->sa_family == AF_INET) {
    auto* in = reinterpret_cast<const sockaddr_in*>(sa);
    // InetNtopW is available from Vista; it takes a non-const pointer in some SDKs.
    if (InetNtopW(AF_INET, const_cast<IN_ADDR*>(&in->sin_addr), buf, INET6_ADDRSTRLEN)) return toUtf8(std::wstring(buf));
  } else if (sa->sa_family == AF_INET6) {
    auto* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
    if (InetNtopW(AF_INET6, const_cast<IN6_ADDR*>(&in6->sin6_addr), buf, INET6_ADDRSTRLEN)) {
      std::string s = toUtf8(std::wstring(buf));
      if (in6->sin6_scope_id && (in6->sin6_addr.s6_addr[0] == 0xfe && (in6->sin6_addr.s6_addr[1] & 0xc0) == 0x80))
        s += "%" + std::to_string(in6->sin6_scope_id);
      return s;
    }
  }
  return std::string();
}

struct AddrInfo {
  std::vector<std::string> v4, v6;
};

std::map<uint64_t, AddrInfo> queryAddresses() {
  std::map<uint64_t, AddrInfo> out;
  ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG size = 16 * 1024;
  std::vector<unsigned char> buf;
  ULONG r = ERROR_BUFFER_OVERFLOW;
  for (int tries = 0; tries < 4 && r == ERROR_BUFFER_OVERFLOW; ++tries) {
    buf.resize(size);
    r = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  }
  if (r != NO_ERROR) return out;
  for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
    AddrInfo info;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      if (!u->Address.lpSockaddr) continue;
      std::string s = sockaddrText(u->Address.lpSockaddr);
      if (s.empty()) continue;
      if (u->Address.lpSockaddr->sa_family == AF_INET) info.v4.push_back(s);
      else info.v6.push_back(s);
    }
    out[a->Luid.Value] = info;
  }
  return out;
}

std::string operStatusName(int s) {
  switch (s) {
    case 1: return "up";
    case 2: return "down";
    case 3: return "testing";
    case 5: return "dormant";
    case 6: return "not present";
    case 7: return "lower layer down";
    default: return "unknown";
  }
}

}  // namespace

std::vector<NetInterface> NetworkCollector::sample(bool includeAll) {
  std::vector<NetInterface> out;
  MIB_IF_TABLE2* table = nullptr;
  if (GetIfTable2(&table) != NO_ERROR || !table) return out;
  double now = win::monotonicSeconds();
  auto addrs = queryAddresses();
  std::set<uint64_t> present;
  for (ULONG i = 0; i < table->NumEntries; ++i) {
    const MIB_IF_ROW2& r = table->Table[i];
    NetInterface n;
    n.luid = r.InterfaceLuid.Value;
    n.ifIndex = r.InterfaceIndex;
    n.name = sanitize(toUtf8(std::wstring(r.Alias)));
    n.description = sanitize(toUtf8(std::wstring(r.Description)));
    n.type = ifTypeName(r.Type);
    n.hardware = r.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
    bool filter = r.InterfaceAndOperStatusFlags.FilterInterface != FALSE;
    n.connected = r.OperStatus == IfOperStatusUp && r.MediaConnectState != MediaConnectStateDisconnected;
    n.status = r.MediaConnectState == MediaConnectStateDisconnected ? "disconnected" : operStatusName(r.OperStatus);
    if (!includeAll) {
      if (filter || r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
      if (!n.hardware && !n.connected) continue;
    }
    if (r.PhysicalAddressLength > 0 && r.PhysicalAddressLength <= 8) {
      char mac[32] = {};
      size_t pos = 0;
      for (ULONG b = 0; b < r.PhysicalAddressLength; ++b)
        pos += std::snprintf(mac + pos, sizeof(mac) - pos, b ? "-%02X" : "%02X", r.PhysicalAddress[b]);
      n.mac = mac;
    }
    if (r.ReceiveLinkSpeed && r.ReceiveLinkSpeed != ~0ull) n.linkSpeedRxBps = r.ReceiveLinkSpeed;
    if (r.TransmitLinkSpeed && r.TransmitLinkSpeed != ~0ull) n.linkSpeedTxBps = r.TransmitLinkSpeed;
    n.rxBytes = r.InOctets;
    n.txBytes = r.OutOctets;
    auto it = addrs.find(n.luid);
    if (it != addrs.end()) {
      n.ipv4 = it->second.v4;
      n.ipv6 = it->second.v6;
    }
    auto p = prev_.find(n.luid);
    if (p != prev_.end()) computeNetRates(true, p->second.rx, p->second.tx, p->second.t, n, now);
    else computeNetRates(false, 0, 0, 0, n, now);
    prev_[n.luid] = Prev{n.rxBytes, n.txBytes, now};
    present.insert(n.luid);
    out.push_back(n);
  }
  FreeMibTable(table);
  // Forget interfaces that disappeared so a re-added adapter starts fresh.
  for (auto it = prev_.begin(); it != prev_.end();) {
    if (!present.count(it->first)) it = prev_.erase(it);
    else ++it;
  }
  return out;
}

}  // namespace sm
