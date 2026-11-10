#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

// One-slot mailbox: the newest frame replaces a frame not yet rendered.
// No unbounded queues or blocking on the renderer in submit().
template<class T> class LatestFrameMailbox {
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<T> frame_;
    bool redraw_ = false, closed_ = true;
public:
    struct Work { std::optional<T> frame; bool redraw = false; };
    void open() {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_.reset(); redraw_ = false; closed_ = false;
    }
    bool submit(T value) {
        bool replaced;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return false;
            replaced = frame_.has_value();
            frame_ = std::move(value);
        }
        ready_.notify_one();
        return replaced;
    }
    void redraw() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            redraw_ = true;
        }
        ready_.notify_one();
    }
    bool wait(Work& work) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return closed_ || frame_ || redraw_; });
        if (closed_) return false;
        work.frame = std::move(frame_); frame_.reset();
        work.redraw = redraw_; redraw_ = false;
        return true;
    }
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true; frame_.reset(); redraw_ = false;
        }
        ready_.notify_all();
    }
};
