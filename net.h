#pragma once

#include <string>
#include <vector>

namespace Net {

typedef std::tuple<std::string, std::string, std::string> IpInfo;

std::vector<IpInfo> getIfaceIp4Addresses(const std::string &ifaceName);
std::string getNameserverIp();

}
