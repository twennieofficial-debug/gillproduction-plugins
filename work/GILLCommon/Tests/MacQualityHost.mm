#import <AppKit/AppKit.h>
#include <cstdio>
extern "C" void gillInitialiseMacQualityHost() { @autoreleasepool { [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited]; } }
extern "C" bool gillClickMacQualityHost(void* native, double x, double y) {
    @autoreleasepool {
      @try {
        NSView* root = (__bridge NSView*) native;
        NSWindow* window = root.window;
        if (!root || !window) return false;
        const NSRect bounds = root.bounds;
        NSPoint local = NSMakePoint(NSMinX(bounds) + x * NSWidth(bounds), NSMinY(bounds) + (root.isFlipped ? y : 1.0 - y) * NSHeight(bounds));
        std::fprintf(stderr, "APPKIT hitTest begin\n"); std::fflush(stderr);
        NSView* target = [root hitTest:[root convertPoint:local toView:root.superview]];
        std::fprintf(stderr, "APPKIT hitTest end\n"); std::fflush(stderr);
        if (!target) return false;
        const NSPoint location = [root convertPoint:local toView:nil];
        const NSTimeInterval time = NSProcessInfo.processInfo.systemUptime;
        NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location modifierFlags:0 timestamp:time windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
        NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location modifierFlags:0 timestamp:time + .01 windowNumber:window.windowNumber context:nil eventNumber:2 clickCount:1 pressure:0.0];
        std::fprintf(stderr, "APPKIT mouseDown begin\n"); std::fflush(stderr);
        [target mouseDown:down];
        std::fprintf(stderr, "APPKIT mouseDown end; mouseUp begin\n"); std::fflush(stderr);
        [target mouseUp:up];
        std::fprintf(stderr, "APPKIT mouseUp end\n"); std::fflush(stderr); return true;
      } @catch (NSException* exception) {
        std::fprintf(stderr, "APPKIT exception %s: %s\n", exception.name.UTF8String, exception.reason.UTF8String); std::fflush(stderr);
        @throw;
      }
    }
}
