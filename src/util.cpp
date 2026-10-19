#include "util.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#ifndef _WIN32
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool getNum(const json& j, const char* key, double& out) {
    auto it = j.find(key);
    if (it == j.end()) return false;
    if (it->is_string()) out = std::strtod(it->get_ref<const std::string&>().c_str(), nullptr);
    else if (it->is_number()) out = it->get<double>();
    else return false;
    return std::isfinite(out);
}

int decimalsOf(const std::string& s) {
    size_t p = s.find('.');
    if (p == std::string::npos) return 0;
    size_t e = s.size();
    while (e > p + 1 && s[e - 1] == '0') --e;
    return (int)(e - p - 1);
}

bool validSymbol(const std::string& s) {
    if (s.empty() || s.size() > 24) return false;
    for (unsigned char c : s) if (!(std::isalnum(c) || c == '_')) return false;
    return true;
}

double niceStep(double raw) {
    if (!(raw > 0) || !std::isfinite(raw)) return 1;
    const double m = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / m;
    return (r <= 1 ? 1 : r <= 2 ? 2 : r <= 5 ? 5 : 10) * m;
}

void fmtTime(int64_t ms, const char* f, char* out, size_t n) {
    time_t s = (time_t)(ms / 1000);
    tm t{};
#ifdef _WIN32
    localtime_s(&t, &s);
#else
    localtime_r(&s, &t);
#endif
    if (strftime(out, n, f, &t) == 0) out[0] = 0;
}

void fmtDuration(int64_t ms, char* out, size_t n) {
    int64_t m = ms / 60000;
    const int64_t d = m / 1440; m %= 1440;
    const int64_t h = m / 60;   m %= 60;
    size_t len = 0;
    out[0] = 0;
    auto add = [&](int64_t v, char u) {
        if (v > 0 && len < n)
            len += (size_t)std::snprintf(out + len, n - len, "%s%lld%c", len ? " " : "", (long long)v, u);
    };
    add(d, 'd'); add(h, 'h'); add(m, 'm');
    if (len == 0) std::snprintf(out, n, "0m");
}

void fmtCompact(double v, char* out, size_t n) {
    if (v >= 1e9)      std::snprintf(out, n, "%.2fB", v / 1e9);
    else if (v >= 1e6) std::snprintf(out, n, "%.2fM", v / 1e6);
    else if (v >= 1e3) std::snprintf(out, n, "%.1fK", v / 1e3);
    else               std::snprintf(out, n, "%.0f", v);
}

void notifyDesktop(const std::string& title, const std::string& body) {
#ifndef _WIN32
    std::vector<std::string> a = {"notify-send", "-a", "mini-terminal", "-u", "normal", "-t", "10000", title, body};
    std::vector<char*> argv;
    for (auto& s : a) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    posix_spawnp(&pid, "notify-send", nullptr, nullptr, argv.data(), environ);
#else
    (void)title;
    (void)body;
#endif
}
