#include "composer.hpp"
#include "deck.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <csignal>
#include <limits>
#include <memory>
#include <unistd.h>
#include <utility>

namespace lapis::ultratab {
namespace {
constexpr auto kOwnerOnly = QFile::ReadOwner | QFile::WriteOwner;
constexpr qint64 kHelperOutputBytes = qint64{1024} * 1024;
constexpr int kFileVersion = 1;

QString now_iso() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

bool write_private(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(kOwnerOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
        return false;
    QFile::setPermissions(path, kOwnerOnly);
    return true;
}

QByteArray compact(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
} // namespace

ComposerSettings parse_composer(const QJsonValue& value) {
    ComposerSettings settings;
    const auto object = value.toObject();
    if (object.value(QStringLiteral("enabled")).isBool())
        settings.enabled = object.value(QStringLiteral("enabled")).toBool();
    if (const auto seconds = object.value(QStringLiteral("timeoutSeconds")); seconds.isDouble())
        settings.timeout_ms = std::clamp(seconds.toInt(), 10, 600) * 1000;
    for (const auto* name : {"model", "effort", "endpoint"})
        if (const auto text = object.value(QLatin1String(name)).toString().trimmed();
            !text.isEmpty())
            settings.helper.insert(QLatin1String(name), text.left(200));
    return settings;
}

QString compose_key(const AgentState& state) {
    if (state.offer && !state.offer->key.isEmpty())
        return state.offer->key;
    return QStringLiteral("turn:%1").arg(compose_turn(state));
}

qint64 compose_turn(const AgentState& state) {
    // A turn that finished before this lapis window started has no time;
    // when the agent began to wait stands for it.
    return state.turn_at_ms > 0 ? state.turn_at_ms : state.needed_at_ms;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ProcessComposeRunner::ProcessComposeRunner(const QString& python, const QString& script,
                                           QObject* parent)
    : QObject(parent), python_(python), script_(script) {}

ProcessComposeRunner::~ProcessComposeRunner() {
    for (auto* process : std::as_const(running_)) {
        process->disconnect();
        if (const auto pid = process->processId(); pid > 0)
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
        process->waitForFinished(1000);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void ProcessComposeRunner::start(const QJsonObject& job, std::chrono::milliseconds timeout,
                                 Done done) {
    if (python_.isEmpty() || script_.isEmpty()) {
        // Reported from the event loop, never from inside start().
        QTimer::singleShot(0, this, [done = std::move(done)] {
            done(std::nullopt, QStringLiteral("helper unavailable"));
        });
        return;
    }
    auto* process = new QProcess(this);
    running_.append(process);
    process->setProgram(python_);
    process->setArguments({script_, QStringLiteral("compose")});
    process->setWorkingDirectory(QDir::tempPath());
    process->setProcessChannelMode(QProcess::SeparateChannels);
    // Its own group, so a timeout ends the model call it started too.
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
    struct State {
        QByteArray output;
        bool timed_out{};
        bool done{};
    };
    auto state = std::make_shared<State>();
    connect(process, &QProcess::readyReadStandardOutput, process, [process, state] {
        state->output += process->readAllStandardOutput();
        if (state->output.size() > kHelperOutputBytes)
            state->output.truncate(kHelperOutputBytes + 1);
    });
    connect(process, &QProcess::readyReadStandardError, process,
            [process] { process->readAllStandardError(); });
    const auto finish = [this, process, state, done = std::move(done)] {
        if (std::exchange(state->done, true))
            return;
        state->output += process->readAllStandardOutput();
        running_.removeOne(process);
        process->deleteLater();
        if (process->error() == QProcess::FailedToStart) {
            done(std::nullopt, QStringLiteral("helper unavailable"));
            return;
        }
        if (state->timed_out) {
            done(std::nullopt, QStringLiteral("timeout"));
            return;
        }
        if (state->output.size() > kHelperOutputBytes) {
            done(std::nullopt, QStringLiteral("output too large"));
            return;
        }
        const auto lines = state->output.trimmed().split('\n');
        const auto document =
            QJsonDocument::fromJson(lines.isEmpty() ? QByteArray() : lines.last());
        if (!document.isObject()) {
            done(std::nullopt, process->exitCode() != 0 ? QStringLiteral("helper failed")
                                                        : QStringLiteral("invalid helper output"));
            return;
        }
        done(document.object(), {});
    };
    connect(process, &QProcess::finished, this, finish);
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish();
    });
    QTimer::singleShot(timeout, process, [process, state] {
        state->timed_out = true;
        if (const auto pid = process->processId(); pid > 0)
            ::kill(-static_cast<pid_t>(pid), SIGKILL);
    });
    process->start();
    process->write(compact(job) + '\n');
    process->closeWriteChannel();
}

QString install_compose_helper(const QString& folder) {
    auto searchable = QFileDevice::Permissions(kOwnerOnly);
    searchable.setFlag(QFile::ExeOwner);
    if (!QDir().mkpath(folder) || !QFile::setPermissions(folder, searchable))
        return {};
    for (const auto* name : {"next_prompt.py", "compose.py"}) {
        QFile resource(QStringLiteral(":/ultratab/compose/") + QLatin1String(name));
        if (!resource.open(QIODevice::ReadOnly) ||
            !write_private(QDir(folder).filePath(QLatin1String(name)), resource.readAll()))
            return {};
    }
    return QDir(folder).filePath(QStringLiteral("compose.py"));
}

QString find_tool(const QString& name) {
    if (auto found = QStandardPaths::findExecutable(name); !found.isEmpty())
        return found;
    // An app opened from Finder has launchd's short PATH.
    const auto home = QDir::homePath();
    return QStandardPaths::findExecutable(
        name, {home + QStringLiteral("/.local/bin"), home + QStringLiteral("/.claude/local"),
               QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin"),
               QStringLiteral("/usr/bin")});
}

Composer::Composer(ComposeRunner& runner, Paths paths, ComposerSettings settings, QObject* parent)
    : QObject(parent), runner_(runner), paths_(std::move(paths)), settings_(std::move(settings)) {
    clock_.start();
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Composer::pump);
    // The cards composed before a restart stay until their agents move on.
    QFile file(paths_.cards);
    const QFileInfo info(paths_.cards);
    if (!info.isSymLink() && info.isFile() && info.size() <= max_file_bytes &&
        file.open(QIODevice::ReadOnly)) {
        written_ = file.readAll();
        const auto root = QJsonDocument::fromJson(written_).object();
        if (root.value(QStringLiteral("v")).toInt() == kFileVersion) {
            const auto cards = root.value(QStringLiteral("cards")).toObject();
            for (auto card = cards.begin(); card != cards.end(); ++card) {
                if (!card.value().isObject())
                    continue;
                // A card composed for a finished turn names it in its key, so
                // a guess for that turn after a restart still only patches it.
                const auto object = card.value().toObject();
                const auto key = object.value(QStringLiteral("key")).toString();
                qint64 turn = -1;
                if (key.startsWith(QStringLiteral("turn:"))) {
                    bool ok = false;
                    const auto parsed = key.mid(5).toLongLong(&ok);
                    if (ok)
                        turn = parsed;
                }
                cards_.insert(card.key(), {object, turn});
            }
        }
    }
}

const Agent* Composer::agent(const QString& id) const {
    for (const auto& agent : published_.agents)
        if (agent.id == id)
            return &agent;
    return nullptr;
}

void Composer::setPublished(const Published& published) {
    published_ = published;
    reconcile();
}

void Composer::setHeld(const QString& agent_id) {
    if (agent_id == held_)
        return;
    const auto released = std::exchange(held_, agent_id);
    if (const auto kept = held_back_.take(released); !kept.card.isEmpty()) {
        cards_.insert(released, kept);
        write();
    }
}

QJsonObject Composer::cards() const {
    QJsonObject cards;
    for (auto entry = cards_.cbegin(); entry != cards_.cend(); ++entry)
        cards.insert(entry.key(), entry->card);
    return cards;
}

void Composer::reconcile() {
    if (!settings_.enabled)
        return;
    const auto now = clock_.elapsed();
    order_.clear();
    for (const auto& card : waiting_cards(published_)) {
        const auto& id = card.agent_id;
        const auto& state = published_.states[id];
        const auto key = compose_key(state);
        const auto turn = compose_turn(state);
        order_.append(id);
        if (const auto have = cards_.constFind(id); have != cards_.cend()) {
            if (have->card.value(QStringLiteral("key")).toString() == key) {
                wanted_.remove(id);
                continue;
            }
            // A guess for the turn already composed: the card takes the
            // guess as its prompt without another model call.
            if (have->turn == turn && state.offer) {
                auto patched = have->card;
                patched.insert(QStringLiteral("key"), key);
                patched.insert(QStringLiteral("prompt"), state.offer->text);
                wanted_.remove(id);
                store(id, patched, turn);
                log({{QStringLiteral("event"), QStringLiteral("guess")},
                     {QStringLiteral("session"), id},
                     {QStringLiteral("key"), key}});
                continue;
            }
        }
        // Composing this turn already; its guess is applied when it lands.
        if (const auto run = running_.constFind(id);
            run != running_.cend() && (run->key == key || run->turn == turn))
            continue;
        if (const auto want = wanted_.constFind(id); want != wanted_.cend() && want->key == key)
            continue;
        wanted_.insert(id, {key, turn, now + settings_.debounce_ms});
    }
    for (auto want = wanted_.begin(); want != wanted_.end();)
        want = order_.contains(want.key()) ? std::next(want) : wanted_.erase(want);
    // Cards of agents no longer in the registry go; the rest stay, cheap and
    // ready for the agent's next wait.
    if (published_.has_registry) {
        bool removed = false;
        for (auto entry = cards_.begin(); entry != cards_.end();)
            if (agent(entry.key()) == nullptr) {
                entry = cards_.erase(entry);
                removed = true;
            } else {
                ++entry;
            }
        if (removed)
            write();
    }
    pump();
}

void Composer::pump() {
    const auto now = clock_.elapsed();
    qint64 next = std::numeric_limits<qint64>::max();
    // A runner may finish inside start(), which rebuilds order_: walk a copy.
    const auto order = order_;
    for (const auto& id : order) {
        const auto want = wanted_.constFind(id);
        if (want == wanted_.cend() || running_.contains(id))
            continue;
        if (want->due_ms > now) {
            next = std::min(next, want->due_ms);
            continue;
        }
        if (running_.size() >= settings_.max_running)
            break;
        const auto* target = agent(id);
        if (target == nullptr)
            continue;
        const Running run{want->key, want->turn};
        const auto input = job(*target, published_.states[id], run.key);
        wanted_.erase(want);
        running_.insert(id, run);
        QElapsedTimer took;
        took.start();
        runner_.start(input, std::chrono::milliseconds(settings_.timeout_ms),
                      [self = QPointer<Composer>(this), id, run,
                       took](std::optional<QJsonObject> answer, const QString& failure) {
                          if (self)
                              self->finished(id, run, answer, failure, took.elapsed());
                      });
    }
    if (next != std::numeric_limits<qint64>::max())
        timer_.start(static_cast<int>(std::clamp<qint64>(next - now, 0, 60000)));
}

void Composer::finished(const QString& id, const Running& run,
                        const std::optional<QJsonObject>& answer, const QString& failure,
                        qint64 ms) {
    running_.remove(id);
    QJsonObject event{{QStringLiteral("session"), id},
                      {QStringLiteral("key"), run.key},
                      {QStringLiteral("ms"), ms}};
    auto card = answer ? answer->value(QStringLiteral("card")).toObject() : QJsonObject{};
    const bool fits = compact(card).size() <= max_card_bytes;
    if (!card.isEmpty() && fits && card.value(QStringLiteral("key")).toString() == run.key) {
        const bool ok = answer->value(QStringLiteral("ok")).toBool();
        event.insert(QStringLiteral("event"),
                     ok ? QStringLiteral("composed") : QStringLiteral("fallback"));
        event.insert(QStringLiteral("model"), answer->value(QStringLiteral("model")).toString());
        event.insert(QStringLiteral("dropped"), answer->value(QStringLiteral("dropped")).toInt());
        if (!ok)
            event.insert(QStringLiteral("reason"),
                         answer->value(QStringLiteral("reason")).toString().left(80));
    } else {
        event.insert(QStringLiteral("event"), QStringLiteral("failed"));
        event.insert(QStringLiteral("reason"),
                     !failure.isEmpty() ? failure
                     : !fits            ? QStringLiteral("card too large")
                     : answer           ? answer->value(QStringLiteral("reason"))
                                              .toString(QStringLiteral("invalid helper output"))
                                              .left(80)
                                        : QStringLiteral("helper failed"));
        card = fallback(published_.states.value(id), run.key);
    }
    log(event);
    store(id, card, run.turn);
    reconcile();
}

QJsonObject Composer::fallback(const AgentState& state, const QString& key) const {
    QJsonObject card{
        {QStringLiteral("key"), key},
        {QStringLiteral("composed"), now_iso()},
        {QStringLiteral("model"), settings_.helper.value(QStringLiteral("model")).toString()},
        {QStringLiteral("blocks"), QJsonArray{}},
        {QStringLiteral("prompt"), state.offer ? state.offer->text : QString()}};
    if (state.offer)
        if (const auto line = one_sentence(state.offer->said); !line.isEmpty())
            card.insert(QStringLiteral("tldr"), line);
    return card;
}

void Composer::store(const QString& id, const QJsonObject& card, qint64 turn) {
    // The card in front keeps its content while the person looks at it,
    // unless what it was composed for changed.
    if (id == held_)
        if (const auto have = cards_.constFind(id);
            have != cards_.cend() &&
            have->card.value(QStringLiteral("key")) == card.value(QStringLiteral("key"))) {
            held_back_.insert(id, {card, turn});
            return;
        }
    held_back_.remove(id);
    cards_.insert(id, {card, turn});
    write();
}

void Composer::write() {
    // The oldest cards give way when the file would be too large.
    QByteArray bytes;
    while (true) {
        bytes = compact({{QStringLiteral("v"), kFileVersion}, {QStringLiteral("cards"), cards()}});
        if (bytes.size() <= max_file_bytes || cards_.isEmpty())
            break;
        auto oldest = cards_.begin();
        for (auto entry = cards_.begin(); entry != cards_.end(); ++entry)
            if (entry->card.value(QStringLiteral("composed")).toString() <
                oldest->card.value(QStringLiteral("composed")).toString())
                oldest = entry;
        cards_.erase(oldest);
    }
    if (bytes == written_)
        return;
    if (!write_private(paths_.cards, bytes)) {
        log({{QStringLiteral("event"), QStringLiteral("failed")},
             {QStringLiteral("reason"), QStringLiteral("cards not written")}});
        return;
    }
    written_ = std::move(bytes);
    emit cardsChanged();
}

void Composer::log(const QJsonObject& event) const {
    if (paths_.log.isEmpty())
        return;
    auto line = event;
    line.insert(QStringLiteral("time"), now_iso());
    if (QFileInfo(paths_.log).size() > max_log_bytes) {
        const auto older = paths_.log + QStringLiteral(".1");
        QFile::remove(older);
        QFile::rename(paths_.log, older);
    }
    QFile file(paths_.log);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    file.setPermissions(kOwnerOnly);
    file.write(compact(line) + '\n');
}

QJsonObject Composer::job(const Agent& agent, const AgentState& state, const QString& key) const {
    QString category;
    for (const auto& known : published_.categories)
        if (known.id == agent.category)
            category = known.name;
    QJsonObject job{{QStringLiteral("session"), agent.id},
                    {QStringLiteral("key"), key},
                    {QStringLiteral("title"), agent.title},
                    {QStringLiteral("category"), category},
                    {QStringLiteral("directory"), agent.directory},
                    {QStringLiteral("harness"), agent.harness},
                    {QStringLiteral("endpoint"), agent.endpoint},
                    {QStringLiteral("neededAtMs"), state.needed_at_ms},
                    {QStringLiteral("turnAtMs"), state.turn_at_ms},
                    {QStringLiteral("request"), state.requests > 0 ? state.request : QString()},
                    {QStringLiteral("runtime"), paths_.runtime},
                    {QStringLiteral("home"), paths_.home},
                    {QStringLiteral("claude"), paths_.claude},
                    {QStringLiteral("composer"), settings_.helper},
                    {QStringLiteral("timeout"), settings_.timeout_ms / 1000}};
    if (state.offer)
        job.insert(QStringLiteral("offer"),
                   QJsonObject{{QStringLiteral("text"), state.offer->text},
                               {QStringLiteral("said"), state.offer->said}});
    return job;
}
} // namespace lapis::ultratab
