#include "core/game_config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace crusader {

namespace {

std::string_view key_of(std::string_view line) {
    const auto eq = line.find('=');
    return eq == std::string_view::npos ? std::string_view{} : line.substr(0, eq);
}

// atoi, as the original reads numbers.
int number(std::string_view value) { return std::atoi(std::string(value).c_str()); }

}  // namespace

// @orig 1020:0000
GameConfig GameConfig::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return parse(s.str());
}

// @orig 1020:0000
GameConfig GameConfig::parse(std::string_view text) {
    GameConfig c;
    while (!text.empty()) {
        const auto end = text.find_first_of("\r\n");
        const std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        c.lines_.emplace_back(line);
        const std::string_view key = key_of(line);
        const std::string_view value = line.substr(key.size() + (key.empty() ? 0 : 1));
        if (key == "soundvolume") c.sound_volume = number(value);
        else if (key == "musicvolume") c.music_volume = number(value);
        else if (key == "filter") c.filter = value == "true";
        else if (key == "animation") c.animation = number(value);
        else if (key == "frameskip") c.frameskip = number(value);
        else if (key == "limitblasts") c.limit_blasts = value == "true";
        else if (key == "mousespeed") c.mouse_speed = number(value);
        else if (key == "bigvideo") c.big_video = value == "true";
        else if (key == "subtitles") c.subtitles = value == "true";
    }
    return c;
}

// @orig 1020:0C7F
std::string GameConfig::text() const {
    GameConfig out = *this;
    out.set("musicvolume", std::to_string(music_volume));
    out.set("soundvolume", std::to_string(sound_volume));
    out.set("mousespeed", std::to_string(mouse_speed));
    out.set("animation", std::to_string(animation));
    out.set("limitblasts", limit_blasts ? "true" : "false");
    out.set("frameskip", std::to_string(frameskip));
    out.set("filter", filter ? "true" : "false");
    // Written only when set (DS:68CE == 0x80013).
    if (big_video) out.set("bigvideo", "true");
    else out.erase("bigvideo");
    // Written only when set (DS:0179 != 0).
    if (subtitles) out.set("subtitles", "true");
    else out.erase("subtitles");
    std::string s;
    for (const std::string& line : out.lines_) s += line + "\r\n";
    return s;
}

// @orig 1020:0C7F
void GameConfig::save(const std::filesystem::path& path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string s = text();
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// @orig none: the original sets its globals directly
void GameConfig::set(std::string_view key, const std::string& value) {
    const std::string line = std::string(key) + "=" + value;
    for (std::string& l : lines_) {
        if (key_of(l) == key) {
            l = line;
            return;
        }
    }
    lines_.push_back(line);
}

void GameConfig::erase(std::string_view key) {
    std::erase_if(lines_, [key](const std::string& l) { return key_of(l) == key; });
}

}  // namespace crusader
