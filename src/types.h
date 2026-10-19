#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct Candle {
    int64_t t;
    double o, h, l, c;
    double qv = 0, qb = 0;
    int64_t n = 0;
};

struct Interval {
    const char* label;
    const char* api;
    int64_t ms;
};

inline constexpr Interval INTERVALS[] = {
    {"1m", "1m", 60000LL},     {"5m", "5m", 300000LL},     {"15m", "15m", 900000LL},
    {"1h", "1h", 3600000LL},   {"12h", "12h", 43200000LL}, {"24h", "1d", 86400000LL},
};
inline constexpr int N_INTERVALS = 6;

enum class Kind { Initial, Older, Tail };

struct LiveEvent {
    int gen = 0;
    bool isKline = false, closed = false;
    int64_t t = 0, E = 0;
    double price = 0, o = 0, h = 0, l = 0, c = 0;
    double qv = 0, qb = 0;
    int64_t n = 0;
};

struct HistResult {
    int gen = 0;
    Kind kind = Kind::Initial;
    bool ok = false, fatal = false;
    int limit = 0, prec = 0, retryAfter = 0;
    std::vector<Candle> v;
    std::string err;
};

struct Shared {
    std::mutex m;
    std::vector<LiveEvent> live;
    std::vector<HistResult> hist;
    std::string status;
};

inline Shared S;
