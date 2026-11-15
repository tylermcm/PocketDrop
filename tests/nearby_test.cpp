// Discovery between PocketDrop instances: presence, invitations and what is refused. Two instances run on separate
// discovery ports on this machine and talk by unicast.
#include "../src/core/nearby.h"
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using Sock = int;
#define CLOSESOCK close
#endif

static int fails = 0;
static void expect(const char* name, bool ok) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << std::endl;
    if (!ok) fails++;
}

template <class F> static bool waitFor(F f, int ms = 4000) {
    for (int waited = 0; waited < ms; waited += 20) {
        if (f()) return true;
        plat::sleep_ms(20);
    }
    return f();
}

static void sendTo(int port, const std::string& packet) {
    Sock s = (Sock)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &to.sin_addr);
    sendto(s, packet.data(), (int)packet.size(), 0, (sockaddr*)&to, sizeof to);
    CLOSESOCK(s);
}

int main() {
    const std::string offer(32, 'o'), id(32, 'i');
    // ---- the message formats
    NearbyInvite invite;
    expect("an invitation parses", NearbyDiscovery::parseInvite(NearbyDiscovery::inviteMessage(id, "Study PC", 47291, offer), invite) &&
                                       invite.name == "Study PC" && invite.port == 47291 && invite.offer == offer && invite.senderId == id);
    expect("control characters are stripped from a name", NearbyDiscovery::parseInvite(NearbyDiscovery::inviteMessage(id, std::string("A\nB\t") + "C", 1, offer), invite) &&
                                                              invite.name == "ABC");
    expect("an invitation needs a real offer", !NearbyDiscovery::parseInvite("POCKETDROP/2I\n" + id + "\n5\nPC\n", invite));
    expect("a short offer is refused", !NearbyDiscovery::parseInvite("POCKETDROP/2I\n" + id + "\n5\nPC\nabc", invite));
    expect("a bad port is refused", !NearbyDiscovery::parseInvite("POCKETDROP/2I\n" + id + "\n0\nPC\n" + offer, invite) &&
                                        !NearbyDiscovery::parseInvite("POCKETDROP/2I\n" + id + "\n70000\nPC\n" + offer, invite) &&
                                        !NearbyDiscovery::parseInvite("POCKETDROP/2I\n" + id + "\nx\nPC\n" + offer, invite));
    expect("trailing data is refused", !NearbyDiscovery::parseInvite(NearbyDiscovery::inviteMessage(id, "PC", 5, offer) + "\nextra", invite));
    expect("an announcement is not an invitation", !NearbyDiscovery::parseInvite(NearbyDiscovery::announce(id, "PC", 5, offer), invite));
    NearbyPeer peer;
    expect("an invitation is not an announcement", !NearbyDiscovery::parse(NearbyDiscovery::inviteMessage(id, "PC", 5, offer), peer));

    // ---- two instances
    NearbyDiscovery a(47461), b(47462);
    expect("both start", a.start(47301) && b.start(47302));
    NearbyPeer target;
    target.ip = "127.0.0.1";
    expect("an invitation reaches the other instance", a.invite(target, offer, 47462) && waitFor([&] { return !b.invites().empty(); }));
    auto got = b.invites();
    expect("with the sender's address and fetch details", got.size() == 1 && got[0].ip == "127.0.0.1" && got[0].port == 47301 &&
                                                              got[0].offer == offer && !got[0].name.empty());
    a.invite(target, std::string(32, 'p'), 47462);
    waitFor([&] { return b.invites().size() == 1 && b.invites()[0].offer == std::string(32, 'p'); });
    expect("a newer invitation from the same PC replaces the old one", b.invites().size() == 1 && b.invites()[0].offer == std::string(32, 'p'));
    b.dismissInvite(got[0].senderId);
    expect("a dismissed invitation is gone", b.invites().empty());
    expect("an invitation from another PC is kept", [&] {
        sendTo(47461, NearbyDiscovery::inviteMessage(std::string(32, 'z'), "Someone", 5, offer));   // to a, from somebody else
        plat::sleep_ms(150);
        return a.invites().size() == 1;
    }());
    expect("a bad offer is refused when sending", !a.invite(target, "short", 47462));
    expect("a bad address is refused when sending", [&] { NearbyPeer bad; bad.ip = "not-an-ip"; return !a.invite(bad, offer, 47462); }());

    // ---- many invitations are capped
    for (int i = 0; i < 20; i++) {
        std::string other(32, (char)('a' + i % 26));
        other[0] = (char)('A' + i);
        sendTo(47462, NearbyDiscovery::inviteMessage(other, "Flood " + std::to_string(i), 5, offer));
    }
    plat::sleep_ms(300);
    expect("a flood of invitations is capped", b.invites().size() <= 8);

    // ---- presence
    sendTo(47462, NearbyDiscovery::announce(std::string(32, 'q'), "Presence PC", 47303));
    expect("a PC announcing itself without an offer is listed", waitFor([&] {
        for (auto& p : b.peers()) if (p.name == "Presence PC" && p.offer.empty()) return true;
        return false;
    }));
    a.stop();
    b.stop();
    expect("stopping clears peers and invitations", b.peers().empty() && b.invites().empty() && !b.running());
    return fails ? 1 : 0;
}
