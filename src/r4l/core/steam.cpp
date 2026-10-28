#include "steam.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#else
#include <unistd.h>
#include <climits>
#endif

namespace fs = std::filesystem;

namespace r4l {

// ---------------------------------------------------------------- VDF

static bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

VdfNode* VdfNode::find(const std::string& k) {
    for (auto& c : kids)
        if (ieq(c.key, k)) return &c;
    return nullptr;
}
const VdfNode* VdfNode::find(const std::string& k) const {
    for (auto& c : kids)
        if (ieq(c.key, k)) return &c;
    return nullptr;
}
uint32_t VdfNode::u32() const {
    if (raw.size() < 4) return 0;
    return raw[0] | (raw[1] << 8) | (raw[2] << 16) | (static_cast<uint32_t>(raw[3]) << 24);
}
VdfNode VdfNode::string(const std::string& k, const std::string& v) {
    VdfNode n;
    n.type = String;
    n.key = k;
    n.str = v;
    return n;
}
VdfNode VdfNode::int32(const std::string& k, uint32_t v) {
    VdfNode n;
    n.type = Int32;
    n.key = k;
    n.raw = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
    return n;
}
VdfNode VdfNode::map(const std::string& k) {
    VdfNode n;
    n.type = Map;
    n.key = k;
    return n;
}

static int fixed_width(uint8_t t) {
    switch (t) {
    case VdfNode::Int32: case VdfNode::Float: case VdfNode::Ptr: case VdfNode::Color: return 4;
    case VdfNode::UInt64: case VdfNode::Int64: return 8;
    default: return -1;
    }
}

static bool read_cstr(const std::string& b, size_t* p, std::string* out) {
    const size_t z = b.find('\0', *p);
    if (z == std::string::npos) return false;
    *out = b.substr(*p, z - *p);
    *p = z + 1;
    return true;
}

// Reads nodes until 0x08 (consumed). depth guards hostile files.
static bool parse_map(const std::string& b, size_t* p, std::vector<VdfNode>* out, int depth) {
    if (depth > 32) return false;
    while (*p < b.size()) {
        const uint8_t t = static_cast<uint8_t>(b[(*p)++]);
        if (t == 0x08) return true;
        VdfNode n;
        n.type = t;
        if (!read_cstr(b, p, &n.key)) return false;
        if (t == VdfNode::Map) {
            if (!parse_map(b, p, &n.kids, depth + 1)) return false;
        } else if (t == VdfNode::String) {
            if (!read_cstr(b, p, &n.str)) return false;
        } else {
            const int w = fixed_width(t);
            if (w < 0 || *p + w > b.size()) return false;
            n.raw.assign(b.begin() + static_cast<long>(*p), b.begin() + static_cast<long>(*p + w));
            *p += w;
        }
        out->push_back(std::move(n));
    }
    return false;  // ran off the end without 0x08
}

bool vdf_parse(const std::string& bytes, std::vector<VdfNode>* top) {
    top->clear();
    size_t p = 0;
    if (!parse_map(bytes, &p, top, 0)) return false;
    return p == bytes.size();
}

static void write_nodes(const std::vector<VdfNode>& v, std::string* o) {
    for (const auto& n : v) {
        o->push_back(static_cast<char>(n.type));
        o->append(n.key);
        o->push_back('\0');
        if (n.type == VdfNode::Map) {
            write_nodes(n.kids, o);
        } else if (n.type == VdfNode::String) {
            o->append(n.str);
            o->push_back('\0');
        } else {
            o->append(reinterpret_cast<const char*>(n.raw.data()), n.raw.size());
        }
    }
    o->push_back('\x08');
}

std::string vdf_write(const std::vector<VdfNode>& top) {
    std::string o;
    write_nodes(top, &o);
    return o;
}

// ---------------------------------------------------------------- ids

uint32_t steam_crc32(const std::string& s) {
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static std::string quoted(const std::string& s) { return "\"" + s + "\""; }

uint32_t steam_shortcut_appid(const std::string& exe, const std::string& name) {
    return steam_crc32(quoted(exe) + name) | 0x80000000u;
}

// ---------------------------------------------------------------- paths

static fs::path u8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}
static std::string str8(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
static bool is_dir(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(u8(p), ec);
}

std::string steam_root() {
    if (const char* e = std::getenv("R4L_STEAM_ROOT")) return is_dir(e) ? e : "";
    std::vector<std::string> cand;
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS)
        cand.push_back(str8(fs::path(buf)));
#else
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
#ifdef __APPLE__
    if (!h.empty()) cand.push_back(h + "/Library/Application Support/Steam");
#else
    if (!h.empty()) {
        cand.push_back(h + "/.steam/steam");
        cand.push_back(h + "/.local/share/Steam");
        cand.push_back(h + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
    }
#endif
#endif
    for (const auto& c : cand)
        if (is_dir(c + "/userdata")) return c;
    return "";
}

std::vector<std::string> steam_user_config_dirs(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path ud = u8(root) / "userdata";
    for (fs::directory_iterator it(ud, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const std::string id = str8(it->path().filename());
        if (id.empty() || id == "0" || !std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; }))
            continue;
        out.push_back(str8(it->path() / "config"));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool running_under_steam() {
    for (const char* k : {"SteamAppId", "SteamGameId", "SteamClientLaunch"}) {
        const char* v = std::getenv(k);
        if (v && *v) return true;
    }
    return false;
}

std::string current_exe_path() {
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (!n || n >= 32768) return "";
    return str8(fs::path(std::wstring(buf, n)));
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    char real[PATH_MAX];
    return realpath(buf, real) ? real : buf;
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
#endif
}

std::string steam_icon_for(const std::string& exe) {
    const fs::path e = u8(exe);
    std::error_code ec;
    for (const fs::path& c : {e.parent_path() / fs::path(e.stem()).concat(".ico"), e.parent_path() / "icon.ico",
                              e.parent_path() / "icon.png"})
        if (fs::is_regular_file(c, ec)) return str8(c);
#ifdef _WIN32
    return exe;
#else
    return "";
#endif
}

// ---------------------------------------------------------------- shortcuts

static std::string norm_exe(std::string s) {
    while (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
#ifdef _WIN32
    for (auto& c : s) {
        if (c == '/') c = '\\';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
#endif
    return s;
}

static bool read_file(const std::string& p, std::string* out) {
    std::ifstream f(u8(p), std::ios::binary);
    if (!f) return false;
    std::stringstream s;
    s << f.rdbuf();
    *out = s.str();
    return true;
}
static bool write_file(const std::string& p, const std::string& data) {
    const std::string tmp = p + ".tmp";
    {
        std::ofstream f(u8(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(u8(tmp), u8(p), ec);
    if (ec) fs::remove(u8(tmp), ec);
    return !ec;
}

// Loads userdata/<id>/config/shortcuts.vdf; a missing file is an empty list.
static bool load_shortcuts(const std::string& path, std::vector<VdfNode>* top, bool* exists) {
    std::string bytes;
    *exists = read_file(path, &bytes);
    if (!*exists || bytes.empty()) {
        top->clear();
        top->push_back(VdfNode::map("shortcuts"));
        return true;
    }
    if (!vdf_parse(bytes, top)) return false;
    for (const auto& n : *top)
        if (n.type == VdfNode::Map && ieq(n.key, "shortcuts")) return true;
    top->push_back(VdfNode::map("shortcuts"));
    return true;
}

static VdfNode* shortcuts_map(std::vector<VdfNode>& top) {
    for (auto& n : top)
        if (n.type == VdfNode::Map && ieq(n.key, "shortcuts")) return &n;
    return nullptr;
}

static bool has_exe(const VdfNode& list, const std::string& exe) {
    const std::string want = norm_exe(exe);
    for (const auto& e : list.kids) {
        const VdfNode* x = e.find("Exe");
        if (x && x->type == VdfNode::String && norm_exe(x->str) == want) return true;
    }
    return false;
}

bool steam_shortcut_registered(const std::string& root, const std::string& exe) {
    const auto users = steam_user_config_dirs(root);
    if (users.empty()) return false;
    for (const auto& dir : users) {
        std::vector<VdfNode> top;
        bool exists = false;
        if (!load_shortcuts(dir + "/shortcuts.vdf", &top, &exists)) return false;
        if (!has_exe(*shortcuts_map(top), exe)) return false;
    }
    return true;
}

static VdfNode make_entry(const std::string& index, const SteamShortcut& sc) {
    VdfNode e = VdfNode::map(index);
    e.kids.push_back(VdfNode::int32("appid", steam_shortcut_appid(sc.exe, sc.name)));
    e.kids.push_back(VdfNode::string("AppName", sc.name));
    e.kids.push_back(VdfNode::string("Exe", quoted(sc.exe)));
    e.kids.push_back(VdfNode::string("StartDir", quoted(sc.start_dir)));
    e.kids.push_back(VdfNode::string("icon", sc.icon));
    e.kids.push_back(VdfNode::string("ShortcutPath", ""));
    e.kids.push_back(VdfNode::string("LaunchOptions", sc.launch_options));
    e.kids.push_back(VdfNode::int32("IsHidden", 0));
    e.kids.push_back(VdfNode::int32("AllowDesktopConfig", 1));
    e.kids.push_back(VdfNode::int32("AllowOverlay", 1));
    e.kids.push_back(VdfNode::int32("OpenVR", 0));
    e.kids.push_back(VdfNode::int32("Devkit", 0));
    e.kids.push_back(VdfNode::string("DevkitGameID", ""));
    e.kids.push_back(VdfNode::int32("DevkitOverrideAppID", 0));
    e.kids.push_back(VdfNode::int32("LastPlayTime", 0));
    e.kids.push_back(VdfNode::string("FlatpakAppID", ""));
    e.kids.push_back(VdfNode::map("tags"));
    return e;
}

int steam_add_shortcut(const std::string& root, const SteamShortcut& sc, std::string* err) {
    const auto users = steam_user_config_dirs(root);
    if (users.empty()) {
        if (err) *err = "No Steam user found";
        return -1;
    }
    // Read every user's file first: one that cannot be parsed stops the add
    // before anything is written.
    struct Pending { std::string path; std::vector<VdfNode> top; bool exists; };
    std::vector<Pending> todo;
    for (const auto& dir : users) {
        Pending pd{dir + "/shortcuts.vdf", {}, false};
        if (!load_shortcuts(pd.path, &pd.top, &pd.exists)) {
            if (err) *err = "Could not read " + pd.path;
            return -1;
        }
        if (!has_exe(*shortcuts_map(pd.top), sc.exe)) todo.push_back(std::move(pd));
    }
    int written = 0;
    for (auto& pd : todo) {
        VdfNode& list = *shortcuts_map(pd.top);
        int next = 0;
        for (const auto& k : list.kids) next = std::max(next, std::atoi(k.key.c_str()) + 1);
        list.kids.push_back(make_entry(std::to_string(next), sc));
        std::error_code ec;
        fs::create_directories(u8(pd.path).parent_path(), ec);
        if (pd.exists) {
            fs::copy_file(u8(pd.path), u8(pd.path + ".bak"), fs::copy_options::overwrite_existing, ec);
            if (ec) {
                if (err) *err = "Could not back up " + pd.path;
                return -1;
            }
        }
        if (!write_file(pd.path, vdf_write(pd.top))) {
            if (err) *err = "Could not write " + pd.path;
            return -1;
        }
        ++written;
    }
    return written;
}

}  // namespace r4l
