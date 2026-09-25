#import <AppKit/AppKit.h>
#include <cstdio>
extern "C" void gillInitialiseMacMixHost() { @autoreleasepool { [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited]; } }
extern "C" bool gillPressMacMixHost(void* native, const char* buttonTitle) {
    @autoreleasepool {
      @try {
        NSView* root = (__bridge NSView*) native;
        if (![NSThread isMainThread] || !root || !root.window || !buttonTitle) return false;
        NSString* wanted = [NSString stringWithUTF8String:buttonTitle];
        if (!wanted) return false;

        // Cross the hosted NSView boundary through AppKit's native accessibility
        // tree. JUCE's press action queues triggerClick and the existing onClick.
        // No processor, parameter, MixBus, or private JUCE object is used.
        NSMutableArray* pending = [NSMutableArray arrayWithObject:root];
        NSMutableArray* matches = [NSMutableArray array];
        NSHashTable* visited = [NSHashTable hashTableWithOptions:
            NSPointerFunctionsStrongMemory | NSPointerFunctionsObjectPointerPersonality];
        while (pending.count != 0 && visited.count < 512) {
            id node = pending.lastObject;
            if ([visited containsObject:node]) { [pending removeLastObject]; continue; }
            [visited addObject:node];
            [pending removeLastObject];
            NSString* role = [node respondsToSelector:@selector(accessibilityRole)] ? [node accessibilityRole] : nil;
            NSString* title = [node respondsToSelector:@selector(accessibilityTitle)] ? [node accessibilityTitle] : nil;
            NSString* label = [node respondsToSelector:@selector(accessibilityLabel)] ? [node accessibilityLabel] : nil;
            if ([role isEqualToString:NSAccessibilityButtonRole]
                && ([title isEqualToString:wanted] || [label isEqualToString:wanted]))
                [matches addObject:node];
            if ([node respondsToSelector:@selector(accessibilityChildren)]) {
                NSArray* children = [node accessibilityChildren];
                if (children) [pending addObjectsFromArray:children];
            }
            // Native hosting containers may be ignored in the AX tree. Their
            // NSView descendants still expose the actual plugin's controls.
            if ([node isKindOfClass:[NSView class]])
                [pending addObjectsFromArray:[(NSView*) node subviews]];
        }
        std::fprintf(stderr, "APPKIT accessibility search %s: %lu nodes, %lu exact buttons\n",
                     buttonTitle, (unsigned long) visited.count, (unsigned long) matches.count);
        std::fflush(stderr);
        if (pending.count != 0 || matches.count != 1) return false;
        id button = matches.firstObject;
        if (![button respondsToSelector:@selector(isAccessibilityEnabled)] || ![button isAccessibilityEnabled]
            || ![button respondsToSelector:@selector(accessibilityPerformPress)]) return false;
        const BOOL pressed = [button accessibilityPerformPress];
        std::fprintf(stderr, "APPKIT accessibilityPerformPress %s: %s\n", buttonTitle, pressed ? "accepted" : "rejected");
        std::fflush(stderr);
        return pressed == YES;
      } @catch (NSException* exception) {
        std::fprintf(stderr, "APPKIT exception %s: %s\n", exception.name.UTF8String, exception.reason.UTF8String); std::fflush(stderr);
        @throw;
      }
    }
}
