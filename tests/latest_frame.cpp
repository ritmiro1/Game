#include "../src/latest_frame.h"
#include <cassert>
#include <atomic>
#include <memory>
#include <thread>
#include <cstdio>
int main() {
    LatestFrameMailbox<std::unique_ptr<int>> m;
    m.open();
    assert(!m.submit(std::make_unique<int>(1)));
    assert(m.submit(std::make_unique<int>(2)));
    m.redraw(); m.redraw();
    LatestFrameMailbox<std::unique_ptr<int>>::Work work;
    assert(m.wait(work) && work.frame && **work.frame == 2 && work.redraw);
    m.redraw(); assert(m.wait(work) && !work.frame && work.redraw);
    m.close(); assert(!m.wait(work));
    m.open();
    std::atomic<int> last{0};
    std::thread consumer([&] { LatestFrameMailbox<std::unique_ptr<int>>::Work w;
        while (m.wait(w)) if (w.frame) last = **w.frame;
    });
    for(int i=1;i<=10000;i++) m.submit(std::make_unique<int>(i));
    // A forced final hand-off, not a time-dependent sleep.
    while (last.load() != 10000) std::this_thread::yield();
    m.close(); consumer.join();
    puts("latest frame replacement, redraw, shutdown and concurrency: OK");
}
