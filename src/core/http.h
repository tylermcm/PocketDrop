#pragma once
#include "bundle.h"
#include "platform.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct TransferInfo {
    uint64_t id = 0;
    std::string name, device;
    uint64_t total = 0, sent = 0;
    bool done = false, ok = false, viaTunnel = false;
    uint64_t start = 0, end = 0; // plat::tick_ms()
};

struct Req;

class HttpServer {
public:
    enum class Event { Visit, TransferStart, TransferEnd };
    std::function<void(Event)> onEvent; // invoked on worker threads

    ~HttpServer() { stop(); }
    bool start(int preferredPort, bool loopbackOnly = false);
    void stop();
    int port() const { return port_; }

    // Swap what is being shared. An empty token disables sharing (all URLs 404).
    void publish(std::shared_ptr<Bundle> bundle, const std::string& token);

    std::vector<TransferInfo> transfers() const;
    std::string lastDevice() const;

    struct Transfer;

private:
    void acceptLoop();
    void serve(plat::Sock s);
    bool handle(plat::Sock s, const Req& r, bool viaTunnel);
    std::shared_ptr<Transfer> begin(const std::string& name, const std::string& device, uint64_t total, bool tunnel);
    void end(const std::shared_ptr<Transfer>& t, bool ok);

    plat::Sock listen_ = plat::BAD_SOCK;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<int> threads_{0};
    std::thread acceptThread_;

    mutable std::mutex mu_;
    std::shared_ptr<Bundle> bundle_;
    std::string token_;
    int rev_ = 0;
    std::set<plat::Sock> clients_;
    std::vector<std::shared_ptr<Transfer>> transfers_;
    std::string lastDevice_;
    uint64_t nextId_ = 1;
};
