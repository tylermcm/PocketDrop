// Runs the HTTP server on loopback for tests/server_check.py, with shortened link expiry.
// usage: server_test <seconds> <path>...   (port 47291, or PD_TEST_PORT)
// State lives in build/server_test_out: token.txt (current QR token), inbox/, received.log, and
// flag files the checker creates: unshare (nothing shared), diskfull, done (exit early).
#include "../src/core/http.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string dir = "build/server_test_out", inbox = dir + "/inbox";
    plat::make_dir("build");
    plat::make_dir(dir);
    plat::make_dir(inbox);
    for (const auto& e : plat::list_dir(inbox)) plat::file_remove(inbox + "/" + e.name);
    for (const char* f : {"unshare", "diskfull", "done", "received.log", "token.txt"}) plat::file_remove(dir + "/" + f);

    HttpServer srv;
    srv.onEvent = [](HttpServer::Event e) { std::cout << "event " << (int)e << std::endl; };
    LinkPolicy fast;
    fast.rotateMs = 3000;
    fast.idleExpireMs = 6000;
    fast.activeWindowMs = 1500;
    srv.setPolicy(fast);
    srv.setInbox(inbox);
    const char* portEnv = getenv("PD_TEST_PORT");
    int port = portEnv && *portEnv ? atoi(portEnv) : 47291;
    if (!srv.start(port, true) || srv.port() != port) {
        // The checkers talk to this exact port; a fallback port would silently test some other server.
        std::cerr << "server_test: port " << port << " is unavailable (is PocketDrop running? set PD_TEST_PORT)\n";
        return 1;
    }
    srv.debugSetToken("testtoken123");

    std::vector<std::string> paths;
    for (int i = 2; i < argc; i++) paths.push_back(argv[i]);
    auto b = build_bundle(paths, {"hello from the pc\nline two", "https://example.com/a?b=1"}, nullptr);
    b->startPrepare(nullptr);
    srv.publish(b);
    std::cout << "port " << srv.port() << std::endl;

    auto exists = [&](const char* f) { return plat::file_stat(dir + "/" + f).exists; };
    auto writeToken = [&] {
        std::ofstream(dir + "/token.tmp", std::ios::binary) << srv.token();
        plat::file_rename(dir + "/token.tmp", dir + "/token.txt");
    };
    writeToken();

    uint64_t deadline = plat::tick_ms() + (uint64_t)atoi(argv[1]) * 1000;
    while (plat::tick_ms() < deadline && !exists("done")) {
        srv.debugDiskFull(exists("diskfull"));
        if (srv.maintain(!exists("unshare"))) {
            writeToken();
            std::cout << "token rotated" << std::endl;
        }
        auto items = srv.takeReceived();
        if (!items.empty()) {
            std::ofstream log(dir + "/received.log", std::ios::app | std::ios::binary);
            for (const auto& it : items)
                log << (it.isText ? "text" : "file") << "\t" << it.device << "\t"
                    << (it.isText ? std::to_string(it.size) : it.name) << "\n";
        }
        plat::sleep_ms(100);
    }
    for (auto& t : srv.transfers())
        std::cout << "transfer " << (t.incoming ? "in " : "out ") << t.name << " " << t.sent << "/" << t.total
                  << " ok=" << t.ok << " device=" << t.device << "\n";
    srv.stop();
    return 0;
}
