#include "overlay_view.hpp"
#include "deck.hpp"

#include <QDebug>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickView>
#include <QUrl>

namespace lapis::ultratab {
bool load_overlay(QQuickView& view, Deck& deck, const ViewOptions& options) {
    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    auto* context = view.rootContext();
    context->setContextProperty(QStringLiteral("deck"), &deck);
    context->setContextProperty(QStringLiteral("backdrop"), options.backdrop);
    context->setContextProperty(QStringLiteral("reducedMotion"), options.reduced_motion);
    view.setSource(QUrl(QStringLiteral("qrc:/ultratab/qml/Overlay.qml")));
    if (view.status() != QQuickView::Ready) {
        for (const auto& error : view.errors())
            qWarning().noquote() << "Ultra Tab overlay:" << error.toString();
        return false;
    }
    return true;
}
} // namespace lapis::ultratab
