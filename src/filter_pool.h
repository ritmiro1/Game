#pragma once
#include "video_edit.h"
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Persistent workers process disjoint pixel ranges. The playback thread handles
// one range itself, so creating a worker never happens on every frame.
class FilterPool {
    struct Job { uint8_t* pixels = nullptr; size_t count = 0; VideoEdit edit; size_t parts = 1; };
    std::mutex mu;
    std::condition_variable workReady, workDone;
    std::vector<std::thread> workers;
    Job job;
    unsigned long long generation = 0;
    size_t finished = 0;
    bool stopping = false, initialized = false;
    void worker(size_t part) {
        unsigned long long seen = 0;
        for (;;) {
            Job local;
            {
                std::unique_lock<std::mutex> lock(mu);
                workReady.wait(lock, [&] { return stopping || generation != seen; });
                if (stopping) return;
                seen = generation; local = job;
            }
            size_t begin = local.count * part / local.parts;
            size_t end = local.count * (part + 1) / local.parts;
            AdjustColors(local.pixels + begin * 4, end - begin, local.edit);
            {
                std::lock_guard<std::mutex> lock(mu);
                if (++finished == workers.size()) workDone.notify_one();
            }
        }
    }
    void start() {
        if (initialized) return;
        initialized = true;
        unsigned n = std::thread::hardware_concurrency();
        // Limit workers: excessive parallelism competes with video/audio decoding.
        n = std::min(4u, n ? n : 1u);
        workers.reserve(n - 1);
        for (unsigned i=1; i<n; ++i) {
            try { workers.emplace_back(&FilterPool::worker, this, (size_t)i); }
            catch (...) { break; } // safe serial/partial fallback
        }
    }
public:
    FilterPool() = default;
    FilterPool(const FilterPool&) = delete;
    FilterPool& operator=(const FilterPool&) = delete;
    ~FilterPool() {
        { std::lock_guard<std::mutex> lock(mu); stopping = true; }
        workReady.notify_all();
        for (auto& t : workers) if (t.joinable()) t.join();
    }
    void apply(uint8_t* pixels, size_t count, VideoEdit edit) {
        if (!count || !HasColorAdjustments(edit)) return;
        // Thread coordination costs more than it saves on small images.
        if (count < 320u * 240u) { AdjustColors(pixels, count, edit); return; }
        start();
        if (workers.empty()) { AdjustColors(pixels, count, edit); return; }
        Job next{pixels, count, edit, workers.size() + 1};
        {
            std::lock_guard<std::mutex> lock(mu);
            job = next; finished = 0; ++generation;
        }
        workReady.notify_all();
        AdjustColors(pixels, count / next.parts, edit);
        std::unique_lock<std::mutex> lock(mu);
        workDone.wait(lock, [&] { return finished == workers.size(); });
    }
};
