#include "../src/win/update_win.h"
#include "../src/core/platform.h"
#include <cassert>
#include <fstream>
#include <iostream>

int main() {
    auto stable = parseUpdateRelease(R"({"tag_name":"v2.10.0","prerelease":false,"draft":false,"assets":[{"name":"PocketDrop-Setup.exe","browser_download_url":"https://github.com/tylermcm/PocketDrop/releases/download/v2.10.0/PocketDrop-Setup.exe","digest":"sha256:abc"}]})");
    assert(stable.error.empty() && stable.newer && stable.tag == "v2.10.0");
    assert(!parseUpdateRelease("invalid json").error.empty());
    assert(!parseUpdateRelease(R"({"tag_name":"v3.0.0-beta","prerelease":true,"draft":false,"assets":[]})").error.empty());
    assert(!parseUpdateRelease(R"({"tag_name":"v3.0.0","prerelease":false,"draft":true,"assets":[]})").error.empty());
    assert(!parseUpdateRelease(R"({"tag_name":"../../other","prerelease":false,"draft":false,"assets":[]})").error.empty());
    std::atomic<bool> cancel{false};
    auto path = std::string("build/update-hash-test.txt");
    std::ofstream(path, std::ios::binary) << "abc";
    const std::string digest = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    assert(verifyUpdateFile(path, digest, cancel));
    std::ofstream(path, std::ios::binary) << "abd";
    assert(!verifyUpdateFile(path, digest, cancel));
    cancel = true;
    assert(!verifyUpdateFile(path, digest, cancel));
    std::string error;
    assert(downloadUpdate(stable, cancel, error).empty() && !error.empty());
    stable.url = "https://example.com/malicious.exe";
    stable.digest = "sha256:" + digest;
    assert(downloadUpdate(stable, cancel, error).empty());
    plat::file_remove(path);
    std::cout << "Release parsing, update URL restrictions, hash corruption and cancellation checks passed\n";
}
