#import <AppKit/AppKit.h>
extern "C" void gillInitialiseMacQualityHost() { @autoreleasepool { [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited]; } }
extern "C" bool gillClickMacQualityHost(void* native, double x, double y) {
    @autoreleasepool {
        NSView* root = (__bridge NSView*) native;
        NSWindow* window = root.window;
        if (!root || !window) return false;
        const NSRect bounds = root.bounds;
        NSPoint local = NSMakePoint(NSMinX(bounds) + x * NSWidth(bounds), NSMinY(bounds) + (root.isFlipped ? y : 1.0 - y) * NSHeight(bounds));
        NSView* target = [root hitTest:[root convertPoint:local toView:root.superview]];
        if (!target) return false;
        const NSPoint location = [root convertPoint:local toView:nil];
        const NSTimeInterval time = NSProcessInfo.processInfo.systemUptime;
        NSEvent* down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:location modifierFlags:0 timestamp:time windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1.0];
        NSEvent* up = [NSEvent mouseEventWithType:NSEventTypeLeftMouseUp location:location modifierFlags:0 timestamp:time + .01 windowNumber:window.windowNumber context:nil eventNumber:2 clickCount:1 pressure:0.0];
        [target mouseDown:down]; [target mouseUp:up]; return true;
    }
}
