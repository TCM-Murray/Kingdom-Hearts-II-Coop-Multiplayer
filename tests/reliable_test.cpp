// Self-test of src/dll/reliable.hpp, no game or Python needed: run build/Release/reliable_test.exe (exit 0 = PASS).
// A sender and a receiver over a simulated link: loss, duplicates and reordering both ways; the confirmations ride
// on a "pose" sent every 16 ms, as in avatar_link.cpp. The receiver hands messages over through an Orderer, as
// avatar_link.cpp does.
#include <cstdio>
#include <random>
#include <vector>

#include "../src/dll/reliable.hpp"

using namespace kh2coop::reliable;

namespace {

int g_failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Wire {  // one direction: packets due at a time; lost, doubled, delayed at random
    struct Packet {
        std::uint32_t due;
        std::vector<char> bytes;
    };
    std::vector<Packet> inFlight;
    std::mt19937 rng;
    int lossPct, dupPct, minMs, maxMs;
    bool down = false;
    Wire(unsigned seed, int loss, int dup, int lo, int hi) : rng(seed), lossPct(loss), dupPct(dup), minMs(lo), maxMs(hi) {}
    void Send(std::uint32_t now, const void* data, int len) {
        if (down) return;
        int copies = static_cast<int>(rng() % 100) < dupPct ? 2 : 1;
        for (int i = 0; i < copies; ++i) {
            if (static_cast<int>(rng() % 100) < lossPct) continue;
            std::uint32_t delay = minMs + rng() % (maxMs - minMs + 1);
            const char* p = static_cast<const char*>(data);
            inFlight.push_back({now + delay, std::vector<char>(p, p + len)});
        }
    }
    template <typename F>
    void Deliver(std::uint32_t now, F f) {
        for (std::size_t i = 0; i < inFlight.size();) {
            if (static_cast<std::int32_t>(now - inFlight[i].due) >= 0) {
                f(inFlight[i].bytes);
                inFlight[i] = inFlight.back();
                inFlight.pop_back();
            } else {
                ++i;
            }
        }
    }
};

struct Message {  // what the wrapper packet carries
    std::uint32_t epoch, seq, oldest, prev;
    std::uint32_t id;  // the test's payload: a message number
};

Sender g_sender;  // big (the slots): kept out of the stack
Orderer g_orderer;

// Pushes `count` messages over a link with these settings; returns how many times each id arrived. stream: as
// Sender::Push; order: every id in the order it was handed over.
std::vector<int> Run(Sender& s, Receiver& r, Wire& out, Wire& back, int count, std::uint32_t& now,
                     std::uint32_t untilMs, std::uint32_t idBase = 0, int stream = 0,
                     std::vector<std::uint32_t>* order = nullptr) {
    std::vector<int> seen(count, 0);
    int pushed = 0;
    std::mt19937 rng(7);
    auto take = [&](std::uint32_t id) {
        if (id >= idBase && id - idBase < static_cast<std::uint32_t>(count)) ++seen[id - idBase];
        if (order) order->push_back(id);
    };
    for (; now < untilMs; now += 16) {
        // a few messages a frame at random, as game events come
        while (pushed < count && rng() % 3 == 0) {
            std::uint32_t id = idBase + pushed;
            if (!s.Push(&id, sizeof(id), 0, stream)) break;
            ++pushed;
        }
        s.Pump(now, 50, 10000, [&](std::uint32_t seq, std::uint32_t prev, const char* data, int) {
            Message m {s.Epoch(), seq, s.Oldest(), prev, 0};
            std::memcpy(&m.id, data, sizeof(m.id));
            out.Send(now, &m, sizeof(m));
        });
        out.Deliver(now, [&](const std::vector<char>& b) {
            Message m;
            std::memcpy(&m, b.data(), sizeof(m));
            if (r.Accept(m.epoch, m.seq, m.oldest) && g_orderer.Offer(r, m.seq, m.prev, &m.id, sizeof(m.id), now))
                take(m.id);
        });
        char kept[kMaxPayload];
        while (g_orderer.Next(r, now, 10000, kept) == sizeof(std::uint32_t)) {
            std::uint32_t id;
            std::memcpy(&id, kept, sizeof(id));
            take(id);
        }
        Ack a = r.Current();  // the pose every frame
        back.Send(now, &a, sizeof(a));
        back.Deliver(now, [&](const std::vector<char>& b) {
            Ack got;
            std::memcpy(&got, b.data(), sizeof(got));
            s.OnAck(got);
        });
    }
    return seen;
}

bool AllOnce(const std::vector<int>& seen) {
    for (int n : seen)
        if (n != 1) return false;
    return true;
}

bool Rising(const std::vector<std::uint32_t>& order) {
    for (std::size_t i = 1; i < order.size(); ++i)
        if (order[i] <= order[i - 1]) return false;
    return true;
}

// One message straight to the receiver side, as avatar_link.cpp's DrainReceive: true = handed over now.
bool Arrive(Receiver& r, const Message& m, std::uint32_t now) {
    return r.Accept(m.epoch, m.seq, m.oldest) && g_orderer.Offer(r, m.seq, m.prev, &m.id, sizeof(m.id), now);
}
std::vector<std::uint32_t> Drain(Receiver& r, std::uint32_t now, std::uint32_t capMs = 10000) {
    std::vector<std::uint32_t> out;
    char kept[kMaxPayload];
    while (g_orderer.Next(r, now, capMs, kept) == sizeof(std::uint32_t)) {
        std::uint32_t id;
        std::memcpy(&id, kept, sizeof(id));
        out.push_back(id);
    }
    return out;
}

}  // namespace

int main() {
    std::uint32_t now = 0;
    {
        Receiver r;
        g_sender.Start(0x1234);
        Wire out(1, 30, 5, 20, 220), back(2, 30, 5, 20, 220);
        auto seen = Run(g_sender, r, out, back, 3000, now, 200000);
        Check(AllOnce(seen), "30% loss, 5% doubled, 20-220 ms each way: every message arrives exactly once");
        Check(g_sender.InFlight() == 0, "every message confirmed at the end");
        Check(g_sender.givenUp == 0, "nothing given up");
        std::printf("      sent %u, resent %u, confirmed %u, refused (window full) %u\n", g_sender.sent,
                    g_sender.resent, g_sender.confirmed, g_sender.refused);

        // the other game's link goes away for 15 s: its messages are given up, then the stream carries on
        Wire deadOut(3, 0, 0, 20, 40), deadBack(4, 0, 0, 20, 40);
        deadOut.down = deadBack.down = true;
        std::uint32_t before = g_sender.givenUp;
        Run(g_sender, r, deadOut, deadBack, 20, now, now + 15000, 100000);
        Check(g_sender.givenUp > before, "messages unconfirmed for 10 s are given up");
        Wire out2(5, 30, 5, 20, 220), back2(6, 30, 5, 20, 220);
        auto after = Run(g_sender, r, out2, back2, 500, now, now + 60000, 200000);
        Check(AllOnce(after), "after the outage, new messages still arrive exactly once (the receiver skips the given-up ones)");
    }
    {
        // the sender's game restarts: a new epoch, numbers from 1 again
        Receiver r;
        g_sender.Start(0x1111);
        Wire out(7, 10, 0, 10, 50), back(8, 10, 0, 10, 50);
        Run(g_sender, r, out, back, 200, now, now + 20000);
        g_sender.Start(0x2222);
        Wire out2(9, 10, 0, 10, 50), back2(10, 10, 0, 10, 50);
        auto seen = Run(g_sender, r, out2, back2, 200, now, now + 20000, 300000);
        Check(AllOnce(seen), "after the sender restarts (new epoch), its new messages arrive exactly once");
        Ack stale {0x1111, 500, 0};
        int inFlight = g_sender.InFlight();
        std::uint32_t id = 1;
        g_sender.Push(&id, sizeof(id));
        g_sender.OnAck(stale);
        Check(g_sender.InFlight() == inFlight + 1, "a confirmation for the old run confirms nothing");
    }
    {
        // a message with its own give-up time (an RC press: wrong late) goes alone; the next keeps the default
        g_sender.Start(0x3333);
        std::uint32_t id = 7, before = g_sender.givenUp;
        g_sender.Push(&id, sizeof(id), 100);
        g_sender.Push(&id, sizeof(id));
        for (std::uint32_t t = 0; t <= 208; t += 16)
            g_sender.Pump(t, 50, 10000, [](std::uint32_t, std::uint32_t, const char*, int) {});
        Check(g_sender.givenUp == before + 1 && g_sender.InFlight() == 1, "a message's own give-up time drops it alone");
    }
    {
        // an ordered stream under the same loss: each message once, in push order, though they arrive in any order
        Receiver r;
        g_sender.Start(0x5555);
        Wire out(11, 30, 5, 20, 220), back(12, 30, 5, 20, 220);
        std::vector<std::uint32_t> order;
        std::uint32_t keptBefore = g_orderer.kept;
        auto seen = Run(g_sender, r, out, back, 2000, now, now + 150000, 0, 1, &order);
        Check(AllOnce(seen) && Rising(order), "an ordered stream at 30% loss: every message once, in push order");
        Check(g_orderer.Count() == 0 && g_orderer.overCap == 0, "nothing left kept, nothing let go by the cap");
        std::printf("      kept back for order %u of 2000\n", g_orderer.kept - keptBefore);
    }
    {
        // A (give-up 100 ms) lost, B and C after it on the same stream: kept until the sender's give-up of A reaches
        // the receiver; a message on another stream or on none never waits for them
        Receiver r;
        g_sender.Start(0x6666);
        std::uint32_t ids[] = {1, 2, 3, 4, 5};
        g_sender.Push(&ids[0], 4, 100, 1);
        g_sender.Push(&ids[1], 4, 0, 1);
        g_sender.Push(&ids[2], 4, 0, 1);
        std::vector<Message> wire;
        auto pump = [&](std::uint32_t t) {
            wire.clear();
            g_sender.Pump(t, 50, 10000, [&](std::uint32_t seq, std::uint32_t prev, const char* data, int) {
                Message m {g_sender.Epoch(), seq, g_sender.Oldest(), prev, 0};
                std::memcpy(&m.id, data, 4);
                wire.push_back(m);
            });
        };
        pump(0);
        bool c = Arrive(r, wire[2], 10), b = Arrive(r, wire[1], 20);
        Check(!c && !b && Drain(r, 30).empty(), "B and C kept while A is missing");
        g_sender.Push(&ids[3], 4, 0, 2);
        g_sender.Push(&ids[4], 4, 0, 0);
        pump(40);  // only the two new ones: A, B and C aren't due again yet
        Check(wire.size() == 2 && Arrive(r, wire[0], 50) && Arrive(r, wire[1], 50),
              "messages on another stream and on none: at once");
        pump(120);  // A given up; Advance moves oldest after this Pump
        pump(180);
        Check(!Arrive(r, wire[0], 190), "B again is a duplicate");  // but it carries oldest past A
        auto got = Drain(r, 190);
        Check(got.size() == 2 && got[0] == 2 && got[1] == 3, "A given up: B then C handed over");

        // the receiver never hears the missing one was given up (nothing came after it): the cap lets the next go
        g_sender.Start(0x7777);
        g_sender.Push(&ids[0], 4, 0, 1);
        g_sender.Push(&ids[1], 4, 0, 1);
        pump(0);
        std::uint32_t capped = g_orderer.overCap;
        Check(!Arrive(r, wire[1], 1000) && Drain(r, 1000 + 1999, 2000).empty(), "kept under the cap");
        got = Drain(r, 1000 + 2000, 2000);
        Check(got.size() == 1 && got[0] == 2 && g_orderer.overCap == capped + 1, "let go at the cap");

        g_sender.Start(0x8888);
        g_sender.Push(&ids[0], 4, 0, 1);
        g_sender.Push(&ids[1], 4, 0, 1);
        pump(0);
        Arrive(r, wire[1], 0);
        g_sender.Start(0x9999);
        g_sender.Push(&ids[2], 4, 0, 1);
        pump(10);
        Check(Arrive(r, wire[0], 20) && Drain(r, 20000).empty(),
              "a new run of the sender drops what was kept for its old one");
    }
    {
        Receiver r;
        Check(r.Accept(5, 1, 1) && !r.Accept(5, 1, 1), "a duplicate is refused");
        Check(r.Accept(5, 3, 1) && r.Current().base == 1 && r.Current().bits == 2, "out of order: base 1, bit for 3");
        Check(r.Accept(5, 2, 1) && r.Current().base == 3 && r.Current().bits == 0, "the gap filled: base 3");
        Check(!r.Accept(5, 3 + kRecvBits + 1, 1), "far beyond the window: refused");
        Check(r.Accept(5, 50, 50) && r.Current().base == 50, "the sender's oldest moves the base past given-up ones");
    }
    std::printf("%s (%d failed)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
