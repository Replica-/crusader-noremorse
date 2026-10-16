#include "core/data_info.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

namespace crusader {

namespace {

constexpr std::uintmax_t kUsUnkcoffSize = 311 * 4;

}  // namespace

// @orig none: port-only data version detection
DataInfo detect_data_info(const std::filesystem::path& data) {
    DataInfo info;
    std::ifstream in(data / "CRUSADER.EXE", std::ios::binary);
    if (!in) {
        // Without the executable, the size of the intrinsic table tells the
        // US usecode (311 entries; 309 in the French one).
        std::error_code error;
        if (std::filesystem::file_size(data / "USECODE/UNKCOFF.DAT", error) == kUsUnkcoffSize) info.language = "US";
        return info;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    constexpr std::string_view kBanner = "No Remorse V";
    for (std::size_t at = bytes.find(kBanner); at != std::string::npos && info.version.empty();
         at = bytes.find(kBanner, at + 1)) {
        std::size_t end = at + kBanner.size();
        const std::size_t begin = end;
        while (end < bytes.size() && (std::isdigit(static_cast<unsigned char>(bytes[end])) || bytes[end] == '.')) ++end;
        if (end > begin) info.version = bytes.substr(begin, end - begin);
    }
    struct Language {
        std::string_view marker, name;
    };
    constexpr Language kLanguages[] = {
        {"Appuyer sur une touche pour continuer.", "FR"},
        {"Press any key to continue.", "US"},
    };
    for (const Language& l : kLanguages) {
        if (bytes.find(l.marker) != std::string::npos) {
            info.language = l.name;
            break;
        }
    }
    return info;
}

}  // namespace crusader
