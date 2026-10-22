// XeBrightness: menu bar brightness slider for the Iris Xe panel.
// Talks to NootedGreen's kern.xe_brightness sysctl (PCH PWM backlight), so it needs no privileges.
// The level is kept in ~/.xe-bright and restored when the app starts (it is started at login by a LaunchAgent).
#import <Cocoa/Cocoa.h>
#include <sys/sysctl.h>

static NSString *const kKey = @"kern.xe_brightness";

static int readLevel(void) {
    int v = 0;
    size_t len = sizeof(v);
    if (sysctlbyname(kKey.UTF8String, &v, &len, NULL, 0) != 0) return -1;
    return v;
}

static BOOL writeLevel(int v) {
    if (v < 1) v = 1;
    if (v > 100) v = 100;
    return sysctlbyname(kKey.UTF8String, NULL, NULL, &v, sizeof(v)) == 0;
}

static NSString *statePath(void) { return [NSHomeDirectory() stringByAppendingPathComponent:@".xe-bright"]; }

@interface AppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>
@property (strong) NSStatusItem *item;
@property (strong) NSSlider *slider;
@property (strong) NSTextField *label;
@property (strong) NSTimer *saveTimer;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    // restore the remembered level (retry a few times: the kext's MMIO may not be ready right at login)
    NSString *saved = [NSString stringWithContentsOfFile:statePath() encoding:NSUTF8StringEncoding error:nil];
    int want = saved.intValue;
    if (want >= 1 && want <= 100) {
        __block int tries = 0;
        [NSTimer scheduledTimerWithTimeInterval:2.0 repeats:YES block:^(NSTimer *t) {
            if (writeLevel(want) || ++tries >= 15) [t invalidate];
        }];
    }

    self.item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    NSImage *icon = [NSImage imageWithSystemSymbolName:@"sun.max" accessibilityDescription:@"Brightness"];
    if (icon) { icon.template = YES; self.item.button.image = icon; }
    else self.item.button.title = @"☀";

    NSMenu *menu = [[NSMenu alloc] init];
    menu.delegate = self;

    NSView *box = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 230, 54)];
    NSTextField *title = [NSTextField labelWithString:@"Brightness"];
    title.frame = NSMakeRect(14, 32, 120, 17);
    title.font = [NSFont menuFontOfSize:0];
    self.label = [NSTextField labelWithString:@"--%"];
    self.label.frame = NSMakeRect(150, 32, 66, 17);
    self.label.alignment = NSTextAlignmentRight;
    self.label.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
    self.slider = [NSSlider sliderWithValue:50 minValue:1 maxValue:100 target:self action:@selector(sliderMoved:)];
    self.slider.frame = NSMakeRect(14, 8, 202, 22);
    self.slider.continuous = YES;
    [box addSubview:title];
    [box addSubview:self.label];
    [box addSubview:self.slider];
    NSMenuItem *sliderItem = [[NSMenuItem alloc] init];
    sliderItem.view = box;
    [menu addItem:sliderItem];

    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit XeBrightness" action:@selector(terminate:) keyEquivalent:@"q"];
    [menu addItem:quit];
    self.item.menu = menu;
}

- (void)refresh {
    int v = readLevel();
    if (v < 0) {
        self.label.stringValue = @"n/a";
        self.slider.enabled = NO;
        return;
    }
    self.slider.enabled = YES;
    self.slider.intValue = v;
    self.label.stringValue = [NSString stringWithFormat:@"%d%%", v];
}

- (void)menuWillOpen:(NSMenu *)menu { [self refresh]; }

- (void)sliderMoved:(NSSlider *)s {
    int v = s.intValue;
    if (writeLevel(v)) self.label.stringValue = [NSString stringWithFormat:@"%d%%", v];
    else self.label.stringValue = @"error";
    // write the state file shortly after the slider stops moving
    [self.saveTimer invalidate];
    self.saveTimer = [NSTimer scheduledTimerWithTimeInterval:0.6 repeats:NO block:^(NSTimer *t) {
        int cur = readLevel();
        if (cur >= 1) [[NSString stringWithFormat:@"%d\n", cur] writeToFile:statePath() atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }];
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyAccessory;   // no Dock icon
        AppDelegate *d = [[AppDelegate alloc] init];
        app.delegate = d;
        [app run];
    }
    return 0;
}
