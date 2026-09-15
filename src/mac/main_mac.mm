// macOS host: application, window, drag & drop, menu bar, and the Shell services the shared UI uses.
#import <AppKit/AppKit.h>
#include "../ui/ui.h"
#include "gfx_cg.h"

class MacShell;

@interface PDMenuTarget : NSObject
@property(nonatomic) NSInteger chosen;
- (void)pick:(NSMenuItem*)item;
@end

@implementation PDMenuTarget
- (void)pick:(NSMenuItem*)item {
    self.chosen = item.tag;
}
@end

@interface PDView : NSView <NSDraggingDestination>
@property(nonatomic, assign) Ui* ui;
@end

namespace {

NSString* s2ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }

std::string savePastedImage(NSImage* img) {
    CGImageRef cg = [img CGImageForProposedRect:nullptr context:nil hints:nil];
    if (!cg) return {};
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cg];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if (!png) return {};
    std::string dir = util::path_join(plat::app_data_dir(), "Pasted");
    plat::make_dir(dir);
    NSDateFormatter* fmt = [NSDateFormatter new];
    fmt.dateFormat = @"yyyy-MM-dd HH.mm.ss";
    std::string path = util::path_join(dir, "Pasted image " + std::string([fmt stringFromDate:[NSDate date]].UTF8String) + ".png");
    return [png writeToFile:s2ns(path) atomically:YES] ? path : std::string();
}

NSMenu* buildMenu(const std::vector<MenuItem>& items, PDMenuTarget* target) {
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
    menu.autoenablesItems = NO;
    for (const auto& it : items) {
        if (it.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle:s2ns(it.label) action:nil keyEquivalent:@""];
        if (!it.submenu.empty()) {
            mi.submenu = buildMenu(it.submenu, target);
        } else {
            mi.tag = it.id;
            mi.target = target;
            mi.action = @selector(pick:);
            mi.state = it.checked ? NSControlStateValueOn : NSControlStateValueOff;
            mi.enabled = it.enabled;
        }
        [menu addItem:mi];
    }
    return menu;
}

} // namespace

class MacShell : public Shell {
public:
    NSWindow* window = nil;
    PDView* view = nil;
    NSTimer* fastTimer = nil;
    Ui* ui = nullptr;

    void invalidate() override { view.needsDisplay = YES; }

    void setAnimating(bool on) override {
        if (on && !fastTimer) {
            Ui* u = ui;
            fastTimer = [NSTimer timerWithTimeInterval:1.0 / 30 repeats:YES block:^(NSTimer*) { u->tick(); }];
            [[NSRunLoop mainRunLoop] addTimer:fastTimer forMode:NSRunLoopCommonModes];
        } else if (!on && fastTimer) {
            [fastTimer invalidate];
            fastTimer = nil;
        }
    }

    void post(std::function<void()> fn) override {
        dispatch_async(dispatch_get_main_queue(), ^{ fn(); });
    }

    void clientSize(float& w, float& h) override {
        w = (float)view.bounds.size.width;
        h = (float)view.bounds.size.height;
    }

    void copyText(const std::string& text) override {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:s2ns(text) forType:NSPasteboardTypeString];
    }

    void readClipboard(std::vector<std::string>& paths, std::string& text) override {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ NSURL.class ]
                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
        if (urls.count) {
            for (NSURL* u in urls) paths.push_back(u.path.UTF8String);
            return;
        }
        if (NSString* s = [pb stringForType:NSPasteboardTypeString]) {
            text = s.UTF8String;
            return;
        }
        NSArray* images = [pb readObjectsForClasses:@[ NSImage.class ] options:nil];
        if (images.count) {
            std::string png = savePastedImage(images[0]);
            if (!png.empty()) paths.push_back(png);
        }
    }

    void browse(bool folders, std::function<void(const std::vector<std::string>&)> done) override {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = !folders;
        panel.canChooseDirectories = folders;
        panel.allowsMultipleSelection = YES;
        panel.message = folders ? @"Choose folders to share" : @"Choose files to share";
        [panel beginSheetModalForWindow:window
                      completionHandler:^(NSModalResponse result) {
                        if (result != NSModalResponseOK) return;
                        std::vector<std::string> list;
                        for (NSURL* u in panel.URLs) list.push_back(u.path.UTF8String);
                        done(list);
                      }];
    }

    void openUrl(const std::string& url) override {
        if (NSURL* u = [NSURL URLWithString:s2ns(url)]) [[NSWorkspace sharedWorkspace] openURL:u];
    }

    void chooseFolder(const std::string& title, std::function<void(const std::string&)> done) override {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = NO;
        panel.canChooseDirectories = YES;
        panel.canCreateDirectories = YES;
        panel.allowsMultipleSelection = NO;
        panel.message = s2ns(title);
        panel.prompt = @"Choose";
        [panel beginSheetModalForWindow:window
                      completionHandler:^(NSModalResponse result) {
                        if (result == NSModalResponseOK && panel.URL) done(panel.URL.path.UTF8String);
                      }];
    }

    void revealPath(const std::string& path) override {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:s2ns(path)] ]];
    }

    void attention() override {
        if (!NSApp.isActive) [NSApp requestUserAttention:NSInformationalRequest];
    }

    int popupMenu(const std::vector<MenuItem>& items, float x, float y) override {
        PDMenuTarget* target = [PDMenuTarget new];
        NSMenu* menu = buildMenu(items, target);
        NSPoint loc = NSMakePoint(x - menu.size.width, y);
        [menu popUpMenuPositioningItem:nil atLocation:loc inView:view];
        return (int)target.chosen;
    }

    void alert(const std::string& title, const std::string& message) override {
        NSAlert* a = [NSAlert new];
        a.messageText = s2ns(title);
        a.informativeText = s2ns(message);
        [a runModal];
    }

    int loadSetting(const char* key, int def) override {
        NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
        NSString* k = @(key);
        return [d objectForKey:k] ? (int)[d integerForKey:k] : def;
    }

    void saveSetting(const char* key, int value) override {
        [[NSUserDefaults standardUserDefaults] setInteger:value forKey:@(key)];
    }

    std::string loadString(const char* key, const std::string& def) override {
        NSString* v = [[NSUserDefaults standardUserDefaults] stringForKey:@(key)];
        return v ? std::string(v.UTF8String) : def;
    }

    void saveString(const char* key, const std::string& value) override {
        [[NSUserDefaults standardUserDefaults] setObject:s2ns(value) forKey:@(key)];
    }

    void setTopmost(bool on) override { window.level = on ? NSFloatingWindowLevel : NSNormalWindowLevel; }

    std::string cleanPath(const std::string& path) override {
        NSString* p = s2ns(path).stringByStandardizingPath;
        if (!p.absolutePath)
            p = [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:p].stringByStandardizingPath;
        while (p.length > 1 && [p hasSuffix:@"/"]) p = [p substringToIndex:p.length - 1];
        return [[NSFileManager defaultManager] fileExistsAtPath:p] ? std::string(p.UTF8String) : std::string();
    }
};

@implementation PDView {
    CGGfx _gfx;
    NSTrackingArea* _tracking;
}

- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (void)drawRect:(NSRect)dirty {
    if (!self.ui) return;
    CGContextRef ctx = NSGraphicsContext.currentContext.CGContext;
    CGFloat scale = self.window ? self.window.backingScaleFactor : 2.0;
    _gfx.begin(ctx, (float)self.bounds.size.width, (float)self.bounds.size.height, (float)scale);
    self.ui->paint(_gfx);
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways |
                     NSTrackingInVisibleRect | NSTrackingCursorUpdate
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (NSPoint)point:(NSEvent*)e {
    return [self convertPoint:e.locationInWindow fromView:nil];
}

- (void)updateCursor {
    [(self.ui && self.ui->wantsPointer() ? NSCursor.pointingHandCursor : NSCursor.arrowCursor) set];
}

- (void)mouseMoved:(NSEvent*)e {
    NSPoint p = [self point:e];
    self.ui->mouseMove((float)p.x, (float)p.y);
    [self updateCursor];
}
- (void)mouseDragged:(NSEvent*)e {
    [self mouseMoved:e];
}
- (void)cursorUpdate:(NSEvent*)e {
    [self updateCursor];
}
- (void)mouseExited:(NSEvent*)e {
    self.ui->mouseLeave();
    [NSCursor.arrowCursor set];
}
- (void)mouseDown:(NSEvent*)e {
    NSPoint p = [self point:e];
    self.ui->mouseDown((float)p.x, (float)p.y);
}
- (void)mouseUp:(NSEvent*)e {
    NSPoint p = [self point:e];
    self.ui->mouseUp((float)p.x, (float)p.y);
    [self updateCursor];
}
- (void)scrollWheel:(NSEvent*)e {
    CGFloat dy = e.scrollingDeltaY;
    self.ui->wheel((float)(e.hasPreciseScrollingDeltas ? dy / 44.0 : dy));
}

- (BOOL)canTake:(id<NSDraggingInfo>)info {
    NSPasteboard* pb = info.draggingPasteboard;
    return [pb canReadObjectForClasses:@[ NSURL.class ] options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}] ||
           [pb availableTypeFromArray:@[ NSPasteboardTypeString ]] != nil;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info {
    BOOL ok = [self canTake:info];
    self.ui->setDragOver(ok);
    return ok ? NSDragOperationCopy : NSDragOperationNone;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info {
    return [self canTake:info] ? NSDragOperationCopy : NSDragOperationNone;
}
- (void)draggingExited:(id<NSDraggingInfo>)info {
    self.ui->setDragOver(false);
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)info {
    self.ui->setDragOver(false);
    NSPasteboard* pb = info.draggingPasteboard;
    NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ NSURL.class ]
                                              options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    if (urls.count) {
        std::vector<std::string> list;
        for (NSURL* u in urls) list.push_back(u.path.UTF8String);
        self.ui->addPaths(list);
        return YES;
    }
    if (NSString* s = [pb stringForType:NSPasteboardTypeString]) {
        self.ui->addText(s.UTF8String);
        return YES;
    }
    return NO;
}
@end

@interface PDAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation PDAppDelegate {
    MacShell* _shell;
    Ui* _ui;
    NSWindow* _window;
    NSTimer* _slowTimer;
    std::vector<std::string> _pending;
    BOOL _launched;
}

- (void)applicationWillFinishLaunching:(NSNotification*)n {
    [self buildMenuBar];
}

- (void)applicationDidFinishLaunching:(NSNotification*)n {
    NSRect content = NSMakeRect(0, 0, 385, 722); // ~385x750 with the title bar, matching Windows
    _window = [[NSWindow alloc] initWithContentRect:content
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"PocketDrop";
    _window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    _window.titlebarAppearsTransparent = YES;
    _window.backgroundColor = [NSColor colorWithSRGBRed:0x0F / 255.0 green:0x0F / 255.0 blue:0x13 / 255.0 alpha:1];
    _window.releasedWhenClosed = NO;

    PDView* view = [[PDView alloc] initWithFrame:content];
    [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
    _window.contentView = view;

    _shell = new MacShell();
    _shell->window = _window;
    _shell->view = view;
    _ui = new Ui(*_shell);
    _shell->ui = _ui;
    view.ui = _ui;
    _ui->start();

    Ui* ui = _ui;
    _slowTimer = [NSTimer timerWithTimeInterval:0.5 repeats:YES block:^(NSTimer*) { ui->tick(); }];
    [[NSRunLoop mainRunLoop] addTimer:_slowTimer forMode:NSRunLoopCommonModes];

    // Paths passed on the command line (e.g. `PocketDrop.app/Contents/MacOS/PocketDrop file ...`).
    NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
    for (NSUInteger i = 1; i < args.count; i++)
        if (![args[i] hasPrefix:@"-"] && [[NSFileManager defaultManager] fileExistsAtPath:args[i]])
            _pending.push_back(args[i].UTF8String);
    _launched = YES;
    if (!_pending.empty()) {
        _ui->addPaths(_pending);
        _pending.clear();
    }

    [_window center];
    [_window makeKeyAndOrderFront:nil];
    [_window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];
}

// Files dropped on the Dock icon, "Open With", or `open -a PocketDrop file`.
- (void)application:(NSApplication*)app openURLs:(NSArray<NSURL*>*)urls {
    std::vector<std::string> list;
    for (NSURL* u in urls)
        if (u.isFileURL) list.push_back(u.path.UTF8String);
    if (_launched) {
        _ui->addPaths(list);
        [_window makeKeyAndOrderFront:nil];
    } else {
        _pending.insert(_pending.end(), list.begin(), list.end());
    }
}

- (void)applicationDidBecomeActive:(NSNotification*)n {
    if (_ui) _ui->refreshNetwork();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)n {
    [_slowTimer invalidate];
    if (_ui) _ui->shutdown(); // stops the server and cloudflared
}

- (void)addFiles:(id)sender {
    _ui->browse(false);
}
- (void)addFolder:(id)sender {
    _ui->browse(true);
}
- (void)pasteItems:(id)sender {
    _ui->paste();
}
- (void)copyLink:(id)sender {
    _ui->copyLink();
}

- (void)buildMenuBar {
    NSMenu* bar = [NSMenu new];

    NSMenuItem* appItem = [NSMenuItem new];
    NSMenu* appMenu = [NSMenu new];
    [appMenu addItemWithTitle:@"About PocketDrop" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide PocketDrop" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit PocketDrop" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [bar addItem:appItem];

    NSMenuItem* fileItem = [NSMenuItem new];
    NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    [fileMenu addItemWithTitle:@"Add Files…" action:@selector(addFiles:) keyEquivalent:@"o"].target = self;
    NSMenuItem* folder = [fileMenu addItemWithTitle:@"Add Folder…" action:@selector(addFolder:) keyEquivalent:@"O"];
    folder.target = self;
    [fileMenu addItem:[NSMenuItem separatorItem]];
    [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    fileItem.submenu = fileMenu;
    [bar addItem:fileItem];

    NSMenuItem* editItem = [NSMenuItem new];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editMenu addItemWithTitle:@"Copy Link" action:@selector(copyLink:) keyEquivalent:@"c"].target = self;
    [editMenu addItemWithTitle:@"Paste" action:@selector(pasteItems:) keyEquivalent:@"v"].target = self;
    editItem.submenu = editMenu;
    [bar addItem:editItem];

    NSMenuItem* windowItem = [NSMenuItem new];
    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    windowItem.submenu = windowMenu;
    [bar addItem:windowItem];
    NSApp.windowsMenu = windowMenu;

    NSApp.mainMenu = bar;
}
@end

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        PDAppDelegate* delegate = [PDAppDelegate new];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
