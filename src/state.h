#pragma once

#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocket.h>

#include "types.h"
#include "worker.h"

inline std::atomic<int> g_gen{0};
inline std::atomic<bool> g_quit{false};
inline std::atomic<bool> g_wake{false};
inline std::atomic<int64_t> g_lastMsgMs{0};
inline std::atomic<int64_t> g_msgCount{0};
inline volatile std::sig_atomic_t g_sig = 0;

inline Worker g_http;
inline Worker g_wsw;
inline std::unique_ptr<ix::WebSocket> g_ws;

inline std::mutex g_argsM;
inline ix::HttpRequestArgsPtr g_curArgs;

inline std::vector<Candle> candles;
inline std::string symbol = "BTCUSDT";
inline int ivIdx = 0;
inline double viewRightT = 0, barPx = 8.0;
inline bool follow = true, reachedStart = false, loadingOlder = false;
inline bool initPending = false, initFatal = false, needTail = false;
inline double lastTradeMs = 0, olderRetryAt = 0, initRetryAt = 0, tailRetryAt = 0, initBackoff = 2.0;
inline int pricePrec = 2;
inline int g_wsPath = 0;
inline int64_t g_lastRestartMs = 0;

inline bool measureDrag = false, measureShown = false;
inline bool showVol = true;
inline double mT1 = 0, mP1 = 0, mT2 = 0, mP2 = 0;
inline char symBuf[32];
inline bool g_scrOpen = true;
