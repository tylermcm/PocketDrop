#pragma once
// What the shared UI needs from the host window/OS. Implemented per platform.
#include <functional>
#include <string>
#include <vector>

struct MenuItem {
    int id = 0; // 0 for separators and submenu parents
    std::string label;
    bool checked = false, enabled = true, separator = false;
    std::vector<MenuItem> submenu;
};

class Shell {
public:
    virtual ~Shell() = default;

    // The host calls Ui::tick() every 500 ms, and about every 33 ms while animating.
    virtual void invalidate() = 0;
    virtual void setAnimating(bool on) = 0;
    virtual void post(std::function<void()> fn) = 0; // thread-safe; runs fn on the UI thread
    virtual void clientSize(float& w, float& h) = 0; // DIPs

    virtual void copyText(const std::string& text) = 0;
    // Clipboard contents: file paths, else an image saved as a PNG file path, else text.
    virtual void readClipboard(std::vector<std::string>& paths, std::string& text) = 0;
    virtual void browse(bool folders, std::function<void(const std::vector<std::string>&)> done) = 0;
    virtual void openUrl(const std::string& url) = 0;
    // Shows a menu with its top-right corner at (x, y) in DIPs. Returns the chosen id or 0.
    virtual int popupMenu(const std::vector<MenuItem>& items, float x, float y) = 0;
    virtual void alert(const std::string& title, const std::string& message) = 0;

    virtual int loadSetting(const char* key, int def) = 0;
    virtual void saveSetting(const char* key, int value) = 0;
    virtual void setTopmost(bool on) = 0;

    // Platform-only menu entries (ids >= 1000), e.g. Windows' "Send to" shortcut.
    virtual std::vector<MenuItem> extraMenuItems() { return {}; }
    virtual void handleExtraMenu(int) {}

    // Absolute path with trailing separators removed, or empty if it doesn't exist.
    virtual std::string cleanPath(const std::string& path) = 0;
};
