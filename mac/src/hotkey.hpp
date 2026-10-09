#ifndef LAPIS_ULTRATAB_HOTKEY_HPP
#define LAPIS_ULTRATAB_HOTKEY_HPP
#include <QString>
#include <optional>

namespace lapis::ultratab {
// A global key such as "Option-Space" or "Command-Shift-U": modifiers, then
// one key (Space, a letter, a digit, F1 to F12, Return, Tab, Escape), joined
// by '-' or '+', in any case. At least one of Command, Option or Control is
// required so a plain key is never taken from every app. "LeftOption" or
// "RightOption" names one Option key only.
enum class Side : unsigned char { any, left, right };
struct Hotkey {
    bool command{};
    bool option{};
    bool control{};
    bool shift{};
    Side optionSide{}; // which Option key, when option is set
    QString key;       // canonical: "Space", "A", "7", "F5", "Return", "Tab", "Escape"
    bool operator==(const Hotkey&) const = default;
};
inline constexpr const char* default_hotkey = "LeftOption-Space";
[[nodiscard]] std::optional<Hotkey> parse_hotkey(const QString& text);
[[nodiscard]] QString describe(const Hotkey& hotkey);
} // namespace lapis::ultratab
#endif
