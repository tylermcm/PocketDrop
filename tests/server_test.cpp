// Runs the HTTP server on loopback for tests/server_check.py.
// usage: server_test <seconds> <path>...
#include "../src/core/http.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    HttpServer srv;
    srv.onEvent = [](HttpServer::Event e) { std::cout << "event " << (int)e << std::endl; };
    if (!srv.start(47291, true)) {
        std::cerr << "start failed\n";
        return 1;
    }
    std::vector<std::string> paths;
    for (int i = 2; i < argc; i++) paths.push_back(argv[i]);
    auto b = build_bundle(paths, {"hello from the pc\nline two", "https://example.com/a?b=1"}, nullptr);
    b->startPrepare(nullptr);
    srv.publish(b, "testtoken123");
    for (auto& a : plat::lan_addresses()) std::cout << "lan " << a.ip << " (" << a.adapter << ")\n";
    std::cout << "port " << srv.port() << std::endl;
    plat::sleep_ms(atoi(argv[1]) * 1000);
    for (auto& t : srv.transfers())
        std::cout << "transfer " << t.name << " " << t.sent << "/" << t.total << " ok=" << t.ok << " device=" << t.device
                  << "\n";
    std::cout << "last device " << srv.lastDevice() << std::endl;
    srv.stop();
    return 0;
}
