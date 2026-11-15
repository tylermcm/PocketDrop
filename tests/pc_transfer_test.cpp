#include "../src/core/http.h"
#include "../src/core/pc_secure.h"
#include "../src/core/pc_transfer.h"
#include <atomic>
#include <thread>
#include "../src/core/deflate.h"
#include <cassert>
#include <fstream>
#include <iostream>

namespace {
template<class F> bool waitFor(F condition, int ms = 15000) {
    auto until = plat::tick_ms() + ms;
    do { if (condition()) return true; plat::sleep_ms(1); } while (plat::tick_ms() < until);
    return false;
}
bool ended(const PcDownload& s) {
    return s.phase == PcDownload::Phase::Done || s.phase == PcDownload::Phase::Failed || s.phase == PcDownload::Phase::Cancelled;
}
void write(const std::string& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
std::string read(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
std::shared_ptr<Bundle> prepared(const std::vector<std::string>& paths) {
    auto bundle = build_bundle(paths, {}, nullptr);
    bundle->startPrepare(nullptr);
    assert(waitFor([&] { return bundle->ready.load(); }));
    return bundle;
}
}

// A receiver written by hand, to probe the sender the way a hostile PC on the LAN could: it pairs (the person at the
// sender allows it) and then speaks the protocol directly.
struct Raw {
    pcsecure::Keys keys;
    std::vector<std::string> headers;
    bool pair(const std::string& base, const std::string& name = "Raw receiver") {
        uint8_t sk[32], pk[32], peer[32];
        pcsecure::make_keypair(sk, pk);
        auto hello = plat::http_get(base + "hello", {"X-PocketDrop-Receiver: " + std::string(32, 'a'), "X-PocketDrop-Name: " + name,
                                                      "X-PocketDrop-Key: " + crypto::to_hex(pk, 32), "Connection: close"},
                                    plat::BAD_FILE, {}, 3000);
        if (hello.status != 200 || hello.body.size() != 64) return false;
        for (size_t i = 0; i < 32; i++) peer[i] = (uint8_t)std::stoi(hello.body.substr(2 * i, 2), nullptr, 16);
        if (!pcsecure::derive(sk, peer, peer, pk, keys)) return false;
        headers = {"X-PocketDrop-Receiver: " + keys.claim};
        for (int i = 0; i < 500; i++) {
            if (plat::http_get(base + "status", headers, plat::BAD_FILE, {}, 3000).body == "accepted") return true;
            plat::sleep_ms(10);
        }
        return false;
    }
    // The data of `file`, opened frame by frame; empty when the stream is not intact.
    std::string file(const std::string& base, int& status) {
        pcsecure::Decoder decoder(keys);
        std::string out;
        auto result = plat::http_get(base + "file", headers, plat::BAD_FILE, {}, 3000, [&](const uint8_t* data, size_t n) {
            return decoder.push(data, n, [&](const uint8_t* p, size_t m) { out.append((const char*)p, m); return true; });
        });
        status = result.status;
        return decoder.complete() ? out : std::string();
    }
};

int main(int argc, char** argv) {
    if (argc == 5) {
        NearbyPeer peer;
        peer.ip = argv[1]; peer.port = atoi(argv[2]); peer.offer = argv[3]; peer.name = "Test sender";
        PcReceiver receiver;
        receiver.start(peer, argv[4]);
        if (!waitFor([&] { return ended(receiver.snapshot()); }, 20000)) return 3;
        auto result = receiver.snapshot();
        std::cout << result.error << '\n';
        return result.phase == PcDownload::Phase::Done ? 0 : 1;
    }
    std::string name;
    uint64_t size = 0;
    assert(PcReceiver::parseInfo("PDPC1\n0\nempty.txt", name, size) && size == 0);
    for (auto body : {"PDPC1\n1\n../escape", "PDPC1\n1\nC:\\escape", "PDPC1\n1\nNUL.txt", "PDPC1\n1\nfile. ",
                      "PDPC1\n-1\nfile", "PDPC1\n18446744073709551616\nfile", "PDPC1\n1\nfile\nextra"})
        assert(!PcReceiver::parseInfo(body, name, size));
    auto addresses = plat::lan_addresses();
    assert(!addresses.empty());
    const std::string root = "build/pc-test-" + util::random_token(6);
    const auto src = util::path_join(root, "source"), inbox = util::path_join(root, "received");
    plat::make_dir(root); plat::make_dir(src); plat::make_dir(inbox);
    const auto single = util::path_join(src, "single.txt");
    const std::string content = "PC transfer fixture: café 日本語\n";
    write(single, content);
    write(util::path_join(inbox, "single.txt"), "keep existing");
    HttpServer server;
    assert(server.start(47320, false, addresses[0].ip));
    // The person at the sender allows every pairing, as soon as one asks.
    std::atomic<bool> stopAllowing{false};
    std::thread allower([&] {
        while (!stopAllowing) {
            if (server.pcPairing().pending) server.pcAnswer(true);
            plat::sleep_ms(2);
        }
    });
    const auto phoneToken = server.token();
    auto bundle = prepared({single});
    auto offer = server.startPcShare(bundle);
    assert(offer.size() == 32);
    NearbyPeer peer;
    peer.ip = addresses[0].ip; peer.port = server.port(); peer.offer = offer; peer.name = "Fixture PC";
    const auto base = "http://" + peer.ip + ":" + std::to_string(peer.port) + "/pc/" + offer + "/";
    std::vector<std::string> client = {"X-PocketDrop-Receiver: " + std::string(32, 'a')};
    auto get = [&](const std::string& route, const std::vector<std::string>& headers = std::vector<std::string>{}) {
        return plat::http_get(base + route, headers, plat::BAD_FILE, {}, 3000);
    };
    // The server listens on the LAN address only, so nothing answers on loopback (where the tunnel would come from).
    assert(plat::http_get("http://127.0.0.1:" + std::to_string(peer.port) + "/pc/" + offer + "/info", {}, plat::BAD_FILE, {}, 3000).status == 0);
    assert(get("file").status == 400);                              // no identity at all
    assert(get("file", client).status == 404);                      // an identity nobody paired
    assert(get("info", client).status == 404);
    Raw raw;
    assert(raw.pair(base));
    {
        Raw second;
        assert(!second.pair(base));                                 // one receiver at a time
    }
    int status = 0;
    assert(raw.file(base, status) == content && status == 200);     // the data arrives opened, never as plain bytes
    assert(plat::http_get(base + "file", raw.headers, plat::BAD_FILE, {}, 3000).status == 409);  // and only once
    assert(get("verify?crc=123", raw.headers).status == 422);
    assert(get("keepalive", raw.headers).status == 204 && server.pcAwaitingReceipt());
    assert(!server.pcShareComplete());
    assert(get("cancel", raw.headers).status == 204);
    assert(!server.pcAwaitingReceipt());
    assert(get("file", raw.headers).status == 404);                 // cancelling ended that pairing
    PcReceiver receiver;
    receiver.start(peer, inbox);
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    auto result = receiver.snapshot();
    if (result.phase != PcDownload::Phase::Done) std::cerr << result.error << '\n';
    assert(result.phase == PcDownload::Phase::Done);
    assert(result.name == "single (2).txt" && read(result.path) == content);
    assert(read(util::path_join(inbox, "single.txt")) == "keep existing");
    assert(server.pcShareComplete() && get("info", client).status == 404 && get("file", client).status == 404);
    assert(server.token() == phoneToken);

    auto folder = util::path_join(src, "Photos"); plat::make_dir(folder);
    plat::make_dir(util::path_join(folder, "Empty"));
    write(util::path_join(folder, "image.txt"), "photo fixture");
    write(util::path_join(folder, "日本語.txt"), "unicode fixture");
    bundle = prepared({folder, single});
    peer.offer = server.startPcShare(bundle);
    receiver.start(peer, inbox);
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    result = receiver.snapshot();
    assert(result.phase == PcDownload::Phase::Done && read(result.path).substr(0, 2) == "PK");
    std::ofstream("build/pc-test-result.json") << "{\"zip\":\"" + util::json_escape(result.path) + "\",\"source\":\"" + util::json_escape(src) + "\"}";

    auto empty = util::path_join(src, "empty.txt"); write(empty, {});
    peer.offer = server.startPcShare(prepared({empty}));
    receiver.start(peer, inbox);
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    assert(receiver.snapshot().phase == PcDownload::Phase::Done && plat::file_stat(receiver.snapshot().path).size == 0);

    auto large = util::path_join(src, "large.jpg");
    { std::ofstream out(large, std::ios::binary); std::string block(1 << 20, 'X');
      for (int i = 0; i < 128; ++i) { block[0] = (char)i; out.write(block.data(), block.size()); } }
    bundle = prepared({large});
    peer.offer = server.startPcShare(bundle);
    receiver.start(peer, inbox);
    assert(waitFor([&] { return receiver.snapshot().received > 0; }));
    receiver.cancel();
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    assert(receiver.snapshot().phase == PcDownload::Phase::Cancelled);
    for (const auto& entry : plat::list_dir(inbox)) assert(entry.name.find(".pcpart") == std::string::npos);
    assert(!plat::file_stat(util::path_join(inbox, "large.jpg")).exists);
    receiver.start(peer, inbox); // cancellation releases the claim, allowing retry
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    assert(receiver.snapshot().phase == PcDownload::Phase::Done);

    peer.offer = server.startPcShare(bundle);
    receiver.start(peer, inbox);
    assert(waitFor([&] { return receiver.snapshot().received > 0; }));
    server.stopPcShare();
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    assert(receiver.snapshot().phase == PcDownload::Phase::Failed);
    assert(!plat::file_stat(util::path_join(inbox, "large (2).jpg")).exists);
    for (const auto& entry : plat::list_dir(inbox)) assert(entry.name.find(".pcpart") == std::string::npos);
    receiver.stop();
    stopAllowing = true;
    allower.join();
    server.stop();
    plat::file_remove(large);
    plat::file_remove(util::path_join(inbox, "large.jpg"));
    std::cout << "Native PC single/ZIP/empty transfers, checksum, collision, one receiver, cancellation/retry, revocation and phone isolation passed\n";
}
