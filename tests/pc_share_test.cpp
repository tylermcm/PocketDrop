// PC Share v2 end to end over the machine's LAN address: pairing, acceptance, encrypted transfer, and refusals.
#include "../src/core/http.h"
#include "../src/core/pc_secure.h"
#include "../src/core/pc_transfer.h"
#include <filesystem>
#include <fstream>
#include <iostream>

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

template <class F> static bool waitFor(F f, int ms = 15000) {
    for (int waited = 0; waited < ms; waited += 20) {
        if (f()) return true;
        plat::sleep_ms(20);
    }
    return f();
}

static std::shared_ptr<Bundle> bundleOf(const std::vector<std::string>& paths) {
    auto b = build_bundle(paths, {}, nullptr);
    b->startPrepare(nullptr);
    waitFor([&] { return b->ready.load(); });
    return b;
}

int main() {
    std::string root = (fs::temp_directory_path() / "pd_pc_share_test").string();
    fs::remove_all(root);
    fs::create_directories(root + "/src/folder");
    fs::create_directories(root + "/inbox");
    std::string big(3 * 1024 * 1024 + 12345, 0);
    for (size_t i = 0; i < big.size(); i++) big[i] = (char)((i * 131 + (i >> 8)) & 255);
    std::ofstream(root + "/src/big.bin", std::ios::binary) << big;
    std::ofstream(root + "/src/folder/a.txt") << "alpha\n";
    std::ofstream(root + "/src/folder/b.txt") << std::string(100000, 'b');
    std::ofstream(root + "/src/empty.txt", std::ios::binary);

    // PC Share is LAN-only: the server treats loopback callers as the tunnel and refuses them, so use a LAN address.
    std::string lan;
    for (const auto& a : plat::lan_addresses()) if (a.ip.rfind("127.", 0) != 0) { lan = a.ip; break; }
    if (lan.empty()) { std::cout << "SKIP no LAN address on this machine\n"; return 0; }
    HttpServer server;
    int events = 0;
    server.onEvent = [&](HttpServer::Event) { events++; };
    if (!server.start(47391, false, lan)) { std::cerr << "port unavailable\n"; return 1; }
    int port = server.port();

    auto runReceive = [&](const std::string& offer, PcReceiver& rx) {
        NearbyPeer peer;
        peer.name = "Sender";
        peer.ip = lan;
        peer.port = port;
        peer.offer = offer;
        rx.start(peer, root + "/inbox");
    };
    auto finished = [&](PcReceiver& rx) {
        auto phase = rx.snapshot().phase;
        return phase == PcDownload::Phase::Done || phase == PcDownload::Phase::Failed || phase == PcDownload::Phase::Cancelled;
    };

    // ---- one big file, accepted after comparing codes
    {
        std::string offer = server.startPcShare(bundleOf({root + "/src/big.bin"}));
        expect("offer starts", !offer.empty());
        PcReceiver rx;
        runReceive(offer, rx);
        expect("sender sees a pairing request", waitFor([&] { return server.pcPairing().pending; }));
        auto pairing = server.pcPairing();
        expect("receiver shows the same six digits", waitFor([&] { return rx.snapshot().phase == PcDownload::Phase::Pairing && rx.snapshot().code == pairing.code; }),
               pairing.code + " vs " + rx.snapshot().code);
        expect("nothing is sent before the sender accepts", rx.snapshot().received == 0 && !finished(rx));
        {
            PcReceiver second;
            runReceive(offer, second);
            expect("a second PC cannot join a pending pairing", waitFor([&] { return finished(second); }) &&
                                                                    second.snapshot().phase == PcDownload::Phase::Failed);
        }
        server.pcAnswer(true);
        expect("accepted transfer finishes", waitFor([&] { return finished(rx); }) && rx.snapshot().phase == PcDownload::Phase::Done,
               rx.snapshot().error);
        expect("received bytes equal the original", readAll(rx.snapshot().path) == big);
        expect("sender records the completion", waitFor([&] { return server.pcShareComplete(); }));
        server.stopPcShare();
    }

    // ---- declined
    {
        std::string offer = server.startPcShare(bundleOf({root + "/src/big.bin"}));
        PcReceiver rx;
        runReceive(offer, rx);
        waitFor([&] { return server.pcPairing().pending; });
        server.pcAnswer(false);
        expect("a declined transfer fails with a clear message", waitFor([&] { return finished(rx); }) &&
                                                                     rx.snapshot().phase == PcDownload::Phase::Failed &&
                                                                     rx.snapshot().error.find("declined") != std::string::npos,
               rx.snapshot().error);
        std::error_code ec;
        size_t leftovers = 0;
        for (auto& e : fs::directory_iterator(root + "/inbox", ec))
            if (e.path().extension() == ".pcpart") leftovers++;
        expect("nothing partial is left behind", leftovers == 0);
        server.stopPcShare();
    }

    // ---- a folder (zip) and an empty file
    {
        std::string offer = server.startPcShare(bundleOf({root + "/src/folder", root + "/src/empty.txt"}));
        PcReceiver rx;
        runReceive(offer, rx);
        waitFor([&] { return server.pcPairing().pending; });
        server.pcAnswer(true);
        bool done = waitFor([&] { return finished(rx); }) && rx.snapshot().phase == PcDownload::Phase::Done;
        expect("a folder arrives as a zip", done && rx.snapshot().name.size() > 4 && rx.snapshot().name.substr(rx.snapshot().name.size() - 4) == ".zip",
               rx.snapshot().error);
        server.stopPcShare();
    }
    {
        std::string offer = server.startPcShare(bundleOf({root + "/src/empty.txt"}));
        PcReceiver rx;
        runReceive(offer, rx);
        waitFor([&] { return server.pcPairing().pending; });
        server.pcAnswer(true);
        expect("an empty file transfers", waitFor([&] { return finished(rx); }) && rx.snapshot().phase == PcDownload::Phase::Done &&
                                              readAll(rx.snapshot().path).empty(),
               rx.snapshot().error);
        server.stopPcShare();
    }

    // ---- someone who only saw the multicast offer
    {
        std::string offer = server.startPcShare(bundleOf({root + "/src/big.bin"}));
        std::string base = "http://" + lan + ":" + std::to_string(port) + "/pc/" + offer + "/";
        std::vector<std::string> guess = {"X-PocketDrop-Receiver: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "Connection: close"};
        auto info = plat::http_get(base + "info", guess, plat::BAD_FILE, {}, 3000);
        auto file = plat::http_get(base + "file", guess, plat::BAD_FILE, {}, 3000);
        expect("info needs a pairing", info.status == 404, std::to_string(info.status));
        expect("file needs a pairing", file.status == 404, std::to_string(file.status));
        auto plainV1 = plat::http_get(base + "info", {"Connection: close"}, plat::BAD_FILE, {}, 3000);
        expect("a request without any identity is refused", plainV1.status == 400, std::to_string(plainV1.status));
        auto badKey = plat::http_get(base + "hello", {"X-PocketDrop-Receiver: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "X-PocketDrop-Key: zz", "Connection: close"},
                                     plat::BAD_FILE, {}, 3000);
        expect("a malformed key is refused", badKey.status == 400, std::to_string(badKey.status));
        auto lowOrder = plat::http_get(base + "hello", {"X-PocketDrop-Receiver: AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                                          "X-PocketDrop-Key: " + std::string(64, '0'), "Connection: close"}, plat::BAD_FILE, {}, 3000);
        expect("an all-zero public key is refused", lowOrder.status == 400, std::to_string(lowOrder.status));
        server.stopPcShare();
    }

    server.stop();
    fs::remove_all(root);
    return fails ? 1 : 0;
}
