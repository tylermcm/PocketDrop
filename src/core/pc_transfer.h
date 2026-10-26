#pragma once
#include "nearby.h"
#include <functional>

struct PcDownload {
    enum class Phase { Idle, Connecting, Receiving, Verifying, Done, Failed, Cancelled };
    Phase phase = Phase::Idle;
    std::string name, device, path, error;
    uint64_t received = 0, total = 0;
};

class PcReceiver {
public:
    ~PcReceiver() { stop(); }
    void start(const NearbyPeer& peer, const std::string& inbox);
    void cancel() { cancel_ = true; }
    void stop();
    PcDownload snapshot() const;
    static bool parseInfo(const std::string& body, std::string& name, uint64_t& size);
private:
    void run(NearbyPeer peer, std::string inbox);
    void fail(const std::string& message);
    mutable std::mutex mu_;
    PcDownload state_;
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};
