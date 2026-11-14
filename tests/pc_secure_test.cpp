// PC Share v2 pairing and framing (src/core/pc_secure.h).
#include "../src/core/pc_secure.h"
#include <iostream>

static int fails = 0;
static void expect(const char* name, bool ok) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << std::endl;
    if (!ok) fails++;
}

struct Pair {
    uint8_t skS[32], pkS[32], skR[32], pkR[32];
    pcsecure::Keys sender, receiver;
    bool ok = false;
    Pair() {
        pcsecure::make_keypair(skS, pkS);
        pcsecure::make_keypair(skR, pkR);
        ok = pcsecure::derive(skS, pkR, pkS, pkR, sender) && pcsecure::derive(skR, pkS, pkS, pkR, receiver);
    }
};

static std::vector<uint8_t> encode(const pcsecure::Keys& k, const std::vector<uint8_t>& data, size_t slice) {
    pcsecure::Encoder enc(k);
    std::vector<uint8_t> out;
    auto emit = [&](const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); return true; };
    for (size_t i = 0; i < data.size(); i += slice) enc.push(data.data() + i, std::min(slice, data.size() - i), emit);
    enc.finish(emit);
    return out;
}

static bool decode(const pcsecure::Keys& k, const std::vector<uint8_t>& wire, size_t slice, std::vector<uint8_t>& plain, bool* complete = nullptr) {
    pcsecure::Decoder dec(k);
    auto emit = [&](const uint8_t* p, size_t n) { plain.insert(plain.end(), p, p + n); return true; };
    bool ok = true;
    for (size_t i = 0; i < wire.size() && ok; i += slice) ok = dec.push(wire.data() + i, std::min(slice, wire.size() - i), emit);
    if (complete) *complete = dec.complete();
    return ok && dec.complete();
}

int main() {
    Pair p;
    expect("both sides derive keys", p.ok);
    expect("same key", std::memcmp(p.sender.key, p.receiver.key, 32) == 0);
    expect("same claim", p.sender.claim == p.receiver.claim && p.sender.claim.size() == 32);
    expect("same six-digit code", p.sender.code == p.receiver.code && p.sender.code.size() == 7 && p.sender.code[3] == ' ');

    // A person between the PCs gets a different key with each side, so the codes differ.
    uint8_t skM[32], pkM[32];
    pcsecure::make_keypair(skM, pkM);
    pcsecure::Keys atSender, atReceiver;
    bool a = pcsecure::derive(p.skS, pkM, p.pkS, pkM, atSender);   // sender talks to the attacker
    bool b = pcsecure::derive(p.skR, pkM, pkM, p.pkR, atReceiver); // receiver talks to the attacker
    expect("a man in the middle cannot show matching codes", a && b && atSender.code != atReceiver.code &&
                                                              std::memcmp(atSender.key, atReceiver.key, 32) != 0);
    uint8_t low[32] = {};
    pcsecure::Keys unused;
    expect("a low-order peer key is refused", !pcsecure::derive(p.skS, low, p.pkS, low, unused));

    // Round trips at awkward sizes and slice widths.
    bool roundTrips = true;
    for (size_t size : {size_t(0), size_t(1), pcsecure::FRAME - 1, pcsecure::FRAME, pcsecure::FRAME + 1, pcsecure::FRAME * 3 + 17}) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++) data[i] = (uint8_t)((i * 31 + size) & 255);
        auto wire = encode(p.sender, data, 1000);
        roundTrips = roundTrips && wire.size() == pcsecure::sealed_size(size);
        for (size_t slice : {size_t(1), size_t(7), size_t(4096), size_t(1 << 20)}) {
            if (slice == 1 && size > 70000) continue; // keep the one-byte feed quick
            std::vector<uint8_t> plain;
            roundTrips = roundTrips && decode(p.receiver, wire, slice, plain) && plain == data;
        }
    }
    expect("frames round trip and sealed_size is exact", roundTrips);

    std::vector<uint8_t> data(pcsecure::FRAME * 2 + 123, 0x5a);
    auto wire = encode(p.sender, data, 5000);
    {
        std::vector<uint8_t> plain;
        expect("data is not visible on the wire",
               std::search(wire.begin(), wire.end(), data.begin(), data.begin() + 64) == wire.end());
    }
    {
        auto bad = wire;
        bad[wire.size() / 2] ^= 1;
        std::vector<uint8_t> plain;
        expect("a changed byte is refused", !decode(p.receiver, bad, 4096, plain));
    }
    {
        std::vector<uint8_t> plain;
        std::vector<uint8_t> cut(wire.begin(), wire.end() - 20);
        bool complete = true;
        decode(p.receiver, cut, 4096, plain, &complete);
        expect("a stream cut before the closing frame is incomplete", !complete);
    }
    {
        // Swap the first two frames: valid on their own, wrong in order.
        size_t frame = pcsecure::HEADER + pcsecure::FRAME + crypto::AEAD_TAG;
        auto swapped = wire;
        std::copy(wire.begin() + frame, wire.begin() + 2 * frame, swapped.begin());
        std::copy(wire.begin(), wire.begin() + frame, swapped.begin() + frame);
        std::vector<uint8_t> plain;
        expect("reordered frames are refused", !decode(p.receiver, swapped, 4096, plain));
    }
    {
        // Drop a middle frame.
        size_t frame = pcsecure::HEADER + pcsecure::FRAME + crypto::AEAD_TAG;
        auto dropped = wire;
        dropped.erase(dropped.begin() + frame, dropped.begin() + 2 * frame);
        std::vector<uint8_t> plain;
        expect("a missing frame is refused", !decode(p.receiver, dropped, 4096, plain));
    }
    {
        auto extra = wire;
        extra.push_back(0);
        std::vector<uint8_t> plain;
        expect("trailing bytes after the closing frame are refused", !decode(p.receiver, extra, 4096, plain));
    }
    {
        Pair other;
        std::vector<uint8_t> plain;
        expect("another pairing's key cannot read it", !decode(other.receiver, wire, 4096, plain));
    }
    {
        std::vector<uint8_t> huge = {0xff, 0xff, 0xff, 0x7f};
        huge.resize(100, 0);
        std::vector<uint8_t> plain;
        expect("an oversized frame length is refused", !decode(p.receiver, huge, 100, plain));
    }
    {
        std::string info = "PDPC1\n1234\nholiday photos.zip";
        std::string sealed = pcsecure::seal_message(p.sender, info), plain;
        expect("info message round trip", pcsecure::open_message(p.receiver, sealed, plain) && plain == info);
        sealed[3] ^= 1;
        expect("changed info message refused", !pcsecure::open_message(p.receiver, sealed, plain));
    }
    return fails ? 1 : 0;
}
