#include "../src/core/http.h"
#include "../src/core/pc_transfer.h"
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
    assert(server.start(47320));
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
    assert(plat::http_get("http://127.0.0.1:" + std::to_string(peer.port) + "/pc/" + offer + "/info", {}, plat::BAD_FILE, {}, 3000).status == 404);
    assert(get("file").status == 400);
    assert(get("file", client).body == content);
    assert(get("file", {"X-PocketDrop-Receiver: " + std::string(32, 'b')}).status == 409);
    assert(get("verify?crc=123", client).status == 422);
    assert(get("keepalive", client).status == 204 && server.pcAwaitingReceipt());
    assert(!server.pcShareComplete());
    assert(get("cancel", client).status == 204);
    assert(!server.pcAwaitingReceipt());
    PcReceiver receiver;
    receiver.start(peer, inbox);
    assert(waitFor([&] { return ended(receiver.snapshot()); }));
    auto result = receiver.snapshot();
    if (result.phase != PcDownload::Phase::Done) std::cerr << result.error << '\n';
    assert(result.phase == PcDownload::Phase::Done);
    assert(result.name == "single (2).txt" && read(result.path) == content);
    assert(read(util::path_join(inbox, "single.txt")) == "keep existing");
    assert(server.pcShareComplete() && get("info").status == 404 && get("file", client).status == 404);
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
    receiver.stop(); server.stop();
    plat::file_remove(large);
    plat::file_remove(util::path_join(inbox, "large.jpg"));
    std::cout << "Native PC single/ZIP/empty transfers, checksum, collision, one receiver, cancellation/retry, revocation and phone isolation passed\n";
}
