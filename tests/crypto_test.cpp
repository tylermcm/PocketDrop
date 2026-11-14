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
    return fails ? 1 : 0;
}
