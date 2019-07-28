#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "net.h"
#include "util.h"

#include <string>
#include <vector>

namespace Net {

//
// iface
//

std::vector<IpInfo> getIfaceIp4Addresses(const std::string &ifaceName) {
  std::vector<IpInfo> addrs;
  struct ifaddrs *ifap;
  int res;
  char host[NI_MAXHOST];
  char netmask[NI_MAXHOST];

  // helpers
  auto countBits = [](auto i) {
    unsigned cnt = 0;
    for (decltype(i) bit = 1; bit; bit <<= 1)
      if (i & bit)
        cnt++;
    return cnt;
  };
  auto netFromHostAndNatmaskV4 = [countBits](const std::string &host, const std::string &netmask) {
    auto hostVec =    Util::splitString(host, ".");
    auto netmaskVec = Util::splitString(netmask, ".");
    unsigned nbits = 0;
    std::ostringstream ss;
    for (int i = 0; i < 4; i++) {
      if (i > 0)
        ss << ".";
      uint8_t mask = std::stoul(netmaskVec[i]);
      ss << (std::stoul(hostVec[i]) & mask);
      nbits += countBits(mask);
    }
    ss << "/" << nbits;
    return ss.str();
  };

  if (::getifaddrs(&ifap) == -1)
    ERR2("network interface", "getifaddrs() failed: " << strerror(errno))
  RunAtEnd destroyAddresses([ifap]() {
    ::freeifaddrs(ifap);
  });

  for (struct ifaddrs *a = ifap; a; a = a->ifa_next)
    if (a->ifa_addr->sa_family == AF_INET && ::strcmp(a->ifa_name, ifaceName.c_str()) == 0) { // IPv4 for the requested interface
      res = ::getnameinfo(a->ifa_addr,
                          sizeof(struct sockaddr_in),
                          host, NI_MAXHOST,
                          nullptr, 0, NI_NUMERICHOST);
      if (res != 0)
        ERR2("get network interface address", "getnameinfo() failed: " << ::gai_strerror(res));

      res = ::getnameinfo(a->ifa_netmask,
                          sizeof(struct sockaddr_in),
                          netmask, NI_MAXHOST,
                          nullptr, 0, NI_NUMERICHOST);
      if (res != 0)
        ERR2("get network interface address", "getnameinfo() failed: " << ::gai_strerror(res));

      addrs.push_back({host, netmask, netFromHostAndNatmaskV4(host, netmask)});
    }

  return addrs;
}

}
