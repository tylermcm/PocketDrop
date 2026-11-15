// The whole UI engine against stand-in PCs: it is invited to receive files, declines an invitation, and sends to a PC it
// found nearby. The remote PCs are played by this test (an HttpServer sender and a PcReceiver) over the machine's LAN address.
#include "../src/core/http.h"
#include "../src/core/nearby.h"
#include "../src/core/pc_transfer.h"
#include "../src/ui/ui.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
using Sock = int;
#define CLOSESOCK close
#endif

namespace fs = std::filesystem;
static int fails = 0;
static void expect(const char* name, bool ok, const std::string& detail = {}) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << (ok || detail.empty() ? "" : "  [" + detail + "]") << std::endl;
    if (!ok) fails++;
}

static std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

struct TestShell : Shell {
    std::string inbox;
    std::mutex mu;
    std::queue<std::function<void()>> queued;
    void invalidate() override {}
    void setAnimating(bool) override {}
    void post(std::function<void()> fn) override { std::lock_guard<std::mutex> l(mu); queued.push(std::move(fn)); }
    void drain() {
        for (;;) {
            std::function<void()> fn;
            {
                std::lock_guard<std::mutex> l(mu);
                if (queued.empty()) return;
                fn = std::move(queued.front());
                queued.pop();
            }
            fn();
        }
    }
    void clientSize(float& w, float& h) override { w = 385; h = 750; }
    void copyText(const std::string&) override {}
    void readClipboard(std::vector<std::string>&, std::string&) override {}
    void browse(bool, std::function<void(const std::vector<std::string>&)>) override {}
    void chooseFolder(const std::string&, std::function<void(const std::string&)>) override {}
    void openUrl(const std::string&) override {}
    void revealPath(const std::string&) override {}
    int popupMenu(const std::vector<MenuItem>&, float, float) override { return 0; }
    void alert(const std::string&, const std::string& message) override { std::cerr << "alert: " << message << '\n'; }
    int loadSetting(const char*, int def) override { return def; }
    void saveSetting(const char*, int) override {}
    std::string loadString(const char* key, const std::string& def) override { return std::string(key) == "InboxDir" ? inbox : def; }
    void saveString(const char*, const std::string&) override {}
    void setTopmost(bool) override {}
    std::string cleanPath(const std::string& p) override { return p; }
};

static std::string field(const std::string& json, const std::string& key) {
    auto at = json.find("\"" + key + "\":");
    return at == std::string::npos ? std::string() : json.substr(at + key.size() + 3, 24);
}

static void udpTo(const std::string& ip, int port, const std::string& packet) {
    Sock s = (Sock)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, ip.c_str(), &to.sin_addr);
    sendto(s, packet.data(), (int)packet.size(), 0, (sockaddr*)&to, sizeof to);
    CLOSESOCK(s);
}

int main() {
    std::string lan;
    for (const auto& a : plat::lan_addresses()) if (a.ip.rfind("127.", 0) != 0) { lan = a.ip; break; }
    if (lan.empty()) { std::cout << "SKIP no LAN address on this machine\n"; return 0; }
    const std::string root = (fs::temp_directory_path() / "pd_ui_nearby").string();
    fs::remove_all(root);
    fs::create_directories(root + "/inbox");
    fs::create_directories(root + "/inbox2");
    std::ofstream(root + "/outgoing.txt") << "files for the other PC\n";
    std::ofstream(root + "/incoming.txt") << "files from the other PC\n";

    TestShell shell;
    shell.inbox = root + "/inbox";
    Ui ui(shell);
    ui.start();
    auto pump = [&](int ms) {
        for (int waited = 0; waited < ms; waited += 10) { shell.drain(); ui.tick(); plat::sleep_ms(10); }
    };
    auto waitUntil = [&](auto cond, int ms = 8000) {
        for (int waited = 0; waited < ms; waited += 10) {
            shell.drain(); ui.tick();
            if (cond()) return true;
            plat::sleep_ms(10);
        }
        return cond();
    };
    auto state = [&] { return ui.stateJson(); };
    pump(300);
    expect("the engine is listening for other PCs", state().find("\"nearby\":{\"visible\":true") != std::string::npos);

    // ---- another PC invites this one to receive files
    HttpServer sender;
    sender.start(47510, false, lan);
    std::atomic<bool> allow{true}, stop{false};
    std::thread allower([&] {
        while (!stop) { if (allow && sender.pcPairing().pending) sender.pcAnswer(true); plat::sleep_ms(3); }
    });
    auto bundle = build_bundle({root + "/incoming.txt"}, {}, nullptr);
    bundle->startPrepare(nullptr);
    waitUntil([&] { return bundle->ready.load(); });
    const std::string offer = sender.startPcShare(bundle), senderId(32, 'r');
    bool seen = waitUntil([&] {
        udpTo(lan, 47292, NearbyDiscovery::inviteMessage(senderId, "Remote PC", sender.port(), offer));
        return state().find("\"invite\":{") != std::string::npos;
    });
    expect("an invitation shows up with the sender's name", seen && state().find("\"name\":\"Remote PC\"") != std::string::npos);
    ui.command("pc_invite_accept", "");
    bool done = waitUntil([&] { return field(state(), "phase").rfind("4", 0) == 0; }, 15000);
    expect("accepting it receives the files", done, state().substr(0, 200));
    bool landed = false;
    for (auto& e : fs::directory_iterator(root + "/inbox")) if (readAll(e.path().string()) == "files from the other PC\n") landed = true;
    expect("and they are saved intact in the inbox", landed);
    ui.command("pc_stop", "");
    pump(100);
    expect("the invitation is cleared once it is answered", state().find("\"invite\":null") != std::string::npos);

    // ---- an invitation can be declined
    sender.stopPcShare();
    const std::string offer2 = sender.startPcShare(bundle);
    waitUntil([&] {
        udpTo(lan, 47292, NearbyDiscovery::inviteMessage(std::string(32, 's'), "Other PC", sender.port(), offer2));
        return state().find("\"invite\":{") != std::string::npos;
    });
    ui.command("pc_invite_decline", "");
    pump(100);
    expect("declining clears it without receiving anything", state().find("\"invite\":null") != std::string::npos &&
                                                               field(state(), "phase").rfind("2", 0) != 0);

    // ---- sending to a PC found nearby
    stop = true;
    allower.join();
    sender.stop();
    const std::string peerId(32, 'p');
    bool listed = waitUntil([&] {
        udpTo("127.0.0.1", 47292, NearbyDiscovery::announce(peerId, "Kitchen PC", 47520));
        return state().find("\"name\":\"Kitchen PC\"") != std::string::npos;
    });
    expect("a PC announcing itself is listed as nearby", listed);
    // From here on, what the Ui sends to that PC (at 127.0.0.1) arrives on a socket this test owns.
    Sock listener = (Sock)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in bindAt{};
    bindAt.sin_family = AF_INET;
    bindAt.sin_port = htons(47292);
    inet_pton(AF_INET, "127.0.0.1", &bindAt.sin_addr);
    expect("the test can listen where that PC would", bind(listener, (sockaddr*)&bindAt, sizeof bindAt) == 0);
    ui.addPaths({root + "/outgoing.txt"});
    pump(300);
    ui.command("pc_send_to", peerId);
    NearbyInvite got;
    bool invited = false;
    for (int i = 0; i < 400 && !invited; i++) {
        shell.drain(); ui.tick();
        fd_set reads;
        FD_ZERO(&reads); FD_SET(listener, &reads);
        timeval wait{0, 10000};
        if (select((int)listener + 1, &reads, nullptr, nullptr, &wait) > 0) {
            char buffer[300];
            int n = (int)recv(listener, buffer, sizeof buffer, 0);
            invited = n > 0 && NearbyDiscovery::parseInvite(std::string(buffer, (size_t)n), got);
        }
    }
    expect("dropping files on a PC invites it, naming where to fetch from", invited && got.offer.size() == 32 && got.port > 0, state().substr(0, 200));
    expect("the screen says who it is waiting for", state().find("\"target\":\"Kitchen PC\"") != std::string::npos);
    PcReceiver receiver;
    NearbyPeer from;
    from.name = "Kitchen PC"; from.ip = lan; from.port = got.port; from.offer = got.offer;
    receiver.start(from, root + "/inbox2");
    bool asked = waitUntil([&] { return state().find("\"pair\":{\"pending\":true") != std::string::npos; }, 8000);
    expect("the sender is asked to compare codes before anything leaves", asked);
    ui.command("pc_allow", "");
    bool arrived = waitUntil([&] { auto s = receiver.snapshot(); return s.phase == PcDownload::Phase::Done || s.phase == PcDownload::Phase::Failed; }, 15000);
    expect("the files arrive at the PC", arrived && receiver.snapshot().phase == PcDownload::Phase::Done &&
                                              readAll(receiver.snapshot().path) == "files for the other PC\n", receiver.snapshot().error);
    CLOSESOCK(listener);
    receiver.stop();
    ui.shutdown();
    fs::remove_all(root);
    return fails ? 1 : 0;
}
