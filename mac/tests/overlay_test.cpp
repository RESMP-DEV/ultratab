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
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
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

// Composed cards, as the composer would write them: every block type, a key
// in each accepted form, and one stale key that falls back to the plain card.
void writeCards(const QString& runtime) {
    // "<session>|<turn>|<needed>|<offer key>|<requests>"
    const auto key = [](int index, const QString& rest) {
        return id(index) + QLatin1Char('|') + rest;
    };
    const QJsonObject table{
        {QStringLiteral("type"), QStringLiteral("table")},
        {QStringLiteral("columns"), QJsonArray{QStringLiteral("check"), QStringLiteral("before"),
                                               QStringLiteral("after"), QStringLiteral("change")}},
        {QStringLiteral("rows"),
         QJsonArray{QJsonArray{QStringLiteral("restore prompt"), QStringLiteral("412 ms"),
                               QStringLiteral("38 ms"), QStringLiteral("-90.8%")},
                    QJsonArray{QStringLiteral("cold launch"), QStringLiteral("1,240 ms"),
                               QStringLiteral("1,198 ms"), QStringLiteral("-3.4%")},
                    QJsonArray{QStringLiteral("unit tests"), QStringLiteral("212"),
                               QStringLiteral("219"), QStringLiteral("+7")},
                    QJsonArray{QStringLiteral("ui-review captures"), QStringLiteral("14"),
                               QStringLiteral("14"), QStringLiteral("0")}}}};
    const QJsonObject first{
        {QStringLiteral("key"), key(0, QStringLiteral("100|100|%1:1|0").arg(id(0)))},
        {QStringLiteral("composed"), QStringLiteral("2026-10-06T21:04:00Z")},
        {QStringLiteral("model"), QStringLiteral("fixture-model")},
        {QStringLiteral("attention"), QStringLiteral("needs")},
        {QStringLiteral("since"), QStringLiteral("You last looked 3 h ago; 2 turns since")},
        {QStringLiteral("tldr"), QStringLiteral("Restored prompts now survive a restart; "
                                                "every check passes and launch got faster.")},
        {QStringLiteral("blocks"),
         QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                {QStringLiteral("text"),
                                 QStringLiteral("The guessed prompt is saved with the layout and "
                                                "restored before the first frame, so Tab works "
                                                "right after a relaunch.")}},
                    table,
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("link")},
                                {QStringLiteral("label"), QStringLiteral("Review captures")},
                                {QStringLiteral("url"),
                                 QStringLiteral("file:///tmp/fixture/report.html")}}}},
        {QStringLiteral("prompt"), QStringLiteral("merge it and install the build")}};
    const QJsonObject second{
        {QStringLiteral("key"), QStringLiteral("200|200|%1|0").arg(id(1) + QStringLiteral(":1"))},
        {QStringLiteral("composed"), QStringLiteral("2026-10-06T21:05:00Z")},
        {QStringLiteral("model"), QStringLiteral("fixture-model")},
        {QStringLiteral("attention"), QStringLiteral("needs")},
        {QStringLiteral("tldr"), QStringLiteral("Server is up on the new box; your friend is "
                                                "still on the old address.")},
        {QStringLiteral("blocks"),
         QJsonArray{
             QJsonObject{{QStringLiteral("type"), QStringLiteral("list")},
                         {QStringLiteral("items"),
                          QJsonArray{QStringLiteral("Moved the world save to the new machine"),
                                     QStringLiteral("Opened port 25565; health check passes"),
                                     QStringLiteral("No player has joined in 40 minutes")}}},
             QJsonObject{
                 {QStringLiteral("type"), QStringLiteral("diagram")},
                 {QStringLiteral("svg"),
                  QStringLiteral(
                      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 600 140'>"
                      "<defs><marker id='arrow' markerWidth='8' markerHeight='8' refX='6' "
                      "refY='4' orient='auto'><path d='M0 0 L8 4 L0 8 z' fill='#e8b931'/>"
                      "</marker></defs>"
                      "<rect x='10' y='40' width='150' height='60' rx='10' fill='none' "
                      "stroke='#4a86ff' stroke-width='2'/>"
                      "<text x='85' y='76' text-anchor='middle' font-size='16'>friend</text>"
                      "<rect x='225' y='40' width='150' height='60' rx='10' fill='none' "
                      "stroke='#5b6678' stroke-width='2' stroke-dasharray='6 5'/>"
                      "<text x='300' y='76' text-anchor='middle' font-size='16'>old box</text>"
                      "<rect x='440' y='40' width='150' height='60' rx='10' fill='none' "
                      "stroke='#e8b931' stroke-width='2'/>"
                      "<text x='515' y='76' text-anchor='middle' font-size='16'>new box</text>"
                      "<path d='M160 70 H222' stroke='#5b6678' stroke-width='2'/>"
                      "<path d='M375 70 H432' stroke='#e8b931' stroke-width='2' "
                      "marker-end='url(#arrow)'/>"
                      "<text x='300' y='126' text-anchor='middle' font-size='13' "
                      "fill='#8592a6'>he still connects to the old address</text></svg>")}}}},
        {QStringLiteral("prompt"),
         QStringLiteral("send him the new address and ping me when he joins")}};
    const QJsonObject stale{{QStringLiteral("key"), key(2, QStringLiteral("299|299|old|0"))},
                            {QStringLiteral("tldr"), QStringLiteral("An older turn's card")},
                            {QStringLiteral("prompt"), QStringLiteral("not this one")}};
    write(
        QDir(runtime).filePath(QStringLiteral("ultratab_cards.json")),
        {{QStringLiteral("v"), 1},
         {QStringLiteral("cards"), QJsonObject{{id(0), first}, {id(1), second}, {id(2), stale}}}});
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
    writeCards(runtime);
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

void click(QQuickView& view, QQuickItem* item, Qt::KeyboardModifiers modifiers) {
    require(item != nullptr, "the item to click");
    const auto at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, at, view.mapToGlobal(at), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          modifiers);
        QCoreApplication::sendEvent(&view, &event);
    }
    settle(20);
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

    require(published.composed.present && published.composed.cards.size() == 3,
            "the composed cards are read beside them");

    FakeSender sender;
    Deck deck(sender);
    std::vector<QUrl> opened;
    deck.setLinkOpener([&opened](const QUrl& url) { opened.push_back(url); });
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
    require(find(root, QStringLiteral("behindCard")) == nullptr, "no cards peek under the panel");
    // The lights: two need you, the request too; one could steer; three run.
    const auto groups = deck.groups();
    require(groups.value(QStringLiteral("needs")).toList().size() == 3 &&
                groups.value(QStringLiteral("steer")).toList().size() == 1 &&
                groups.value(QStringLiteral("running")).toList().size() == 3,
            "every agent is in its light");
    require(find(root, QStringLiteral("needsLight"))->isVisible() &&
                find(root, QStringLiteral("steerLight"))->isVisible() &&
                find(root, QStringLiteral("runningLight"))->isVisible(),
            "the footer shows the three lights");
    require(find(root, QStringLiteral("frontTile")) != nullptr &&
                find(root, QStringLiteral("lapisMapLit")) != nullptr,
            "the front agent's logo and its place in lapis show");
    // Command-L shows it in lapis and puts the overlay away.
    QString inLapis;
    deck.setLapisOpener([&inLapis](const QString& agent) { inLapis = agent; });
    key(view, Qt::Key_L, QStringLiteral("l"), Qt::ControlModifier);
    require(inLapis == id(0) && dismissed, "Command-L shows the front agent in lapis");
    dismissed = false;

    // The composed card: headline, text, table and link blocks. When the
    // person last looked frames the card; it is not shown.
    require(find(root, QStringLiteral("since")) == nullptr, "no since line");
    require(find(root, QStringLiteral("line"))
                ->property("text")
                .toString()
                .startsWith(QStringLiteral("Restored prompts")),
            "the composed tldr is the headline");
    require(find(root, QStringLiteral("textBlock")) && find(root, QStringLiteral("tableBlock")) &&
                find(root, QStringLiteral("link")),
            "text, table and link blocks render");
    require(find(root, QStringLiteral("proposal"))->property("text").toString() ==
                QLatin1String("merge it and install the build"),
            "the composed prompt is the proposed reply");
    capture(view, QStringLiteral("overlay-composed-table.png"));

    // Links: a plain click opens nothing; Command-click and Command-O do, and
    // the card stays.
    auto* link = find(root, QStringLiteral("link"));
    click(view, link, Qt::NoModifier);
    require(opened.empty(), "a plain click on a link opens nothing");
    click(view, link, Qt::ControlModifier);
    require(opened.size() == 1 &&
                opened[0] == QUrl(QStringLiteral("file:///tmp/fixture/report.html")),
            "Command-click opens the link");
    key(view, Qt::Key_O, QStringLiteral("o"), Qt::ControlModifier);
    require(opened.size() == 2 && sender.sent.empty() &&
                entry->property("text").toString().isEmpty() &&
                name->property("text").toString() == QLatin1String("persist GUI state"),
            "Command-O opens the first link and keeps the card");

    // Tab: the proposed reply goes to the front agent.
    key(view, Qt::Key_Tab);
    require(sender.sent.size() == 1 && sender.sent[0].agent == id(0) &&
                sender.sent[0].text == QLatin1String("merge it and install the build"),
            "Tab sends the proposed reply to the front agent");
    require(
        waitFor([&] { return name->property("text").toString() == QLatin1String("game server"); }),
        "the next card comes forward");
    require(find(root, QStringLiteral("listBlock")) && find(root, QStringLiteral("diagramBlock")),
            "list and diagram blocks render (key without the session prefix)");
    auto* diagram = find(root, QStringLiteral("diagram"));
    require(diagram && waitFor([&] { return diagram->property("status").toInt() == 1; }),
            "the diagram image loads");
    capture(view, QStringLiteral("overlay-composed-diagram.png"));

    // Typing anywhere, then Return.
    for (const QChar character : QStringLiteral("he is on the other server"))
        key(view, character.toUpper().unicode(), QString(character));
    require(entry->property("text").toString() == QLatin1String("he is on the other server"),
            "typed text collects in the card");
    capture(view, QStringLiteral("overlay-typing.png"));
    key(view, Qt::Key_Backspace); // erases while text is typed; never skips
    require(entry->property("text").toString() == QLatin1String("he is on the other serve") &&
                name->property("text").toString() == QLatin1String("game server"),
            "Delete erases typed text rather than skipping");
    key(view, Qt::Key_R, QStringLiteral("r"));
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
                .contains(QStringLiteral("own window")),
            "a request is answered where the agent runs");
    capture(view, QStringLiteral("overlay-request.png"));
    key(view, Qt::Key_Return);
    require(sender.sent.size() == 2, "Return types nothing over a request");
    key(view, Qt::Key_Left, {}, Qt::KeypadModifier); // as macOS delivers the arrow
    require(sender.sent.size() == 2 &&
                name->property("text").toString() == QLatin1String("fp8 gemm tune"),
            "Left skips without sending");
    // Its composed card is for an older turn: the plain card shows.
    require(find(root, QStringLiteral("line"))
                    ->property("text")
                    .toString()
                    .startsWith(QStringLiteral("New tile is faster")) &&
                find(root, QStringLiteral("proposal"))->property("text").toString() ==
                    QLatin1String("ship it for big shapes only"),
            "a stale composed card falls back to the plain card");
    capture(view, QStringLiteral("overlay-fallback.png"));
    key(view, Qt::Key_Backspace); // Delete with nothing typed skips too
    require(waitFor([&] { return find(root, QStringLiteral("empty"))->isVisible(); }),
            "an empty deck says nothing needs you");
    capture(view, QStringLiteral("overlay-empty.png"));
    key(view, Qt::Key_Escape);
    require(dismissed && sender.sent.size() == 2, "Escape with nothing typed puts it away");
}
// With motion on, answers still land at once: two Tabs in a row reach two
// agents without waiting for the slide.
void motionNeverDelaysInput() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(read_published(directory.path()));
    QQuickView view;
    view.resize(1280, 760);
    require(load_overlay(view, deck, {.backdrop = true, .reduced_motion = false}),
            "the overlay QML loads");
    view.show();
    view.requestActivate();
    auto* entry = find(view.rootObject(), QStringLiteral("entry"));
    require(entry != nullptr && waitFor([&] { return entry->hasActiveFocus(); }),
            "typing goes to the overlay's entry");
    key(view, Qt::Key_Tab);
    key(view, Qt::Key_Tab);
    require(sender.sent.size() == 2 && sender.sent[0].agent == id(0) &&
                sender.sent[1].agent == id(1),
            "a second Tab during the slide answers the next card");
    // Mid-slide, for a person to see the motion's shape.
    settle(60);
    const auto image = view.grabWindow();
    const QDir reports(QStringLiteral(ULTRATAB_CAPTURE_DIR));
    require(!image.isNull() && image.save(reports.filePath(QStringLiteral("overlay-motion.png"))),
            "the mid-slide capture is saved");
    // A skip, frame by frame: the card leaves left as the next edge rises.
    settle(400);
    key(view, Qt::Key_Left, {}, Qt::KeypadModifier);
    for (int frame = 1; frame <= 4; ++frame) {
        settle(55);
        require(view.grabWindow().save(
                    reports.filePath(QStringLiteral("overlay-skip-%1.png").arg(frame))),
                "the skip frames are saved");
    }
}
// A card the composer judged only running is not a card: its agent sits in
// the green light. Command-Return opens the push back; Return is a new line
// in it and Command-Return sends it, as typed, to the card's agent.
void pushBackAndRunning() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    const auto cards_path = QDir(directory.path()).filePath(QStringLiteral("ultratab_cards.json"));
    QFile cards_file(cards_path);
    require(cards_file.open(QIODevice::ReadOnly), "read the fixture cards");
    auto cards = QJsonDocument::fromJson(cards_file.readAll()).object();
    cards_file.close();
    auto all = cards.value(QStringLiteral("cards")).toObject();
    auto second = all.value(id(1)).toObject();
    second.insert(QStringLiteral("attention"), QStringLiteral("fyi"));
    all.insert(id(1), second);
    cards.insert(QStringLiteral("cards"), all);
    write(cards_path, cards);

    FakeSender sender;
    Deck deck(sender);
    std::vector<QJsonObject> logged;
    deck.setAnswerLog([&logged](const QJsonObject& answer) { logged.push_back(answer); });
    deck.setPublished(read_published(directory.path()));
    const auto groups = deck.groups();
    const auto running = groups.value(QStringLiteral("running")).toList();
    require(running.size() == 4 && running.front().toMap().value(QStringLiteral("name")) ==
                                       QLatin1String("game server"),
            "a report the composer judged needs nothing is in the green light");

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
    // persist needs you; the request comes next, never game server.
    require(name->property("text").toString() == QLatin1String("persist GUI state") &&
                deck.behind().value(QStringLiteral("name")) == QLatin1String("cleanup build cache"),
            "the running agent is skipped over");

    key(view, Qt::Key_Return, {}, Qt::ControlModifier);
    auto* box = find(root, QStringLiteral("pushBox"));
    auto* text = find(root, QStringLiteral("pushText"));
    require(box && box->isVisible() && text && waitFor([&] { return text->hasActiveFocus(); }),
            "Command-Return opens the push back");
    for (const QChar character : QStringLiteral("not yet"))
        key(view, character.toUpper().unicode(), QString(character));
    key(view, Qt::Key_Return);
    for (const QChar character : QStringLiteral("profile first"))
        key(view, character.toUpper().unicode(), QString(character));
    require(sender.sent.empty(), "Return is a new line in the push back");
    capture(view, QStringLiteral("overlay-pushback.png"));
    key(view, Qt::Key_Return, {}, Qt::ControlModifier);
    require(sender.sent.size() == 1 && sender.sent[0].agent == id(0) &&
                sender.sent[0].text == QLatin1String("not yet\nprofile first"),
            "Command-Return sends the push back to that agent");
    require(!logged.empty() &&
                logged.back().value(QStringLiteral("how")) == QLatin1String("pushback"),
            "it is logged as a push back");
    require(!box->isVisible() && entry->hasActiveFocus(), "the one-line reply is back");
}
// With the deck empty the entry still has to hold the keyboard: an invisible
// item cannot, and Escape and the category keys are how the overlay is put
// away.
void anEmptyDeckStillTakesKeys() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(read_published(directory.path()));
    bool dismissed = false;
    QObject::connect(&deck, &Deck::dismissRequested, [&dismissed] { dismissed = true; });
    QQuickView view;
    view.resize(1280, 760);
    require(load_overlay(view, deck, {.backdrop = true, .reduced_motion = true}),
            "the overlay QML loads");
    view.show();
    view.requestActivate();
    auto* entry = find(view.rootObject(), QStringLiteral("entry"));
    require(entry != nullptr && waitFor([&] { return entry->hasActiveFocus(); }),
            "typing goes to the overlay's entry");
    const int cards = 4;
    for (int index = 0; index < cards; ++index)
        key(view, Qt::Key_Left, {}, Qt::KeypadModifier);
    require(deck.front().isEmpty(), "the deck is empty");
    require(entry->isVisible() && waitFor([&] { return entry->hasActiveFocus(); }),
            "the entry keeps the keyboard with an empty deck");
    key(view, Qt::Key_Escape);
    require(dismissed, "Escape still puts the overlay away");
}

// The frozen card that leaves is placed where the card sat, so clearing a
// wrapped draft or emptying the deck does not move it mid-slide.
void theLeavingCardKeepsItsPlace() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(read_published(directory.path()));
    QQuickView view;
    // The width the overlay asks the window for, so the room past the panel is
    // the room the copy really has.
    view.resize(720, 760);
    // Motion on: the ghost only exists while the slide runs.
    require(load_overlay(view, deck, {.backdrop = true, .reduced_motion = false}),
            "the overlay QML loads");
    view.show();
    view.requestActivate();
    auto* entry = find(view.rootObject(), QStringLiteral("entry"));
    require(entry != nullptr && waitFor([&] { return entry->hasActiveFocus(); }),
            "typing goes to the overlay's entry");
    auto* reply = find(view.rootObject(), QStringLiteral("replyLine"));
    require(reply != nullptr && reply->property("height").toReal() > 0,
            "the reply line is showing with a card");
    const qreal showing = reply->property("height").toReal();
    // Skip the rest of the deck, so the leaving copy is the last card's and
    // the reply line collapses to nothing while it slides.
    while (!deck.front().isEmpty())
        key(view, Qt::Key_Left, {}, Qt::KeypadModifier);
    auto* ghost = find(view.rootObject(), QStringLiteral("ghost"));
    require(ghost != nullptr, "the leaving card exists");
    // Follow the copy's left edge frame by frame while it leaves: the copy is
    // drawn in this window, so it can only travel into the room past the
    // panel's edge on the side it leaves by, not wherever a long slide would
    // be clipped away.
    auto* panel = find(view.rootObject(), QStringLiteral("panel"));
    const qreal panel_x = panel->property("x").toReal();
    const qreal panel_w = panel->property("width").toReal();
    const qreal room = std::min(panel_x, view.width() - panel_x - panel_w);
    require(room > 0, "there is room past the panel's edge for the copy to travel into");
    double furthest = 0;
    while (ghost->isVisible() && furthest < room * 4) {
        settle(16);
        const auto at = -ghost->mapToScene(QPointF(0, 0)).x();
        furthest = std::max(furthest, at);
    }
    require(furthest <= room + 0.5,
            "the leaving card travels only into the room past the panel's edge");
    require(reply->property("height").toReal() == 0 && showing > 0,
            "the reply line collapsed when the deck emptied");
}

// A push back is a send: it leaves right with a green edge, and a refused one
// leaves the card where it is.
void aPushBackLeavesLikeASend() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    writeFixture(directory.path());
    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(read_published(directory.path()));
    QQuickView view;
    view.resize(1280, 760);
    require(load_overlay(view, deck, {.backdrop = true, .reduced_motion = false}),
            "the overlay QML loads");
    view.show();
    view.requestActivate();
    auto* root = view.rootObject();
    auto* entry = find(root, QStringLiteral("entry"));
    require(entry != nullptr && waitFor([&] { return entry->hasActiveFocus(); }),
            "typing goes to the overlay's entry");
    auto* name = find(root, QStringLiteral("agentName"));
    const auto before = name->property("text").toString();
    key(view, Qt::Key_Return, {}, Qt::ControlModifier); // opens the push back
    auto* box = find(root, QStringLiteral("pushBox"));
    auto* text = find(root, QStringLiteral("pushText"));
    require(box && text && waitFor([&] { return text->hasActiveFocus(); }),
            "Command-Return opens the push back");
    for (const QChar character : QStringLiteral("use the new address"))
        key(view, character.toUpper().unicode(), QString(character));
    key(view, Qt::Key_Return, {}, Qt::ControlModifier);
    require(sender.sent.size() == 1, "the push back was sent");
    auto* ghost = find(root, QStringLiteral("ghost"));
    require(ghost != nullptr && ghost->property("direction").toInt() == 1,
            "a sent push back leaves right, like a send and not a skip");
    require(sender.sent[0].agent == id(0) && before == QLatin1String("persist GUI state"),
            "and it went to the card it was written for");

    // Escape out of the box returns the text to the one-line reply and keeps
    // it written for the card the box was opened on, even when a card that
    // outranks it arrives meanwhile: clearing `pushing` before the text went
    // back let `typing` rest on an empty entry, which dropped the draft pin and
    // rebound the restored text to the card that had taken the front.
    settle(400);
    const auto pinned = deck.front().value(QStringLiteral("name")).toString();
    const auto for_agent = deck.front().value(QStringLiteral("agent")).toString();
    key(view, Qt::Key_Return, {}, Qt::ControlModifier);
    auto* text2 = find(root, QStringLiteral("pushText"));
    require(text2 && waitFor([&] { return text2->hasActiveFocus(); }), "the push back opens again");
    for (const QChar character : QStringLiteral("and this one"))
        key(view, character.toUpper().unicode(), QString(character));
    // A request, which outranks every other card, arrives while the box is up.
    auto busier = read_published(directory.path());
    busier.states[id(3)].requests = 1;
    busier.states[id(3)].request = QStringLiteral("Allow rm -rf build/cache?");
    deck.setPublished(busier);
    settle(20);
    // The pin must not be dropped and retaken on the way out: that republishes
    // the front card and flickers every binding that reads it.
    int published = 0;
    QObject::connect(&deck, &Deck::changed, [&published] { ++published; });
    key(view, Qt::Key_Escape);
    require(published == 0, "Escape does not republish the deck");
    auto* entry2 = find(root, QStringLiteral("entry"));
    require(entry2 != nullptr &&
                entry2->property("text").toString() == QLatin1String("and this one"),
            "Escape returns the text to the one-line reply");
    require(deck.front().value(QStringLiteral("agent")).toString() == for_agent,
            "and it is still written for the card the box was opened on");
    require(sender.sent.size() == 1, "Escape sends nothing");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        overlayAnswersEveryCard();
        motionNeverDelaysInput();
        pushBackAndRunning();
        anEmptyDeckStillTakesKeys();
        theLeavingCardKeepsItsPlace();
        aPushBackLeavesLikeASend();
    } catch (const std::exception& error) {
        std::cerr << "ultratab overlay test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ultratab overlay tests passed\n";
    return 0;
}
