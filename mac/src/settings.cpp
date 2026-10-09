#include "settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <array>
#include <cstdlib>

namespace lapis::ultratab {
namespace {
constexpr qint64 kSettingsLimit = qint64{64} * 1024;
constexpr int kPositionsVersion = 1;
constexpr int kPositionsLimit = 64;

QByteArray read_small(const QString& path) {
    QFile file(path);
    if (file.size() > kSettingsLimit || !file.open(QIODevice::ReadOnly))
        return {};
    return file.read(kSettingsLimit);
}

QString positions_path(const QString& home) {
    return QDir(home).filePath(QStringLiteral("ultratab-window.json"));
}
} // namespace

Settings parse_settings(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    Settings settings;
    settings.hotkey = root.value(QStringLiteral("hotkey")).toString();
    settings.start_at_login = root.value(QStringLiteral("startAtLogin")).toBool(true);
    return settings;
}

Settings read_settings(const QString& home) {
    if (home.isEmpty())
        return {};
    return parse_settings(read_small(QDir(home).filePath(QStringLiteral("ultratab.json"))));
}

QString screen_key(const QString& name, const QRect& geometry) {
    return QStringLiteral("%1@%2x%3+%4+%5")
        .arg(name)
        .arg(geometry.width())
        .arg(geometry.height())
        .arg(geometry.x())
        .arg(geometry.y());
}

Positions parse_positions(const QByteArray& bytes) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    Positions positions;
    if (root.value(QStringLiteral("v")).toInt() != kPositionsVersion)
        return positions;
    const auto saved = root.value(QStringLiteral("positions")).toObject();
    for (auto entry = saved.constBegin(); entry != saved.constEnd(); ++entry) {
        const auto pair = entry.value().toArray();
        if (pair.size() == 2 && pair.at(0).isDouble() && pair.at(1).isDouble() &&
            positions.size() < kPositionsLimit)
            positions.insert(entry.key(), QPoint(pair.at(0).toInt(), pair.at(1).toInt()));
    }
    return positions;
}

Positions read_positions(const QString& home) {
    return home.isEmpty() ? Positions{} : parse_positions(read_small(positions_path(home)));
}

bool write_positions(const QString& home, const Positions& positions) {
    if (home.isEmpty() || !QDir(home).exists())
        return false;
    QJsonObject saved;
    for (auto entry = positions.constBegin(); entry != positions.constEnd(); ++entry)
        saved.insert(entry.key(), QJsonArray{entry.value().x(), entry.value().y()});
    QSaveFile file(positions_path(home));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("v"), kPositionsVersion},
                                         {QStringLiteral("positions"), saved}})
                   .toJson());
    return file.commit();
}

QRect place_window(const QRect& available, const QSize& size, const std::optional<QPoint>& saved) {
    const QSize fitted = size.boundedTo(available.size());
    // Centered across, a fifth of the way down, as a command bar sits.
    QRect centered(QPoint(), fitted);
    centered.moveCenter(available.center());
    centered.moveTop(std::max(available.top(), std::min(available.top() + available.height() / 5,
                                                        available.bottom() - fitted.height() + 1)));
    if (!saved)
        return centered;
    QRect rect(*saved, fitted);
    // Mostly off this screen (it was rearranged): start again in the middle.
    if (!available.intersects(rect) || available.intersected(rect).width() * 2 < rect.width() ||
        available.intersected(rect).height() * 2 < rect.height())
        return centered;
    rect.moveLeft(
        std::clamp(rect.left(), available.left(), available.right() - fitted.width() + 1));
    rect.moveTop(std::clamp(rect.top(), available.top(), available.bottom() - fitted.height() + 1));
    return rect;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Snap snap_window(const QRect& available, const QSize& window, int panel_top, int panel_height,
                 QPoint wanted, int reach) {
    Snap snap{wanted};
    const int center_x = available.left() + (available.width() - window.width()) / 2;
    if (std::abs(wanted.x() - center_x) <= reach) {
        snap.position.setX(center_x);
        snap.centered = true;
    }
    const int height = available.height();
    const std::array<int, 4> tops{available.top() + height * 6 / 100, available.top() + height / 5,
                                  available.top() + (height - panel_height) / 2,
                                  available.top() + height * 62 / 100};
    int best = reach + 1;
    for (const int top : tops) {
        const int y = top - panel_top;
        if (std::abs(wanted.y() - y) < best) {
            best = std::abs(wanted.y() - y);
            if (best <= reach) {
                snap.position.setY(y);
                snap.level = true;
            }
        }
    }
    const QPoint snapped = snap.position;
    snap.position.setX(
        std::clamp(snap.position.x(), available.left(),
                   std::max(available.left(), available.right() - window.width() + 1)));
    snap.position.setY(
        std::clamp(snap.position.y(), available.top() - panel_top,
                   std::max(available.top(), available.bottom() - window.height() + 1)));
    // A guide the clamp moved the panel off of is no longer a guide: the gold
    // border and the centered light would claim a snap that did not happen.
    if (snap.position.x() != snapped.x())
        snap.centered = false;
    if (snap.position.y() != snapped.y())
        snap.level = false;
    return snap;
}
} // namespace lapis::ultratab
