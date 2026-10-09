// Ultra Tab's card composer: when a card is composed (a new finished turn or a
// new guess, at most two at a time, debounced), what it keeps when a guess
// lands for a turn already composed, the fallback when the helper fails, the
// cards file it owns, and the helper process it runs. A fake runner stands in
// for the model; nothing here calls one.
#include "composer.hpp"
#include "published.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <chrono>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace lapis::ultratab;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds limit = 5s) {
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < limit.count())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return done();
}

bool ownerOnly(const QString& path) {
    const auto permissions = QFileInfo(path).permissions();
    return !permissions.testFlag(QFile::ReadGroup) && !permissions.testFlag(QFile::ReadOther) &&
           !permissions.testFlag(QFile::WriteGroup) && !permissions.testFlag(QFile::WriteOther);
}

QString id(char letter) {
    return QStringLiteral("00000000-0000-4000-8000-00000000000") +
           QString::number(letter - 'a', 16);
}

struct Job {
    QJsonObject input;
    ComposeRunner::Done done;
};
class FakeRunner final : public ComposeRunner {
  public:
    void start(const QJsonObject& job, std::chrono::milliseconds /*timeout*/, Done done) override {
        jobs.push_back({job, std::move(done)});
    }
    // Answers job `index` as the helper would, with a card for its key.
    void answer(std::size_t index, const QString& tldr) {
        // A copy: answering can start another job, which grows `jobs`.
        const auto job = jobs.at(index);
        const QJsonObject card{{QStringLiteral("key"), job.input.value(QStringLiteral("key"))},
                               {QStringLiteral("composed"), QStringLiteral("2026-10-06T12:00:00Z")},
                               {QStringLiteral("model"), QStringLiteral("fake")},
                               {QStringLiteral("tldr"), tldr},
                               {QStringLiteral("blocks"), QJsonArray{}},
                               {QStringLiteral("prompt"), QStringLiteral("composed prompt")}};
        job.done(QJsonObject{{QStringLiteral("card"), card},
                             {QStringLiteral("ok"), true},
                             {QStringLiteral("model"), QStringLiteral("fake")}},
                 {});
    }
    std::vector<Job> jobs;
};

struct Waiting {
    char letter{};
    qint64 turn{};
    QString offer_key;
    QString offer_text;
    QString said;
};
Published published(const QString& runtime, const std::vector<Waiting>& waiting) {
    QJsonArray agents;
    QJsonArray states;
    for (const auto& agent : waiting) {
        agents.append(QJsonObject{
            {QStringLiteral("id"), id(agent.letter)},
            {QStringLiteral("title"), QStringLiteral("agent-") + QLatin1Char(agent.letter)},
            {QStringLiteral("category"), QStringLiteral("c1")},
            {QStringLiteral("directory"), QStringLiteral("/work/") + QLatin1Char(agent.letter)},
            {QStringLiteral("harness"), QStringLiteral("claude")},
            {QStringLiteral("program"), QStringLiteral("/bin/claude")},
            {QStringLiteral("endpoint"), QDir(runtime).filePath(id(agent.letter) + ".sock")}});
        QJsonObject state{{QStringLiteral("id"), id(agent.letter)},
                          {QStringLiteral("status"), QStringLiteral("finished")},
                          {QStringLiteral("unseen"), true},
                          {QStringLiteral("requests"), 0},
                          {QStringLiteral("neededAtMs"), agent.turn},
                          {QStringLiteral("turnAtMs"), agent.turn}};
        if (!agent.offer_key.isEmpty())
            state.insert(QStringLiteral("offer"),
                         QJsonObject{{QStringLiteral("key"), agent.offer_key},
                                     {QStringLiteral("text"), agent.offer_text},
                                     {QStringLiteral("said"), agent.said},
                                     {QStringLiteral("seen"), false}});
        states.append(state);
    }
    const QJsonObject registry{
        {QStringLiteral("categories"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")},
                                {QStringLiteral("name"), QStringLiteral("lapis")}}}},
        {QStringLiteral("agents"), agents}};
    const QJsonObject state{{QStringLiteral("version"), 1},
                            {QStringLiteral("pid"), 1},
                            {QStringLiteral("agents"), states}};
    return parse_published(runtime, QJsonDocument(registry).toJson(),
                           QJsonDocument(state).toJson());
}

QJsonObject fileCards(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "the cards file exists");
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    require(root.value(QStringLiteral("v")).toInt() == 1, "cards file version 1");
    return root.value(QStringLiteral("cards")).toObject();
}
QString cardKey(const QString& path, char letter) {
    return fileCards(path).value(id(letter)).toObject().value(QStringLiteral("key")).toString();
}

Composer::Paths paths(const QTemporaryDir& directory) {
    return {directory.filePath(QStringLiteral("ultratab_cards.json")),
            directory.filePath(QStringLiteral("ultratab_compose.jsonl")), directory.path(),
            directory.path(), QStringLiteral("/bin/claude")};
}
ComposerSettings immediate() {
    ComposerSettings settings;
    settings.debounce_ms = 0;
    return settings;
}

void composesOnNewKeysOnly() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    FakeRunner runner;
    Composer composer(runner, paths(directory), immediate());
    int writes = 0;
    QObject::connect(&composer, &Composer::cardsChanged, [&writes] { ++writes; });
    const auto cards = paths(directory).cards;

    composer.setPublished(
        published(directory.path(),
                  {{'a', 100, {}, {}, {}}, {'b', 200, {}, {}, {}}, {'c', 300, {}, {}, {}}}));
    require(runner.jobs.size() == 2, "at most two compositions run at once");
    require(runner.jobs[0].input.value(QStringLiteral("session")) == id('a') &&
                runner.jobs[0].input.value(QStringLiteral("key")) == QLatin1String("turn:100") &&
                runner.jobs[0].input.value(QStringLiteral("category")) == QLatin1String("lapis"),
            "the card in front composes first, for its finished turn");
    runner.answer(0, QStringLiteral("a's card"));
    require(runner.jobs.size() == 3, "the third starts when one finishes");
    runner.answer(1, QStringLiteral("b's card"));
    runner.answer(2, QStringLiteral("c's card"));
    require(fileCards(cards).size() == 3 && cardKey(cards, 'b') == QLatin1String("turn:200"),
            "each card is written under its agent with its key");
    require(ownerOnly(cards), "the cards file is owner-only");

    // Nothing new: no composition and no write.
    const int before = writes;
    composer.setPublished(
        published(directory.path(),
                  {{'a', 100, {}, {}, {}}, {'b', 200, {}, {}, {}}, {'c', 300, {}, {}, {}}}));
    require(runner.jobs.size() == 3 && writes == before,
            "the same keys compose nothing and rewrite nothing");

    // B finished another turn: only B composes.
    composer.setPublished(
        published(directory.path(),
                  {{'a', 100, {}, {}, {}}, {'b', 500, {}, {}, {}}, {'c', 300, {}, {}, {}}}));
    require(runner.jobs.size() == 4 &&
                runner.jobs[3].input.value(QStringLiteral("key")) == QLatin1String("turn:500"),
            "a newer finished turn composes that agent's card");
    runner.answer(3, QStringLiteral("b again"));

    // lapis's guess arrives for A's turn: the card takes it without a model call.
    composer.setPublished(
        published(directory.path(), {{'a', 100, QStringLiteral("a:1"), QStringLiteral("ship it"),
                                      QStringLiteral("Tests pass.")},
                                     {'b', 500, {}, {}, {}},
                                     {'c', 300, {}, {}, {}}}));
    const auto a = fileCards(cards).value(id('a')).toObject();
    require(runner.jobs.size() == 4 && a.value(QStringLiteral("key")) == QLatin1String("a:1") &&
                a.value(QStringLiteral("prompt")) == QLatin1String("ship it") &&
                a.value(QStringLiteral("tldr")) == QLatin1String("a's card"),
            "a guess for a composed turn becomes the card's prompt and key");

    // C finishes a turn and its guess lands while the card composes.
    composer.setPublished(
        published(directory.path(), {{'a', 100, QStringLiteral("a:1"), QStringLiteral("ship it"),
                                      QStringLiteral("Tests pass.")},
                                     {'b', 500, {}, {}, {}},
                                     {'c', 900, {}, {}, {}}}));
    require(runner.jobs.size() == 5, "C's new turn composes");
    composer.setPublished(
        published(directory.path(), {{'a', 100, QStringLiteral("a:1"), QStringLiteral("ship it"),
                                      QStringLiteral("Tests pass.")},
                                     {'b', 500, {}, {}, {}},
                                     {'c', 900, QStringLiteral("c:1"), QStringLiteral("go"), {}}}));
    require(runner.jobs.size() == 5, "a guess for the turn being composed waits for it");
    runner.answer(4, QStringLiteral("c's new card"));
    require(cardKey(cards, 'c') == QLatin1String("c:1"), "the guess is applied when it lands");
}

void failuresLeaveTheLastMessage() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    FakeRunner runner;
    Composer composer(runner, paths(directory), immediate());
    composer.setPublished(published(
        directory.path(), {{'a', 100, QStringLiteral("a:7"), QStringLiteral("secret guess text"),
                            QStringLiteral("Migrated the schema. Two tables left.")}}));
    require(runner.jobs.size() == 1, "composes");
    const auto done = runner.jobs[0].done;
    done(std::nullopt, QStringLiteral("timeout"));
    const auto card = fileCards(paths(directory).cards).value(id('a')).toObject();
    require(card.value(QStringLiteral("key")) == QLatin1String("a:7") &&
                card.value(QStringLiteral("tldr")) == QLatin1String("Migrated the schema.") &&
                card.value(QStringLiteral("blocks")).toArray().isEmpty(),
            "a failed composition leaves the agent's last message as the tldr");
    QFile log(paths(directory).log);
    require(log.open(QIODevice::ReadOnly), "failures are logged");
    const auto logged = log.readAll();
    require(logged.contains("\"timeout\"") && !logged.contains("secret guess") &&
                !logged.contains("Migrated"),
            "the log names the failure and carries no conversation text");
    composer.setPublished(published(
        directory.path(), {{'a', 100, QStringLiteral("a:7"), QStringLiteral("secret guess text"),
                            QStringLiteral("Migrated the schema. Two tables left.")}}));
    require(runner.jobs.size() == 1, "a failed key is not retried in a loop");
}

void debouncesAndHolds() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    FakeRunner runner;
    ComposerSettings settings;
    settings.debounce_ms = 60;
    Composer composer(runner, paths(directory), settings);
    composer.setPublished(published(directory.path(), {{'a', 100, {}, {}, {}}}));
    require(runner.jobs.empty(), "a new turn waits out the debounce");
    composer.setPublished(published(directory.path(), {{'a', 150, {}, {}, {}}}));
    require(waitFor([&runner] { return !runner.jobs.empty(); }, 2s) && runner.jobs.size() == 1 &&
                runner.jobs[0].input.value(QStringLiteral("key")) == QLatin1String("turn:150"),
            "after the debounce, one composition for the newest turn");
    runner.answer(0, QStringLiteral("first"));

    // The card in front changes while held only because its key changed.
    composer.setHeld(id('a'));
    composer.setPublished(published(directory.path(), {{'a', 400, {}, {}, {}}}));
    require(waitFor([&runner] { return runner.jobs.size() == 2; }, 2s), "next turn composes");
    runner.answer(1, QStringLiteral("second"));
    require(cardKey(paths(directory).cards, 'a') == QLatin1String("turn:400"),
            "a held card follows a new key");
    composer.setHeld({});

    ComposerSettings off;
    off.enabled = false;
    FakeRunner idle;
    QTemporaryDir other;
    Composer disabled(idle, paths(other), off);
    disabled.setPublished(published(other.path(), {{'a', 100, {}, {}, {}}}));
    QCoreApplication::processEvents();
    require(idle.jobs.empty(), "a disabled composer composes nothing");
}

void readsSettingsAndKeys() {
    const auto settings = parse_composer(
        QJsonObject{{QStringLiteral("enabled"), false},
                    {QStringLiteral("model"), QStringLiteral("qwen3")},
                    {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:8000/v1")},
                    {QStringLiteral("timeoutSeconds"), 5}});
    require(!settings.enabled && settings.timeout_ms == 10000 &&
                settings.helper.value(QStringLiteral("endpoint")) ==
                    QLatin1String("http://127.0.0.1:8000/v1"),
            "composer settings");
    require(parse_composer({}).enabled, "on by default");
    AgentState state;
    state.needed_at_ms = 5;
    require(compose_key(state) == QLatin1String("turn:5"), "a turn from before lapis started");
    state.turn_at_ms = 9;
    state.offer = Offer{QStringLiteral("k"), QStringLiteral("t"), {}, false};
    require(compose_key(state) == QLatin1String("k") && compose_turn(state) == 9, "offer key");
}

void runsTheHelperProcess() {
    QTemporaryDir directory;
    require(directory.isValid(), "fixture directory");
    const auto script = directory.filePath(QStringLiteral("helper.sh"));
    QFile file(script);
    require(file.open(QIODevice::WriteOnly), "fake helper");
    // Reads the job and answers with a card for its key, as compose.py does.
    file.write("read job\n"
               "case \"$job\" in *slow*) sleep 5;; esac\n"
               "echo noise\n"
               "echo '{\"ok\": true, \"card\": {\"key\": \"k1\"}}'\n");
    file.close();
    ProcessComposeRunner runner(QStringLiteral("/bin/sh"), script);
    std::optional<QJsonObject> answer;
    QString failure;
    bool done = false;
    runner.start(QJsonObject{{QStringLiteral("key"), QStringLiteral("k1")}}, 5s,
                 [&](std::optional<QJsonObject> result, const QString& why) {
                     answer = std::move(result);
                     failure = why;
                     done = true;
                 });
    require(waitFor([&done] { return done; }), "the helper answers");
    require(answer &&
                answer->value(QStringLiteral("card")).toObject().value(QStringLiteral("key")) ==
                    QLatin1String("k1"),
            "the helper's last line is its answer");
    done = false;
    runner.start(QJsonObject{{QStringLiteral("key"), QStringLiteral("slow")}}, 200ms,
                 [&](std::optional<QJsonObject> result, const QString& why) {
                     answer = std::move(result);
                     failure = why;
                     done = true;
                 });
    require(waitFor([&done] { return done; }, 3s) && !answer && failure == QLatin1String("timeout"),
            "a helper past its time is stopped");
    done = false;
    ProcessComposeRunner missing(directory.filePath(QStringLiteral("no-python")), script);
    missing.start({}, 1s, [&](std::optional<QJsonObject> result, const QString& why) {
        answer = std::move(result);
        failure = why;
        done = true;
    });
    require(waitFor([&done] { return done; }) && failure == QLatin1String("helper unavailable"),
            "no interpreter is reported");

    const auto installed = install_compose_helper(directory.filePath(QStringLiteral("helper")));
    require(!installed.isEmpty() && QFileInfo::exists(installed) &&
                QFileInfo::exists(directory.filePath(QStringLiteral("helper/next_prompt.py"))) &&
                ownerOnly(installed),
            "the helper and the module it imports are installed owner-only");

    // The installed helper runs and imports its module. An agent without a
    // conversation is answered without a model.
    const auto python = find_tool(QStringLiteral("python3"));
    if (python.isEmpty())
        return;
    done = false;
    ProcessComposeRunner real(python, installed);
    real.start(QJsonObject{{QStringLiteral("session"), id('a')},
                           {QStringLiteral("key"), QStringLiteral("turn:7")},
                           {QStringLiteral("harness"), QStringLiteral("none")}},
               20s, [&](std::optional<QJsonObject> result, const QString& why) {
                   answer = std::move(result);
                   failure = why;
                   done = true;
               });
    require(waitFor([&done] { return done; }, 20s) && answer &&
                answer->value(QStringLiteral("reason")) == QLatin1String("no conversation") &&
                answer->value(QStringLiteral("card")).toObject().value(QStringLiteral("key")) ==
                    QLatin1String("turn:7"),
            "the installed helper answers with a card for the key");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        composesOnNewKeysOnly();
        failuresLeaveTheLastMessage();
        debouncesAndHolds();
        readsSettingsAndKeys();
        runsTheHelperProcess();
    } catch (const std::exception& error) {
        std::cerr << "ultratab composer test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "ultratab composer tests passed\n";
    return 0;
}
