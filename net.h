#pragma once

#include <string>
#include <vector>

namespace Net {

std::vector<std::string> getIfaceIp4Addresses(const std::string &ifaceName);

}
