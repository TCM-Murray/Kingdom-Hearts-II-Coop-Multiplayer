#pragma once
// Reliable events (TODO 6.3, the network work): numbered messages, sent again until the other game confirms them,
// duplicates dropped. Pure logic, no sockets and no game reads: avatar_link.cpp carries the numbers (a wrapper
// packet) and the confirmations (in its pose packet, sent every frame); tests/reliable_test.cpp runs it under heavy
// loss. Unordered by default: a message is handed over as soon as it first arrives. A message pushed on a stream
// (1..kStreams-1) names the one before it in that stream, and Orderer holds it until that one was handed over.
#include <cstdint>
#include <cstring>

namespace kh2coop::reliable {

constexpr int kMaxPayload = 1200;
constexpr int kWindow = 128;     // messages in flight at most; a full window means the link is gone
constexpr int kRecvBits = 256;   // the receiver's duplicate window past its base (more than kWindow)
constexpr int kStreams = 4;      // 0 = unordered; 1..3 ordered (avatar_link.hpp names them)

// A receiver's confirmation: every number <= base arrived, and bit i = base + 1 + i arrived too.
struct Ack {
    std::uint32_t epoch = 0, base = 0, bits = 0;
};

inline bool After(std::uint32_t a, std::uint32_t b) { return static_cast<std::int32_t>(a - b) > 0; }

class Sender {
public:
    // epoch: nonzero, new for each run of the game, so the other side knows to start over.
    void Start(std::uint32_t epoch) {
        for (Slot& s : slots_) s.live = false;
        epoch_ = epoch;
        next_ = oldest_ = 1;
        std::memset(last_, 0, sizeof(last_));
    }
    std::uint32_t Epoch() const { return epoch_; }
    std::uint32_t Oldest() const { return oldest_; }  // older numbers are confirmed or given up
    int InFlight() const { return static_cast<int>(next_ - oldest_); }

    // false: empty, too big, no Start yet, no such stream, or the window is full (counted in refused). giveUpMs: this
    // message's own give-up time, for an event that's wrong late (0 = Pump's). stream: handed over in push order
    // with the others on it (0 = as it comes).
    bool Push(const void* data, int len, std::uint32_t giveUpMs = 0, int stream = 0) {
        if (!epoch_ || len <= 0 || len > kMaxPayload || InFlight() >= kWindow || stream < 0 || stream >= kStreams) {
            ++refused;
            return false;
        }
        Slot& s = slots_[next_ % kWindow];
        s.seq = next_++;
        s.len = len;
        s.giveUpMs = giveUpMs;
        s.prev = stream ? last_[stream] : 0;
        if (stream) last_[stream] = s.seq;
        s.live = true;
        s.sent = false;
        std::memcpy(s.data, data, static_cast<std::size_t>(len));
        return true;
    }

    // emit(seq, prev, data, len) for each message never sent, or sent resendMs ago and not confirmed yet. A message still
    // unconfirmed giveUpMs after its first send is dropped (counted in givenUp).
    template <typename Emit>
    void Pump(std::uint32_t nowMs, std::uint32_t resendMs, std::uint32_t giveUpMs, Emit emit) {
        for (std::uint32_t seq = oldest_; seq != next_; ++seq) {
            Slot& s = slots_[seq % kWindow];
            if (!s.live) continue;
            if (s.sent && nowMs - s.firstMs >= (s.giveUpMs ? s.giveUpMs : giveUpMs)) {
                s.live = false;
                ++givenUp;
                continue;
            }
            if (s.sent && nowMs - s.lastMs < resendMs) continue;
            if (s.sent) {
                ++resent;
            } else {
                s.sent = true;
                s.firstMs = nowMs;
                ++sent;
            }
            s.lastMs = nowMs;
            emit(s.seq, s.prev, s.data, s.len);
        }
        Advance();
    }

    void OnAck(const Ack& a) {
        if (!epoch_ || a.epoch != epoch_) return;  // confirms another run of ours
        for (std::uint32_t seq = oldest_; seq != next_; ++seq) {
            Slot& s = slots_[seq % kWindow];
            if (!s.live) continue;
            std::uint32_t d = seq - a.base - 1;
            if (!After(seq, a.base) || (d < 32 && ((a.bits >> d) & 1))) {
                s.live = false;
                ++confirmed;
            }
        }
        Advance();
    }

    std::uint32_t sent = 0, resent = 0, confirmed = 0, givenUp = 0, refused = 0;

private:
    struct Slot {
        std::uint32_t seq = 0, prev = 0, firstMs = 0, lastMs = 0, giveUpMs = 0;  // prev: the one before in its stream
        int len = 0;
        bool live = false, sent = false;
        char data[kMaxPayload];
    };
    void Advance() {
        while (oldest_ != next_ && !slots_[oldest_ % kWindow].live) ++oldest_;
    }
    Slot slots_[kWindow];
    std::uint32_t epoch_ = 0, next_ = 1, oldest_ = 1;
    std::uint32_t last_[kStreams] = {};  // each stream's newest number
};

class Receiver {
public:
    // true: a new message, hand it over; false: a duplicate (or beyond the window, which a sender never does).
    // oldest = the sender's Oldest() when it sent this: everything before it is confirmed or given up.
    bool Accept(std::uint32_t epoch, std::uint32_t seq, std::uint32_t oldest) {
        if (epoch != epoch_) {  // the other game started (again): its numbers start over
            epoch_ = epoch;
            base_ = 0;
            std::memset(w_, 0, sizeof(w_));
        }
        SkipTo(oldest - 1);
        if (!After(seq, base_)) return false;
        std::uint32_t d = seq - base_ - 1;
        if (d >= kRecvBits || Get(d)) return false;
        w_[d / 64] |= 1ull << (d % 64);
        while (Get(0)) Shift();
        return true;
    }
    Ack Current() const { return {epoch_, base_, static_cast<std::uint32_t>(w_[0])}; }
    // seq arrived, or the sender gave it up: it won't come (again) as new.
    bool Has(std::uint32_t seq) const {
        std::uint32_t d = seq - base_ - 1;
        return !After(seq, base_) || (d < static_cast<std::uint32_t>(kRecvBits) && Get(d));
    }

private:
    bool Get(std::uint32_t d) const { return (w_[d / 64] >> (d % 64)) & 1; }
    void Shift() {  // base + 1 arrived (or was given up): the window moves one on
        ++base_;
        for (int i = 0; i < 4; ++i) w_[i] = (w_[i] >> 1) | (i < 3 ? w_[i + 1] << 63 : 0);
    }
    void SkipTo(std::uint32_t newBase) {
        if (!After(newBase, base_)) return;
        if (newBase - base_ > static_cast<std::uint32_t>(kRecvBits)) {
            base_ = newBase;
            std::memset(w_, 0, sizeof(w_));
            return;
        }
        while (After(newBase, base_)) Shift();
        while (Get(0)) Shift();
    }
    std::uint64_t w_[4] = {};  // bit i = base_ + 1 + i arrived
    std::uint32_t epoch_ = 0, base_ = 0;
};

// Each stream's messages in push order: one whose previous message (prev) hasn't been handed over waits here until
// it has, or the sender gave it up, or capMs passed (a give-up the receiver never heard of: no message came after it).
class Orderer {
public:
    // A message Receiver::Accept just took as new. true: hand it over now; false: kept, Next gives it later.
    bool Offer(const Receiver& r, std::uint32_t seq, std::uint32_t prev, const void* data, int len, std::uint32_t nowMs) {
        Sync(r);
        if (Done(r, prev)) return true;
        if (count_ == kWindow || len <= 0 || len > kMaxPayload) {  // can't happen inside the sender's window
            ++overflow;
            return true;
        }
        Held& h = held_[count_++];
        h.seq = seq;
        h.prev = prev;
        h.sinceMs = nowMs;
        h.len = len;
        std::memcpy(h.data, data, static_cast<std::size_t>(len));
        ++kept;
        return false;
    }
    // A kept message whose turn came, copied to out (room for kMaxPayload bytes): its length, 0 = none.
    int Next(const Receiver& r, std::uint32_t nowMs, std::uint32_t capMs, void* out) {
        Sync(r);
        int pick = -1;
        bool late = false;
        for (int i = 0; i < count_ && pick < 0; ++i) {
            if (Done(r, held_[i].prev)) pick = i;
            else if (nowMs - held_[i].sinceMs >= capMs) late = true;
        }
        if (pick < 0 && late) {  // the oldest kept goes first, so every stream stays in order
            pick = 0;
            for (int i = 1; i < count_; ++i)
                if (After(held_[pick].seq, held_[i].seq)) pick = i;
            ++overCap;
        }
        if (pick < 0) return 0;
        int len = held_[pick].len;
        std::memcpy(out, held_[pick].data, static_cast<std::size_t>(len));
        if (pick != --count_) held_[pick] = held_[count_];
        return len;
    }
    int Count() const { return count_; }
    std::uint32_t kept = 0, overCap = 0, overflow = 0;

private:
    struct Held {
        std::uint32_t seq, prev, sinceMs;
        int len;
        char data[kMaxPayload];
    };
    void Sync(const Receiver& r) {  // the other game started again: what we kept was for its old run
        if (r.Current().epoch == epoch_) return;
        epoch_ = r.Current().epoch;
        count_ = 0;
    }
    bool Holds(std::uint32_t seq) const {
        for (int i = 0; i < count_; ++i)
            if (held_[i].seq == seq) return true;
        return false;
    }
    bool Done(const Receiver& r, std::uint32_t prev) const { return !prev || (r.Has(prev) && !Holds(prev)); }
    Held held_[kWindow];
    int count_ = 0;
    std::uint32_t epoch_ = 0;
};

}  // namespace kh2coop::reliable
