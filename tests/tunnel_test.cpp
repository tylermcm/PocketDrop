// Mirrors the app: HTTP server with live sockets + Tunnel, reporting every state change.
// usage: tunnel_test <seconds>
#include "../src/core/http.h"
#include "../src/core/tunnel.h"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 40;
    HttpServer srv;
    if (!srv.start(47391, true)) {
        std::cerr << "server start failed\n";
        return 1;
    }
    srv.publish(build_bundle({}, {"tunnel test"}, nullptr));

    uint64_t t0 = plat::tick_ms();
    Tunnel* tp = nullptr;
    Tunnel t([&] {
        static const char* names[] = {"Off", "Missing", "Downloading", "Starting", "Online", "Failed"};
        if (!tp) return;
        std::cout << "[" << (plat::tick_ms() - t0) / 1000.0 << "s] " << names[(int)tp->state()] << " | "
                  << tp->message() << " | url=" << tp->url() << std::endl;
    });
    tp = &t;
    t.start(srv.port(), argc > 2 && std::string(argv[2]) == "--download");
    for (int i = 0; i < seconds * 10 && t.state() != Tunnel::State::Failed; i++) plat::sleep_ms(100);
    Tunnel::State finalState = t.state();
    std::cout << "final state " << (int)finalState << " url=" << t.url() << std::endl;
    t.stop();
    srv.stop();
    return finalState == Tunnel::State::Online ? 0 : 1;
}
