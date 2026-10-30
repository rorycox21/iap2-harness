#import <AppKit/AppKit.h>
#import <IOBluetooth/IOBluetooth.h>
#import <IOBluetoothUI/IOBluetoothUI.h>

@interface PairingApp : NSObject <NSApplicationDelegate>
@property(strong) NSWindow *window;
@property(strong) NSTextField *status;
@property(strong) IOBluetoothPairingController *panel;
@property(strong) NSURL *logURL;
@end

@implementation PairingApp
- (void)record:(NSString *)text {
    NSString *line = [NSString stringWithFormat:@"%@ %@\n", [NSDate date], text];
    NSFileHandle *file = [NSFileHandle fileHandleForWritingToURL:self.logURL error:nil];
    if (file) { [file seekToEndOfFile]; [file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]]; [file closeFile]; }
    else { [line writeToURL:self.logURL atomically:YES encoding:NSUTF8StringEncoding error:nil]; }
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.logURL = [[[NSBundle mainBundle].bundleURL URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"pairing-app-result.log"];
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,560,250) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"iAP2 iPhone Pairing";
    NSTextField *intro = [NSTextField wrappingLabelWithString:@"Keep the iPhone unlocked with Settings → Bluetooth open. Select Rory in Apple’s pairing dialog and compare any confirmation code on both devices."];
    intro.frame = NSMakeRect(24,160,512,65);
    [self.window.contentView addSubview:intro];
    self.status = [NSTextField wrappingLabelWithString:@"Ready. A matching code alone does not establish a saved pairing."];
    self.status.frame = NSMakeRect(24,72,512,65);
    [self.window.contentView addSubview:self.status];
    NSButton *pair = [NSButton buttonWithTitle:@"Open Apple pairing dialog" target:self action:@selector(pair:)];
    pair.frame = NSMakeRect(24,24,250,32);
    [self.window.contentView addSubview:pair];
    NSButton *check = [NSButton buttonWithTitle:@"Check saved pairing" target:self action:@selector(check:)];
    check.frame = NSMakeRect(294,24,240,32);
    [self.window.contentView addSubview:check];
    [self.window center]; [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self record:@"App launched with AppKit lifecycle and Apple IOBluetoothUI pairing controller."];
    [self performSelector:@selector(pair:) withObject:nil afterDelay:0.5];
}
- (void)pair:(id)sender {
    if (self.panel) return;
    self.panel = [IOBluetoothPairingController pairingController];
    if (!self.panel) { self.status.stringValue = @"Apple pairing controller unavailable."; [self record:self.status.stringValue]; return; }
    [self.panel setTitle:@"Pair iPhone for iAP2"];
    [self.panel setDescriptionText:@"Choose your iPhone (Rory). Keep its Bluetooth settings open."];
    [self.panel setOptions:kIOBluetoothServiceBrowserControllerOptionsAutoStartInquiry];
    [self.panel clearAllowedUUIDs];
    [self record:@"Opening Apple pairing dialog (no service filter)."];
    int result = [self.panel runModal];
    [self record:[NSString stringWithFormat:@"Apple pairing dialog returned %d; result count %lu.", result, (unsigned long)[self.panel getResults].count]];
    self.panel = nil;
    [self check:nil];
}
- (void)check:(id)sender {
    IOBluetoothDevice *phone = [IOBluetoothDevice deviceWithAddressString:@"4C:5D:6A:BE:D1:55"];
    BOOL paired = phone && phone.isPaired;
    BOOL listed = NO;
    for (IOBluetoothDevice *device in [IOBluetoothDevice pairedDevices]) {
        if ([[[device addressString] lowercaseString] isEqualToString:@"4c-5d-6a-be-d1-55"] ||
            [[[device addressString] lowercaseString] isEqualToString:@"4c:5d:6a:be:d1:55"]) listed = YES;
    }
    self.status.stringValue = paired && listed ? @"Rory is paired and appears in macOS’s saved-device list. Ready to test iAP2." : @"Rory has no verified saved pairing yet. Pairing needs to finish before testing iAP2.";
    [self record:[NSString stringWithFormat:@"Saved pairing check: isPaired=%d listed=%d.", paired, listed]];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app { return YES; }
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        PairingApp *delegate = [PairingApp new];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
