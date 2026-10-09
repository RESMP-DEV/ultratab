#ifndef LAPIS_ULTRATAB_PLATFORM_OVERLAY_HPP
#define LAPIS_ULTRATAB_PLATFORM_OVERLAY_HPP
#include "hotkey.hpp"

#include <QRectF>
#include <QString>
#include <QtGlobal>
#include <functional>

class QWindow;

// The macOS pieces of the overlay; elsewhere they do nothing and report
// themselves unavailable.
namespace lapis::ultratab::platform {
#ifdef Q_OS_MACOS
// Real blur behind a transparent window: an NSVisualEffectView (behind-window
// blending) becomes the window's content view, with Qt's view inside it.
bool make_translucent(QWindow& window);
// Limits the blur to `rect` (window coordinates, top-left origin) with
// rounded corners; outside it the window is clear, so what is drawn there
// (the overlay's peeking cards) sits over the plain desktop.
void set_blur_rect(QWindow& window, const QRectF& rect, qreal radius);
// Runs `pressed` when the key goes down in any app. False when the key could
// not be taken (another app holds it). An empty handler releases it.
bool register_hotkey(const Hotkey& hotkey, const std::function<void()>& pressed);
// No Dock icon or menu bar: Ultra Tab sits in the background.
void become_accessory();
// The person pressed the hotkey: bring Ultra Tab forward. Never called for
// anything that arrives on its own.
void activate();
// Brings the app with `bundle_id` forward, launching it if it is not running.
void activate_app(const QString& bundle_id);
// Hand the keyboard back to the app that had it.
void yield();
// The system's Reduce Motion accessibility setting.
bool reduce_motion();
// Registers (or removes) Ultra Tab as a login item with SMAppService. Only a
// bundle installed in an Applications folder registers; anything else (a
// build folder, a test) is left alone. Returns what happened, for the log.
QString set_start_at_login(bool on);
#else
inline bool make_translucent(QWindow&) { return false; }
inline void set_blur_rect(QWindow&, const QRectF&, qreal) {}
inline bool register_hotkey(const Hotkey&, const std::function<void()>&) { return false; }
inline void become_accessory() {}
inline void activate() {}
inline void yield() {}
inline void activate_app(const QString&) {}
inline bool reduce_motion() { return false; }
inline QString set_start_at_login(bool) { return QStringLiteral("not available here"); }
#endif
} // namespace lapis::ultratab::platform
#endif
