#include "solaredge/Mocks/MockNetworkClient.h"

#include <chrono>
#include <utility>

#include "solaredge/Core/Events.h"
#include "solaredge/Util/Logger.h"

namespace sse::mocks {

namespace {
constexpr auto kConnectLatency = std::chrono::milliseconds(150);
constexpr auto kPollInterval = std::chrono::milliseconds(50);
} // namespace

MockNetworkClient::MockNetworkClient(core::EventQueue& queue, config::MockConfig config)
    : queue_(queue), config_(std::move(config)) {
    running_.store(true);
    worker_ = std::thread(&MockNetworkClient::WorkerLoop, this);
}

MockNetworkClient::~MockNetworkClient() {
    shutdown();
}

void MockNetworkClient::initialize() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initializeRequested_ = true;
    }
    connected_.store(false);
    cv_.notify_one();
}

void MockNetworkClient::publishTelemetry(const std::string& topic, const std::string& jsonPayload) {
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    util::Logger::Debug("MockNetworkClient", "publish " + topic + " -> " + jsonPayload);
}

bool MockNetworkClient::isConnected() const {
    return connected_.load();
}

void MockNetworkClient::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void MockNetworkClient::simulateNetworkLoss() {
    connected_.store(false);
    queue_.push(events::EvNetworkLost{});
}

void MockNetworkClient::simulateNetworkRestore() {
    connected_.store(true);
    queue_.push(events::EvNetworkRestored{});
}

int MockNetworkClient::callCount() const {
    return publishCallCount_.load();
}

void MockNetworkClient::WorkerLoop() {
    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, kPollInterval, [this] { return initializeRequested_ || !running_.load(); });
            if (!running_.load()) {
                return;
            }
            if (!initializeRequested_) {
                continue;
            }
            initializeRequested_ = false;
        }

        if (config_.networkStartOffline) {
            // Stays disconnected until a test/console explicitly restores
            // it -- exercises Boot_Init's offline-boot path.
            continue;
        }
        std::this_thread::sleep_for(kConnectLatency);
        if (!running_.load()) {
            return;
        }
        connected_.store(true);
        queue_.push(events::EvBootNetworkReady{});
    }
}

} // namespace sse::mocks
