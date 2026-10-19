#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class Worker {
public:
    void start() { th_ = std::thread([this] { loop(); }); }

    void post(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(f));
        }
        cv_.notify_one();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(m_);
            quit_ = true;
        }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }

private:
    void loop() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return quit_ || !q_.empty(); });
                if (q_.empty()) return;
                f = std::move(q_.front());
                q_.pop_front();
            }
            try {
                f();
            } catch (...) {
            }
        }
    }

    std::thread th_;
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> q_;
    bool quit_ = false;
};
