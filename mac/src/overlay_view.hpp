#ifndef LAPIS_ULTRATAB_OVERLAY_VIEW_HPP
#define LAPIS_ULTRATAB_OVERLAY_VIEW_HPP
class QQuickView;

namespace lapis::ultratab {
class Deck;
struct ViewOptions {
    bool backdrop{};       // a stand-in for the blurred desktop, for captures
    bool reduced_motion{}; // no arrival slide or pulsing dots
};
// Loads the overlay QML into a transparent `view` showing `deck` (borrowed;
// it must outlive the view). False, with Qt's errors logged, when it fails.
[[nodiscard]] bool load_overlay(QQuickView& view, Deck& deck, const ViewOptions& options);
} // namespace lapis::ultratab
#endif
