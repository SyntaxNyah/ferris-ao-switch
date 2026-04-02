#pragma once
#include <atomic>
#include <cstring>

namespace ao {

// Lock-free single-producer / single-consumer ring buffer.
// No heap allocation after construction.
// T must be trivially copyable.
template<typename T, int N>
class SPSCQueue {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Called from the producer thread.
    // Returns false if the queue is full (item dropped).
    bool push(const T& item) {
        const int h = head_.load(std::memory_order_relaxed);
        const int next = (h + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire))
            return false; // full
        buf_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Called from the consumer thread.
    // Returns false if the queue is empty.
    bool pop(T& out) {
        const int t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false; // empty
        out = buf_[t];
        tail_.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

private:
    T buf_[N];
    std::atomic<int> head_;
    std::atomic<int> tail_;
};

// ── Concrete queue types used in the project ────────────────────────────────

// Outgoing: raw AO2 packet strings (max 2048 bytes each)
struct OutPacket {
    char data[2048];
    int  len;
};

// Incoming: raw AO2 packet strings received from the server.
// 65536 bytes to handle large SM packets (500+ music tracks ≈ 30–100 KB).
struct InPacket {
    char data[65536];
    int  len;
};

using OutQueue = SPSCQueue<OutPacket, 64>;
using InQueue  = SPSCQueue<InPacket,  32>; // 32 × 64 KB ≈ 2 MB

} // namespace ao
