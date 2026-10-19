#include "screener.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#include <ixwebsocket/IXHttpClient.h>

#include "config.h"
#include "types.h"
#include "util.h"

void Screener::start() {
    th_ = std::thread([this] { loop(); });
}

void Screener::stop() {
    {
        std::lock_guard<std::mutex> lk(m_);
        quit_ = true;
    }
    cancel();
    cv_.notify_all();
    if (th_.joinable()) th_.join();
}

void Screener::add(std::string sym) {
    std::transform(sym.begin(), sym.end(), sym.begin(), [](unsigned char c) { return (char)std::toupper(c); });
    if (!validSymbol(sym)) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        removed_.erase(std::remove(removed_.begin(), removed_.end(), sym), removed_.end());
        for (auto& it : items_) if (it.sym == sym) { it.manual = true; return; }
        if (items_.size() >= SCR_MAX_ITEMS) return;
        ScrItem n;
        n.sym = sym;
        n.manual = true;
        items_.push_back(std::move(n));
    }
    cv_.notify_all();
}

void Screener::remove(const std::string& sym) {
    std::lock_guard<std::mutex> lk(m_);
    items_.erase(std::remove_if(items_.begin(), items_.end(),
                                [&](const ScrItem& it) { return it.sym == sym; }), items_.end());
    if (std::find(removed_.begin(), removed_.end(), sym) == removed_.end()) removed_.push_back(sym);
}

void Screener::setInterval(int idx) {
    if (idx < 0 || idx >= N_INTERVALS) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (idx == ivIdx_) return;
        ivIdx_ = idx;
        ++gen_;
        for (auto& it : items_) {
            it.has = it.shortHist = false;
            it.alerted = false;
            it.err.clear();
            it.fails = 0;
            it.nextDue = 0;
        }
    }
    cancel();
    cv_.notify_all();
}

void Screener::setAlert(double pct) {
    std::lock_guard<std::mutex> lk(m_);
    alertPct_ = std::isfinite(pct) ? pct : 0.0;
    const double s = alertPct_ > 0 ? 1.0 : -1.0, a = std::fabs(alertPct_);
    for (auto& it : items_)
        it.alerted = alertPct_ != 0 && it.has && !it.shortHist && it.dev * s >= a;
}

double Screener::alert() {
    std::lock_guard<std::mutex> lk(m_);
    return alertPct_;
}

int Screener::interval() {
    std::lock_guard<std::mutex> lk(m_);
    return ivIdx_;
}

std::vector<ScrItem> Screener::snapshot() {
    std::lock_guard<std::mutex> lk(m_);
    return items_;
}

void Screener::cancel() {
    std::lock_guard<std::mutex> lk(argsM_);
    if (cur_) cur_->cancel = true;
}

ix::HttpResponsePtr Screener::httpGet(const std::string& url, int transferTimeout) {
    ix::HttpClient http;
    ix::SocketTLSOptions tls;
    tls.caFile = "SYSTEM";
    http.setTLSOptions(tls);
    auto args = http.createRequest();
    args->connectTimeout = 5;
    args->transferTimeout = transferTimeout;
    args->cancel = false;
    { std::lock_guard<std::mutex> lk(argsM_); cur_ = args; }
    auto res = http.get(url, args);
    { std::lock_guard<std::mutex> lk(argsM_); cur_.reset(); }
    return res;
}

void Screener::runUniverse() {
    std::vector<std::pair<std::string, double>> all;
    int retryAfter = 0;
    std::string err;
    try {
        auto res = httpGet(std::string(REST_HOST) + "/fapi/v1/ticker/24hr", 15);
        if (!res || res->statusCode == 0) {
            err = "network error";
        } else if (res->statusCode == 200) {
            json j = json::parse(res->body);
            for (auto& o : j) {
                if (!o.is_object()) continue;
                auto si = o.find("symbol"), qi = o.find("quoteVolume");
                if (si == o.end() || qi == o.end() || !si->is_string()) continue;
                const std::string s = si->get<std::string>();
                if (s.size() <= 4 || s.compare(s.size() - 4, 4, "USDT") != 0 || !validSymbol(s)) continue;
                double v = 0;
                if (qi->is_string()) v = std::strtod(qi->get_ref<const std::string&>().c_str(), nullptr);
                else if (qi->is_number()) v = qi->get<double>();
                if (!std::isfinite(v)) continue;
                all.emplace_back(s, v);
            }
            if (all.empty()) err = "no data";
        } else if (res->statusCode == 429 || res->statusCode == 418) {
            int ra = 30;
            auto it = res->headers.find("Retry-After");
            if (it != res->headers.end()) ra = std::clamp(std::atoi(it->second.c_str()), 5, 600);
            retryAfter = ra;
            err = "rate limited";
        } else {
            err = "http " + std::to_string(res->statusCode);
        }
    } catch (...) {
        all.clear();
        err = "parse error";
    }

    std::lock_guard<std::mutex> lk(m_);
    if (quit_) return;
    const int64_t now = nowMs();
    if (!err.empty() || all.empty()) {
        int64_t back = 15000;
        if (retryAfter > 0) {
            pausedUntil_ = now + (int64_t)retryAfter * 1000;
            back = std::max<int64_t>(back, (int64_t)retryAfter * 1000);
        }
        univDue_ = now + back;
        return;
    }
    univDue_ = now + SCR_UNIVERSE_MS;

    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::unordered_map<std::string, double> vmap;
    vmap.reserve(all.size() * 2);
    for (auto& p : all) vmap[p.first] = p.second;

    for (auto& it : items_) {
        auto f = vmap.find(it.sym);
        if (f != vmap.end()) it.vol = f->second;
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const ScrItem& it) {
        if (it.manual) return false;
        auto f = vmap.find(it.sym);
        return f == vmap.end() || f->second < SCR_MIN_VOL;
    }), items_.end());

    for (auto& p : all) {
        if (p.second < SCR_MIN_VOL || items_.size() >= SCR_MAX_ITEMS) break;
        if (std::find(removed_.begin(), removed_.end(), p.first) != removed_.end()) continue;
        bool exists = false;
        for (auto& it : items_) if (it.sym == p.first) { exists = true; break; }
        if (exists) continue;
        ScrItem n;
        n.sym = p.first;
        n.vol = p.second;
        items_.push_back(std::move(n));
    }
}

void Screener::loop() {
    for (;;) {
        Task task = Task::Item;
        std::string sym;
        int gen = 0, idx = 0;
        {
            std::unique_lock<std::mutex> lk(m_);
            for (;;) {
                if (quit_) return;
                const int64_t now = nowMs();
                int64_t wake = now + 1000;

                if (univDue_ <= now) {
                    const int64_t allowed = std::max(pausedUntil_, lastReq_ + SCR_FAST_GAP_MS);
                    if (now >= allowed) {
                        task = Task::Universe;
                        lastReq_ = now;
                        univDue_ = now + SCR_UNIVERSE_MS;
                        break;
                    }
                    wake = std::min(wake, allowed);
                } else {
                    wake = std::min(wake, univDue_);
                }

                ScrItem* best = nullptr;
                int64_t active = 0;
                for (auto& it : items_) if (!it.dead) ++active;
                for (auto& it : items_) {
                    if (it.dead) continue;
                    if (it.nextDue <= now) { if (!best || it.nextDue < best->nextDue) best = &it; }
                    else wake = std::min(wake, it.nextDue);
                }
                if (best) {
                    const int64_t gap = best->has ? std::max(SCR_MIN_GAP_MS, SCR_PERIOD_MS / std::max<int64_t>(active, 1))
                                                  : SCR_FAST_GAP_MS;
                    const int64_t allowed = std::max(pausedUntil_, lastReq_ + gap);
                    if (now >= allowed) {
                        task = Task::Item;
                        sym = best->sym;
                        gen = gen_;
                        idx = ivIdx_;
                        lastReq_ = now;
                        best->nextDue = now + SCR_PERIOD_MS;
                        break;
                    }
                    wake = std::min(wake, allowed);
                }
                cv_.wait_for(lk, std::chrono::milliseconds(std::max<int64_t>(20, wake - now)));
            }
        }

        if (task == Task::Universe) { runUniverse(); continue; }

        bool ok = false, dead = false, shortH = false;
        int retryAfter = 0, prec = 0;
        double price = 0, dev = 0;
        std::string err;
        try {
            const std::string url = std::string(REST_HOST) + "/fapi/v1/klines?symbol=" + sym +
                                    "&interval=" + INTERVALS[idx].api + "&limit=" + std::to_string(SMA_PERIOD);
            auto res = httpGet(url, 10);
            if (!res || res->statusCode == 0) {
                err = "network error";
            } else if (res->statusCode == 200) {
                json j = json::parse(res->body);
                std::vector<double> cl;
                cl.reserve(j.size());
                for (auto& k : j) {
                    if (!k.is_array() || k.size() < 5) continue;
                    const std::string& cs = k[4].get_ref<const std::string&>();
                    const double c = std::strtod(cs.c_str(), nullptr);
                    if (!std::isfinite(c) || c <= 0) continue;
                    cl.push_back(c);
                    prec = std::max(prec, decimalsOf(cs));
                }
                if (cl.empty()) {
                    err = "no data";
                } else {
                    ok = true;
                    price = cl.back();
                    if ((int)cl.size() < SMA_PERIOD) {
                        shortH = true;
                    } else {
                        double sum = 0;
                        for (size_t k = cl.size() - (size_t)SMA_PERIOD; k < cl.size(); ++k) sum += cl[k];
                        const double sma = sum / (double)SMA_PERIOD;
                        dev = sma != 0 ? (price - sma) / sma * 100.0 : 0.0;
                    }
                }
            } else if (res->statusCode == 429 || res->statusCode == 418) {
                int ra = 30;
                auto it = res->headers.find("Retry-After");
                if (it != res->headers.end()) ra = std::clamp(std::atoi(it->second.c_str()), 5, 600);
                retryAfter = ra;
                err = "rate limited, " + std::to_string(ra) + "s";
            } else if (res->statusCode == 400) {
                if (res->body.find("-1121") != std::string::npos) { err = "unknown symbol"; dead = true; }
                else err = "bad request";
            } else {
                err = "http " + std::to_string(res->statusCode);
            }
        } catch (...) {
            ok = false;
            err = "parse error";
        }

        std::lock_guard<std::mutex> lk(m_);
        if (quit_) return;
        if (gen != gen_) continue;
        ScrItem* it = nullptr;
        for (auto& x : items_) if (x.sym == sym) { it = &x; break; }
        if (!it) continue;
        const int64_t now = nowMs();
        if (ok) {
            const bool first = !it->has;
            it->has = true;
            it->shortHist = shortH;
            it->price = price;
            it->dev = dev;
            it->prec = prec;
            it->updMs = now;
            it->err.clear();
            it->fails = 0;
            it->nextDue = now + SCR_PERIOD_MS;

            if (alertPct_ != 0 && !shortH) {
                const double s = alertPct_ > 0 ? 1.0 : -1.0, a = std::fabs(alertPct_);
                const bool cond = dev * s >= a;
                if (cond && !it->alerted) {
                    it->alerted = true;
                    if (!first) {
                        char t[96], b[200];
                        std::snprintf(t, sizeof t, "%s  SMA %+.2f%%", it->sym.c_str(), dev);
                        std::snprintf(b, sizeof b, "Price %.*f  (%s)  threshold %+.2f%%",
                                      prec, price, INTERVALS[idx].label, alertPct_);
                        notifyDesktop(t, b);
                    }
                } else if (!cond && dev * s < 0.8 * a) {
                    it->alerted = false;
                }
            }
        } else if (dead) {
            it->dead = true;
            it->err = err;
        } else {
            ++it->fails;
            it->err = err;
            int64_t back = std::min<int64_t>(30000LL * it->fails, SCR_PERIOD_MS);
            if (retryAfter > 0) {
                pausedUntil_ = now + (int64_t)retryAfter * 1000;
                back = std::max<int64_t>(back, (int64_t)retryAfter * 1000);
            }
            it->nextDue = now + back;
        }
    }
}
