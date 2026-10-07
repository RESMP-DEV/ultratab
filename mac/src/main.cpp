// Ultra Tab: an overlay beside lapis that deals the agents that need you as a
// deck of cards. It reads what lapis publishes and answers an agent by joining
// its session service, so the lapis window keeps its own attachment. It never
// takes lapis's workspace lock or writes lapis's files.
#include "composer.hpp"
#include "deck.hpp"
#include "hotkey.hpp"
#include "overlay_view.hpp"
#include "platform_overlay.hpp"
#include "published.hpp"
#include "session_sender.hpp"
#include "settings.hpp"

#include <QCommandLineParser>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickView>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <optional>
#include <utility>

namespace {
using namespace lapis::ultratab;

// <home>/ultratab.json, read only; lapis.json is lapis's own.
QJsonObject configuration(const QString& home) {
    QFile file(QDir(home).filePath(QStringLiteral("ultratab.json")));
    if (home.isEmpty() || file.size() > qint64{64} * 1024 || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// --list: the deck as text, from the same files, without a window or a key.
int print_deck(const QString& home) {
    const auto published =
        read_published(home.isEmpty() ? QString() : QDir(home).filePath(QStringLiteral("runtime")));
    QTextStream out(stdout);
    const auto cards = waiting_cards(published);
    for (const auto& card : cards)
        out << card.name << " [" << card.category_name << "] "
            << (card.composed && !card.composed->tldr.isEmpty() ? card.composed->tldr : card.line)
            << (card.proposal.isEmpty() ? QString() : QStringLiteral(" -> ") + card.proposal)
            << '\n';
    if (cards.empty())
        out << "Nothing needs you.\n";
    if (!published.has_state || !published.problem.isEmpty())
        out << "("
            << (published.problem.isEmpty() ? QStringLiteral("no agent state published")
                                            : published.problem)
            << ")\n";
    out << published.agents.size() << " agents in the registry\n";
    return 0;
}

class Overlay final {
  public:
    Overlay(QQuickView& view, Deck& deck, PublishedSource& source, const QString& home)
        : view_(view), deck_(deck), source_(source), home_(home),
          positions_(read_positions(home_)) {
        QObject::connect(&deck_, &Deck::dismissRequested, &view_, [this] { hide(true); });
        // Clicking elsewhere puts the overlay away, as Spotlight does.
        QObject::connect(&view_, &QWindow::activeChanged, &view_, [this] {
            // Activation lands a moment after show(); losing it in that
            // window may be the launch race, so look again once it has passed.
            if (view_.isActive() || !view_.isVisible())
                return;
            const auto elapsed = shown_.isValid() ? shown_.elapsed() : settle_ms;
            if (elapsed >= settle_ms) {
                hide(false);
                return;
            }
            QTimer::singleShot(settle_ms - elapsed, &view_, [this] {
                if (!view_.isActive() && view_.isVisible())
                    hide(false);
            });
        });
        // Dragged by its background: remember where, per screen, once it rests.
        remember_.setSingleShot(true);
        remember_.setInterval(400);
        QObject::connect(&remember_, &QTimer::timeout, &view_, [this] { remember(); });
        QObject::connect(&view_, &QWindow::xChanged, &view_, [this] { moved(); });
        QObject::connect(&view_, &QWindow::yChanged, &view_, [this] { moved(); });
    }
    // Only the person's hotkey (or --show) calls this; nothing that arrives
    // brings the overlay forward or takes the keyboard on its own.
    void toggle() {
        if (view_.isVisible() && view_.isActive())
            hide(true);
        else
            show();
    }
    void show() {
        auto* screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
            screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
            return;
        const auto area = screen->availableGeometry();
        // The window keeps one size per screen; only where it sits changes.
        const QSize size(std::min(1500, area.width() * 82 / 100),
                         std::min(900, area.height() * 84 / 100));
        const auto key = screen_key(screen->name(), screen->geometry());
        const auto saved = positions_.constFind(key);
        placing_ = true;
        view_.setGeometry(place_window(
            area, size, saved == positions_.cend() ? std::nullopt : std::optional<QPoint>(*saved)));
        placing_ = false;
        shown_.start();
        view_.show();
        if (!translucent_)
            translucent_ = platform::make_translucent(view_);
        platform::activate();
        view_.requestActivate();
        source_.reload();
        source_.setPolling(true);
    }
    void hide(bool give_back) {
        if (remember_.isActive()) {
            remember_.stop();
            remember();
        }
        view_.hide();
        source_.setPolling(false);
        deck_.setListening(false);
        if (give_back)
            platform::yield();
    }

  private:
    static constexpr qint64 settle_ms = 600;
    void moved() {
        if (!placing_ && view_.isVisible())
            remember_.start();
    }
    void remember() {
        auto* screen = view_.screen();
        if (screen == nullptr)
            return;
        positions_.insert(screen_key(screen->name(), screen->geometry()), view_.position());
        write_positions(home_, positions_);
    }
    QQuickView& view_;
    Deck& deck_;
    PublishedSource& source_;
    QString home_;
    Positions positions_;
    QTimer remember_;
    bool placing_{};
    bool translucent_{};
    QElapsedTimer shown_;
};
} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Ultra Tab"));
    QGuiApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Ultra Tab: answer your agents from a deck"));
    parser.addHelpOption();
    const QCommandLineOption home_option(
        QStringLiteral("home"),
        QStringLiteral("the agents' data folder (default: LAPIS_HOME or ~/.lapis)"),
        QStringLiteral("folder"));
    const QCommandLineOption hotkey_option(
        QStringLiteral("hotkey"), QStringLiteral("Show or hide key (default: LeftOption-Space)"),
        QStringLiteral("keys"));
    const QCommandLineOption show_option(QStringLiteral("show"),
                                         QStringLiteral("Show the overlay at launch"));
    const QCommandLineOption list_option(
        QStringLiteral("list"), QStringLiteral("Print the deck, in order, and exit (no window)"));
    parser.addOptions({home_option, hotkey_option, show_option, list_option});
    parser.process(app);

    const auto home =
        parser.isSet(home_option) ? parser.value(home_option) : lapis::ultratab::lapis_home();
    const auto config = configuration(home);
    const auto settings = read_settings(home);
    auto key_text = parser.isSet(hotkey_option) ? parser.value(hotkey_option) : settings.hotkey;
    if (key_text.isEmpty())
        key_text = QString::fromLatin1(default_hotkey);
    const auto hotkey = parse_hotkey(key_text);
    if (!hotkey) {
        qCritical().noquote() << "Ultra Tab: not a key:" << key_text;
        return 2;
    }

    if (parser.isSet(list_option))
        return print_deck(home);

    platform::become_accessory();
    SessionSender sender;
    Deck deck(sender);
    PublishedSource source(home.isEmpty() ? QString()
                                          : QDir(home).filePath(QStringLiteral("runtime")));
    QObject::connect(&source, &PublishedSource::loaded, &deck, &Deck::setPublished);

    // Composed cards: runtime/ultratab_cards.json, beside what lapis publishes.
    const auto& runtime = source.runtime();
    const auto helper =
        runtime.isEmpty()
            ? QString()
            : install_compose_helper(QDir(runtime).filePath(QStringLiteral("ultratab_compose")));
    ProcessComposeRunner runner(find_tool(QStringLiteral("python3")), helper);
    std::optional<Composer> composer;
    if (!runtime.isEmpty()) {
        composer.emplace(
            runner,
            Composer::Paths{QDir(runtime).filePath(QStringLiteral("ultratab_cards.json")),
                            QDir(runtime).filePath(QStringLiteral("ultratab_compose.jsonl")),
                            runtime, home, find_tool(QStringLiteral("claude"))},
            parse_composer(config.value(QStringLiteral("composer"))));
        QObject::connect(&source, &PublishedSource::loaded, &*composer, &Composer::setPublished);
    }
    source.reload();

    QQuickView view;
    view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    view.setTitle(QStringLiteral("Ultra Tab"));
    if (!load_overlay(view, deck, {.backdrop = false, .reduced_motion = platform::reduce_motion()}))
        return 1;
    Overlay overlay(view, deck, source, home);
    qInfo().noquote() << "Ultra Tab:" << platform::set_start_at_login(settings.start_at_login);
    if (composer) {
        // The card in front keeps its content while the overlay shows it.
        const auto hold = [&view, &deck, &composer] {
            composer->setHeld(view.isVisible()
                                  ? deck.front().value(QStringLiteral("agent")).toString()
                                  : QString());
        };
        QObject::connect(&deck, &Deck::changed, &*composer, hold);
        QObject::connect(&view, &QWindow::visibleChanged, &*composer, hold);
    }
    if (!platform::register_hotkey(*hotkey, [&overlay] { overlay.toggle(); }))
        qWarning().noquote() << "Ultra Tab: could not take" << describe(*hotkey)
                             << "; another app may hold it. Use --hotkey.";
    else
        qInfo().noquote() << "Ultra Tab:" << describe(*hotkey) << "shows the deck";
    if (parser.isSet(show_option))
        overlay.show();
    const int result = QGuiApplication::exec();
    platform::register_hotkey(*hotkey, {});
    return result;
}
