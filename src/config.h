#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr const char* REST_HOST = "https://fapi.binance.com";
inline constexpr const char* WS_BASE = "wss://fstream.binance.com";
inline constexpr const char* WS_PATHS[] = {"/market", "/public"};

inline constexpr int HIST_LIMIT = 1000;
inline constexpr int TAIL_LIMIT = 500;
inline constexpr double PAD_BARS = 6.0;
inline constexpr int64_t STALE_MS = 20000;
inline constexpr size_t MAX_CANDLES = 500000;
inline constexpr size_t MAX_LIVE_Q = 20000;
inline constexpr int SMA_PERIOD = 288;

inline constexpr int64_t SCR_PERIOD_MS = 3LL * 60 * 1000;
inline constexpr int64_t SCR_FAST_GAP_MS = 200;
inline constexpr int64_t SCR_MIN_GAP_MS = 200;
inline constexpr int64_t SCR_UNIVERSE_MS = 10LL * 60 * 1000;
inline constexpr double SCR_MIN_VOL = 1e6;
inline constexpr size_t SCR_MAX_ITEMS = 800;
