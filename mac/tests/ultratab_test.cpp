// Ultra Tab's deck: which agents are cards and in what order, the four
// answers against a fake transport, the published files it reads, the global
// key it parses, and the join it makes to an agent's session service (against
// a fake service speaking wire v6).
#include "deck.hpp"
#include "hotkey.hpp"
#include "launch_spec.hpp"
#include "published.hpp"
#include "session_sender.hpp"
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

void hotkeys() {
    const auto standard = parse_hotkey(QStringLiteral("Option-Space"));
    require(standard && standard->option && !standard->command && standard->key == "Space",
            "the default");
    const auto other = parse_hotkey(QStringLiteral("cmd+shift+u"));
    require(other && other->command && other->shift && other->key == "U" &&
                describe(*other) == QLatin1String("Shift-Command-U"),
            "aliases, either separator, any case");
    require(parse_hotkey(QStringLiteral("Control-F12")).has_value(), "function keys");
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
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        readsWhatLapisPublishes();
        cardsFollowLapisTabOrder();
        sentencesAreShort();
        fourAnswers();
        hotkeys();
        joinsBesideTheWindow();
    } catch (const std::exception& error) {
        std::cerr << "ultratab test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ultratab tests passed\n";
    return 0;
}
