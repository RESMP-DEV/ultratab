// Loads Ultra Tab's overlay offscreen (QT_QPA_PLATFORM=offscreen, software
// Quick) from fixture files in a private runtime folder, drives the four
// answers with Qt key events sent to that offscreen window only, and saves
// captures under build/reports/ultratab/ for a person to look at. No native
// window, OS input, input source or clipboard is touched.
#include "deck.hpp"
#include "overlay_view.hpp"
#include "published.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lapis::ultratab;

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}
void settle(int ms) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}
void write(const QString& path, const QJsonObject& object) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "write a fixture file");
    file.write(QJsonDocument(object).toJson());
}

class FakeSender final : public Sender {
  public:
    struct Sent {
        QString agent;
        QString text;
    };
    std::vector<Sent> sent;
    void submit(const Agent& agent, const QString& text, Done done) override {
        sent.push_back({agent.id, text});
        done(true, {});
    }
};

QString id(int number) {
    return QStringLiteral("00000000-0000-4000-8000-0000000000%1")
        .arg(number, 2, 10, QLatin1Char('0'));
}

// Made-up agents, as lapis would publish them.
void writeFixture(const QString& runtime) {
    struct Row {
        const char* title;
        const char* folder;
        const char* category;
        const char* status;
        bool unseen;
        qint64 needed;
        const char* guess;
        const char* said;
        int requests;
    };
    const std::vector<Row> rows{
        {"persist GUI state", "lapis", "c1", "finished", true, 100, "merge it and install",
         "Restores the guessed prompt after a restart; checks pass.", 0},
        {"game server", "gameserver", "c3", "finished", true, 200, "send me the join info",
         "Server is up; your friend has not joined yet.", 0},
        {"fp8 gemm tune", "kernels", "c2", "idle", false, 300, "ship it for big shapes only",
         "New tile is faster on big shapes, slower on small ones.", 0},
        {"cleanup build cache", "lapis", "c1", "waiting", true, 400, "", "", 1},
        {"LoRA sweep", "sweeps", "c2", "working", false, 0, "", "", 0},
        {"site rebuild", "site", "c1", "working", false, 0, "", "", 0},
        {"vocab eval", "vocab", "c2", "working", false, 0, "", "", 0}};
    QJsonArray agents;
    QJsonArray states;
    for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
        const auto& row = rows.at(static_cast<std::size_t>(index));
        agents.append(QJsonObject{
            {QStringLiteral("id"), id(index)},
            {QStringLiteral("title"), QString::fromUtf8(row.title)},
            {QStringLiteral("category"), QString::fromUtf8(row.category)},
            {QStringLiteral("directory"), QStringLiteral("/work/") + QString::fromUtf8(row.folder)},
            {QStringLiteral("harness"), QStringLiteral("claude")},
            {QStringLiteral("mode"), QStringLiteral("claude")},
            {QStringLiteral("program"), QStringLiteral("/bin/claude")},
            {QStringLiteral("arguments"), QJsonArray{}},
            {QStringLiteral("endpoint"),
             QDir(runtime).filePath(id(index) + QStringLiteral(".sock"))}});
        QJsonObject state{{QStringLiteral("id"), id(index)},
                          {QStringLiteral("status"), QString::fromUtf8(row.status)},
                          {QStringLiteral("unseen"), row.unseen},
                          {QStringLiteral("requests"), row.requests},
                          {QStringLiteral("neededAtMs"), row.needed},
                          {QStringLiteral("turnAtMs"), row.needed}};
        if (row.requests > 0)
            state.insert(QStringLiteral("request"), QStringLiteral("Allow rm -rf build/cache?"));
        if (*row.guess != '\0')
            state.insert(QStringLiteral("offer"),
                         QJsonObject{{QStringLiteral("key"), id(index) + QStringLiteral(":1")},
                                     {QStringLiteral("text"), QString::fromUtf8(row.guess)},
                                     {QStringLiteral("seen"), row.needed >= 300},
                                     {QStringLiteral("said"), QString::fromUtf8(row.said)}});
        states.append(state);
    }
    write(QDir(runtime).filePath(QStringLiteral("workspace.json")),
          {{QStringLiteral("categories"),
            QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")},
                                   {QStringLiteral("name"), QStringLiteral("lapis")}},
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("c2")},
                                   {QStringLiteral("name"), QStringLiteral("kernels")}},
                       QJsonObject{{QStringLiteral("id"), QStringLiteral("c3")},
                                   {QStringLiteral("name"), QStringLiteral("games")}}}},
           {QStringLiteral("agents"), agents}});
    write(QDir(runtime).filePath(QStringLiteral("agent_state.json")),
          {{QStringLiteral("version"), 1},
           {QStringLiteral("pid"), QCoreApplication::applicationPid()},
           {QStringLiteral("agents"), states}});
}

QQuickItem* find(QQuickItem* item, const QString& name) {
    if (item->objectName() == name)
        return item;
    for (auto* child : item->childItems())
        if (auto* found = find(child, name))
            return found;
    return nullptr;
}

void key(QQuickView& view, int code, const QString& text = {},
         Qt::KeyboardModifiers modifiers = Qt::NoModifier, QEvent::Type type = QEvent::KeyPress) {
    QKeyEvent event(type, code, modifiers, text);
    QCoreApplication::sendEvent(&view, &event);
}

void capture(QQuickView& view, const QString& name) {
    settle(120);
    const auto image = view.grabWindow();
    require(!image.isNull(), "the overlay renders");
    const QDir reports(QStringLiteral(ULTRATAB_CAPTURE_DIR));
    require(reports.mkpath(QStringLiteral(".")), "the capture folder");
    const auto path = reports.filePath(name);
    require(image.save(path), "the capture is saved");
    std::cout << "capture: " << path.toStdString() << '\n';
}

void overlayAnswersEveryCard() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    const auto published = read_published(directory.path());
    require(published.has_registry && published.has_state && published.agents.size() == 7,
            "the fixture reads as lapis's publication");

    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(published);
    bool dismissed = false;
    QObject::connect(&deck, &Deck::dismissRequested, [&dismissed] { dismissed = true; });

    QQuickView view;
    view.resize(1280, 760);
    require(load_overlay(view, deck, {.backdrop = true, .reduced_motion = true}),
            "the overlay QML loads");
    view.show();
    view.requestActivate();
    auto* root = view.rootObject();
    auto* entry = find(root, QStringLiteral("entry"));
    require(entry != nullptr && waitFor([&] { return entry->hasActiveFocus(); }),
            "typing goes to the overlay's entry");
    auto* name = find(root, QStringLiteral("agentName"));
    require(name && name->property("text").toString() == QLatin1String("persist GUI state"),
            "the agent that needs you first is in front");
    require(find(root, QStringLiteral("behindCard"))->isVisible(), "one card peeks behind");
    capture(view, QStringLiteral("overlay-deck.png"));

    // Tab: lapis's guess goes to the front agent.
    key(view, Qt::Key_Tab);
    require(sender.sent.size() == 1 && sender.sent[0].agent == id(0) &&
                sender.sent[0].text == QLatin1String("merge it and install"),
            "Tab sends the guess to the front agent");
    require(waitFor([&] {
                return name->property("text").toString() == QLatin1String("game server");
            }),
            "the next card comes forward");

    // Typing anywhere, then Return.
    for (const QChar character : QStringLiteral("he is on the other server"))
        key(view, character.toUpper().unicode(), QString(character));
    require(entry->property("text").toString() == QLatin1String("he is on the other server"),
            "typed text collects in the card");
    capture(view, QStringLiteral("overlay-typing.png"));
    key(view, Qt::Key_Left); // moves the cursor while text is typed; never skips
    require(sender.sent.size() == 1 &&
                name->property("text").toString() == QLatin1String("game server"),
            "the left arrow edits typed text rather than skipping");
    key(view, Qt::Key_Return);
    require(sender.sent.size() == 2 && sender.sent[1].agent == id(1) &&
                sender.sent[1].text == QLatin1String("he is on the other server") &&
                entry->property("text").toString().isEmpty(),
            "Return sends the typed text to that agent");

    // Holding Option shows the speak placeholder and sends nothing.
    key(view, Qt::Key_Alt, {}, Qt::AltModifier);
    require(waitFor([&] { return deck.listening(); }, 2000), "holding Option listens");
    capture(view, QStringLiteral("overlay-listening.png"));
    key(view, Qt::Key_Alt, {}, Qt::NoModifier, QEvent::KeyRelease);
    require(!deck.listening() && sender.sent.size() == 2, "releasing it stops; nothing is sent");

    // Left with nothing typed skips; nothing is sent.
    require(name->property("text").toString() == QLatin1String("cleanup build cache"),
            "the request card (an unseen request ranks before a seen guess)");
    require(find(root, QStringLiteral("proposal"))
                ->property("text")
                .toString()
                .contains(QStringLiteral("Answer it in lapis")),
            "a request is answered in lapis");
    key(view, Qt::Key_Return);
    require(sender.sent.size() == 2, "Return types nothing over a request");
    key(view, Qt::Key_Left);
    require(sender.sent.size() == 2 &&
                name->property("text").toString() == QLatin1String("fp8 gemm tune"),
            "Left skips without sending");
    key(view, Qt::Key_Left);
    require(waitFor([&] { return find(root, QStringLiteral("empty"))->isVisible(); }),
            "an empty deck says nothing needs you");
    capture(view, QStringLiteral("overlay-empty.png"));
    key(view, Qt::Key_Escape);
    require(dismissed && sender.sent.size() == 2, "Escape with nothing typed puts it away");
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        overlayAnswersEveryCard();
    } catch (const std::exception& error) {
        std::cerr << "ultratab overlay test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ultratab overlay tests passed\n";
    return 0;
}
