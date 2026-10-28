// posix_compat.h — the few POSIX calls the tests use, for MinGW builds.
#pragma once
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <cstdlib>
#include <cstring>
#include <string>

inline int setenv(const char* k, const char* v, int) { return _putenv_s(k, v); }
inline int unsetenv(const char* k) { return _putenv_s(k, ""); }
inline int mkdir(const char* p, int) { return _mkdir(p); }
// "/tmp/x-XXXXXX" -> %TEMP%/x-<unique>, created.
inline char* mkdtemp(char* tmpl) {
    static std::string out;
    std::string t = tmpl;
    if (t.rfind("/tmp/", 0) == 0) {
        const char* temp = std::getenv("TEMP");
        t = std::string(temp ? temp : ".") + "/" + t.substr(5);
    }
    std::string buf = t;
    if (_mktemp_s(&buf[0], buf.size() + 1) != 0 || _mkdir(buf.c_str()) != 0) return nullptr;
    out = buf;
    return &out[0];
}
#endif
