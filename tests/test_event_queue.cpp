#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>
#include <variant>
#include <vector>

#include "solaredge/Core/EventQueue.h"

using sse::core::EventQueue;
using sse::core::EventVariant;
using sse::events::EvCleanComplete;

TEST_CASE("EventQueue::waitPop times out on an empty queue", "[event_queue]") {
    EventQueue queue;
    EventVariant event;
    CHECK_FALSE(queue.waitPop(event, std::chrono::milliseconds(20)));
}

TEST_CASE("EventQueue preserves FIFO order for a single producer", "[event_queue]") {
    EventQueue queue;
    for (int i = 0; i < 100; ++i) {
        queue.push(EvCleanComplete{{}, i});
    }
    for (int i = 0; i < 100; ++i) {
        EventVariant event;
        REQUIRE(queue.tryPop(event));
        const auto* clean = std::get_if<EvCleanComplete>(&event);
        REQUIRE(clean != nullptr);
        CHECK(clean->sectorId == i);
    }
}

TEST_CASE("EventQueue delivers every event from concurrent producers exactly once", "[event_queue]") {
    constexpr int kProducers = 4;
    constexpr int kEventsPerProducer = 1000;
    constexpr int kTotal = kProducers * kEventsPerProducer;

    EventQueue queue;
    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&queue, p] {
            for (int i = 0; i < kEventsPerProducer; ++i) {
                queue.push(EvCleanComplete{{}, p * kEventsPerProducer + i});
            }
        });
    }

    std::vector<bool> seen(kTotal, false);
    std::vector<int> lastSeenPerProducer(kProducers, -1);
    int received = 0;
    while (received < kTotal) {
        EventVariant event;
        if (!queue.waitPop(event, std::chrono::milliseconds(1000))) {
            FAIL("timed out waiting for events -- producers may have deadlocked");
        }
        const auto* clean = std::get_if<EvCleanComplete>(&event);
        REQUIRE(clean != nullptr);

        const int encoded = clean->sectorId;
        const int producer = encoded / kEventsPerProducer;
        const int index = encoded % kEventsPerProducer;

        REQUIRE_FALSE(seen[encoded]);
        seen[encoded] = true;
        CHECK(index == lastSeenPerProducer[producer] + 1);
        lastSeenPerProducer[producer] = index;

        ++received;
    }

    for (auto& t : producers) {
        t.join();
    }
    CHECK(received == kTotal);
}
