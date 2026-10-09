#ifndef LAPIS_ULTRATAB_OVERLAY_VIEW_HPP
#define LAPIS_ULTRATAB_OVERLAY_VIEW_HPP
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QSize>
#include <tuple>

class QQuickView;

namespace lapis::ultratab {
// What the overlay QML tells the window around it: the panel's rectangle
// (where the blur goes) and the size everything drawn needs (the window's
// size). A child of the view, found with findChild.
class OverlayHost final : public QObject {
    Q_OBJECT
    // While dragging: whether the window snapped to the center line, and to
    // one of the set heights (the QML lights a guide for each).
    Q_PROPERTY(bool centered READ centered NOTIFY snapChanged)
    Q_PROPERTY(bool level READ level NOTIFY snapChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY snapChanged)
  public:
    using QObject::QObject;
    Q_INVOKABLE void setPanel(const QRectF& rect, qreal radius) {
        if (rect == panel_ && radius == radius_)
            return;
        panel_ = rect;
        radius_ = radius;
        emit panelChanged();
    }
    Q_INVOKABLE void setContentSize(int width, int height) {
        const QSize size(width, height);
        if (size == size_)
            return;
        size_ = size;
        emit contentSizeChanged();
    }
    // The panel's background is dragged: `to` is where the window's top-left
    // would follow the pointer; the window snaps from there. `done` ends it.
    Q_INVOKABLE void dragTo(int x, int y, bool done) {
        const bool was_dragging = dragging_;
        dragging_ = !done;
        // `dragging` is what QML binds its snap guides to, so a change has to
        // notify like the other state here does.
        if (dragging_ != was_dragging)
            emit snapChanged();
        emit dragRequested(QPoint(x, y), done);
        if (done)
            setSnap(false, false);
    }
    void setSnap(bool centered, bool level) {
        const auto state = std::tuple(centered, level, dragging_);
        if (state == shown_)
            return;
        centered_ = centered;
        level_ = level;
        shown_ = state;
        emit snapChanged();
    }
    [[nodiscard]] bool centered() const { return centered_; }
    [[nodiscard]] bool level() const { return level_; }
    [[nodiscard]] bool dragging() const { return dragging_; }
    [[nodiscard]] QRectF panel() const { return panel_; }
    [[nodiscard]] qreal radius() const { return radius_; }
    [[nodiscard]] QSize contentSize() const { return size_; }

  signals:
    // The window is about to show: the overlay settles at once, without the
    // card-change motion, since opening is the window's own fade.
    void appearing();
    void dragRequested(QPoint to, bool done);
    void snapChanged();
    void panelChanged();
    void contentSizeChanged();

  private:
    bool centered_{};
    bool level_{};
    bool dragging_{};
    std::tuple<bool, bool, bool> shown_{};
    QRectF panel_;
    qreal radius_{};
    QSize size_;
};
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
