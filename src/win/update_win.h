#pragma once
#include <atomic>
#include <string>

struct UpdateRelease {
    std::string tag, url, digest, error;
    bool newer = false;
};
UpdateRelease findUpdate();
UpdateRelease parseUpdateRelease(const std::string& json);
bool verifyUpdateFile(const std::string& path, const std::string& expected, const std::atomic<bool>& cancel);
// Downloads only an official release asset, verifies the API's SHA-256 digest.
std::string downloadUpdate(const UpdateRelease& release, const std::atomic<bool>& cancel, std::string& error);
