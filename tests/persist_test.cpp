// Round trips and hostile input for src/core/persist.h.
#include "../src/core/persist.h"
#include <iostream>

static int fails = 0;
static void expect(const char* name, bool ok) {
    std::cout << (ok ? "OK   " : "FAIL ") << name << std::endl;
    if (!ok) fails++;
}

int main() {
    using persist::HistoryEntry;
    std::vector<HistoryEntry> in;
    HistoryEntry a;
    a.kind = HistoryEntry::FileIn;
    a.time = 1760000000;
    a.size = 12345;
    a.name = "photo\twith\ttabs\nand newline.jpg";
    a.device = "Pixel \\ 9";
    a.path = "/home/x/Downloads/PocketDrop/photo.jpg";
    in.push_back(a);
    HistoryEntry n;
    n.kind = HistoryEntry::NoteIn;
    n.time = 1760000100;
    n.device = "iPhone";
    n.text = "line one\nline two\twith tab\r\nand a \\ backslash";
    in.push_back(n);
    auto text = persist::serialize(in);
    expect("one line per record", std::count(text.begin(), text.end(), '\n') == 2);
    auto out = persist::parse_history(text);
    expect("history round trip", out.size() == 2 && out[0].name == a.name && out[0].device == a.device && out[0].path == a.path &&
                                     out[0].size == 12345 && out[0].time == 1760000000 && out[1].text == n.text &&
                                     out[1].kind == HistoryEntry::NoteIn);

    expect("garbage lines are skipped", persist::parse_history("hello\nx\t1\t2\n\n").empty());
    expect("bad kind skipped", persist::parse_history("z\t1\t2\ta\tb\tc\td\n").empty());
    expect("non-numeric time skipped", persist::parse_history("f\tabc\t2\ta\tb\tc\td\n").empty());

    std::vector<HistoryEntry> many;
    for (int i = 0; i < 250; i++) {
        HistoryEntry e;
        e.time = i;
        e.name = "f" + std::to_string(i);
        many.push_back(e);
    }
    auto kept = persist::parse_history(persist::serialize(many));
    expect("history is capped to the newest entries", kept.size() == persist::MAX_HISTORY && kept.back().name == "f249" &&
                                                         kept.front().name == "f150");
    HistoryEntry longNote;
    longNote.kind = HistoryEntry::NoteIn;
    longNote.text = std::string(5000, 'n');
    auto trimmed = persist::parse_history(persist::serialize({longNote}));
    expect("long notes are cut", trimmed.size() == 1 && trimmed[0].text.size() == persist::MAX_NOTE_KEPT);

    TrustedDevice d;
    d.id = "abc123";
    d.secret = std::string(48, 'e');
    d.name = "Phone\twith tab";
    d.created = 100;
    d.lastUsed = 200;
    auto devices = persist::parse_devices(persist::serialize({d}));
    expect("devices round trip", devices.size() == 1 && devices[0].id == d.id && devices[0].secret == d.secret &&
                                     devices[0].name == d.name && devices[0].created == 100 && devices[0].lastUsed == 200);
    expect("bad device lines skipped", persist::parse_devices("a\tb\tc\n").empty());

    expect("ago just now", persist::ago(5) == "just now");
    expect("ago minutes", persist::ago(300) == "5 min ago");
    expect("ago hours", persist::ago(3 * 3600) == "3 h ago");
    expect("ago days", persist::ago(2 * 86400 + 3600) == "2 d ago");
    expect("ago negative (clock moved)", persist::ago(-50) == "just now");
    return fails ? 1 : 0;
}
