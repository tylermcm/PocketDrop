// Emits QR images, deflate streams and a zip for tests/verify.py to validate.
#include "../src/core/bundle.h"
#include "../src/core/deflate.h"
#include "../src/core/qr.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

static void writeFile(const std::string& p, const uint8_t* d, size_t n) {
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)d, (std::streamsize)n);
}

static void writeQr(const std::string& p, const qr::Code& c) {
    const int s = 6, q = 4, n = (c.size + 2 * q) * s;
    std::ofstream f(p, std::ios::binary);
    f << "P5\n" << n << " " << n << "\n255\n";
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            int mx = x / s - q, my = y / s - q;
            bool dark = mx >= 0 && my >= 0 && mx < c.size && my < c.size && c.dark(mx, my);
            f.put(dark ? (char)0 : (char)255);
        }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: core_test <outdir> <folder> <file>\n";
        return 2;
    }
    std::string out = argv[1];
    std::mt19937 rng(1234);
    const char* printable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_./:?=&";

    std::vector<std::string> qs = {
        "HELLO",
        "http://192.168.1.23:53317/Ab3dEf-ghIJkLmNoPqRsTu/",
        "https://quiet-forest-lamp-river.trycloudflare.com/Ab3dEf-ghIJkLmNoPqRsTu/",
    };
    for (int len : {90, 160, 300, 600, 1000, 1500}) {
        std::string s;
        for (int i = 0; i < len; i++) s.push_back(printable[rng() % 70]);
        qs.push_back(s);
    }
    for (size_t i = 0; i < qs.size(); i++) {
        auto c = qr::encode(qs[i]);
        writeQr(out + "/qr_" + std::to_string(i) + ".pgm", c);
        std::ofstream(out + "/qr_" + std::to_string(i) + ".txt", std::ios::binary) << qs[i];
        std::cout << "qr " << i << " len=" << qs[i].size() << " version=" << (c.size - 17) / 4 << "\n";
    }

    std::vector<std::vector<uint8_t>> cases(7);
    cases[1] = {'a'};
    {
        const std::string lorem = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor. ";
        while (cases[2].size() < (4u << 20)) {
            cases[2].insert(cases[2].end(), lorem.begin(), lorem.end());
            std::string num = std::to_string(rng() % 100000) + "\n";
            cases[2].insert(cases[2].end(), num.begin(), num.end());
        }
    }
    for (int i = 0; i < 300000; i++) cases[3].push_back((uint8_t)rng());
    cases[4].assign(5u << 20, 0);
    for (int i = 0; i < 2000000; i++) cases[5].push_back((uint8_t)((rng() % 7) * (i % 13)));
    for (int i = 0; i < 200000; i++) cases[6].push_back(i % 3 == 0 ? (uint8_t)rng() : (uint8_t)(i & 0x1F));
    for (size_t i = 0; i < cases.size(); i++) {
        auto t0 = std::chrono::steady_clock::now();
        auto z = deflate_compress(cases[i].data(), cases[i].size());
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        writeFile(out + "/deflate_" + std::to_string(i) + ".raw", cases[i].data(), cases[i].size());
        writeFile(out + "/deflate_" + std::to_string(i) + ".def", z.data(), z.size());
        std::cout << "deflate " << i << " " << cases[i].size() << " -> " << z.size() << " in " << ms << " ms\n";
    }

    auto t0 = std::chrono::steady_clock::now();
    auto b = build_bundle({argv[2], argv[3]}, {}, nullptr);
    b->startPrepare(nullptr);
    while (!b->ready) plat::sleep_ms(5);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::ofstream z(out + "/bundle.zip", std::ios::binary);
    std::vector<char> buf;
    for (const auto& s : b->plan().segs) {
        if (s.mem) {
            z.write((const char*)s.mem, (std::streamsize)s.len);
        } else {
            plat::File f = plat::file_open_read(b->files[(size_t)s.file].path);
            buf.resize((size_t)s.len);
            size_t got = 0;
            while (got < buf.size()) {
                int64_t r = plat::file_read(f, buf.data() + got, buf.size() - got);
                if (r <= 0) break;
                got += (size_t)r;
            }
            plat::file_close(f);
            z.write(buf.data(), (std::streamsize)got);
        }
    }
    std::cout << "zip " << b->files.size() << " files, " << b->dirs.size() << " dirs, deflated=" << b->deflated
              << ", " << b->totalBytes << " -> " << b->zipBytes << " bytes, prep " << ms << " ms, name=" << b->zipName
              << "\n";
    return 0;
}
