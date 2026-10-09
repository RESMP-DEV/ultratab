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
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPropertyAnimation>
#include <QQuickView>
#include <QSaveFile>
#include <QScreen>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <optional>
#include <sys/stat.h>
#include <unistd.h>
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

// One answer per line in runtime/ultratab_answers.jsonl, owner-only, the
// same file the phone gateway appends to; past 16 MB it moves to `.1`.
void log_answer(const QString& path, QJsonObject answer) {
    constexpr qint64 limit = qint64{16} * 1024 * 1024;
    answer.insert(QStringLiteral("t"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    answer.insert(QStringLiteral("from"), QStringLiteral("mac"));
    const auto line = QJsonDocument(answer).toJson(QJsonDocument::Compact) + '\n';
    const auto encoded = QFile::encodeName(path);
    struct stat existing{};
    // The phone gateway owns this file too. Never follow a link, and rotate
    // only a regular file; ::rename replaces the previous archive atomically,
    // so it cannot delete an archive another writer just moved into place.
    if (::lstat(encoded.constData(), &existing) == 0) {
        if (!S_ISREG(existing.st_mode)) {
            qWarning().noquote() << "Ultra Tab: answer log is not a regular file:" << path;
            return;
        }
        if (qint64(existing.st_size) + line.size() > limit) {
            const auto archive = QFile::encodeName(path + QStringLiteral(".1"));
            if (::rename(encoded.constData(), archive.constData()) != 0) {
                qWarning().noquote() << "Ultra Tab: answers not rotated:" << qt_error_string(errno);
            }
        }
    }
    // Owner-only from creation, without the window an open-then-chmod leaves.
    const int fd = ::open(encoded.constData(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0) {
        qWarning().noquote() << "Ultra Tab: answer not logged:" << qt_error_string(errno);
        return;
    }
    QFile file;
    if (!file.open(fd, QIODevice::WriteOnly | QIODevice::Append, QFileDevice::AutoCloseHandle)) {
        qWarning().noquote() << "Ultra Tab: answer not logged:" << file.errorString();
        return;
    }
    if (::fchmod(fd, 0600) != 0) {
        qWarning().noquote() << "Ultra Tab: answer log not owner-only:" << qt_error_string(errno);
    }
    if (file.write(line) != line.size() || !file.flush()) {
        qWarning().noquote() << "Ultra Tab: answer not written:" << file.errorString();
    }
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
        if (auto* host = view_.findChild<OverlayHost*>()) {
            QObject::connect(host, &OverlayHost::contentSizeChanged, &view_,
                             [this] { follow_content(); });
            QObject::connect(host, &OverlayHost::panelChanged, &view_, [this] { follow_panel(); });
            QObject::connect(host, &OverlayHost::dragRequested, &view_,
                             [this, host](QPoint to, bool done) { drag(*host, to, done); });
        }
        fade_.setDuration(110);
        fade_.setEasingCurve(QEasingCurve::OutCubic);
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
        // The deck is read before the first frame, and the card's own motion
        // is held back: opening is one short fade of the whole window.
        if (auto* host = view_.findChild<OverlayHost*>())
            emit host->appearing();
        source_.reload();
        auto* screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr)
            screen = QGuiApplication::primaryScreen();
        if (screen == nullptr)
            return;
        const auto area = screen->availableGeometry();
        // As large as the panel and its peeking cards; it grows and shrinks
        // with the card from its top edge.
        const QSize size = wanted_size(area);
        const auto key = screen_key(screen->name(), screen->geometry());
        const auto saved = positions_.constFind(key);
        placing_ = true;
        view_.setGeometry(place_window(
            area, size, saved == positions_.cend() ? std::nullopt : std::optional<QPoint>(*saved)));
        placing_ = false;
        shown_.start();
        if (!reduce_motion_) {
            view_.setOpacity(0.0);
            fade_.stop();
            fade_.setStartValue(0.0);
            fade_.setEndValue(1.0);
        }
        view_.show();
        if (!translucent_)
            translucent_ = platform::make_translucent(view_);
        follow_panel();
        platform::activate();
        view_.requestActivate();
        if (!reduce_motion_)
            fade_.start();
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
    [[nodiscard]] QSize wanted_size(const QRect& area) const {
        auto* host = view_.findChild<OverlayHost*>();
        const auto wanted = host != nullptr && host->contentSize().isValid() ? host->contentSize()
                                                                             : QSize(720, 400);
        // Overlay.qml budgets 0.7 of the screen for the panel and adds about 56
        // points of chrome around it, so a 3/4 cap cut the bottom edge off on
        // every screen shorter than roughly 1120 points. Bound against the
        // available area instead and keep the whole content on screen.
        return wanted.boundedTo({area.width(), area.height()});
    }
    void follow_content() {
        auto* screen = view_.screen();
        if (screen == nullptr || !view_.isVisible())
            return;
        const auto area = screen->availableGeometry();
        const auto size = wanted_size(area);
        // A taller card grows the window from its top-left, and a dragged
        // position is remembered, so the window can end up past the bottom of
        // the screen. Clamp where it is: place_window's full reset would jump
        // the panel back to the default spot.
        const int lowest = std::max(area.top(), area.bottom() - size.height() + 1);
        const int rightmost = std::max(area.left(), area.right() - size.width() + 1);
        const QPoint where(std::clamp(view_.x(), area.left(), rightmost),
                           std::clamp(view_.y(), area.top(), lowest));
        if (size == view_.size() && where == view_.position())
            return;
        placing_ = true;
        if (size != view_.size())
            view_.resize(size);
        if (where != view_.position())
            view_.setPosition(where);
        placing_ = false;
    }
    // The panel dragged by its background: the window follows the pointer and
    // snaps to the screen's center line and set heights (snap_window).
    void drag(OverlayHost& host, QPoint to, bool done) {
        auto* screen = QGuiApplication::screenAt(to + QPoint(view_.width() / 2, 40));
        if (screen == nullptr)
            screen = view_.screen();
        if (screen == nullptr)
            return;
        const auto snap = snap_window(screen->availableGeometry(), view_.size(),
                                      static_cast<int>(host.panel().y()),
                                      static_cast<int>(host.panel().height()), to);
        view_.setPosition(snap.position);
        host.setSnap(snap.centered && !done, snap.level && !done);
        if (done)
            remember();
    }
    void follow_panel() {
        if (auto* host = view_.findChild<OverlayHost*>(); host != nullptr && translucent_)
            platform::set_blur_rect(view_, host->panel(), host->radius());
    }
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
    const bool reduce_motion_ = platform::reduce_motion();
    // Opening: the whole window, blur included, fades in.
    QPropertyAnimation fade_{&view_, "opacity"};
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
    // Command-L: lapis shows that agent. The window watches this request
    // beside its registry; Ultra Tab then brings lapis forward.
    if (!source.runtime().isEmpty())
        deck.setLapisOpener(
            [path = QDir(source.runtime()).filePath(QStringLiteral("ultratab_open.json"))](
                const QString& agent) {
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly)) {
                    file.write(QJsonDocument(QJsonObject{{QStringLiteral("agent"), agent},
                                                         {QStringLiteral("atMs"),
                                                          QDateTime::currentMSecsSinceEpoch()}})
                                   .toJson(QJsonDocument::Compact));
                    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
                    file.commit();
                }
                platform::activate_app(QStringLiteral("dev.lapis.desktop"));
            });
    if (!source.runtime().isEmpty())
        deck.setAnswerLog(
            [path = QDir(source.runtime()).filePath(QStringLiteral("ultratab_answers.jsonl"))](
                const QJsonObject& answer) { log_answer(path, answer); });

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
