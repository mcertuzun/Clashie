#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "pebble/platform/platform.h"

// --- Cocoa Window Implementation ---

@interface PebbleAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, assign) bool shouldClose;
@end

@implementation PebbleAppDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end

@interface PebbleWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) bool* closeFlag;
@end

@implementation PebbleWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    if (self.closeFlag) *self.closeFlag = true;
    return YES;
}
@end

struct CocoaWindowData {
    NSWindow* window;
    MTKView* metalView;
    id<MTLDevice> device;
    PebbleAppDelegate* appDelegate;
    PebbleWindowDelegate* windowDelegate;
    bool shouldClose;
};

namespace pebble::platform {

WindowHandle window_create(const WindowConfig& config) {
    @autoreleasepool {
        [NSApplication sharedApplication];

        auto* data = new CocoaWindowData();
        data->shouldClose = false;

        // App delegate
        data->appDelegate = [[PebbleAppDelegate alloc] init];
        [NSApp setDelegate:data->appDelegate];

        // Metal device
        data->device = MTLCreateSystemDefaultDevice();
        if (!data->device) {
            log_error("Metal is not supported on this device");
            delete data;
            return 0;
        }

        // Window
        NSRect frame = NSMakeRect(100, 100, config.width, config.height);
        NSWindowStyleMask style = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable
                                | NSWindowStyleMaskResizable;

        data->window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];

        [data->window setTitle:[NSString stringWithUTF8String:config.title]];
        [data->window center];

        // Window delegate (close detection)
        data->windowDelegate = [[PebbleWindowDelegate alloc] init];
        data->windowDelegate.closeFlag = &data->shouldClose;
        [data->window setDelegate:data->windowDelegate];

        // Metal view
        data->metalView = [[MTKView alloc] initWithFrame:frame device:data->device];
        data->metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        data->metalView.depthStencilPixelFormat = MTLPixelFormatDepth32Float;
        data->metalView.preferredFramesPerSecond = 60;
        data->metalView.enableSetNeedsDisplay = NO;
        data->metalView.paused = YES; // We drive rendering manually

        [data->window setContentView:data->metalView];
        [data->window makeKeyAndOrderFront:nil];

        // Finish launching app if not already
        [NSApp finishLaunching];

        log_info("Window created: %dx%d (Metal)", config.width, config.height);
        return reinterpret_cast<WindowHandle>(data);
    }
}

void window_destroy(WindowHandle handle) {
    if (!handle) return;
    auto* data = reinterpret_cast<CocoaWindowData*>(handle);
    @autoreleasepool {
        [data->window close];
    }
    delete data;
}

bool window_should_close(WindowHandle handle) {
    if (!handle) return true;
    auto* data = reinterpret_cast<CocoaWindowData*>(handle);
    return data->shouldClose;
}

void window_poll_events(WindowHandle handle) {
    (void)handle;
    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    }
}

void window_swap_buffers(WindowHandle handle) {
    if (!handle) return;
    auto* data = reinterpret_cast<CocoaWindowData*>(handle);
    @autoreleasepool {
        [data->metalView draw];
    }
}

} // namespace pebble::platform
