#import <AppKit/AppKit.h>

// Test-host setup only. A DAW already provides NSApplication for the plugin.
// ScopedJuceInitialiser_GUI alone creates JUCE's CFRunLoop source but leaves
// NSApp nil in a console executable. In that state [NSApp run] returns without
// delivering queued parameter attachments or real editor timer callbacks.
bool initialiseGilleqTestApplication()
{
    @autoreleasepool
    {
        if (![NSThread isMainThread])
            return false;

        NSApplication* application = [NSApplication sharedApplication];
        // This test has no native windows and must not take focus from the user.
        [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
        return application != nil;
    }
}
