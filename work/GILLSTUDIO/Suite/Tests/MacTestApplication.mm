#import <AppKit/AppKit.h>
extern "C" void gillInitialiseMacTestApplication() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
    }
}
extern "C" void gillPumpMacTestEvents() {
    @autoreleasepool {
        for (int i = 0; i < 512; ++i) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!event) break;
            [NSApp sendEvent:event];
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.001]];
    }
}
