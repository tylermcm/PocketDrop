#include "update_win.h"
#include "../core/platform.h"
#include "../core/util.h"
#include "../core/version.h"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>

UpdateRelease findUpdate() {
    UpdateRelease result;
    auto response = plat::http_get("https://api.github.com/repos/tylermcm/PocketDrop/releases/latest",
        {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"}, plat::BAD_FILE, {}, 15000);
    if (response.status != 200) {
        result.error = response.status == 404 ? "No published release is available yet." :
            "Couldn't check GitHub for updates. Try again later. HTTP " + std::to_string(response.status);
        return result;
    }
    return parseUpdateRelease(response.body);
}

UpdateRelease parseUpdateRelease(const std::string& body) {
    UpdateRelease result;
    try {
        using namespace winrt::Windows::Data::Json;
        auto json = JsonObject::Parse(winrt::to_hstring(body));
        result.tag = winrt::to_string(json.GetNamedString(L"tag_name"));
        std::array<int, 3> parsed{};
        if (!parseVersion(result.tag, parsed) || json.GetNamedBoolean(L"prerelease") || json.GetNamedBoolean(L"draft")) {
            result.error = "The latest release doesn't have a supported stable version.";
            return result;
        }
        result.newer = newerVersion(result.tag);
        for (auto value : json.GetNamedArray(L"assets")) {
            auto asset = value.GetObject();
            if (asset.GetNamedString(L"name") != L"PocketDrop-Setup.exe") continue;
            result.url = winrt::to_string(asset.GetNamedString(L"browser_download_url"));
            result.digest = winrt::to_string(asset.GetNamedString(L"digest", L""));
        }
    } catch (...) { result.error = "GitHub returned an unreadable release description."; }
    return result;
}

std::string downloadUpdate(const UpdateRelease& release, const std::atomic<bool>& cancel, std::string& error) {
    std::array<int, 3> parsed{};
    const std::string expected = "https://github.com/tylermcm/PocketDrop/releases/download/" + release.tag + "/PocketDrop-Setup.exe";
    if (!parseVersion(release.tag, parsed) || !newerVersion(release.tag) || release.url != expected ||
        release.digest.size() != 71 || release.digest.rfind("sha256:", 0) != 0 ||
        release.digest.substr(7).find_first_not_of("0123456789abcdef") != std::string::npos) {
        error = "This release has no verified Windows installer. Open its release page to download manually.";
        return {};
    }
    auto dir = util::path_join(plat::app_data_dir(), "Updates");
    plat::make_dir(dir);
    auto path = util::path_join(dir, "PocketDrop-" + release.tag + "-" + util::random_token(8) + "-Setup.exe");
    auto file = plat::file_create(path);
    if (file == plat::BAD_FILE) { error = "Couldn't create the update download."; return {}; }
    auto response = plat::http_get(release.url, {}, file,
        [&](uint64_t got, uint64_t) { return !cancel && got <= 256ull * 1024 * 1024; }, 30000);
    plat::file_close(file);
    if (response.status != 200 || cancel) {
        error = "The update download did not finish. Try again later.";
        plat::file_remove(path); return {};
    }
    if (!verifyUpdateFile(path, release.digest.substr(7), cancel)) {
        error = "The downloaded installer failed SHA-256 verification. It was removed.";
        plat::file_remove(path); return {};
    }
    return path;
}

bool verifyUpdateFile(const std::string& path, const std::string& expected, const std::atomic<bool>& cancel) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    auto input = plat::file_open_read(path);
    ok = ok && input != plat::BAD_FILE;
    std::vector<uint8_t> buffer(1 << 20);
    while (ok) {
        auto n = plat::file_read(input, buffer.data(), buffer.size());
        if (n <= 0) { ok = n == 0; break; }
        ok = !cancel && BCryptHashData(hash, buffer.data(), (ULONG)n, 0) >= 0;
    }
    if (input != plat::BAD_FILE) plat::file_close(input);
    uint8_t digest[32]{};
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    std::string hex;
    for (auto b : digest) { hex += "0123456789abcdef"[b >> 4]; hex += "0123456789abcdef"[b & 15]; }
    return ok && !cancel && hex == expected;
}
