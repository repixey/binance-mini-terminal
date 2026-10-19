#include "session.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <vector>

#include "config.h"
#include "net.h"
#include "state.h"
#include "util.h"

void startSession() {
    if (!validSymbol(symbol)) { setStatus("bad ticker"); return; }
    const int gen = ++g_gen;
    cancelHttp();
    candles.clear();
    reachedStart = loadingOlder = initFatal = needTail = false;
    measureDrag = measureShown = false;
    follow = true;
    lastTradeMs = 0;
    olderRetryAt = initRetryAt = tailRetryAt = 0;
    initBackoff = 2.0;
    {
        std::lock_guard<std::mutex> lk(S.m);
        S.live.clear();
        S.hist.clear();
        S.status = "loading...";
    }
    restartWs(gen, false);
    initPending = true;
    fetchKlines(gen, symbol, INTERVALS[ivIdx].api, 0, Kind::Initial, HIST_LIMIT);
}

static void applyLive(const LiveEvent& e, int64_t iv) {
    if (e.gen != g_gen.load() || candles.empty()) return;
    Candle& L = candles.back();
    if (!e.isKline) {
        const int64_t b = e.t / iv * iv;
        if (b == L.t) {
            L.h = std::max(L.h, e.price);
            L.l = std::min(L.l, e.price);
            if ((double)e.t >= lastTradeMs) L.c = e.price;
        } else if (b > L.t) {
            candles.push_back({b, e.price, e.price, e.price, e.price});
        }
        lastTradeMs = std::max(lastTradeMs, (double)e.t);
    } else {
        Candle k{e.t, e.o, e.h, e.l, e.c};
        k.qv = e.qv; k.qb = e.qb; k.n = e.n;
        if (e.t == L.t) {
            if (e.closed) L = k;
            else {
                L.o = k.o;
                L.h = std::max(L.h, k.h);
                L.l = std::min(L.l, k.l);
                L.qv = std::max(L.qv, k.qv);
                L.qb = std::max(L.qb, k.qb);
                L.n = std::max(L.n, k.n);
                if ((double)e.E >= lastTradeMs) L.c = k.c;
            }
        } else if (e.t > L.t) {
            candles.push_back(k);
        } else {
            auto it = std::lower_bound(candles.begin(), candles.end(), e.t,
                                       [](const Candle& c, int64_t t) { return c.t < t; });
            if (it != candles.end() && it->t == e.t) *it = k;
        }
    }
}

static void mergeTail(HistResult& h, int64_t iv) {
    if (h.v.empty()) return;
    pricePrec = std::max(pricePrec, h.prec);
    if (candles.empty() || h.v.front().t > candles.back().t + iv) {
        candles = std::move(h.v);
        reachedStart = (int)candles.size() < h.limit;
        viewRightT = (double)candles.back().t + PAD_BARS * iv;
        follow = true;
        return;
    }
    const int64_t lastV = h.v.back().t;
    std::vector<Candle> tail;
    for (auto it = std::upper_bound(candles.begin(), candles.end(), lastV,
                                    [](int64_t t, const Candle& c) { return t < c.t; });
         it != candles.end(); ++it) tail.push_back(*it);
    auto cut = std::lower_bound(candles.begin(), candles.end(), h.v.front().t,
                                [](const Candle& c, int64_t t) { return c.t < t; });
    candles.erase(cut, candles.end());
    candles.insert(candles.end(), h.v.begin(), h.v.end());
    candles.insert(candles.end(), tail.begin(), tail.end());
}

void drain() {
    const int64_t iv = INTERVALS[ivIdx].ms;
    const double now = glfwGetTime();
    std::vector<HistResult> hs;
    {
        std::lock_guard<std::mutex> lk(S.m);
        hs.swap(S.hist);
    }
    for (auto& h : hs) {
        if (h.gen != g_gen.load()) continue;
        if (!h.ok) {
            if (h.kind == Kind::Initial) {
                initPending = false;
                if (h.fatal) initFatal = true;
                else {
                    initRetryAt = now + std::max<double>(initBackoff, h.retryAfter);
                    initBackoff = std::min(initBackoff * 2, 30.0);
                }
            } else if (h.kind == Kind::Older) {
                loadingOlder = false;
                olderRetryAt = now + std::max(3, h.retryAfter);
            } else {
                needTail = true;
                tailRetryAt = now + std::max(3, h.retryAfter);
            }
            setStatus(h.err);
            continue;
        }
        switch (h.kind) {
        case Kind::Initial:
            initPending = false;
            initBackoff = 2.0;
            if (candles.empty()) {
                candles = std::move(h.v);
                pricePrec = h.prec;
                reachedStart = (int)candles.size() < h.limit;
                if (!candles.empty()) viewRightT = (double)candles.back().t + PAD_BARS * iv;
            } else mergeTail(h, iv);
            setStatus(candles.empty() ? "no data" : "");
            break;
        case Kind::Older: {
            loadingOlder = false;
            reachedStart = (int)h.v.size() < h.limit;
            pricePrec = std::max(pricePrec, h.prec);
            if (candles.empty()) break;
            while (!h.v.empty() && h.v.back().t >= candles.front().t) h.v.pop_back();
            candles.insert(candles.begin(), h.v.begin(), h.v.end());
            break;
        }
        case Kind::Tail:
            needTail = false;
            mergeTail(h, iv);
            setStatus("");
            break;
        }
    }
    if (candles.empty()) return;
    std::vector<LiveEvent> ev;
    {
        std::lock_guard<std::mutex> lk(S.m);
        ev.swap(S.live);
    }
    for (auto& e : ev) applyLive(e, iv);
}

void housekeeping() {
    const double now = glfwGetTime();
    const int gen = g_gen.load();
    if (!validSymbol(symbol)) return;
    if (candles.empty() && !initPending && !initFatal && now >= initRetryAt) {
        initPending = true;
        fetchKlines(gen, symbol, INTERVALS[ivIdx].api, 0, Kind::Initial, HIST_LIMIT);
    }
    if (needTail && !candles.empty() && now >= tailRetryAt) {
        needTail = false;
        fetchKlines(gen, symbol, INTERVALS[ivIdx].api, 0, Kind::Tail, TAIL_LIMIT);
    }
    const int64_t t = nowMs();
    if (t - g_lastMsgMs.load() > STALE_MS && t - g_lastRestartMs > STALE_MS) {
        if (g_msgCount.load() == 0) g_wsPath = (g_wsPath + 1) % 2;
        setStatus("ws: reconnecting...");
        restartWs(gen, true);
    }
}
