// Regression checks and actual Direct2D renders, without driving the user's desktop.
#include "../src/ui/ui.h"
#include "../src/core/version.h"
#include "../src/win/window_fit.h"
#include "../src/win/gfx_d2d.h"
#include <cassert>
#include <iostream>
#include <fstream>

struct TestShell : Shell {
    float width = 400, height = 660;
    int nextMenu = 0;
    std::vector<MenuItem> lastMenu;
    void invalidate() override {}
    void setAnimating(bool) override {}
    void post(std::function<void()>) override {} // callbacks discarded; renders are synchronous
    void clientSize(float& w, float& h) override { w = width; h = height; }
    void copyText(const std::string&) override {}
    void readClipboard(std::vector<std::string>&, std::string&) override {}
    void browse(bool, std::function<void(const std::vector<std::string>&)>) override {}
    void chooseFolder(const std::string&, std::function<void(const std::string&)>) override {}
    void openUrl(const std::string&) override {}
    void revealPath(const std::string&) override {}
    int popupMenu(const std::vector<MenuItem>& items, float, float) override {
        lastMenu = items;
        int result = nextMenu; nextMenu = 0;
        return result;
    }
    void alert(const std::string&, const std::string& message) override { std::cerr << message << '\n'; }
    int loadSetting(const char*, int def) override { return def; }
    void saveSetting(const char*, int) override {}
    std::string loadString(const char* key, const std::string& def) override {
        return std::string(key) == "InboxDir" ? "build/desktop-inbox" : def;
    }
    void saveString(const char*, const std::string&) override {}
    void setTopmost(bool) override {}
    std::string cleanPath(const std::string& p) override { return p; }
};

struct UiRenderTest {
    static void selection(Ui& ui, TestShell& shell) {
        ui.stopPc();
        ui.pcMode_ = Ui::PcMode::Receive;
        NearbyPeer a; a.id = std::string(32, 'a'); a.name = "Office A"; a.offer = a.id;
        a.ip = "127.0.0.1"; a.port = ui.server_.port(); a.seen = plat::tick_ms();
        NearbyPeer b = a; b.id = std::string(32, 'b'); b.name = "Office B"; b.offer = b.id;
        ui.nearby_.peers_ = {a, b};
        ui.pollPc();
        assert(!ui.pcStarted_); // multiple senders require a name choice
        shell.nextMenu = 101;
        ui.pcAction();
        assert(shell.lastMenu.size() == 2 && ui.pcStarted_ && ui.pcDownload_.device == "Office B");
        ui.stopPc();
        ui.pcMode_ = Ui::PcMode::Receive;
        ui.nearby_.peers_ = {a};
        ui.pollPc();
        assert(ui.pcStarted_ && ui.pcDownload_.device == "Office A"); // one sender connects automatically
        ui.stopPc();
        assert(ui.pcMode_ == Ui::PcMode::Phone);
    }
    static void card(Ui& ui, Tunnel::State state) {
        ui.mode_ = 1;
        ui.tunnel_->state_ = state;
        ui.tunnel_->msg_ = "Fixture network error";
        ui.receiveCode_ = "ABCDEFGH";
    }
    static void pc(Ui& ui, bool send, int phase = 0) {
        ui.pcMode_ = send ? Ui::PcMode::Send : Ui::PcMode::Receive;
        ui.pcOffer_ = send ? std::string(32, 'a') : "";
        ui.pcStarted_ = phase != 0;
        ui.pcDownload_.phase = (PcDownload::Phase)phase;
        ui.pcDownload_.device = "Office PC";
        ui.pcDownload_.name = "Photos.zip";
        ui.pcDownload_.received = 40 << 20;
        ui.pcDownload_.total = 100 << 20;
        ui.pcDownload_.error = "Sender disconnected. Choose Receive to try again.";
        ui.pcReceived_ = phase == (int)PcDownload::Phase::Done;
        if (send) {
            ui.paths_ = {"Photos.zip"};
            ui.bundle_ = std::make_shared<Bundle>();
            ui.bundle_->totalBytes = 100 << 20;
            ui.bundle_->ready = true;
            BundleFile file; file.name = "Photos.zip"; file.path = "Photos.zip"; file.size = 100 << 20;
            ui.bundle_->files.push_back(file);
            BundleRoot root; root.path = "Photos.zip"; root.name = "Photos.zip"; root.bytes = 100 << 20; root.fileCount = 1;
            ui.bundle_->roots.push_back(root);
        }
    }
};

void testNativeWindow();

int main() {
    testNativeWindow();
    auto laptop = fitWindow(-50, -100, 600, 1125, 0, 0, 1366, 728);
    assert(laptop.x == 0 && laptop.y == 0 && laptop.height == 728);
    auto second = fitWindow(2100, 900, 700, 900, 1920, -200, 3200, 700);
    assert(second.x == 2100 && second.y == -200 && second.height == 900);
    assert(newerVersion("v2.10.0", "2.9.9"));
    assert(!newerVersion("v2.1.0", "2.1.0"));
    assert(!newerVersion("2.0.9", "2.1.0"));
    assert(!newerVersion("v3.0.0-beta"));
    assert(!newerVersion("9999999999.0.0"));
    NearbyPeer peer;
    const std::string id(32, 'a');
    assert(NearbyDiscovery::parse(NearbyDiscovery::announce(id, "Office PC", 47291), peer));
    assert(peer.name == "Office PC" && peer.port == 47291);
    assert(!NearbyDiscovery::parse("POCKETDROP/1\n" + id + "\n65536\nPC", peer));
    assert(!NearbyDiscovery::parse("POCKETDROP/1\n" + id + "\n80\nPC\nsecret", peer));
    assert(!NearbyDiscovery::parse("POCKETDROP/1\ninvalid\n80\nPC", peer));
    assert(!NearbyDiscovery::parse(std::string(257, 'x'), peer));
    OleInitialize(nullptr);
    {
    plat::make_dir("build/renders");
    TestShell shell;
    Ui ui(shell);
    ui.start();
    D2DGfx gfx;
    assert(gfx.init());
    struct Size { int w, h; const wchar_t* path; float dpi; };
    for (auto s : {Size{400, 660, L"build/renders/default.png", 96},
                   Size{360, 460, L"build/renders/laptop.png", 96},
                   Size{320, 440, L"build/renders/minimum.png", 96},
                   Size{304, 341, L"build/renders/resize-minimum.png", 96},
                   Size{800, 700, L"build/renders/wide.png", 96},
                   Size{360, 460, L"build/renders/laptop-150.png", 144},
                   Size{320, 325, L"build/renders/laptop-200.png", 192}}) {
        shell.width = (float)s.w; shell.height = (float)s.h;
        assert(gfx.beginBitmap((UINT)(s.w * s.dpi / 96), (UINT)(s.h * s.dpi / 96), s.dpi));
        ui.paint(gfx);
        assert(gfx.saveBitmap(s.path));
    }
    shell.width = 320; shell.height = 440;
    for (auto s : {std::pair{Tunnel::State::Missing, L"build/renders/laptop-enable.png"},
                   std::pair{Tunnel::State::Failed, L"build/renders/laptop-error.png"},
                   std::pair{Tunnel::State::Starting, L"build/renders/laptop-busy.png"}}) {
        UiRenderTest::card(ui, s.first);
        assert(gfx.beginBitmap(320, 440));
        ui.paint(gfx);
        assert(gfx.saveBitmap(s.second));
    }
    std::ofstream("build/renders/model.json") << ui.stateJson();
    for (auto size : {Size{400, 660, L"", 96}, Size{320, 440, L"", 96}, Size{320, 325, L"", 192},
                     Size{304, 341, L"", 96}}) {
        shell.width = (float)size.w; shell.height = (float)size.h;
        for (int scene = 0; scene < 5; ++scene) {
            UiRenderTest::pc(ui, scene == 0, scene == 2 ? (int)PcDownload::Phase::Receiving :
                scene == 3 ? (int)PcDownload::Phase::Done : scene == 4 ? (int)PcDownload::Phase::Failed : 0);
            assert(gfx.beginBitmap((UINT)(size.w * size.dpi / 96), (UINT)(size.h * size.dpi / 96), size.dpi));
            ui.paint(gfx);
            auto path = L"build/renders/pc-" + std::to_wstring(scene) + L"-" + std::to_wstring(size.h) + L".png";
            assert(gfx.saveBitmap(path.c_str()));
        }
    }
    std::ofstream("build/renders/pc-model.json") << ui.stateJson();
    UiRenderTest::selection(ui, shell);
    ui.shutdown();
    }
    OleUninitialize();
    std::cout << "Desktop geometry, discovery protocol, versions, phone and native PC Direct2D renders passed\n";
}
