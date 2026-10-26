#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../src/core/nearby.h"
#include <cassert>
#include <iostream>

int main() {
    NearbyDiscovery discovery(47314); // isolate from any running PocketDrop
    assert(discovery.start(47313));
    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(sender != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_port = htons(47314);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    const auto packet = NearbyDiscovery::announce(std::string(32, 'a'), "Fixture PC", 47315, std::string(32, 'b'));
    for (int i = 0; i < 20 && discovery.peers().empty(); ++i) {
        sendto(sender, packet.data(), (int)packet.size(), 0, (sockaddr*)&address, sizeof address);
        plat::sleep_ms(50);
    }
    auto peers = discovery.peers();
    assert(peers.size() == 1 && peers[0].name == "Fixture PC" && peers[0].port == 47315);
    assert(peers[0].ip == "127.0.0.1");
    assert(peers[0].offer == std::string(32, 'b'));
    const std::string bad = "POCKETDROP/2\n" + std::string(32, 'b') + "\n0\nBad PC\n" + std::string(32, 'c');
    sendto(sender, bad.data(), (int)bad.size(), 0, (sockaddr*)&address, sizeof address);
    plat::sleep_ms(100);
    assert(discovery.peers().size() == 1);
    const auto withdrawn = NearbyDiscovery::announce(std::string(32, 'a'), "Fixture PC", 47315);
    sendto(sender, withdrawn.data(), (int)withdrawn.size(), 0, (sockaddr*)&address, sizeof address);
    plat::sleep_ms(100);
    assert(discovery.peers()[0].offer.empty());
    closesocket(sender);
    auto before = plat::tick_ms();
    discovery.stop();
    assert(plat::tick_ms() - before < 1500 && discovery.peers().empty());
    std::cout << "UDP discovery receive, sender address, malformed packet and shutdown checks passed\n";
}
