// The port's side of the differential checks (tools/diffcheck/run.py): reads
// lines "<function> <arg>..." on stdin, writes one result per line. The same
// inputs go to the original routines in an emulator, and the results are
// compared.

#include "engine/world/world.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace crusader;

namespace {

using Fn = std::function<long(const std::vector<long>&)>;

const std::map<std::string, std::pair<std::size_t, Fn>>& functions() {
    static const std::map<std::string, std::pair<std::size_t, Fn>> table = {
        {"direction_8", {4, [](const auto& a) { return direction_8(a[0], a[1], a[2], a[3]); }}},
        {"direction_16", {4, [](const auto& a) { return direction_16(a[0], a[1], a[2], a[3]); }}},
        {"closest_direction_in_range",
         {7, [](const auto& a) {
              return closest_direction_in_range(a[0], a[1], a[2], a[3], static_cast<int>(a[4]),
                                                static_cast<int>(a[5]), static_cast<int>(a[6]));
          }}},
    };
    return table;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string name;
        in >> name;
        const auto it = functions().find(name);
        if (it == functions().end()) {
            std::cout << "error unknown function " << name << '\n';
            continue;
        }
        std::vector<long> args;
        for (long v; in >> v;) args.push_back(v);
        if (args.size() != it->second.first) {
            std::cout << "error " << name << " takes " << it->second.first << " arguments\n";
            continue;
        }
        std::cout << it->second.second(args) << '\n';
    }
    return 0;
}
