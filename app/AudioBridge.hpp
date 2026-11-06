#pragma once
#include "../src/Engine.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

// A bounded multi-producer FIFO. The audio thread does not acquire locks or allocate.
struct TRIMsg {
    uint64_t time = 0;
    uint64_t order = 0;
    uint8_t status = 0, a = 0, b = 0;
};
class TRIMidiQueue {
    static constexpr uint64_t capacity = 2048;
    struct Slot { std::atomic<uint64_t> sequence; TRIMsg event; };
    std::array<Slot,capacity> slots{};
    std::atomic<uint64_t> write{0};
    uint64_t read = 0;
public:
    TRIMidiQueue() {
        for (uint64_t i = 0; i < capacity; ++i) slots[i].sequence.store(i);
    }
    bool push(TRIMsg e) {
        uint64_t index = write.load(std::memory_order_relaxed);
        for (;;) {
            Slot &slot = slots[index % capacity];
            const int64_t delta = int64_t(slot.sequence.load(std::memory_order_acquire) - index);
            if (delta == 0 && write.compare_exchange_weak(index, index + 1, std::memory_order_relaxed)) {
                e.order = index;
                slot.event = e;
                slot.sequence.store(index + 1, std::memory_order_release);
                return true;
            }
            if (delta < 0) return false;
            if (delta > 0) index = write.load(std::memory_order_relaxed);
        }
    }
    bool pop(TRIMsg &e) {
        auto &slot = slots[read % capacity];
        if (slot.sequence.load(std::memory_order_acquire) != read + 1) return false;
        e = slot.event;
        slot.sequence.store(read + capacity, std::memory_order_release);
        ++read;
        return true;
    }
};

// The input/output devices can use independent sample rates and callback sizes.
class TRIInputFIFO {
    static constexpr uint64_t capacity = 32768;
    std::array<float,capacity> left{}, right{};
    std::atomic<uint64_t> write{0}, read{0};
    double cursor = 0, ratio = 1;
    bool primed = false;
public:
    void prepare(double inputRate, double outputRate) {
        read.store(0); write.store(0); cursor = 0; primed = false;
        ratio = inputRate / outputRate;
    }
    void append(const float *l, const float *r, size_t stride, size_t n) {
        uint64_t w = write.load(std::memory_order_relaxed);
        n = std::min<size_t>(n, capacity - (w - read.load(std::memory_order_acquire)));
        for (size_t i = 0; i < n; ++i) {
            left[(w+i) % capacity] = l[i*stride];
            right[(w+i) % capacity] = r[i*stride];
        }
        write.store(w+n, std::memory_order_release);
    }
    void pull(float *l, float *r, size_t n) {
        uint64_t w = write.load(std::memory_order_acquire);
        // Leave a little headroom for independently clocked input and output devices.
        if (w - uint64_t(cursor) > 8192) cursor = double(w-4096);
        if (!primed && double(w) - cursor >= std::max(256.0, n * ratio + 2.0)) primed = true;
        for (size_t i=0; i<n; ++i) {
            auto a = uint64_t(cursor);
            if (primed && a+1 < w && cursor+ratio <= double(w)) {
                auto b = (a+1) % capacity; a %= capacity;
                float frac = float(cursor - std::floor(cursor));
                l[i] = left[a] + (left[b]-left[a])*frac;
                r[i] = right[a] + (right[b]-right[a])*frac;
                cursor += ratio;
            } else {
                l[i] = r[i] = 0;
                primed = false;
            }
        }
        read.store(uint64_t(cursor), std::memory_order_release);
    }
};

struct TRIRealtime {
    tri::Engine dsp;
    TRIMidiQueue midi;
    TRIInputFIFO input;
    std::atomic<bool> panicFlag{false};
    double framesPerTick = 0;
    std::array<TRIMsg, 2048> upcoming{};
    size_t count = 0;

    void enqueue(uint8_t s, uint8_t a, uint8_t b, uint64_t t=0) {
        if (!midi.push({t,0,s,a,b})) panicFlag.store(true);
    }
    void clearEvents() {
        TRIMsg e;
        while (midi.pop(e)) {}
        count = 0;
        dsp.reset();
    }
    static bool later(const TRIMsg &a, const TRIMsg &b) {
        return a.time == b.time ? a.order > b.order : a.time > b.time;
    }
    void render(float *l, float *r, int n, bool enableInput, uint64_t start) {
        if (panicFlag.exchange(false)) clearEvents();
        TRIMsg e;
        while (midi.pop(e)) {
            if (count == upcoming.size()) { clearEvents(); break; }
            upcoming[count++] = e;
            std::push_heap(upcoming.begin(), upcoming.begin()+count, later);
        }
        if (enableInput) input.pull(l,r,n);
        auto span = [&](int offset, int length) {
            dsp.render(enableInput ? l+offset : nullptr, enableInput ? r+offset : nullptr,
                       l+offset, r+offset, length);
        };
        int offset = 0;
        while (count > 0) {
            const TRIMsg next = upcoming[0];
            double frame = next.time > start && start ? (next.time-start)*framesPerTick : 0;
            if (frame >= n) break;
            int position = std::max(offset, int(frame));
            if (offset < position) span(offset, position-offset);
            dsp.midi(next.status,next.a,next.b);
            std::pop_heap(upcoming.begin(),upcoming.begin()+count,later);
            --count;
            offset = position;
        }
        if (offset < n) span(offset,n-offset);
    }
};
