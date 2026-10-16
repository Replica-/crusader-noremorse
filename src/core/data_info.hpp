#pragma once
// Version and language of the game data, read from CRUSADER.EXE. The version
// is in the banner ("CRUSADER:  No Remorse V1.08"; DS:E8ED4 in the French 1.08
// executable, V1.23 in the US one); the language is told by a message of the
// start-up code that exists in each translation. Without the executable, the
// US data is still told by its usecode.

#include "core/language.hpp"

#include <filesystem>
#include <string>

namespace crusader {

struct DataInfo {
    std::string version;   // "1.08", empty when unknown
    std::string language;  // "FR" or "US", empty when unknown

    bool us() const { return language == "US"; }
    // The language of the texts: English for the US data, else French.
    Language text_language() const { return us() ? Language::English : Language::French; }
};

DataInfo detect_data_info(const std::filesystem::path& data);

}  // namespace crusader
