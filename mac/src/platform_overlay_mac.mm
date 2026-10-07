#include "platform_overlay.hpp"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#include <QHash>
#include <QWindow>
#import <ServiceManagement/ServiceManagement.h>

namespace lapis::ultratab::platform {
namespace {
constexpr CGFloat kCornerRadius = 18;

std::function<void()>& hotkey_handler() {
    static std::function<void()> handler;
    return handler;
}
// Carbon cannot tell the two Option keys apart; the HID state's
// device-dependent bits can, without any input-monitoring permission.
Side& hotkey_side() {
    static Side side = Side::any;
    return side;
}
constexpr CGEventFlags kLeftOption = 0x20;  // NX_DEVICELALTKEYMASK
constexpr CGEventFlags kRightOption = 0x40; // NX_DEVICERALTKEYMASK

bool side_matches() {
    const auto side = hotkey_side();
    if (side == Side::any)
        return true;
    const auto flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    return (flags & (side == Side::left ? kLeftOption : kRightOption)) != 0;
}

OSStatus hotkey_pressed(EventHandlerCallRef, EventRef, void*) {
    if (const auto& handler = hotkey_handler(); handler && side_matches()) {
        handler();
        return noErr;
    }
    return eventNotHandledErr;
}

// Carbon virtual key codes for the keys parse_hotkey accepts.
UInt32 key_code(const QString& key, bool* known) {
    static const QHash<QString, UInt32> codes{
        {QStringLiteral("Space"), kVK_Space}, {QStringLiteral("Return"), kVK_Return},
        {QStringLiteral("Tab"), kVK_Tab},     {QStringLiteral("Escape"), kVK_Escape},
        {QStringLiteral("A"), kVK_ANSI_A},    {QStringLiteral("B"), kVK_ANSI_B},
        {QStringLiteral("C"), kVK_ANSI_C},    {QStringLiteral("D"), kVK_ANSI_D},
        {QStringLiteral("E"), kVK_ANSI_E},    {QStringLiteral("F"), kVK_ANSI_F},
        {QStringLiteral("G"), kVK_ANSI_G},    {QStringLiteral("H"), kVK_ANSI_H},
        {QStringLiteral("I"), kVK_ANSI_I},    {QStringLiteral("J"), kVK_ANSI_J},
        {QStringLiteral("K"), kVK_ANSI_K},    {QStringLiteral("L"), kVK_ANSI_L},
        {QStringLiteral("M"), kVK_ANSI_M},    {QStringLiteral("N"), kVK_ANSI_N},
        {QStringLiteral("O"), kVK_ANSI_O},    {QStringLiteral("P"), kVK_ANSI_P},
        {QStringLiteral("Q"), kVK_ANSI_Q},    {QStringLiteral("R"), kVK_ANSI_R},
        {QStringLiteral("S"), kVK_ANSI_S},    {QStringLiteral("T"), kVK_ANSI_T},
        {QStringLiteral("U"), kVK_ANSI_U},    {QStringLiteral("V"), kVK_ANSI_V},
        {QStringLiteral("W"), kVK_ANSI_W},    {QStringLiteral("X"), kVK_ANSI_X},
        {QStringLiteral("Y"), kVK_ANSI_Y},    {QStringLiteral("Z"), kVK_ANSI_Z},
        {QStringLiteral("0"), kVK_ANSI_0},    {QStringLiteral("1"), kVK_ANSI_1},
        {QStringLiteral("2"), kVK_ANSI_2},    {QStringLiteral("3"), kVK_ANSI_3},
        {QStringLiteral("4"), kVK_ANSI_4},    {QStringLiteral("5"), kVK_ANSI_5},
        {QStringLiteral("6"), kVK_ANSI_6},    {QStringLiteral("7"), kVK_ANSI_7},
        {QStringLiteral("8"), kVK_ANSI_8},    {QStringLiteral("9"), kVK_ANSI_9},
        {QStringLiteral("F1"), kVK_F1},       {QStringLiteral("F2"), kVK_F2},
        {QStringLiteral("F3"), kVK_F3},       {QStringLiteral("F4"), kVK_F4},
        {QStringLiteral("F5"), kVK_F5},       {QStringLiteral("F6"), kVK_F6},
        {QStringLiteral("F7"), kVK_F7},       {QStringLiteral("F8"), kVK_F8},
        {QStringLiteral("F9"), kVK_F9},       {QStringLiteral("F10"), kVK_F10},
        {QStringLiteral("F11"), kVK_F11},     {QStringLiteral("F12"), kVK_F12}};
    const auto found = codes.constFind(key);
    *known = found != codes.cend();
    return *known ? *found : 0;
}

NSImage* rounded_mask() {
    const CGFloat side = kCornerRadius * 2 + 1;
    NSImage* mask = [NSImage imageWithSize:NSMakeSize(side, side)
                                   flipped:NO
                            drawingHandler:^BOOL(NSRect rect) {
                              [NSColor.blackColor set];
                              [[NSBezierPath bezierPathWithRoundedRect:rect
                                                               xRadius:kCornerRadius
                                                               yRadius:kCornerRadius] fill];
                              return YES;
                            }];
    mask.capInsets = NSEdgeInsetsMake(kCornerRadius, kCornerRadius, kCornerRadius, kCornerRadius);
    mask.resizingMode = NSImageResizingModeStretch;
    return mask;
}
} // namespace

bool make_translucent(QWindow& window) {
    // Qt exposes its native NSView through WId; a borrowed view in Qt's window.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* qt_view = reinterpret_cast<NSView*>(window.winId());
    NSWindow* host = qt_view.window;
    if (host == nil)
        return false;
    host.opaque = NO;
    host.backgroundColor = NSColor.clearColor;
    host.hasShadow = YES;
    // Shown over full-screen apps and on whichever Space is current.
    host.collectionBehavior =
        NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    if ([host.contentView isKindOfClass:NSVisualEffectView.class])
        return true;
    NSVisualEffectView* blur =
        [[[NSVisualEffectView alloc] initWithFrame:host.contentView.frame] autorelease];
    blur.material = NSVisualEffectMaterialHUDWindow;
    blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    blur.state = NSVisualEffectStateActive;
    blur.maskImage = rounded_mask();
    blur.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [qt_view retain];
    [qt_view removeFromSuperview];
    host.contentView = blur;
    qt_view.frame = blur.bounds;
    qt_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [blur addSubview:qt_view];
    [qt_view release];
    return true;
}

bool register_hotkey(const Hotkey& hotkey, const std::function<void()>& pressed) {
    static EventHotKeyRef key = nullptr;
    static EventHandlerRef dispatch = nullptr;
    hotkey_handler() = pressed;
    hotkey_side() = hotkey.option ? hotkey.optionSide : Side::any;
    if (key != nullptr) {
        UnregisterEventHotKey(key);
        key = nullptr;
    }
    if (!pressed)
        return true;
    bool known = false;
    const UInt32 code = key_code(hotkey.key, &known);
    if (!known)
        return false;
    if (dispatch == nullptr) {
        const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
        if (InstallEventHandler(GetApplicationEventTarget(), NewEventHandlerUPP(hotkey_pressed), 1,
                                &type, nullptr, &dispatch) != noErr)
            return false;
    }
    // Carbon's modifier masks are plain enum constants; take them unsigned.
    UInt32 modifiers = 0;
    modifiers |= hotkey.command ? UInt32{cmdKey} : 0U;
    modifiers |= hotkey.option ? UInt32{optionKey} : 0U;
    modifiers |= hotkey.control ? UInt32{controlKey} : 0U;
    modifiers |= hotkey.shift ? UInt32{shiftKey} : 0U;
    constexpr OSType signature = 0x756C7462; // 'ultb'
    const EventHotKeyID id{signature, 1};
    return RegisterEventHotKey(code, modifiers, id, GetApplicationEventTarget(), 0, &key) == noErr;
}

void become_accessory() { [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; }

void activate() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // yield() hides the app; activating alone does not unhide it.
    [NSApp unhide:nil];
    [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
}

void yield() { [NSApp hide:nil]; }

bool reduce_motion() { return NSWorkspace.sharedWorkspace.accessibilityDisplayShouldReduceMotion; }

QString set_start_at_login(bool on) {
    NSString* bundle = NSBundle.mainBundle.bundlePath;
    NSString* folder = bundle.stringByDeletingLastPathComponent;
    const bool installed =
        [bundle.pathExtension isEqualToString:@"app"] &&
        ([folder isEqualToString:@"/Applications"] ||
         [folder
             isEqualToString:[NSHomeDirectory() stringByAppendingPathComponent:@"Applications"]]);
    if (!installed)
        return QStringLiteral("not installed in Applications; login item left alone");
    SMAppService* service = SMAppService.mainAppService;
    const bool registered = service.status == SMAppServiceStatusEnabled ||
                            service.status == SMAppServiceStatusRequiresApproval;
    if (on == registered)
        return on ? QStringLiteral("starts at login") : QStringLiteral("does not start at login");
    NSError* error = nil;
    const bool done =
        on ? [service registerAndReturnError:&error] : [service unregisterAndReturnError:&error];
    if (!done)
        return QStringLiteral("login item not changed: %1")
            .arg(QString::fromNSString(error.localizedDescription));
    return on ? QStringLiteral("starts at login (registered)")
              : QStringLiteral("does not start at login (removed)");
}
} // namespace lapis::ultratab::platform
