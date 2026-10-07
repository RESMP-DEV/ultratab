#include "published.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThreadPool>
#include <QUuid>
#include <cerrno>
#include <csignal>
#include <limits>
#include <utility>

namespace lapis::ultratab {
namespace {
constexpr int kPollMs = 2000;

// The file's bytes when it is a regular file (not a link) within `limit`.
QByteArray read_bounded(const QString& path, qint64 limit, QString& problem) {
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    QFile file(path);
    if (info.isSymLink() || !info.isFile() || info.size() > limit ||
        !file.open(QIODevice::ReadOnly)) {
        problem = QStringLiteral("%1 is unreadable or too large").arg(info.fileName());
        return {};
    }
    return file.read(limit);
}

std::optional<Agent> parse_agent(const QJsonObject& object, const QString& runtime) {
    Agent agent;
    agent.id = object.value(QStringLiteral("id")).toString();
    agent.title = object.value(QStringLiteral("title")).toString();
    agent.category = object.value(QStringLiteral("category")).toString();
    agent.directory = object.value(QStringLiteral("directory")).toString();
    agent.harness = object.value(QStringLiteral("harness")).toString(QStringLiteral("codex"));
    agent.endpoint = object.value(QStringLiteral("endpoint")).toString();
    agent.program = object.value(QStringLiteral("program")).toString();
    agent.claude_mode = agent.harness == QLatin1String("claude") &&
                        object.value(QStringLiteral("mode")).toString() == QLatin1String("claude");
    // As the desktop restores a record: the literal arguments when recorded,
    // else a Codex resume of the saved thread.
    if (object.contains(QStringLiteral("arguments"))) {
        for (const auto& argument : object.value(QStringLiteral("arguments")).toArray())
            agent.arguments.append(argument.toString());
    } else if (const auto resume = object.value(QStringLiteral("resumeThread")).toString();
               !resume.isEmpty()) {
        agent.arguments = {QStringLiteral("resume"), resume};
    }
    // Only the registry's own private endpoints are reachable.
    if (QUuid(agent.id).isNull() ||
        agent.endpoint != QDir(runtime).filePath(agent.id + QStringLiteral(".sock")) ||
        !QFileInfo(agent.program).isAbsolute() || !QFileInfo(agent.directory).isAbsolute())
        return std::nullopt;
    return agent;
}

void parse_registry(const QByteArray& bytes, const QString& runtime, Published& published) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root.isEmpty()) {
        published.problem = QStringLiteral("workspace.json is not a workspace");
        return;
    }
    published.has_registry = true;
    for (const auto& value : root.value(QStringLiteral("categories")).toArray()) {
        const auto object = value.toObject();
        published.categories.append({object.value(QStringLiteral("id")).toString(),
                                     object.value(QStringLiteral("name")).toString()});
    }
    for (const auto& value : root.value(QStringLiteral("agents")).toArray())
        if (auto agent = parse_agent(value.toObject(), runtime))
            published.agents.append(std::move(*agent));
}

AgentState parse_state(const QJsonObject& object) {
    AgentState state;
    state.status = object.value(QStringLiteral("status")).toString(QStringLiteral("unknown"));
    state.unseen = object.value(QStringLiteral("unseen")).toBool();
    state.requests = std::max(0, object.value(QStringLiteral("requests")).toInt());
    state.request = object.value(QStringLiteral("request")).toString();
    state.needed_at_ms = object.value(QStringLiteral("neededAtMs")).toInteger();
    state.turn_at_ms = object.value(QStringLiteral("turnAtMs")).toInteger();
    const auto offer = object.value(QStringLiteral("offer")).toObject();
    if (const auto text = offer.value(QStringLiteral("text")).toString(); !text.isEmpty())
        state.offer = Offer{offer.value(QStringLiteral("key")).toString(), text,
                            offer.value(QStringLiteral("said")).toString(),
                            offer.value(QStringLiteral("seen")).toBool()};
    return state;
}

void parse_states(const QByteArray& bytes, Published& published) {
    const auto root = QJsonDocument::fromJson(bytes).object();
    if (root.value(QStringLiteral("version")).toInt() != state_version) {
        published.problem = QStringLiteral("agent_state.json has another version");
        return;
    }
    published.has_state = true;
    published.writer_pid = root.value(QStringLiteral("pid")).toInteger();
    for (const auto& value : root.value(QStringLiteral("agents")).toArray()) {
        const auto object = value.toObject();
        const auto id = object.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            published.states.insert(id, parse_state(object));
    }
}
} // namespace

QString lapis_home() {
    if (const auto set = qEnvironmentVariable("LAPIS_HOME"); !set.isEmpty())
        return QFileInfo(set).absoluteFilePath();
    const QDir app(QDir::home().filePath(QStringLiteral(".lapis")));
    if (QFileInfo::exists(app.filePath(QStringLiteral("runtime/workspace.json"))))
        return app.absolutePath();
    return {};
}

Published parse_published(const QString& runtime, const QByteArray& registry,
                          const QByteArray& state) {
    Published published;
    if (!registry.isEmpty())
        parse_registry(registry, runtime, published);
    if (!state.isEmpty())
        parse_states(state, published);
    return published;
}

Published read_published(const QString& runtime) {
    if (runtime.isEmpty()) {
        Published nothing;
        nothing.problem = QStringLiteral("No lapis workspace found.");
        return nothing;
    }
    const QDir folder(runtime);
    QString problem;
    const auto registry = read_bounded(folder.filePath(QStringLiteral("workspace.json")),
                                       max_registry_bytes, problem);
    const auto state =
        read_bounded(folder.filePath(QStringLiteral("agent_state.json")), max_state_bytes, problem);
    auto published = parse_published(folder.absolutePath(), registry, state);
    if (published.problem.isEmpty())
        published.problem = std::move(problem);
    return published;
}

bool writer_running(qint64 pid) {
    if (pid <= 0 || pid > std::numeric_limits<pid_t>::max())
        return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

PublishedSource::PublishedSource(QString runtime, QObject* parent)
    : QObject(parent), runtime_(std::move(runtime)), watcher_(new QFileSystemWatcher(this)) {
    qRegisterMetaType<Published>();
    pool_.setMaxThreadCount(1);
    // Lapis replaces both files by renaming into the folder, which changes
    // the folder; appends to its logs there do not.
    if (QFileInfo(runtime_).isDir())
        watcher_->addPath(runtime_);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, &PublishedSource::reload);
    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, &PublishedSource::reload);
}

PublishedSource::~PublishedSource() {
    // Each read posts its result back here; wait so none outlives this.
    pool_.waitForDone();
}

void PublishedSource::setPolling(bool on) {
    if (on)
        poll_.start();
    else
        poll_.stop();
}

void PublishedSource::reload() {
    if (reading_) {
        again_ = true;
        return;
    }
    reading_ = true;
    const auto generation = ++generation_;
    pool_.start([this, runtime = runtime_, generation] {
        auto published = read_published(runtime);
        QMetaObject::invokeMethod(
            this,
            [this, generation, published = std::move(published)] {
                reading_ = false;
                if (generation == generation_)
                    emit loaded(published);
                if (std::exchange(again_, false))
                    reload();
            },
            Qt::QueuedConnection);
    });
}
} // namespace lapis::ultratab
