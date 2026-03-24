#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "pebble/platform/platform.h"
#include "pebble/core/input.h"

// --- Helper: convert macOS keyCode to ASCII-based key index ---
static uint8_t cocoa_keycode_to_ascii(unsigned short keyCode) {
    // macOS virtual keycodes -> ASCII mapping for relevant keys
    switch (keyCode) {
        case 0x00: return 'a';  // kVK_ANSI_A
        case 0x01: return 's';  // kVK_ANSI_S
        case 0x02: return 'd';  // kVK_ANSI_D
        case 0x08: return 'c';  // kVK_ANSI_C
        case 0x0D: return 'w';  // kVK_ANSI_W
        case 0x0E: return 'e';  // kVK_ANSI_E
        case 0x23: return 'p';  // kVK_ANSI_P
        case 0x0F: return 'r';  // kVK_ANSI_R
        case 0x12: return '1';  // kVK_ANSI_1
        case 0x13: return '2';  // kVK_ANSI_2
        case 0x14: return '3';  // kVK_ANSI_3
        case 0x15: return '4';  // kVK_ANSI_4
        case 0x17: return '5';  // kVK_ANSI_5
        case 0x16: return '6';  // kVK_ANSI_6
        case 0x1A: return '7';  // kVK_ANSI_7
        case 0x1C: return '8';  // kVK_ANSI_8
        case 0x31: return ' ';  // kVK_Space
        case 0x35: return 27;   // kVK_Escape
        case 0x7A: return 128;  // kVK_F1
        default:   return 0;
    }
}

// --- Cocoa Window Implementation ---

// Custom MTKView subclass that accepts key events
@interface PebbleMetalView : MTKView
@end

@implementation PebbleMetalView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
// Suppress default Cocoa beep on key press by overriding these:
- (void)keyDown:(NSEvent*)event { (void)event; }
- (void)keyUp:(NSEvent*)event { (void)event; }
@end

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
        data->metalView = [[PebbleMetalView alloc] initWithFrame:frame device:data->device];
        data->metalView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        data->metalView.framebufferOnly = YES;
        data->metalView.preferredFramesPerSecond = 60;
        data->metalView.enableSetNeedsDisplay = NO;
        data->metalView.paused = YES; // We drive rendering via CAMetalLayer directly

        [data->window setContentView:data->metalView];
        [data->window makeKeyAndOrderFront:nil];
        [data->window makeFirstResponder:data->metalView];

        // Accept mouse moved events
        [data->window setAcceptsMouseMovedEvents:YES];

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

void window_get_size(WindowHandle handle, int32_t* w, int32_t* h) {
    if (!handle) { *w = 0; *h = 0; return; }
    auto* data = reinterpret_cast<CocoaWindowData*>(handle);
    NSRect frame = [[data->window contentView] frame];
    *w = static_cast<int32_t>(frame.size.width);
    *h = static_cast<int32_t>(frame.size.height);
}

void window_poll_events(WindowHandle handle) {
    if (!handle) return;
    auto* data = reinterpret_cast<CocoaWindowData*>(handle);
    auto& input = pebble::get_input_state_mut();

    // Reset per-frame states
    input.begin_frame();

    @autoreleasepool {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {

            NSEventType type = [event type];

            switch (type) {
                case NSEventTypeMouseMoved:
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged: {
                    // Convert from Cocoa (bottom-left origin) to screen coords (top-left origin)
                    NSPoint loc = [event locationInWindow];
                    NSRect contentRect = [data->metalView frame];
                    float new_x = (float)loc.x;
                    float new_y = (float)(contentRect.size.height - loc.y);
                    input.mouse_dx += new_x - input.mouse_x;
                    input.mouse_dy += new_y - input.mouse_y;
                    input.mouse_x = new_x;
                    input.mouse_y = new_y;
                    break;
                }

                case NSEventTypeLeftMouseDown:
                    input.mouse_left_down = true;
                    input.mouse_left_pressed = true;
                    break;

                case NSEventTypeLeftMouseUp:
                    input.mouse_left_down = false;
                    input.mouse_left_released = true;
                    break;

                case NSEventTypeRightMouseDown:
                    input.mouse_right_down = true;
                    input.mouse_right_pressed = true;
                    break;

                case NSEventTypeRightMouseUp:
                    input.mouse_right_down = false;
                    input.mouse_right_released = true;
                    break;

                case NSEventTypeOtherMouseDown:
                    input.mouse_middle_down = true;
                    break;

                case NSEventTypeOtherMouseUp:
                    input.mouse_middle_down = false;
                    break;

                case NSEventTypeScrollWheel:
                    input.scroll_dy += (float)[event scrollingDeltaY];
                    break;

                case NSEventTypeKeyDown: {
                    if (![event isARepeat]) {
                        uint8_t key = cocoa_keycode_to_ascii([event keyCode]);
                        if (key > 0) {
                            input.keys[key] = true;
                            input.keys_pressed[key] = true;
                        }
                    }
                    break;
                }

                case NSEventTypeKeyUp: {
                    uint8_t key = cocoa_keycode_to_ascii([event keyCode]);
                    if (key > 0) {
                        input.keys[key] = false;
                    }
                    break;
                }

                default:
                    break;
            }

            // Only forward non-key events to Cocoa — we handle keys ourselves
            // Forwarding key events causes Cocoa to beep and can delay processing
            if (type != NSEventTypeKeyDown && type != NSEventTypeKeyUp) {
                [NSApp sendEvent:event];
                [NSApp updateWindows];
            }
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
