#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ixwebsocket/IXHttpClient.h>

struct ScrItem {
    std::string sym;
    double price = 0, dev = 0;
    double vol = 0;
    int prec = 0;
    bool has = false, shortHist = false, dead = false, manual = false;
    bool alerted = false;
    std::string err;
    int64_t updMs = 0, nextDue = 0;
    int fails = 0;
};

class Screener {
public:
    void start();
    void stop();
    void add(std::string sym);
    void remove(const std::string& sym);
    void setInterval(int idx);
    void setAlert(double pct);
    double alert();
    int interval();
    std::vector<ScrItem> snapshot();

private:
    enum class Task { Item, Universe };

    void cancel();
    ix::HttpResponsePtr httpGet(const std::string& url, int transferTimeout);
    void runUniverse();
    void loop();

    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<ScrItem> items_;
    std::vector<std::string> removed_;
    bool quit_ = false;
    int ivIdx_ = 1;
    int gen_ = 0;
    int64_t lastReq_ = 0, pausedUntil_ = 0, univDue_ = 0;
    double alertPct_ = 0;
    std::mutex argsM_;
    ix::HttpRequestArgsPtr cur_;
};

inline Screener g_scr;
