#import <AppKit/AppKit.h>
#import <IOBluetooth/IOBluetooth.h>
#import <IOBluetoothUI/IOBluetoothUI.h>

@interface PairingApp : NSObject <NSApplicationDelegate>
@property(strong) NSWindow *window;
@property(strong) NSTextField *status;
@property(strong) IOBluetoothPairingController *panel;
@property(strong) NSURL *logURL;
@property(copy) NSString *targetAddress;
@property(strong) IOBluetoothDevicePair *directPair;
@end

@implementation PairingApp
- (void)record:(NSString *)text {
    NSString *line = [NSString stringWithFormat:@"%@ %@\n", [NSDate date], text];
    NSFileHandle *file = [NSFileHandle fileHandleForWritingToURL:self.logURL error:nil];
    if (file) { [file seekToEndOfFile]; [file writeData:[line dataUsingEncoding:NSUTF8StringEncoding]]; [file closeFile]; }
    else { [line writeToURL:self.logURL atomically:YES encoding:NSUTF8StringEncoding error:nil]; }
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.targetAddress = nil;
    NSArray<NSString *> *arguments = NSProcessInfo.processInfo.arguments;
    NSUInteger index = [arguments indexOfObject:@"--address"];
    if (index != NSNotFound && index + 1 < arguments.count)
        self.targetAddress = [arguments[index + 1] stringByReplacingOccurrencesOfString:@"-" withString:@":"];
    self.logURL = [[[NSBundle mainBundle].bundleURL URLByDeletingLastPathComponent] URLByAppendingPathComponent:@"pairing-app-result.log"];
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,560,250) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"iAP2 iPhone Pairing";
    NSTextField *intro = [NSTextField wrappingLabelWithString:@"Keep the iPhone unlocked with Settings → Bluetooth open. Select your iPhone in Apple’s pairing dialog and compare any confirmation code on both devices."];
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
    [self performSelector:([arguments containsObject:@"--direct"] ? @selector(direct:) : @selector(pair:)) withObject:nil afterDelay:0.5];
}
- (void)direct:(id)sender {
    if (!self.targetAddress.length) {
        self.status.stringValue = @"Direct diagnostic pairing requires --address PHONE.";
        return;
    }
    IOBluetoothDevice *device = [IOBluetoothDevice deviceWithAddressString:self.targetAddress];
    self.directPair = [IOBluetoothDevicePair pairWithDevice:device];
    self.directPair.delegate = self;
    IOReturn result = [self.directPair start];
    self.status.stringValue = [NSString stringWithFormat:@"Programmatic pairing started: status 0x%x. Watch the iPhone for a code.", result];
    [self record:self.status.stringValue];
}
- (void)devicePairingUserConfirmationRequest:(id)sender numericValue:(BluetoothNumericValue)value {
    self.status.stringValue = [NSString stringWithFormat:@"Compare code %06u on the iPhone and tap Pair only if it matches.", value];
    [self record:self.status.stringValue];
    [self.directPair replyUserConfirmation:YES];
}
- (void)devicePairingFinished:(id)sender error:(IOReturn)error {
    [self record:[NSString stringWithFormat:@"Programmatic pairing finished: status 0x%x.", error]];
    [self check:nil];
}
- (void)pair:(id)sender {
    if (self.panel) return;
    self.panel = [IOBluetoothPairingController pairingController];
    if (!self.panel) { self.status.stringValue = @"Apple pairing controller unavailable."; [self record:self.status.stringValue]; return; }
    [self.panel setTitle:@"Pair iPhone for iAP2"];
    [self.panel setDescriptionText:@"Choose your iPhone. Keep its Bluetooth settings open."];
    [self.panel setOptions:kIOBluetoothServiceBrowserControllerOptionsAutoStartInquiry];
    [self.panel clearAllowedUUIDs];
    [self record:@"Opening Apple pairing dialog (no service filter)."];
    int result = [self.panel runModal];
    [self record:[NSString stringWithFormat:@"Apple pairing dialog returned %d; result count %lu.", result, (unsigned long)[self.panel getResults].count]];
    NSArray *results = [self.panel getResults];
    if (!self.targetAddress.length && results.count == 1)
        self.targetAddress = [[results[0] addressString] stringByReplacingOccurrencesOfString:@"-" withString:@":"];
    self.panel = nil;
    [self check:nil];
    if ([NSProcessInfo.processInfo.arguments containsObject:@"--once"]) [NSApp terminate:nil];
}
- (void)check:(id)sender {
    if (!self.targetAddress.length) {
        self.status.stringValue = @"Select an iPhone in the pairing dialog, or launch with --address PHONE.";
        return;
    }
    IOBluetoothDevice *phone = [IOBluetoothDevice deviceWithAddressString:self.targetAddress];
    BOOL paired = phone && phone.isPaired;
    BOOL listed = NO;
    for (IOBluetoothDevice *device in [IOBluetoothDevice pairedDevices]) {
        NSString *address = [[[device addressString] stringByReplacingOccurrencesOfString:@"-" withString:@":"] lowercaseString];
        if ([address isEqualToString:self.targetAddress.lowercaseString]) listed = YES;
    }
    self.status.stringValue = paired && listed ? @"The selected iPhone is paired and appears in macOS’s saved-device list. Ready to test iAP2." : @"The selected iPhone has no verified saved pairing yet. Pairing needs to finish before testing iAP2.";
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
