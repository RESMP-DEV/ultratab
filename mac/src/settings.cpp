#include "settings.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

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
    QRect centered(QPoint(), fitted);
    centered.moveCenter(available.center());
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
} // namespace lapis::ultratab
