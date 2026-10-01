#pragma once
#include <map>
#include <string>
#include <vector>
namespace fake_nvs {
extern std::map<std::string,std::vector<unsigned char>> durable;
extern bool fail_commit;
void clear();
}
