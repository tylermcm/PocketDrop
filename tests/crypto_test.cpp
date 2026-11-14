// Known-answer tests for src/core/crypto.h (FIPS 180-4, RFC 4231).
#include "../src/core/crypto.h"
#include <iostream>

static int fails = 0;
static void expect(const char* name, bool ok) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << std::endl;
    if (!ok) fails++;
}

int main() {
    expect("sha256 empty", crypto::to_hex(crypto::sha256("")) ==
                               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect("sha256 abc", crypto::to_hex(crypto::sha256("abc")) ==
                             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect("sha256 two blocks",
           crypto::to_hex(crypto::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    crypto::Sha256 million;
    std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; i++) million.update(chunk);
    expect("sha256 one million a", crypto::to_hex(million.finish()) ==
                                       "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Split updates must match a single update across the block boundary.
    crypto::Sha256 split;
    std::string text(200, 'x');
    split.update(text.data(), 63);
    split.update(text.data() + 63, 2);
    split.update(text.data() + 65, 135);
    expect("sha256 split updates", crypto::to_hex(split.finish()) == crypto::to_hex(crypto::sha256(text)));

    std::string key1(20, '\x0b');
    expect("hmac rfc4231 case 1", crypto::to_hex(crypto::hmac_sha256(key1, "Hi There")) ==
                                      "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    expect("hmac rfc4231 case 2", crypto::to_hex(crypto::hmac_sha256("Jefe", "what do ya want for nothing?")) ==
                                      "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    std::string key6(131, '\xaa');
    expect("hmac rfc4231 case 6 (long key)",
           crypto::to_hex(crypto::hmac_sha256(key6, "Test Using Larger Than Block-Size Key - Hash Key First")) ==
               "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    expect("constant-time compare", crypto::equal_constant_time("abc", "abc") && !crypto::equal_constant_time("abc", "abd") &&
                                        !crypto::equal_constant_time("abc", "ab"));
    // X25519, RFC 7748 section 6.1 and a clamping vector.
    auto unhex = [](const std::string& h) {
        std::vector<uint8_t> out;
        for (size_t i = 0; i + 1 < h.size(); i += 2) out.push_back((uint8_t)std::stoi(h.substr(i, 2), nullptr, 16));
        return out;
    };
    auto hex = [](const uint8_t* p, size_t n) { return crypto::to_hex(p, n); };
    auto alice = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto bob = unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    uint8_t pa[32], pb[32], k1[32], k2[32];
    crypto::x25519_public(pa, alice.data());
    crypto::x25519_public(pb, bob.data());
    expect("x25519 alice public", hex(pa, 32) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    expect("x25519 bob public", hex(pb, 32) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    crypto::x25519(k1, alice.data(), pb);
    crypto::x25519(k2, bob.data(), pa);
    expect("x25519 shared secret", hex(k1, 32) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742" && hex(k2, 32) == hex(k1, 32));
    auto c = unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
    auto u = unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
    uint8_t r[32];
    crypto::x25519(r, c.data(), u.data());
    expect("x25519 rfc7748 vector 1", hex(r, 32) == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");
    uint8_t zero[32] = {}, sink[32];
    expect("x25519 refuses a low-order point", !crypto::x25519(sink, alice.data(), zero));

    // ChaCha20-Poly1305, RFC 8439 section 2.8.2.
    std::vector<uint8_t> key(32);
    for (int i = 0; i < 32; i++) key[(size_t)i] = (uint8_t)(0x80 + i);
    auto nonce = unhex("070000004041424344454647");
    auto aad = unhex("50515253c0c1c2c3c4c5c6c7");
    std::string quote = "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it.";
    std::vector<uint8_t> sealed(quote.size() + 16);
    crypto::aead_seal(sealed.data(), key.data(), nonce.data(), aad.data(), aad.size(), (const uint8_t*)quote.data(), quote.size());
    expect("aead rfc8439 vector",
           hex(sealed.data(), sealed.size()) ==
               "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691");
    std::vector<uint8_t> opened(quote.size());
    expect("aead opens", crypto::aead_open(opened.data(), key.data(), nonce.data(), aad.data(), aad.size(), sealed.data(), quote.size()) &&
                             std::string(opened.begin(), opened.end()) == quote);
    auto damaged = sealed;
    damaged[5] ^= 1;
    expect("aead rejects a changed byte", !crypto::aead_open(opened.data(), key.data(), nonce.data(), aad.data(), aad.size(), damaged.data(), quote.size()));
    damaged = sealed;
    damaged.back() ^= 0x80;
    expect("aead rejects a changed tag", !crypto::aead_open(opened.data(), key.data(), nonce.data(), aad.data(), aad.size(), damaged.data(), quote.size()));
    expect("aead rejects other associated data", !crypto::aead_open(opened.data(), key.data(), nonce.data(), aad.data(), aad.size() - 1, sealed.data(), quote.size()));
    // Odd lengths against the reference implementation (key 0..31, nonce 0..11, aad "hdr").
    std::vector<uint8_t> k2b(32), n2b(12);
    for (int i = 0; i < 32; i++) k2b[(size_t)i] = (uint8_t)i;
    for (int i = 0; i < 12; i++) n2b[(size_t)i] = (uint8_t)i;
    struct { size_t n; const char* tail; } cases[] = {{0, "b893326ad7a430db7c9e28273736336f"}, {1, "630929d48d545666cc996cc371b3d2b5"},
        {15, "1d5319d03900d647deabaf0558af6f15"}, {16, "3c4f2254376453779043049e95903433"}, {17, "3784acd0f0b2e881178be5d5f94fb0d1"},
        {64, "f81f0db9d3f8e32b117f36bce0a42aa5"}, {65, "32d347f4cd201111ce0be3bc3e4de7c2"}, {1000, "b75c186694c58275c1cf1b953c14fe67"}};
    bool lengthsOk = true;
    for (auto& cs : cases) {
        std::vector<uint8_t> plain(cs.n), out(cs.n + 16);
        for (size_t i = 0; i < cs.n; i++) plain[i] = (uint8_t)((i * 7) & 255);
        crypto::aead_seal(out.data(), k2b.data(), n2b.data(), (const uint8_t*)"hdr", 3, plain.data(), cs.n);
        lengthsOk = lengthsOk && hex(out.data() + out.size() - 16, 16) == cs.tail;
    }
    expect("aead tags at odd lengths match the reference", lengthsOk);
    return fails ? 1 : 0;
}
