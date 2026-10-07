#include "hotkey.hpp"

#include <QRegularExpression>
#include <QStringList>
#include <optional>

namespace lapis::ultratab {
namespace {
// The modifier a word names, or false when it names none.
bool apply_modifier(const QString& word, Hotkey& hotkey) {
    const auto option_side = [&]() -> std::optional<Side> {
        if (word == QLatin1String("option") || word == QLatin1String("opt") ||
            word == QLatin1String("alt"))
            return Side::any;
        if (word == QLatin1String("leftoption") || word == QLatin1String("leftopt") ||
            word == QLatin1String("leftalt"))
            return Side::left;
        if (word == QLatin1String("rightoption") || word == QLatin1String("rightopt") ||
            word == QLatin1String("rightalt"))
            return Side::right;
        return std::nullopt;
    }();
    if (option_side) {
        // One Option word only: "LeftOption-RightOption" names no key.
        if (hotkey.option)
            return false;
        hotkey.option = true;
        hotkey.optionSide = *option_side;
    } else if (word == QLatin1String("command") || word == QLatin1String("cmd"))
        hotkey.command = true;
    else if (word == QLatin1String("control") || word == QLatin1String("ctrl"))
        hotkey.control = true;
    else if (word == QLatin1String("shift"))
        hotkey.shift = true;
    else
        return false;
    return true;
}

QString canonical_key(const QString& word) {
    if (word == QLatin1String("space"))
        return QStringLiteral("Space");
    if (word == QLatin1String("return") || word == QLatin1String("enter"))
        return QStringLiteral("Return");
    if (word == QLatin1String("tab"))
        return QStringLiteral("Tab");
    if (word == QLatin1String("escape") || word == QLatin1String("esc"))
        return QStringLiteral("Escape");
    if (word.size() == 1 && (word.at(0).isLetter() || word.at(0).isDigit()) &&
        word.at(0).unicode() < 128)
        return word.toUpper();
    static const QRegularExpression function(QStringLiteral("^f([1-9]|1[0-2])$"));
    if (const auto found = function.match(word); found.hasMatch())
        return QStringLiteral("F") + found.captured(1);
    return {};
}
} // namespace

std::optional<Hotkey> parse_hotkey(const QString& text) {
    static const QRegularExpression separators(QStringLiteral("[-+]"));
    const auto words = text.trimmed().toLower().split(separators);
    if (words.size() < 2)
        return std::nullopt;
    Hotkey hotkey;
    for (qsizetype index = 0; index + 1 < words.size(); ++index)
        if (!apply_modifier(words.at(index).trimmed(), hotkey))
            return std::nullopt;
    hotkey.key = canonical_key(words.last().trimmed());
    if (hotkey.key.isEmpty() || !(hotkey.command || hotkey.option || hotkey.control))
        return std::nullopt;
    return hotkey;
}

QString describe(const Hotkey& hotkey) {
    QStringList parts;
    if (hotkey.control)
        parts << QStringLiteral("Control");
    if (hotkey.option)
        parts << (hotkey.optionSide == Side::left    ? QStringLiteral("LeftOption")
                  : hotkey.optionSide == Side::right ? QStringLiteral("RightOption")
                                                     : QStringLiteral("Option"));
    if (hotkey.shift)
        parts << QStringLiteral("Shift");
    if (hotkey.command)
        parts << QStringLiteral("Command");
    parts << hotkey.key;
    return parts.join(QLatin1Char('-'));
}
} // namespace lapis::ultratab
