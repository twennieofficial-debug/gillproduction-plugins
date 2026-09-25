#import <AppKit/AppKit.h>
extern "C" void gillInitialiseMacTestApplication(){@autoreleasepool{[NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];}}
