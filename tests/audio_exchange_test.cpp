#include "../src/audio_exchange.h"
#include "../src/vox.h"
#include <cassert>
#include <cstdio>
#include <thread>
#include <atomic>
#include <cstring>

struct Message { uint32_t sequence; uint32_t words[31]; };
static Message make(uint32_t n) {
    Message m{n,{}};
    for (uint32_t k = 0; k < 31; ++k) m.words[k] = (n * 2654435761u) ^ (k * 0xabcdef13u);
    return m;
}
static void check(const Message &m) {
    Message expected = make(m.sequence);
    assert(std::memcmp(&m, &expected, sizeof m) == 0);
}
int main() {
    constexpr uint32_t ITER = 250000;
    audio_exchange::Latest<Message> latest;
    std::atomic<bool> producerDone{false};
    std::thread producer([&] {
        for (uint32_t i = 1; i <= ITER; ++i) latest.publish(make(i));
        producerDone.store(true, std::memory_order_release);
    });
    uint32_t seen = 0, accepted = 0;
    while (seen != ITER) {
        Message m;
        if (!latest.consume(m)) { std::this_thread::yield(); continue; }
        check(m);
        assert(m.sequence > seen);
        seen = m.sequence; ++accepted;
    }
    producer.join();
    assert(producerDone.load(std::memory_order_acquire));
    Message noNew{}; assert(!latest.consume(noNew)); // no stale re-consumption

    audio_exchange::SpscQueue<Message, 7> queue;
    for (uint32_t i = 1; i <= 7; ++i) assert(queue.push(make(i)));
    assert(!queue.push(make(8)));
    for (uint32_t i = 1; i <= 7; ++i) { Message m; assert(queue.pop(m)); check(m); assert(m.sequence == i); }
    Message empty{}; assert(!queue.pop(empty));
    std::thread qp([&] {
        for (uint32_t i = 1; i <= ITER; ++i) {
            Message m = make(i);
            while (!queue.push(m)) std::this_thread::yield();
        }
    });
    for (uint32_t i = 1; i <= ITER; ++i) {
        Message m;
        while (!queue.pop(m)) std::this_thread::yield();
        check(m); assert(m.sequence == i);
    }
    qp.join();

    // Actual Reader integration: continuously replace a pending equal-length
    // line while the other thread consumes and synthesizes. Every adopted
    // line must be a complete one-character population (no torn mixture).
    vox::Reader reader;
    vox::Voice voice;
    std::atomic<bool> textDone{false};
    std::thread tp([&] {
        char line[128]; line[127] = 0;
        for (uint32_t i = 0; i < ITER; ++i) {
            std::memset(line, char('a' + i % 26), 127);
            reader.post(line, 127);
        }
        std::memset(line, 'z', 127); reader.post(line, 127);
        textDone.store(true, std::memory_order_release);
    });
    uint32_t steps = 0;
    while (!textDone.load(std::memory_order_acquire) || reader.txt[0] != 'z') {
        reader.step(voice);
        (void)voice.tick();
        if (reader.len) for (int i = 1; i < reader.len; ++i) assert(reader.txt[i] == reader.txt[0]);
        ++steps;
    }
    tp.join();
    reader.post("same"); reader.post("size"); reader.step(voice);
    assert(std::strcmp(reader.txt, "size") == 0);
    reader.stop(); reader.step(voice); assert(reader.len == 0);
    std::printf("PASS latest: %u publications, %u coherent receipts; queue: %u ordered; Reader: %u concurrent steps\n",ITER,accepted,ITER,steps);
}
