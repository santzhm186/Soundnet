#pragma once
#include <array>
#include <atomic>
#include <mutex>

namespace tri {
// The UI owns the back slot, the render thread owns the front, and the middle
// slot transfers ownership. Destruction, vector growth and locking stay on the
// producer side. A queued edit may replace an earlier unconsumed edit.
template<class T> class RealtimeAsset {
    std::array<T,3> slots{};
    int front=0, back=2;
    static constexpr int dirty=4;
    std::atomic<int> middle{1};
    std::mutex writers;
public:
    void publish(const T& value) {
        std::lock_guard<std::mutex> lock(writers);
        slots[back]=value;
        back=middle.exchange(back|dirty,std::memory_order_acq_rel)&3;
    }
    bool consume() {
        if (!(middle.load(std::memory_order_acquire)&dirty)) return false;
        front=middle.exchange(front,std::memory_order_acq_rel)&3;
        return true;
    }
    const T& current() const { return slots[front]; }
};
}
