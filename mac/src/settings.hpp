#ifndef LAPIS_ULTRATAB_SETTINGS_HPP
#define LAPIS_ULTRATAB_SETTINGS_HPP
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <optional>

// Ultra Tab's own settings and window memory. The person's options live in
// <home>/ultratab.json, which Ultra Tab only reads; where the window was left
// on each screen lives in <home>/ultratab-window.json, which it writes.
namespace lapis::ultratab {
struct Settings {
    QString hotkey;            // empty: the default key
    bool start_at_login{true}; // "startAtLogin"
};
[[nodiscard]] Settings read_settings(const QString& home);
[[nodiscard]] Settings parse_settings(const QByteArray& bytes);

// One screen, as stable across launches as Qt can name it.
[[nodiscard]] QString screen_key(const QString& name, const QRect& geometry);
using Positions = QHash<QString, QPoint>; // top-left by screen_key
[[nodiscard]] Positions read_positions(const QString& home);
[[nodiscard]] Positions parse_positions(const QByteArray& bytes);
bool write_positions(const QString& home, const Positions& positions);
// Where a window of `size` goes on a screen's `available` area: where it was
// left when that still fits, moved inside the area when it partly does, else
// centered.
[[nodiscard]] QRect place_window(const QRect& available, const QSize& size,
                                 const std::optional<QPoint>& saved);
} // namespace lapis::ultratab
#endif
