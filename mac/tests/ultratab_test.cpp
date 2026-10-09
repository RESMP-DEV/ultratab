// Ultra Tab's deck: which agents are cards and in what order, the four
// answers against a fake transport, the published files it reads, the global
// key it parses, and the join it makes to an agent's session service (against
// a fake service speaking wire v6).
#include "deck.hpp"
#include "hotkey.hpp"
#include "launch_spec.hpp"
#include "published.hpp"
#include "session_sender.hpp"
#include "settings.hpp"
#include "transport/local_protocol.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <lapis/session/terminal.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lapis::ultratab;
namespace wire = lapis::session::wire;

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

// Fixture ids: a UUID per letter.
QString id(char letter) {
    return QStringLiteral("00000000-0000-4000-8000-00000000000") +
           QString::number(letter - 'a', 16);
}
QString fixtureRuntime() { return QStringLiteral("/fixture/runtime"); }

QJsonObject record(char letter, const QString& title, const QString& category) {
    return {{QStringLiteral("id"), id(letter)},
            {QStringLiteral("title"), title},
            {QStringLiteral("category"), category},
            {QStringLiteral("directory"), QStringLiteral("/work/") + title},
            {QStringLiteral("harness"), QStringLiteral("claude")},
            {QStringLiteral("mode"), QStringLiteral("claude")},
            {QStringLiteral("program"), QStringLiteral("/bin/claude")},
            {QStringLiteral("arguments"), QJsonArray{QStringLiteral("--continue")}},
            {QStringLiteral("endpoint"),
             fixtureRuntime() + QLatin1Char('/') + id(letter) + QStringLiteral(".sock")}};
}
QJsonObject state(char letter, const QString& status, bool unseen, qint64 needed,
                  const QJsonObject& offer = {}, int requests = 0) {
    QJsonObject object{
        {QStringLiteral("id"), id(letter)},     {QStringLiteral("status"), status},
        {QStringLiteral("unseen"), unseen},     {QStringLiteral("requests"), requests},
        {QStringLiteral("neededAtMs"), needed}, {QStringLiteral("turnAtMs"), needed}};
    if (requests > 0)
        object.insert(QStringLiteral("request"), QStringLiteral("Allow rm -rf build? It asks."));
    if (!offer.isEmpty())
        object.insert(QStringLiteral("offer"), offer);
    return object;
}
QJsonObject offer(const QString& key, const QString& text, bool seen, const QString& said) {
    return {{QStringLiteral("key"), key},
            {QStringLiteral("text"), text},
            {QStringLiteral("seen"), seen},
            {QStringLiteral("said"), said}};
}

// A–D wait in each of lapis's tiers; E is at work (lapis still counts it as
// unseen), F finished but was seen with no guess, G is unknown; H's record
// points outside the runtime folder and is never read.
Published fixture() {
    QJsonArray agents{record('a', QStringLiteral("persist-gui"), QStringLiteral("c1")),
                      record('b', QStringLiteral("gameserver"), QStringLiteral("c2")),
                      record('c', QStringLiteral("cleanup"), QStringLiteral("c1")),
                      record('d', QStringLiteral("gemm-tune"), QStringLiteral("c1")),
                      record('e', QStringLiteral("lora"), QStringLiteral("c2")),
                      record('f', QStringLiteral("site"), QStringLiteral("c1")),
                      record('g', QStringLiteral("vocab"), QStringLiteral("c1"))};
    auto stray = record('h', QStringLiteral("stray"), QStringLiteral("c1"));
    stray.insert(QStringLiteral("endpoint"), QStringLiteral("/elsewhere/h.sock"));
    agents.append(stray);
    const QJsonObject registry{
        {QStringLiteral("categories"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")},
                                {QStringLiteral("name"), QStringLiteral("lapis")}},
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("c2")},
                                {QStringLiteral("name"), QStringLiteral("games")}}}},
        {QStringLiteral("agents"), agents}};
    const QJsonObject states{
        {QStringLiteral("version"), 1},
        {QStringLiteral("pid"), 4242},
        {QStringLiteral("agents"),
         QJsonArray{
             state('a', QStringLiteral("finished"), true, 300,
                   offer(QStringLiteral("a:1"), QStringLiteral("merge it and install"), false,
                         QStringLiteral("**Restores** after a restart; checks pass. Details "
                                        "follow."))),
             state('b', QStringLiteral("finished"), true, 100),
             state('c', QStringLiteral("waiting"), true, 200, {}, 1),
             state('d', QStringLiteral("idle"), false, 50,
                   offer(QStringLiteral("d:1"), QStringLiteral("ship it for big shapes only"), true,
                         QStringLiteral("New tile is faster on big shapes."))),
             state('e', QStringLiteral("working"), true, 10,
                   offer(QStringLiteral("e:1"), QStringLiteral("keep going"), false, {})),
             state('f', QStringLiteral("finished"), false, 20),
             state('g', QStringLiteral("unknown"), true, 30),
             state('h', QStringLiteral("finished"), true, 1)}}};
    return parse_published(fixtureRuntime(), QJsonDocument(registry).toJson(),
                           QJsonDocument(states).toJson());
}

QStringList names(const std::vector<Card>& cards) {
    QStringList found;
    for (const auto& card : cards)
        found << card.name;
    return found;
}

void readsWhatLapisPublishes() {
    const auto published = fixture();
    require(published.has_registry && published.has_state && published.problem.isEmpty() &&
                published.writer_pid == 4242,
            "both files are read");
    require(published.agents.size() == 7, "a record outside the runtime folder is never read");
    const auto& first = published.agents.front();
    require(first.claude_mode && first.arguments == QStringList{QStringLiteral("--continue")} &&
                first.endpoint ==
                    fixtureRuntime() + QLatin1Char('/') + id('a') + QStringLiteral(".sock"),
            "the launch the service was started with, and its private endpoint");
    const auto other = parse_published(fixtureRuntime(), {}, R"({"version": 2, "agents": []})");
    require(!other.has_state && !other.problem.isEmpty(), "another state version is refused");
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    const auto none = read_published(directory.path());
    require(!none.has_registry && !none.has_state, "missing files are nothing, not an error");
    require(!read_published({}).problem.isEmpty(), "no lapis home");
}

void cardsFollowLapisTabOrder() {
    const auto cards = waiting_cards(fixture());
    require(names(cards) == QStringList{QStringLiteral("persist-gui"), QStringLiteral("gameserver"),
                                        QStringLiteral("cleanup"), QStringLiteral("gemm-tune")},
            "a guess not yet seen, then unseen turns and requests oldest first, then a seen "
            "guess; agents at work, seen without a guess, or unknown are not cards");
    require(cards.at(0).tier == 0 && cards.at(1).tier == 1 && cards.at(2).tier == 1 &&
                cards.at(3).tier == 2,
            "lapis's tiers");
    require(cards.at(0).line == QLatin1String("Restores after a restart; checks pass.") &&
                cards.at(0).proposal == QLatin1String("merge it and install") &&
                cards.at(0).folder == QLatin1String("persist-gui") &&
                cards.at(0).category_name == QLatin1String("lapis"),
            "what happened in one sentence, the guess, the project and category");
    require(cards.at(1).line == QLatin1String("Finished a turn.") && cards.at(1).proposal.isEmpty(),
            "a finished turn without a guess");
    require(cards.at(2).request && cards.at(2).proposal.isEmpty() &&
                cards.at(2).line == QLatin1String("Allow rm -rf build?"),
            "a request shows its reason and offers no guess");

    // Equal tier and wait: registry order decides.
    auto tied = fixture();
    tied.states[id('c')].needed_at_ms = 100;
    const auto ordered = waiting_cards(tied);
    require(ordered.at(1).name == QLatin1String("gameserver") &&
                ordered.at(2).name == QLatin1String("cleanup"),
            "ties keep the registry order");
}

void sentencesAreShort() {
    require(one_sentence(QStringLiteral("# Done.\n\nAll `tests` pass.")) == QLatin1String("Done."),
            "the first sentence, without markup");
    require(one_sentence(QStringLiteral("Edited agent_state.json and v1.2 notes")) ==
                QLatin1String("Edited agent_state.json and v1.2 notes"),
            "file names and versions are not sentence ends");
    const auto clipped =
        one_sentence(QString(30, QLatin1Char('w')).append(QStringLiteral(" ")).repeated(10), 60);
    require(clipped.size() <= 60 && clipped.endsWith(QChar(0x2026)),
            "long text ends in an ellipsis");
}

// Records what would be sent; answers when told.
class FakeSender final : public Sender {
  public:
    struct Sent {
        QString agent;
        QString endpoint;
        QString text;
        Done done;
    };
    std::vector<Sent> sent;
    void submit(const Agent& agent, const QString& text, Done done) override {
        sent.push_back({agent.id, agent.endpoint, text, std::move(done)});
    }
};

void fourAnswers() {
    FakeSender sender;
    Deck deck(sender);
    deck.setWriterCheck([](qint64 pid) { return pid == 4242; });
    deck.setPublished(fixture());
    require(deck.notice().isEmpty(), "lapis is running");
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("persist-gui") &&
                deck.behind().value(QStringLiteral("name")) == QLatin1String("gameserver"),
            "one card in front, one behind");

    // Accept: lapis's guess goes to that agent's session.
    require(deck.accept(), "Tab accepts the guess");
    require(sender.sent.size() == 1 && sender.sent[0].agent == id('a') &&
                sender.sent[0].endpoint ==
                    fixtureRuntime() + QLatin1Char('/') + id('a') + QStringLiteral(".sock") &&
                sender.sent[0].text == QLatin1String("merge it and install"),
            "the guess, to the right agent");
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("gameserver"),
            "the next card comes forward at once");
    sender.sent[0].done(true, {});
    require(deck.message() == QLatin1String("Sent to persist-gui") &&
                deck.history().front().toMap().value(QStringLiteral("outcome")) ==
                    QLatin1String("sent"),
            "the send is confirmed and kept in history");

    // Type: no guess here, so Tab does nothing; Enter sends what was typed.
    require(!deck.accept() && sender.sent.size() == 1, "Tab without a guess sends nothing");
    require(!deck.send(QStringLiteral("   ")), "blank text sends nothing");
    require(deck.send(QStringLiteral("  tell him the new address  ")), "Enter sends typed text");
    require(sender.sent.size() == 2 && sender.sent[1].agent == id('b') &&
                sender.sent[1].text == QLatin1String("tell him the new address"),
            "typed text, trimmed, to the right agent");
    // The service refuses it: the card comes back with the reason.
    sender.sent[1].done(false, QStringLiteral("the agent's session closed"));
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("gameserver") &&
                deck.message().contains(QStringLiteral("the agent's session closed")),
            "a refused send brings the card back and says why");
    require(deck.send(QStringLiteral("again")), "and it can be answered again");
    sender.sent[2].done(true, {});

    // A request: no accept or typing over it; skip still works.
    require(deck.front().value(QStringLiteral("request")).toBool(), "the request card");
    require(!deck.accept() && !deck.send(QStringLiteral("yes")) && sender.sent.size() == 3,
            "nothing is typed over a pending request");

    // Speak: listening only; nothing is sent.
    deck.setListening(true);
    require(deck.listening() && sender.sent.size() == 3, "speaking sends nothing yet");
    deck.setListening(false);

    // Skip: the card goes, nothing is sent, history keeps it.
    require(deck.skip(), "Left skips");
    require(sender.sent.size() == 3 && deck.history().front().toMap().value(
                                           QStringLiteral("how")) == QLatin1String("skipped"),
            "skip sends nothing and stays in history");
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("gemm-tune") &&
                deck.behind().isEmpty(),
            "the last card");
    require(deck.skip() && deck.front().isEmpty() && !deck.skip() && !deck.accept(),
            "an empty deck takes no answers");

    // Still the same states: answered cards stay away. A new turn brings one back.
    auto next = fixture();
    deck.setPublished(next);
    require(deck.front().isEmpty(), "answered cards stay hidden while nothing changed");
    next.states[id('a')].turn_at_ms = 999;
    auto& renewed = next.states[id('a')].offer;
    require(renewed.has_value(), "the fixture's guess");
    if (renewed)
        renewed->key = QStringLiteral("a:2");
    deck.setPublished(next);
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("persist-gui"),
            "a new turn is a new card");

    // The rail: waiting counts per category, and filtering by one.
    const auto rail = deck.rail();
    require(rail.size() == 3 && rail.at(0).toMap().value(QStringLiteral("count")).toInt() == 1 &&
                rail.at(1).toMap().value(QStringLiteral("count")).toInt() == 1 &&
                rail.at(2).toMap().value(QStringLiteral("count")).toInt() == 0,
            "all, then each category, with how many wait");
    deck.nextCategory(2);
    require(deck.front().isEmpty() &&
                deck.rail().at(2).toMap().value(QStringLiteral("selected")).toBool(),
            "a category shows only its own cards");
    deck.nextCategory(1);
    require(!deck.front().isEmpty(), "and wraps back to all");
    require(deck.running().size() == 1 && deck.running().front().toMap().value(
                                              QStringLiteral("name")) == QLatin1String("lora"),
            "agents at work, by name");
    deck.setWriterCheck([](qint64) { return false; });
    require(!deck.notice().isEmpty(), "a closed lapis is noticed");
}

// A card that comes forward while a reply is typed never receives it.
void draftsKeepTheirCard() {
    FakeSender sender;
    Deck deck(sender);
    deck.setPublished(fixture());
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("persist-gui"),
            "persist-gui is in front");
    deck.setDrafting(true);
    // gameserver gets a guess it has not shown and has waited longer: it
    // would go in front of persist-gui.
    auto next = fixture();
    next.states[id('b')].offer = Offer{QStringLiteral("b:1"), QStringLiteral("restart it"),
                                       QStringLiteral("It crashed."), false};
    deck.setPublished(next);
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("persist-gui"),
            "the card being typed to stays in front");
    deck.setDrafting(false);
    require(deck.front().value(QStringLiteral("name")) == QLatin1String("gameserver"),
            "cleared text lets the order through again");
    deck.setDrafting(true);
    // gameserver starts working (answered in lapis) before Return.
    auto busy = next;
    busy.states[id('b')].status = QStringLiteral("working");
    deck.setPublished(busy);
    require(!deck.send(QStringLiteral("for gameserver")) &&
                !deck.send(QStringLiteral("for gameserver")) && sender.sent.empty(),
            "a draft is never sent to another agent, however often Return is pressed");
    deck.setDrafting(false); // Escape clears it
    deck.setDrafting(true);
    require(deck.send(QStringLiteral("for persist-gui")) && sender.sent.size() == 1 &&
                sender.sent[0].agent == id('a'),
            "the next draft goes to the card in front");
}

void hotkeys() {
    const auto standard = parse_hotkey(QStringLiteral("Option-Space"));
    require(standard && standard->option && !standard->command && standard->key == "Space",
            "either Option key");
    const auto other = parse_hotkey(QStringLiteral("cmd+shift+u"));
    require(other && other->command && other->shift && other->key == "U" &&
                describe(*other) == QLatin1String("Shift-Command-U"),
            "aliases, either separator, any case");
    require(parse_hotkey(QStringLiteral("Control-F12")).has_value(), "function keys");
    const auto left = parse_hotkey(QString::fromLatin1(default_hotkey));
    require(left && left->option && left->optionSide == Side::left && left->key == "Space" &&
                describe(*left) == QLatin1String("LeftOption-Space"),
            "the default is the left Option key only");
    const auto right = parse_hotkey(QStringLiteral("rightalt+space"));
    require(right && right->optionSide == Side::right, "the right Option key");
    require(standard && standard->optionSide == Side::any, "plain Option is either key");
    for (const auto* refused : {"Space", "Shift-A", "Option-F13", "Option-", "Hyper-K", ""})
        require(!parse_hotkey(QString::fromLatin1(refused)), refused);
}

// A stand-in session service: answers one attach the way the real service
// answers a join, and records what the view sent.
class FakeService final : public QObject {
  public:
    enum class Mode : std::uint8_t { join, refuse };
    FakeService(const QString& path, QByteArray fingerprint, Mode mode)
        : fingerprint_(std::move(fingerprint)), mode_(mode) {
        QLocalServer::removeServer(path);
        require(server_.listen(path), "fake service listens");
        connect(&server_, &QLocalServer::newConnection, this, [this] {
            ++connections;
            auto* socket = server_.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        });
    }
    int connections{};
    std::optional<wire::AttachRequest> attach;
    std::optional<wire::PasteRequest> paste;
    bool ready{};

  private:
    void read(QLocalSocket* socket) {
        buffer_ += socket->readAll();
        wire::Frame frame;
        while (wire::take_frame(buffer_, frame))
            handle(socket, frame);
    }
    void handle(QLocalSocket* socket, const wire::Frame& frame) {
        if (frame.kind == wire::Kind::attach) {
            attach = wire::decode_attach(frame.payload);
            if (mode_ == Mode::refuse || attach->fingerprint != fingerprint_) {
                socket->write(wire::frame(wire::Kind::status,
                                          wire::encode_status({wire::StatusCode::rejected,
                                                               QStringLiteral("Unknown mode")})));
                return;
            }
            socket->write(wire::frame(
                wire::Kind::hello,
                wire::encode_hello(
                    {.attachment = attachment_, .pid = 1, .paste_transactions = true})));
            lapis::session::Terminal terminal({20, 4});
            terminal.feed("> ");
            socket->write(wire::frame(
                wire::Kind::snapshot,
                wire::encode_snapshot_message({attachment_, 5, terminal.snapshot()}, false)));
        } else if (frame.kind == wire::Kind::ready) {
            const auto acknowledged = wire::decode_ready(frame.payload);
            ready = acknowledged.attachment == attachment_ && acknowledged.sequence == 5;
        } else if (frame.kind == wire::Kind::paste_request) {
            paste = wire::decode_paste_request(frame.payload);
            socket->write(wire::frame(
                wire::Kind::paste_result,
                wire::encode_paste_result({paste->attachment, paste->request_id, true, {}})));
        }
    }
    QLocalServer server_;
    QByteArray buffer_;
    QByteArray fingerprint_;
    Mode mode_;
    wire::Attachment attachment_{{wire::new_id(), wire::new_id()}, 7};
};

void joinsBesideTheWindow() {
    QTemporaryDir directory(QStringLiteral("/tmp/ultratab-XXXXXX"));
    require(directory.isValid(), "fixture directory");
    Agent agent;
    agent.id = id('a');
    agent.harness = QStringLiteral("shell");
    agent.program = QStringLiteral("/bin/sh");
    agent.arguments = {QStringLiteral("-i")};
    agent.directory = directory.path();
    agent.endpoint = directory.filePath(QStringLiteral("a.sock"));
    const auto fingerprint = lapis::session::launch_fingerprint(
        lapis::session::validate_launch({.program = agent.program,
                                         .arguments = agent.arguments,
                                         .directory = agent.directory,
                                         .size = {100, 30},
                                         .agent = lapis::session::AgentMode::terminal}));

    {
        FakeService service(agent.endpoint, fingerprint, FakeService::Mode::join);
        SessionSender sender;
        std::optional<std::pair<bool, QString>> outcome;
        sender.submit(agent, QStringLiteral("merge it and install"),
                      [&outcome](bool admitted, const QString& why) { outcome = {admitted, why}; });
        require(waitFor([&] { return outcome.has_value(); }) && outcome && outcome->first,
                "the service admits it");
        require(service.attach && service.attach->mode == wire::AttachMode::join &&
                    service.attach->paste_transactions &&
                    service.attach->expected.session_id.isEmpty(),
                "a join beside the window, never a takeover, with submitted pastes");
        require(service.ready, "the first screen is acknowledged before input");
        require(service.paste && service.paste->submit &&
                    service.paste->text == QByteArrayLiteral("merge it and install"),
                "the text goes as one paste with Return");
    }
    {
        FakeService service(agent.endpoint, fingerprint, FakeService::Mode::refuse);
        SessionSender sender;
        std::optional<std::pair<bool, QString>> outcome;
        sender.submit(agent, QStringLiteral("hello"),
                      [&outcome](bool admitted, const QString& why) { outcome = {admitted, why}; });
        require(waitFor([&] { return outcome.has_value(); }) && outcome && !outcome->first &&
                    outcome->second.contains(QStringLiteral("cannot be joined")),
                "a service that cannot be joined is reported");
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 200) // labeled negative interval: no second attempt
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        require(service.connections == 1 && !service.paste,
                "no takeover attempt follows and nothing is typed");
    }
    {
        Agent gone = agent;
        gone.endpoint = directory.filePath(QStringLiteral("gone.sock"));
        SessionSender sender;
        std::optional<bool> admitted;
        sender.submit(gone, QStringLiteral("hello"),
                      [&admitted](bool ok, const QString&) { admitted = ok; });
        require(waitFor([&] { return admitted.has_value(); }) && admitted && !*admitted,
                "an agent whose service is gone is reported");
    }
}
QByteArray cardsFile(const QJsonObject& cards) {
    return QJsonDocument(QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("cards"), cards}})
        .toJson();
}

void composedCardsParse() {
    const QJsonObject card{
        {QStringLiteral("key"), QStringLiteral("k")},
        {QStringLiteral("composed"), QStringLiteral("2026-10-06T21:04:00.250Z")},
        {QStringLiteral("model"), QStringLiteral("m")},
        {QStringLiteral("since"), QStringLiteral("2 turns since")},
        {QStringLiteral("tldr"), QStringLiteral("  headline  ")},
        {QStringLiteral("prompt"), QStringLiteral("do it")},
        {QStringLiteral("blocks"),
         QJsonArray{
             QJsonObject{{QStringLiteral("type"), QStringLiteral("table")},
                         {QStringLiteral("columns"),
                          QJsonArray{QStringLiteral("name"), QStringLiteral("ms")}},
                         {QStringLiteral("rows"),
                          QJsonArray{QJsonArray{QStringLiteral("a"), QStringLiteral("1,024")},
                                     QJsonArray{QStringLiteral("b")},
                                     QJsonArray{QStringLiteral("c"), QStringLiteral("3.5 ms"),
                                                QStringLiteral("extra")}}}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("link")},
                         {QStringLiteral("label"), QStringLiteral("bad")},
                         {QStringLiteral("url"), QStringLiteral("javascript:alert(1)")}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("video")}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("list")},
                         {QStringLiteral("items"), QJsonArray{QStringLiteral("one"), {}}}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("link")},
                         {QStringLiteral("url"), QStringLiteral("https://example.com/x")}},
             QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                         {QStringLiteral("text"), QStringLiteral("a fourth block")}}}}};
    const auto parsed = parse_cards(
        cardsFile({{id('a'), card}, {id('b'), QJsonObject{}}, {id('c'), QJsonValue(3)}}));
    require(parsed.present && parsed.problem.isEmpty() && parsed.cards.size() == 1,
            "a card needs a key; anything else is dropped");
    const auto& read = parsed.cards.value(id('a'));
    require(read.tldr == QLatin1String("headline") &&
                read.since == QLatin1String("2 turns since") &&
                read.prompt == QLatin1String("do it") && read.composed.isValid(),
            "the card's lines are read and trimmed");
    require(read.blocks.size() == max_blocks, "at most three valid blocks, in order");
    const auto& table = read.blocks.at(0);
    require(table.type == Block::Type::table && table.rows.size() == 3 &&
                table.rows.at(1) == QStringList{QStringLiteral("b"), QString()} &&
                table.rows.at(2).size() == 2 && table.numeric == QList<bool>{false, true},
            "rows are padded or cut to the columns; a column of numbers is numeric");
    require(read.blocks.at(1).type == Block::Type::list && read.blocks.at(1).items.size() == 1,
            "unsafe links and unknown types are skipped; empty items dropped");
    require(read.blocks.at(2).type == Block::Type::link &&
                read.blocks.at(2).label == QLatin1String("example.com"),
            "a link without a label is named by its host");

    require(!parse_cards(QByteArrayLiteral("{\"v\":2,\"cards\":{}}")).present &&
                !parse_cards(QByteArrayLiteral("{\"v\":2}")).problem.isEmpty(),
            "another version is reported, not read");
    require(!parse_cards({}).present && parse_cards({}).problem.isEmpty(), "no file is no cards");

    QJsonArray rows;
    for (int row = 0; row < max_table_rows + 3; ++row)
        rows.append(QJsonArray{QString::number(row)});
    const auto long_table = parse_cards(cardsFile(
        {{id('a'), QJsonObject{{QStringLiteral("key"), QStringLiteral("k")},
                               {QStringLiteral("blocks"),
                                QJsonArray{QJsonObject{
                                    {QStringLiteral("type"), QStringLiteral("table")},
                                    {QStringLiteral("columns"), QJsonArray{QStringLiteral("n")}},
                                    {QStringLiteral("rows"), rows}}}}}}}));
    const auto& cut = long_table.cards.value(id('a')).blocks.at(0);
    require(cut.rows.size() == max_table_rows && cut.more_rows == 3,
            "a long table keeps its first rows and counts the rest");

    for (const auto* cell : {"1,024", "-3.5%", "12 ms", "2.1x", "+7", "0", "$4.50", "~30 s"})
        require(numeric_cell(QString::fromUtf8(cell)), "a number reads as numeric");
    for (const auto* cell : {"", "abc", "v1.2.3", "12 apples", "-", "1-2"})
        require(!numeric_cell(QString::fromUtf8(cell)), "text is not numeric");

    require(openable(QUrl(QStringLiteral("https://example.com/a"))) &&
                openable(QUrl(QStringLiteral("file:///tmp/report.html"))),
            "https and local files open");
    for (const auto* url : {"http://example.com", "javascript:alert(1)", "file://host/share/x",
                            "https://user:pw@example.com", "smb://x/y", "data:text/html,x", ""})
        require(!openable(QUrl(QString::fromUtf8(url))), "anything else does not");
}

void composedCardsFollowTheTurn() {
    auto published = fixture();
    const QString key_a = id('a') + QStringLiteral("|300|300|a:1|0");
    const auto keyed = [](const QString& key) {
        ComposedCard card;
        card.key = key;
        return card;
    };
    require(key_current(keyed(key_a), key_a) &&
                key_current(keyed(QStringLiteral("300|300|a:1|0")), key_a) &&
                !key_current(keyed(QStringLiteral("299|299|a:1|0")), key_a) &&
                !key_current(keyed({}), key_a) &&
                !key_current(keyed(QStringLiteral("|300|300|a:1|0")), key_a),
            "a key is current with or without the session prefix, and only then");
    published.composed = parse_cards(cardsFile(
        {{id('a'), QJsonObject{{QStringLiteral("key"), QStringLiteral("300|300|a:1|0")},
                               {QStringLiteral("tldr"), QStringLiteral("rich")},
                               {QStringLiteral("prompt"), QStringLiteral("composed reply")}}},
         {id('b'), QJsonObject{{QStringLiteral("key"), QStringLiteral("99|99||0")},
                               {QStringLiteral("prompt"), QStringLiteral("stale reply")}}},
         {id('c'), QJsonObject{{QStringLiteral("key"), id('c') + QStringLiteral("|200|200||1")},
                               {QStringLiteral("prompt"), QStringLiteral("never sent")}}}}));
    const auto cards = waiting_cards(published);
    require(cards.at(0).composed && cards.at(0).proposal == QLatin1String("composed reply"),
            "a current composed card brings its prompt");
    require(!cards.at(1).composed && cards.at(1).proposal.isEmpty(),
            "a stale one falls back to the plain card");
    require(cards.at(2).composed && cards.at(2).request && cards.at(2).proposal.isEmpty(),
            "a request card never takes a composed prompt");

    // The deck: Tab sends the composed prompt, and links open only by index.
    FakeSender sender;
    Deck deck(sender);
    deck.setWriterCheck([](qint64) { return true; });
    std::vector<QUrl> opened;
    deck.setLinkOpener([&opened](const QUrl& url) { opened.push_back(url); });
    deck.setPublished(published);
    require(deck.front().value(QStringLiteral("headline")).toString() == QLatin1String("rich") &&
                deck.front().value(QStringLiteral("composed")).toBool(),
            "the deck shows the tldr as the headline");
    require(!deck.openLink(0) && opened.empty(), "no link, nothing opens");
    require(deck.accept() && sender.sent.back().text == QLatin1String("composed reply"),
            "Tab sends the composed prompt");
}

void svgIsSanitized() {
    const auto ok = sanitize_svg(QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' "
        "viewBox='0 0 10 10'><defs><linearGradient id='g'><stop offset='0' "
        "stop-color='#fff'/></linearGradient></defs><rect width='5' height='5' "
        "fill='url(#g)'/><use xlink:href='#g'/><text>hi</text></svg>"));
    require(ok && ok->contains(QStringLiteral("fill=\"#c6d2e4\"")),
            "a clean diagram passes and gets a light default fill");
    require(sanitize_svg(QStringLiteral("<svg fill='red' viewBox='0 0 1 1'/>")) ==
                QStringLiteral("<svg fill='red' viewBox='0 0 1 1'/>"),
            "a root fill is kept as written");
    const auto commented =
        sanitize_svg(QStringLiteral("<!-- an <svg> sketch --><svg viewBox='0 0 1 1'/>"));
    require(commented &&
                commented->endsWith(QStringLiteral(
                    "<svg viewBox='0 0 1 1' fill=\"#c6d2e4\" color=\"#c6d2e4\"/>")) &&
                commented->startsWith(QStringLiteral("<!-- an <svg> sketch -->")),
            "the default fill goes into the root tag, not a comment before it");
    for (const auto* bad : {
             "<svg><script>alert(1)</script></svg>",
             "<svg><SCRIPT>alert(1)</SCRIPT></svg>",
             "<svg><foreignObject><div/></foreignObject></svg>",
             "<svg onload='alert(1)'/>",
             "<svg><rect onclick='x()'/></svg>",
             "<svg><image href='https://example.com/a.png'/></svg>",
             "<svg><rect fill='url(https://example.com/x#g)'/></svg>",
             "<svg><rect style='fill:url(\"http://x/y\")'/></svg>",
             "<svg><style>@import url(https://example.com/x.css);</style></svg>",
             "<svg><style>rect{fill:url(http://x/y)}</style></svg>",
             "<svg><style><g/>@import url(https://example.com/x.css);</style></svg>",
             "<svg><a href='javascript:alert(1)'><rect/></a></svg>",
             "<?xml-stylesheet href='https://example.com/x.css'?><svg/>",
             "<html><svg/></html>",
             "<svg><rect></svg>",
             "",
         })
        require(!sanitize_svg(QString::fromUtf8(bad)), "unsafe or broken SVG is rejected");
    const auto external_use = QStringLiteral("<svg xmlns:xlink='http://www.w3.org/1999/xlink'>") +
                              QStringLiteral("<use xlink:href='file:///etc/passwd#x'/></svg>");
    const auto entity = QStringLiteral("<?xml version='1.0'?><!DOCTYPE svg [<!ENTITY x SYSTEM ") +
                        QStringLiteral("'file:///etc/passwd'>]><svg>&x;</svg>");
    require(!sanitize_svg(external_use) && !sanitize_svg(entity),
            "an external use and an entity are rejected");
    require(!sanitize_svg(QStringLiteral("<svg>") + QString(max_svg_bytes, QLatin1Char(' ')) +
                          QStringLiteral("</svg>")),
            "an oversized SVG is rejected");

    // A diagram block whose SVG fails is dropped, not shown broken.
    const auto parsed = parse_cards(cardsFile(
        {{id('a'),
          QJsonObject{
              {QStringLiteral("key"), QStringLiteral("k")},
              {QStringLiteral("blocks"),
               QJsonArray{
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("diagram")},
                               {QStringLiteral("svg"), QStringLiteral("<svg><script/></svg>")}},
                   QJsonObject{{QStringLiteral("type"), QStringLiteral("diagram")},
                               {QStringLiteral("svg"),
                                QStringLiteral("<svg xmlns='http://www.w3.org/2000/"
                                               "svg' viewBox='0 0 40 10'><rect "
                                               "width='4' height='4'/></svg>")}}}}}}}));
    const auto& blocks = parsed.cards.value(id('a')).blocks;
    require(blocks.size() == 1 && blocks.at(0).size == QSizeF(40, 10),
            "an unsafe diagram is dropped; a safe one keeps its aspect");
}

void settingsAndPlacement() {
    require(parse_settings({}).start_at_login && parse_settings({}).hotkey.isEmpty(),
            "start at login is on by default");
    const auto off = parse_settings(
        QByteArrayLiteral("{\"hotkey\":\"Control-Option-Space\",\"startAtLogin\":false}"));
    require(!off.start_at_login && off.hotkey == QLatin1String("Control-Option-Space"),
            "both settings are read");

    QTemporaryDir home;
    require(home.isValid(), "a home folder");
    const auto screen = screen_key(QStringLiteral("Built-in"), QRect(0, 0, 1512, 982));
    require(read_positions(home.path()).isEmpty(), "no window memory yet");
    require(write_positions(home.path(), {{screen, QPoint(40, 60)}}) &&
                read_positions(home.path()).value(screen) == QPoint(40, 60),
            "a position is kept per screen across launches");
    require(!write_positions({}, {}), "no home, nothing written");
    require(parse_positions(QByteArrayLiteral("{\"v\":1,\"positions\":{\"s\":[1]}}")).isEmpty(),
            "a malformed position is ignored");

    const QRect area(0, 25, 1512, 950);
    const QSize size(1200, 800);
    const auto centered = place_window(area, size, std::nullopt);
    require(centered.size() == size && centered.center().x() == area.center().x(),
            "with no memory it is centered");
    require(place_window(area, size, QPoint(100, 120)).topLeft() == QPoint(100, 120),
            "where it was left");
    require(place_window(area, size, QPoint(400, 120)).topLeft() == QPoint(312, 120),
            "partly off the screen, it is moved inside");
    require(place_window(area, size, QPoint(5000, 5000)) == centered,
            "off this screen entirely, it is centered");
    require(place_window(QRect(0, 0, 800, 600), size, std::nullopt).size() == QSize(800, 600),
            "never larger than the screen");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        readsWhatLapisPublishes();
        cardsFollowLapisTabOrder();
        sentencesAreShort();
        fourAnswers();
        draftsKeepTheirCard();
        hotkeys();
        joinsBesideTheWindow();
        composedCardsParse();
        composedCardsFollowTheTurn();
        svgIsSanitized();
        settingsAndPlacement();
    } catch (const std::exception& error) {
        std::cerr << "ultratab test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ultratab tests passed\n";
    return 0;
}
