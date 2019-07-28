#include <ifaddrs.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "util.h"

#include <string>
#include <vector>

namespace Net {

//
// iface
//

std::vector<std::string> getIfaceIp4Addresses(const std::string &ifaceName) {
  std::vector<std::string> addrs;
  struct ifaddrs *ifap;
  char host[NI_MAXHOST];

  if (::getifaddrs(&ifap) == -1)
    ERR2("network interface", "getifaddrs() failed: " << strerror(errno))
  RunAtEnd destroyAddresses([ifap]() {
    ::freeifaddrs(ifap);
  });

  while (struct ifaddrs *a = ifap) {
    if (a->ifa_addr->sa_family == AF_INET && ::strcmp(a->ifa_name, ifaceName.c_str()) == 0) { // IPv4 for the requested interface
      int s = ::getnameinfo(a->ifa_addr,
                            sizeof(struct sockaddr_in),
                            host, NI_MAXHOST,
                            nullptr, 0, NI_NUMERICHOST);
      if (s != 0)
        ERR2("get network interface address", "getnameinfo() failed: " << ::gai_strerror(s));

      addrs.push_back(host);
    }
    a = a->ifa_next;
  }

  return addrs;
}

}
