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

void MockNetworkClient::publishStatus(const std::string& state, int sector) {
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    statusCallCount_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(inspectMutex_);
        lastStatusState_ = state;
        lastStatusSector_ = sector;
    }
    util::Logger::Debug("MockNetworkClient", "publishStatus: " + state + " (sector " + std::to_string(sector) + ")");
}

void MockNetworkClient::publishDetection(int sector, const std::string& type, float conf, float cameraMs, float infMs) {
    (void)conf;
    (void)cameraMs;
    (void)infMs;
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    detectionCallCount_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(inspectMutex_);
        lastDetectionType_ = type;
    }
    util::Logger::Debug("MockNetworkClient", "publishDetection: " + type + " (sector " + std::to_string(sector) + ")");
}

void MockNetworkClient::publishHealth(float cpuTemp, int ramMb) {
    (void)cpuTemp;
    (void)ramMb;
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    util::Logger::Debug("MockNetworkClient", "publishHealth");
}

void MockNetworkClient::publishCycleSummary(float duration, float pumpUptime, int sectors, int bird, int dust, int leaf, float avgInf) {
    (void)pumpUptime;
    (void)bird;
    (void)dust;
    (void)leaf;
    (void)avgInf;
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    cycleSummaryCallCount_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(inspectMutex_);
        lastCycleDuration_ = duration;
        lastCycleSectors_ = sectors;
    }
    util::Logger::Debug("MockNetworkClient", "publishCycleSummary: " + std::to_string(duration) + "s");
}

void MockNetworkClient::publishAlert(const std::string& severity, const std::string& code, const std::string& reason) {
    publishCallCount_.fetch_add(1, std::memory_order_relaxed);
    alertCallCount_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(inspectMutex_);
        lastAlertSeverity_ = severity;
        lastAlertCode_ = code;
        lastAlertReason_ = reason;
    }
    util::Logger::Debug("MockNetworkClient", "publishAlert: [" + severity + "] " + code + " - " + reason);
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

int MockNetworkClient::statusCallCount() const {
    return statusCallCount_.load();
}

int MockNetworkClient::detectionCallCount() const {
    return detectionCallCount_.load();
}

int MockNetworkClient::alertCallCount() const {
    return alertCallCount_.load();
}

int MockNetworkClient::cycleSummaryCallCount() const {
    return cycleSummaryCallCount_.load();
}

std::string MockNetworkClient::lastStatusState() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastStatusState_;
}

int MockNetworkClient::lastStatusSector() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastStatusSector_;
}

std::string MockNetworkClient::lastAlertSeverity() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastAlertSeverity_;
}

std::string MockNetworkClient::lastAlertCode() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastAlertCode_;
}

std::string MockNetworkClient::lastAlertReason() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastAlertReason_;
}

std::string MockNetworkClient::lastDetectionType() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastDetectionType_;
}

float MockNetworkClient::lastCycleDuration() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastCycleDuration_;
}

int MockNetworkClient::lastCycleSectors() const {
    std::lock_guard<std::mutex> lock(inspectMutex_);
    return lastCycleSectors_;
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
