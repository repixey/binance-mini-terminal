#include "net.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <memory>

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXWebSocket.h>

#include "config.h"
#include "state.h"
#include "util.h"

void wakeMain() {
    if (!g_wake.exchange(true)) glfwPostEmptyEvent();
}

void setStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(S.m);
    S.status = s;
}

void cancelHttp() {
    std::lock_guard<std::mutex> lk(g_argsM);
    if (g_curArgs) g_curArgs->cancel = true;
}

void fetchKlines(int gen, const std::string& sym, const std::string& api,
                 int64_t endTime, Kind kind, int limit) {
    g_http.post([=]() {
        if (g_quit || gen != g_gen) return;
        HistResult h;
        h.gen = gen;
        h.kind = kind;
        h.limit = limit;
        try {
            std::string url = std::string(REST_HOST) + "/fapi/v1/klines?symbol=" + sym +
                              "&interval=" + api + "&limit=" + std::to_string(limit);
            if (endTime > 0) url += "&endTime=" + std::to_string(endTime);

            ix::HttpClient http;
            ix::SocketTLSOptions tls;
            tls.caFile = "SYSTEM";
            http.setTLSOptions(tls);
            auto args = http.createRequest();
            args->connectTimeout = 5;
            args->transferTimeout = 10;
            args->cancel = false;
            { std::lock_guard<std::mutex> lk(g_argsM); g_curArgs = args; }
            auto res = http.get(url, args);
            { std::lock_guard<std::mutex> lk(g_argsM); g_curArgs.reset(); }

            if (!res || res->statusCode == 0) {
                h.err = "network error";
            } else if (res->statusCode == 200) {
                json j = json::parse(res->body);
                h.v.reserve(j.size());
                for (auto& k : j) {
                    if (!k.is_array() || k.size() < 5) continue;
                    Candle c{k[0].get<int64_t>(), 0, 0, 0, 0};
                    auto sv = [&](int i) { return std::strtod(k[i].get_ref<const std::string&>().c_str(), nullptr); };
                    c.o = sv(1); c.h = sv(2); c.l = sv(3); c.c = sv(4);
                    if (k.size() >= 11) { c.qv = sv(7); c.n = k[8].get<int64_t>(); c.qb = sv(10); }
                    if (!std::isfinite(c.o + c.h + c.l + c.c) || c.h <= 0 || c.l <= 0) continue;
                    h.v.push_back(c);
                    h.prec = std::max(h.prec, decimalsOf(k[4].get_ref<const std::string&>()));
                    h.prec = std::max(h.prec, decimalsOf(k[1].get_ref<const std::string&>()));
                }
                std::sort(h.v.begin(), h.v.end(), [](const Candle& a, const Candle& b) { return a.t < b.t; });
                h.ok = true;
            } else if (res->statusCode == 429 || res->statusCode == 418) {
                int ra = 30;
                auto it = res->headers.find("Retry-After");
                if (it != res->headers.end()) ra = std::clamp(std::atoi(it->second.c_str()), 5, 600);
                h.retryAfter = ra;
                h.err = "rate limited, " + std::to_string(ra) + "s";
            } else if (res->statusCode == 400) {
                if (res->body.find("-1121") != std::string::npos) { h.err = "unknown symbol"; h.fatal = true; }
                else h.err = "bad request";
            } else {
                h.err = "http " + std::to_string(res->statusCode);
            }
        } catch (...) {
            h.ok = false;
            h.err = "parse error";
        }
        if (g_quit || gen != g_gen) return;
        { std::lock_guard<std::mutex> lk(S.m); S.hist.push_back(std::move(h)); }
        wakeMain();
    });
}

void pushLive(const LiveEvent& ev) {
    std::lock_guard<std::mutex> lk(S.m);
    if (S.live.size() >= MAX_LIVE_Q)
        S.live.erase(S.live.begin(), S.live.begin() + MAX_LIVE_Q / 2);
    S.live.push_back(ev);
}

static void wsStartTask(int gen, std::string url, bool refill, std::string sym, std::string api) {
    if (g_quit || gen != g_gen) return;
    if (g_ws) { g_ws->stop(); g_ws.reset(); }

    auto ws = std::make_unique<ix::WebSocket>();
    ws->setUrl(url);
    ix::SocketTLSOptions tls;
    tls.caFile = "SYSTEM";
    ws->setTLSOptions(tls);
    ws->disablePerMessageDeflate();
    ws->setPingInterval(20);
    ws->setHandshakeTimeout(10);
    ws->setMinWaitBetweenReconnectionRetries(500);
    ws->setMaxWaitBetweenReconnectionRetries(5000);

    auto opens = std::make_shared<std::atomic<int>>(0);
    ws->setOnMessageCallback([gen, refill, opens, sym, api](const ix::WebSocketMessagePtr& msg) {
        try {
            if (gen != g_gen.load()) return;
            switch (msg->type) {
            case ix::WebSocketMessageType::Message: {
                json j = json::parse(msg->str, nullptr, false);
                if (j.is_discarded() || !j.is_object()) return;
                auto dit = j.find("data");
                if (dit == j.end() || !dit->is_object()) return;
                const json& d = *dit;
                const std::string e = d.value("e", "");
                LiveEvent ev;
                ev.gen = gen;
                if (e == "aggTrade") {
                    if (!getNum(d, "p", ev.price) || ev.price <= 0) return;
                    ev.t = d.value("T", (int64_t)0);
                    if (ev.t <= 0) return;
                } else if (e == "kline") {
                    auto kit = d.find("k");
                    if (kit == d.end() || !kit->is_object()) return;
                    const json& k = *kit;
                    ev.isKline = true;
                    ev.t = k.value("t", (int64_t)0);
                    ev.E = d.value("E", (int64_t)0);
                    if (ev.t <= 0 || !getNum(k, "o", ev.o) || !getNum(k, "h", ev.h) ||
                        !getNum(k, "l", ev.l) || !getNum(k, "c", ev.c)) return;
                    getNum(k, "q", ev.qv);
                    getNum(k, "Q", ev.qb);
                    ev.n = k.value("n", (int64_t)0);
                    ev.closed = k.value("x", false);
                } else return;
                g_lastMsgMs = nowMs();
                ++g_msgCount;
                pushLive(ev);
                wakeMain();
                break;
            }
            case ix::WebSocketMessageType::Open: {
                g_lastMsgMs = nowMs();
                { std::lock_guard<std::mutex> lk(S.m); if (S.status.rfind("ws", 0) == 0) S.status.clear(); }
                const int n = opens->fetch_add(1);
                if (refill || n > 0)
                    fetchKlines(gen, sym, api, 0, Kind::Tail, TAIL_LIMIT);
                wakeMain();
                break;
            }
            case ix::WebSocketMessageType::Close:
            case ix::WebSocketMessageType::Error:
                setStatus("ws: reconnecting...");
                wakeMain();
                break;
            default: break;
            }
        } catch (...) {
        }
    });
    ws->start();
    g_ws = std::move(ws);
}

void restartWs(int gen, bool refill) {
    std::string low = symbol;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    const std::string api = INTERVALS[ivIdx].api;
    const std::string url = std::string(WS_BASE) + WS_PATHS[g_wsPath] + "/stream?streams=" +
                            low + "@aggTrade/" + low + "@kline_" + api;
    g_lastRestartMs = nowMs();
    g_lastMsgMs = g_lastRestartMs;
    g_msgCount = 0;
    const std::string sym = symbol;
    g_wsw.post([=]() { wsStartTask(gen, url, refill, sym, api); });
}
