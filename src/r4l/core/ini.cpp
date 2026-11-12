#include "ini.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace r4l {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

void IniDoc::parse(const std::string& text) {
    lines_.clear();
    std::istringstream in(text);
    std::string raw, section;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        Line l;
        l.raw = raw;
        const std::string t = trim(raw);
        if (!t.empty() && t.front() == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            l.is_header = true;
            l.section = section;
        } else {
            l.section = section;
            if (!t.empty() && t[0] != '#' && t[0] != ';') {
                const size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    l.key = trim(t.substr(0, eq));
                    l.value = trim(t.substr(eq + 1));
                }
            }
        }
        lines_.push_back(l);
    }
}

bool IniDoc::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        lines_.clear();
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
    return true;
}

std::string IniDoc::text() const {
    std::string out;
    for (const Line& l : lines_) {
        if (l.is_header) out += "[" + l.section + "]";
        else if (!l.key.empty()) out += l.key + " = " + l.value;
        else out += l.raw;
        out += "\n";
    }
    return out;
}

bool IniDoc::save(const std::string& path) const {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text();
        f.close();
        if (!f) return false;
    }
    // filesystem::rename replaces an existing destination on Windows too;
    // the C runtime rename leaves updated bindings stranded in .tmp there.
    std::error_code error;
    std::filesystem::rename(std::filesystem::u8path(tmp),
                            std::filesystem::u8path(path), error);
    return !error;
}

bool IniDoc::has(const std::string& section, const std::string& key) const {
    for (const Line& l : lines_)
        if (!l.key.empty() && iequals(l.section, section) && iequals(l.key, key)) return true;
    return false;
}

std::string IniDoc::get(const std::string& section, const std::string& key,
                        const std::string& fallback) const {
    for (const Line& l : lines_)
        if (!l.key.empty() && iequals(l.section, section) && iequals(l.key, key)) return l.value;
    return fallback;
}

int IniDoc::get_int(const std::string& section, const std::string& key, int fallback) const {
    const std::string v = get(section, key);
    if (v.empty()) return fallback;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    return (end && *end == '\0') ? static_cast<int>(n) : fallback;
}

bool IniDoc::has_section(const std::string& section) const {
    for (const Line& l : lines_)
        if (l.is_header && iequals(l.section, section)) return true;
    return false;
}

std::vector<std::string> IniDoc::sections() const {
    std::vector<std::string> out;
    for (const Line& l : lines_)
        if (l.is_header) out.push_back(l.section);
    return out;
}

void IniDoc::set(const std::string& section, const std::string& key, const std::string& value) {
    for (Line& l : lines_)
        if (!l.key.empty() && iequals(l.section, section) && iequals(l.key, key)) {
            l.value = value;
            return;
        }
    // Insert after the last line of the section, or append a new section.
    int last = -1;
    for (size_t i = 0; i < lines_.size(); ++i)
        if (iequals(lines_[i].section, section) &&
            (lines_[i].is_header || !lines_[i].key.empty()))
            last = static_cast<int>(i);
    Line nl;
    nl.section = section;
    nl.key = key;
    nl.value = value;
    if (last < 0) {
        if (!lines_.empty() && !trim(lines_.back().raw).empty()) {
            Line blank;
            blank.section = lines_.back().section;
            lines_.push_back(blank);
        }
        Line h;
        h.is_header = true;
        h.section = section;
        lines_.push_back(h);
        lines_.push_back(nl);
    } else {
        lines_.insert(lines_.begin() + last + 1, nl);
    }
}

}  // namespace r4l
