#include "overlay_view.hpp"
#include "deck.hpp"

#include <QDebug>
#include <QPainter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickImageProvider>
#include <QQuickView>
#include <QSvgRenderer>
#include <QUrl>
#include <memory>
#include <utility>

namespace lapis::ultratab {
namespace {
// Draws a card's sanitized diagram at the size the overlay asks for.
class DiagramProvider final : public QQuickImageProvider {
  public:
    explicit DiagramProvider(std::shared_ptr<DiagramStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image), store_(std::move(store)) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        const auto svg = store_->svg(id);
        if (svg.isEmpty())
            return {};
        QSvgRenderer renderer(svg.toUtf8());
        if (!renderer.isValid())
            return {};
        const auto box = renderer.viewBoxF().isEmpty() ? QSizeF(renderer.defaultSize())
                                                       : renderer.viewBoxF().size();
        constexpr int kLimit = 4096;
        QSize target = requested.isValid() && !requested.isEmpty()
                           ? box.scaled(QSizeF(requested), Qt::KeepAspectRatio).toSize()
                           : box.toSize();
        target = target.boundedTo({kLimit, kLimit}).expandedTo({1, 1});
        QImage image(target, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(QPointF(), QSizeF(target)));
        painter.end();
        if (size != nullptr)
            *size = target;
        return image;
    }

  private:
    std::shared_ptr<DiagramStore> store_;
};
} // namespace

bool load_overlay(QQuickView& view, Deck& deck, const ViewOptions& options) {
    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    // The engine owns the provider.
    view.engine()->addImageProvider(QStringLiteral("diagram"),
                                    new DiagramProvider(deck.diagrams()));
    auto* context = view.rootContext();
    context->setContextProperty(QStringLiteral("deck"), &deck);
    context->setContextProperty(QStringLiteral("backdrop"), options.backdrop);
    context->setContextProperty(QStringLiteral("reducedMotion"), options.reduced_motion);
    // Owned by the view: the QML engine holds only the borrowed pointer this
    // context property carries, and the view destroys the host with itself.
    context->setContextProperty(QStringLiteral("host"), new OverlayHost(&view));
    view.setSource(QUrl(QStringLiteral("qrc:/ultratab/qml/Overlay.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto& error : view.errors())
            qWarning().noquote() << "Ultra Tab overlay:" << error.toString();
        return false;
    }
    return true;
}
} // namespace lapis::ultratab
