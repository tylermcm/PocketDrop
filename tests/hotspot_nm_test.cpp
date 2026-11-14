// Parsing for the Linux offline hotspot (src/core/hotspot_nm.h).
#include "../src/core/hotspot_nm.h"
#include <iostream>

static int fails = 0;
static void expect(const char* name, bool ok) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << std::endl;
    if (!ok) fails++;
}

int main() {
    using namespace hotspot_nm;
    expect("picks the Wi-Fi adapter", wifi_device("lo:loopback:unmanaged\neth0:ethernet:connected\nwlp3s0:wifi:disconnected\n") == "wlp3s0");
    expect("skips an unavailable adapter", wifi_device("wlan0:wifi:unavailable\nwlan1:wifi:connected\n") == "wlan1");
    expect("no Wi-Fi adapter", wifi_device("eth0:ethernet:connected\nlo:loopback:unmanaged\n").empty());
    expect("unmanaged adapter is not usable", wifi_device("wlan0:wifi:unmanaged\n").empty());
    expect("empty output", wifi_device("").empty());
    expect("garbage lines", wifi_device("nonsense\n::\n").empty());

    expect("address with mask", ipv4_address("10.42.0.1/24\n") == "10.42.0.1");
    expect("first of several addresses", ipv4_address("192.168.12.1/24 | 10.0.0.2/8\n") == "192.168.12.1");
    expect("not an address", ipv4_address("--\n").empty() && ipv4_address("").empty() && ipv4_address("fe80::1/64").empty());
    expect("hostile output", ipv4_address("1.2.3.4.5\n").empty() && ipv4_address("a.b.c.d").empty());

    std::string arp =
        "IP address       HW type     Flags       HW address            Mask     Device\n"
        "192.168.1.1      0x1         0x2         aa:bb:cc:dd:ee:01     *        wlan0\n"
        "10.42.0.23       0x1         0x2         aa:bb:cc:dd:ee:02     *        wlp3s0\n"
        "10.42.0.99       0x1         0x0         00:00:00:00:00:00     *        wlp3s0\n";
    expect("a joined phone is found", has_client(arp, "wlp3s0"));
    expect("other interfaces are ignored", !has_client(arp, "wlan9"));
    expect("incomplete entries do not count", !has_client("IP address HW type Flags HW address Mask Device\n10.42.0.99 0x1 0x0 00:00:00:00:00:00 * wlp3s0\n", "wlp3s0"));
    expect("empty table", !has_client("", "wlp3s0"));
    return fails ? 1 : 0;
}
