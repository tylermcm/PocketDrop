#include "../src/core/platform.h"
#include <iostream>

#ifdef _WIN32
#include <objbase.h>
#endif

int main() {
#ifdef _WIN32
    if (FAILED(OleInitialize(nullptr))) {
        std::cerr << "COM initialization failed\n";
        return 1;
    }
#endif

    bool ok = true;
    {
        plat::Hotspot hotspot([] {});
        if (!hotspot.supported()) {
            std::cout << "offline hotspot unavailable on this platform\n";
        } else {
            hotspot.start();
            for (int i = 0; i < 100 && hotspot.state() == plat::HotspotState::Starting; i++) plat::sleep_ms(100);
            hotspot.poll();
            auto state = hotspot.state();
            bool settled = state == plat::HotspotState::On || state == plat::HotspotState::Waiting ||
                           state == plat::HotspotState::Failed;
            if (state == plat::HotspotState::Waiting)
                std::cout << "waiting for Bluetooth PAN connection\n";
            else
                std::cout << (state == plat::HotspotState::On ? "offline connection ready" : hotspot.message()) << "\n";
            hotspot.stop();
            ok = settled && hotspot.state() == plat::HotspotState::Off;
        }
    }

#ifdef _WIN32
    OleUninitialize();
#endif
    return ok ? 0 : 1;
}
