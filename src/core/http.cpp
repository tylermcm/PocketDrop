#include "http.h"
#include "webpage.h"
#include <algorithm>
#include <cstdlib>
#include <condition_variable>
#include <map>
#include <unordered_map>

struct HttpServer::Transfer {
    uint64_t id = 0;
    std::string name, device;
    uint64_t total = 0;
    std::atomic<uint64_t> sent{0};
    std::atomic<bool> done{false};
    bool ok = false, viaTunnel = false;
    uint64_t start = 0, end = 0;
};

struct Req {
    std::string method, path, query;
    std::map<std::string, std::string> hdr;
    std::string h(const char* k) const {
        auto it = hdr.find(k);
        return it == hdr.end() ? std::string() : it->second;
    }
};

namespace {

const size_t IO_CHUNK = 1 << 20;
std::string g_pcName;
// Limits concurrent thumbnail renders (std::counting_semaphore isn't available on older macOS).
class Slots {
public:
    explicit Slots(int n) : n_(n) {}
    void acquire() {
        std::unique_lock<std::mutex> l(m_);
        cv_.wait(l, [&] { return n_ > 0; });
        n_--;
    }
    void release() {
        {
            std::lock_guard<std::mutex> l(m_);
            n_++;
        }
        cv_.notify_one();
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    int n_;
};
Slots g_thumbSlots(3);
std::mutex g_thumbMu;
std::unordered_map<std::string, std::shared_ptr<const std::vector<uint8_t>>> g_thumbs;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool readRequest(plat::Sock s, std::string& buf, Req& r) {
    for (;;) {
        size_t end = buf.find("\r\n\r\n");
        if (end != std::string::npos) {
            std::string head = buf.substr(0, end);
            buf.erase(0, end + 4);
            size_t le = head.find("\r\n");
            std::string first = head.substr(0, le);
            size_t a = first.find(' '), b = first.rfind(' ');
            if (a == std::string::npos || b <= a) return false;
            r = Req{};
            r.method = first.substr(0, a);
            std::string target = first.substr(a + 1, b - a - 1);
            size_t q = target.find('?');
            r.path = target.substr(0, q);
            if (q != std::string::npos) r.query = target.substr(q + 1);
            size_t pos = le == std::string::npos ? head.size() : le + 2;
            while (pos < head.size()) {
                size_t e = head.find("\r\n", pos);
                if (e == std::string::npos) e = head.size();
                std::string line = head.substr(pos, e - pos);
                size_t c = line.find(':');
                if (c != std::string::npos) r.hdr[util::lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
                pos = e + 2;
            }
            return true;
        }
        if (buf.size() > 32768) return false;
        char tmp[8192];
        int n = plat::tcp_recv(s, tmp, sizeof tmp);
        if (n <= 0) return false;
        buf.append(tmp, (size_t)n);
    }
}

bool sendAll(plat::Sock s, const char* p, size_t n, std::atomic<uint64_t>* counter = nullptr) {
    while (n) {
        int k = plat::tcp_send(s, p, (int)std::min(n, IO_CHUNK));
        if (k <= 0) return false;
        p += k;
        n -= (size_t)k;
        if (counter) *counter += (uint64_t)k;
    }
    return true;
}

bool sendAll(plat::Sock s, const std::string& str) { return sendAll(s, str.data(), str.size()); }

const char* reason(int code) {
    switch (code) {
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 301: return "Moved Permanently";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 416: return "Range Not Satisfiable";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

std::string header(int code, const std::string& type, uint64_t len, bool keep, const std::string& extra = {}) {
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + reason(code) + "\r\n";
    if (!type.empty()) h += "Content-Type: " + type + "\r\n";
    h += "Content-Length: " + std::to_string(len) + "\r\n";
    h += keep ? "Connection: keep-alive\r\nKeep-Alive: timeout=20\r\n" : "Connection: close\r\n";
    h += "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nServer: PocketDrop\r\n";
    h += extra;
    h += "\r\n";
    return h;
}

bool simple(plat::Sock s, int code, bool head, bool keep, const std::string& extra = {}) {
    std::string body = code >= 400 ? std::string(reason(code)) + "\n" : std::string();
    std::string h = header(code, body.empty() ? "" : "text/plain; charset=utf-8", body.size(), keep,
                           extra + "Cache-Control: no-store\r\n");
    return sendAll(s, head ? h : h + body) && keep;
}

// Single "bytes=" range. Returns false when unsatisfiable (-> 416).
bool parseRange(const std::string& v, uint64_t total, uint64_t& start, uint64_t& len, bool& partial) {
    partial = false;
    start = 0;
    len = total;
    if (v.rfind("bytes=", 0) != 0 || v.find(',') != std::string::npos) return true;
    std::string spec = v.substr(6);
    size_t dash = spec.find('-');
    if (dash == std::string::npos) return true;
    std::string a = trim(spec.substr(0, dash)), b = trim(spec.substr(dash + 1));
    if (total == 0) return false;
    uint64_t s, e;
    if (a.empty()) {
        if (b.empty()) return true;
        uint64_t n = std::min<uint64_t>(std::strtoull(b.c_str(), nullptr, 10), total);
        if (n == 0) return false;
        s = total - n;
        e = total - 1;
    } else {
        s = std::strtoull(a.c_str(), nullptr, 10);
        e = b.empty() ? total - 1 : std::min<uint64_t>(std::strtoull(b.c_str(), nullptr, 10), total - 1);
        if (s >= total || e < s) return false;
    }
    start = s;
    len = e - s + 1;
    partial = true;
    return true;
}

std::string extOf(const std::string& name) {
    size_t p = name.rfind('.');
    return p == std::string::npos ? std::string() : util::lower(name.substr(p + 1));
}

std::string mimeOf(const std::string& name) {
    static const std::unordered_map<std::string, const char*> m = {
        {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"}, {"gif", "image/gif"},
        {"webp", "image/webp"}, {"heic", "image/heic"}, {"heif", "image/heif"}, {"avif", "image/avif"},
        {"bmp", "image/bmp"}, {"svg", "image/svg+xml"}, {"tif", "image/tiff"}, {"tiff", "image/tiff"},
        {"mp4", "video/mp4"}, {"m4v", "video/x-m4v"}, {"mov", "video/quicktime"}, {"webm", "video/webm"},
        {"mkv", "video/x-matroska"}, {"3gp", "video/3gpp"}, {"mp3", "audio/mpeg"}, {"m4a", "audio/mp4"},
        {"aac", "audio/aac"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"}, {"opus", "audio/ogg"},
        {"flac", "audio/flac"}, {"pdf", "application/pdf"}, {"txt", "text/plain; charset=utf-8"},
        {"md", "text/plain; charset=utf-8"}, {"log", "text/plain; charset=utf-8"},
        {"csv", "text/csv; charset=utf-8"}, {"json", "application/json"}, {"xml", "application/xml"},
        {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"}, {"zip", "application/zip"},
        {"apk", "application/vnd.android.package-archive"}, {"ics", "text/calendar"}, {"vcf", "text/vcard"},
        {"epub", "application/epub+zip"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    };
    auto it = m.find(extOf(name));
    return it == m.end() ? "application/octet-stream" : it->second;
}

const char* kindOf(const std::string& name) {
    std::string e = extOf(name);
    auto in = [&](std::initializer_list<const char*> l) {
        for (auto x : l)
            if (e == x) return true;
        return false;
    };
    if (in({"jpg", "jpeg", "png", "gif", "webp", "heic", "heif", "avif", "bmp", "tif", "tiff", "svg", "dng", "cr2",
            "nef", "arw"}))
        return "image";
    if (in({"mp4", "m4v", "mov", "webm", "mkv", "avi", "3gp", "wmv", "mpg", "mpeg"})) return "video";
    if (in({"mp3", "m4a", "aac", "wav", "ogg", "opus", "flac", "wma", "aiff"})) return "audio";
    if (e == "pdf") return "pdf";
    if (in({"zip", "7z", "rar", "gz", "tgz", "bz2", "xz", "zst", "tar"})) return "archive";
    if (in({"txt", "md", "csv", "json", "xml", "log", "html", "htm", "css", "js", "ini", "yaml", "yml"}))
        return "text";
    return "file";
}

std::string disposition(const std::string& name, bool attach) {
    std::string ascii;
    for (unsigned char c : name) ascii.push_back(c >= 0x20 && c < 0x7F && c != '"' && c != '\\' ? (char)c : '_');
    return std::string("Content-Disposition: ") + (attach ? "attachment" : "inline") + "; filename=\"" + ascii +
           "\"; filename*=UTF-8''" + util::url_encode(name) + "\r\n";
}

std::string deviceFromUA(const std::string& ua) {
    if (ua.find("iPhone") != std::string::npos) return "iPhone";
    if (ua.find("iPad") != std::string::npos) return "iPad";
    if (ua.find("Android") != std::string::npos) return "Android";
    if (ua.find("Macintosh") != std::string::npos) return "Mac";
    if (ua.find("Windows") != std::string::npos) return "Windows PC";
    if (ua.find("Linux") != std::string::npos) return "Linux";
    return "Browser";
}

bool tokenMatch(const std::string& a, const char* b, size_t blen) {
    if (a.size() != blen) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < blen; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

std::string manifest(const Bundle* b, int rev) {
    std::string j = "{\"v\":1,\"rev\":" + std::to_string(rev) + ",\"pc\":\"" + util::json_escape(g_pcName) + "\"";
    if (!b) return j + ",\"ready\":true,\"prep\":1,\"total\":0,\"files\":[],\"texts\":[],\"zip\":{\"name\":\"\",\"size\":0,\"preferred\":false}}";
    bool ready = b->ready;
    double prep = ready ? 1.0 : b->totalBytes ? std::min(0.99, (double)b->prepDone / (double)b->totalBytes) : 0.0;
    char num[32];
    snprintf(num, sizeof num, "%.3f", prep);
    j += ",\"ready\":" + std::string(ready ? "true" : "false") + ",\"prep\":" + num +
         ",\"total\":" + std::to_string(b->totalBytes);
    j += ",\"zip\":{\"name\":\"" + util::json_escape(b->zipName) + "\",\"size\":" +
         std::to_string(ready ? b->zipBytes : 0) + ",\"preferred\":" + (b->zipPreferred ? "true" : "false") + "}";
    j += ",\"files\":[";
    for (size_t i = 0; i < b->files.size(); i++) {
        const auto& f = b->files[i];
        if (i) j += ",";
        j += "{\"i\":" + std::to_string(i) + ",\"name\":\"" + util::json_escape(f.name) + "\",\"path\":\"" +
             util::json_escape(f.rel) + "\",\"size\":" + std::to_string(f.size) + ",\"kind\":\"" + kindOf(f.name) +
             "\"" + (ready && f.missing ? ",\"missing\":true" : "") + "}";
    }
    j += "],\"texts\":[";
    for (size_t i = 0; i < b->texts.size(); i++) j += (i ? ",\"" : "\"") + util::json_escape(b->texts[i]) + "\"";
    return j + "]}";
}

bool streamFile(plat::Sock s, plat::File h, uint64_t offset, uint64_t len, std::atomic<uint64_t>* counter,
                std::vector<char>& buf, const std::atomic<bool>& running) {
    if (!plat::file_seek(h, offset)) return false;
    while (len) {
        size_t want = (size_t)std::min<uint64_t>(len, buf.size());
        int64_t got = plat::file_read(h, buf.data(), want);
        if (got <= 0) return false;
        if (!sendAll(s, buf.data(), (size_t)got, counter)) return false;
        len -= (uint64_t)got;
        if (!running) return false;
    }
    return true;
}

} // namespace

bool HttpServer::start(int preferredPort, bool loopbackOnly) {
    if (!plat::net_init()) return false;
    g_pcName = plat::host_name();
    for (int p : {preferredPort, preferredPort + 1, preferredPort + 2, 0}) {
        listen_ = plat::tcp_listen(p, loopbackOnly, &port_);
        if (listen_ != plat::BAD_SOCK) break;
    }
    if (listen_ == plat::BAD_SOCK) return false;
    running_ = true;
    acceptThread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void HttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (acceptThread_.joinable()) acceptThread_.join();
    plat::tcp_close(listen_);
    listen_ = plat::BAD_SOCK;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (plat::Sock c : clients_) plat::tcp_shutdown(c);
    }
    for (int i = 0; i < 300 && threads_ > 0; i++) plat::sleep_ms(10);
}

void HttpServer::publish(std::shared_ptr<Bundle> bundle, const std::string& token) {
    std::lock_guard<std::mutex> l(mu_);
    bundle_ = std::move(bundle);
    token_ = token;
    rev_++;
}

std::vector<TransferInfo> HttpServer::transfers() const {
    std::lock_guard<std::mutex> l(mu_);
    std::vector<TransferInfo> v;
    for (const auto& t : transfers_) {
        TransferInfo i;
        i.id = t->id;
        i.name = t->name;
        i.device = t->device;
        i.total = t->total;
        i.sent = t->sent;
        i.done = t->done;
        i.ok = t->ok;
        i.viaTunnel = t->viaTunnel;
        i.start = t->start;
        i.end = t->end;
        v.push_back(std::move(i));
    }
    return v;
}

std::string HttpServer::lastDevice() const {
    std::lock_guard<std::mutex> l(mu_);
    return lastDevice_;
}

std::shared_ptr<HttpServer::Transfer> HttpServer::begin(const std::string& name, const std::string& device,
                                                        uint64_t total, bool tunnel) {
    auto t = std::make_shared<Transfer>();
    t->name = name;
    t->device = device;
    t->total = total;
    t->viaTunnel = tunnel;
    t->start = plat::tick_ms();
    {
        std::lock_guard<std::mutex> l(mu_);
        t->id = nextId_++;
        transfers_.push_back(t);
        if (transfers_.size() > 24) {
            auto it = std::find_if(transfers_.begin(), transfers_.end(), [](auto& x) { return x->done.load(); });
            if (it != transfers_.end()) transfers_.erase(it);
        }
    }
    if (onEvent) onEvent(Event::TransferStart);
    return t;
}

void HttpServer::end(const std::shared_ptr<Transfer>& t, bool ok) {
    {
        std::lock_guard<std::mutex> l(mu_);
        t->ok = ok;
        t->end = plat::tick_ms();
        t->done = true;
    }
    if (onEvent) onEvent(Event::TransferEnd);
}

void HttpServer::acceptLoop() {
    while (running_) {
        plat::Sock c = plat::tcp_accept(listen_, 250);
        if (c == plat::BAD_SOCK) continue;
        if (!running_) {
            plat::tcp_close(c);
            break;
        }
        {
            std::lock_guard<std::mutex> l(mu_);
            clients_.insert(c);
        }
        threads_++;
        std::thread([this, c] {
            serve(c);
            threads_--;
        }).detach();
    }
}

void HttpServer::serve(plat::Sock s) {
    plat::tcp_configure(s, 25000);
    bool viaTunnel = plat::tcp_peer_is_loopback(s);
    std::string buf;
    Req r;
    while (running_ && readRequest(s, buf, r)) {
        if (!handle(s, r, viaTunnel)) break;
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        clients_.erase(s);
    }
    plat::tcp_close(s);
}

bool HttpServer::handle(plat::Sock s, const Req& r, bool viaTunnel) {
    bool head = r.method == "HEAD";
    bool keep = util::lower(r.h("connection")) != "close";
    if (r.method != "GET" && !head) return simple(s, 405, false, false);
    if (r.path == "/ping") return simple(s, 204, head, keep, "Access-Control-Allow-Origin: *\r\n");

    std::shared_ptr<Bundle> b;
    std::string token;
    int rev;
    {
        std::lock_guard<std::mutex> l(mu_);
        b = bundle_;
        token = token_;
        rev = rev_;
    }
    size_t tl = token.size();
    if (tl == 0 || r.path.size() < tl + 1 || !tokenMatch(token, r.path.data() + 1, tl) ||
        (r.path.size() > tl + 1 && r.path[tl + 1] != '/'))
        return simple(s, 404, head, keep);
    std::string rest = r.path.substr(tl + 1);
    std::string device = deviceFromUA(r.h("user-agent"));

    if (rest.empty()) return simple(s, 301, head, keep, "Location: /" + token + "/\r\n");

    if (rest == "/") {
        {
            std::lock_guard<std::mutex> l(mu_);
            lastDevice_ = device;
        }
        if (onEvent) onEvent(Event::Visit);
        const std::string& page = web_page();
        std::string h = header(200, "text/html; charset=utf-8", page.size(), keep,
                               "Cache-Control: no-store\r\nContent-Security-Policy: default-src 'none'; img-src 'self' "
                               "data: blob:; media-src 'self' blob:; style-src 'unsafe-inline'; script-src "
                               "'unsafe-inline'; connect-src 'self'; base-uri 'none'; form-action 'none'\r\n");
        return sendAll(s, head ? h : h + page) && keep;
    }

    if (rest == "/api/manifest") {
        std::string j = manifest(b.get(), rev);
        std::string h = header(200, "application/json; charset=utf-8", j.size(), keep, "Cache-Control: no-store\r\n");
        return sendAll(s, head ? h : h + j) && keep;
    }

    auto indexAfter = [&](size_t prefix) -> long long {
        size_t e = rest.find('/', prefix);
        std::string n = rest.substr(prefix, e == std::string::npos ? std::string::npos : e - prefix);
        if (n.empty() || n.size() > 9 || n.find_first_not_of("0123456789") != std::string::npos) return -1;
        long long i = atoll(n.c_str());
        return b && i < (long long)b->files.size() ? i : -1;
    };

    if (rest.rfind("/f/", 0) == 0) {
        long long i = indexAfter(3);
        if (i < 0) return simple(s, 404, head, keep);
        const BundleFile& f = b->files[(size_t)i];
        plat::File h = plat::file_open_read(f.path);
        if (h == plat::BAD_FILE) return simple(s, 404, head, keep);
        int64_t sz = plat::file_size(h);
        uint64_t total = sz > 0 ? (uint64_t)sz : 0, start, len;
        bool partial, download = r.query.find("dl=1") != std::string::npos;
        if (!parseRange(r.h("range"), total, start, len, partial)) {
            plat::file_close(h);
            return simple(s, 416, head, keep, "Content-Range: bytes */" + std::to_string(total) + "\r\n");
        }
        std::string mime = mimeOf(f.name);
        std::string extra = "Accept-Ranges: bytes\r\nCache-Control: private, no-cache\r\n" + disposition(f.name, download);
        if (partial)
            extra += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(start + len - 1) + "/" +
                     std::to_string(total) + "\r\n";
        if (!download && (mime.find("html") != std::string::npos || mime.find("svg") != std::string::npos ||
                          mime.find("xml") != std::string::npos))
            extra += "Content-Security-Policy: sandbox\r\n";
        bool ok = sendAll(s, header(partial ? 206 : 200, mime, len, keep, extra));
        if (ok && !head && len) {
            std::shared_ptr<Transfer> t;
            if (download && len >= (256 << 10)) t = begin(f.name, device, len, viaTunnel);
            std::vector<char> io(IO_CHUNK);
            ok = streamFile(s, h, start, len, t ? &t->sent : nullptr, io, running_);
            if (t) end(t, ok);
        }
        plat::file_close(h);
        return ok && keep;
    }

    if (rest.rfind("/zip/", 0) == 0) {
        if (!b || b->files.empty()) return simple(s, 404, head, keep);
        if (!b->ready) return simple(s, 503, head, keep, "Retry-After: 2\r\n");
        const ZipPlan& p = b->plan();
        uint64_t start, len;
        bool partial;
        if (!parseRange(r.h("range"), p.total, start, len, partial))
            return simple(s, 416, head, keep, "Content-Range: bytes */" + std::to_string(p.total) + "\r\n");
        std::string extra = "Accept-Ranges: bytes\r\nCache-Control: private, no-cache\r\n" + disposition(b->zipName, true);
        if (partial)
            extra += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(start + len - 1) + "/" +
                     std::to_string(p.total) + "\r\n";
        if (!sendAll(s, header(partial ? 206 : 200, "application/zip", len, keep, extra))) return false;
        if (head || !len) return keep;

        auto t = begin(b->zipName, device, len, viaTunnel);
        std::vector<char> io(IO_CHUNK);
        bool ok = true;
        uint64_t pos = 0, remaining = len;
        for (const ZipSegment& seg : p.segs) {
            if (!remaining) break;
            uint64_t segEnd = pos + seg.len;
            if (segEnd <= start) {
                pos = segEnd;
                continue;
            }
            uint64_t off = start > pos ? start - pos : 0;
            uint64_t n = std::min(seg.len - off, remaining);
            if (seg.mem) {
                ok = sendAll(s, (const char*)seg.mem + off, (size_t)n, &t->sent);
            } else {
                const BundleFile& f = b->files[(size_t)seg.file];
                plat::File h = plat::file_open_read(f.path);
                ok = h != plat::BAD_FILE && plat::file_size(h) == (int64_t)f.size &&
                     streamFile(s, h, off, n, &t->sent, io, running_);
                if (h != plat::BAD_FILE) plat::file_close(h);
            }
            if (!ok) break;
            remaining -= n;
            pos = segEnd;
        }
        end(t, ok);
        return ok && keep;
    }

    if (rest.rfind("/t/", 0) == 0) {
        long long i = indexAfter(3);
        if (i < 0) return simple(s, 404, head, keep);
        const BundleFile& f = b->files[(size_t)i];
        std::string kind = kindOf(f.name);
        if (kind != "image" && kind != "video" && kind != "pdf") return simple(s, 404, head, keep);
        std::string key = f.path + "|" + std::to_string(f.size) + "|" + std::to_string(f.dosDate) + "|" +
                          std::to_string(f.dosTime);
        std::shared_ptr<const std::vector<uint8_t>> jpg;
        {
            std::lock_guard<std::mutex> l(g_thumbMu);
            auto it = g_thumbs.find(key);
            if (it != g_thumbs.end()) jpg = it->second;
        }
        if (!jpg) {
            g_thumbSlots.acquire();
            auto data = plat::thumbnail_jpeg(f.path, 256);
            g_thumbSlots.release();
            jpg = std::make_shared<const std::vector<uint8_t>>(std::move(data));
            std::lock_guard<std::mutex> l(g_thumbMu);
            if (g_thumbs.size() > 2000) g_thumbs.clear();
            g_thumbs[key] = jpg;
        }
        if (jpg->empty()) return simple(s, 404, head, keep);
        std::string h = header(200, "image/jpeg", jpg->size(), keep, "Cache-Control: private, max-age=3600\r\n");
        if (!sendAll(s, h)) return false;
        return (head || sendAll(s, (const char*)jpg->data(), jpg->size())) && keep;
    }

    return simple(s, 404, head, keep);
}
