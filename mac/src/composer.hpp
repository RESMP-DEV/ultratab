#ifndef LAPIS_ULTRATAB_COMPOSER_HPP
#define LAPIS_ULTRATAB_COMPOSER_HPP
#include "published.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <chrono>
#include <functional>
#include <optional>

class QProcess;

// Composed cards: for each agent that waits on the person, a card that helps
// them re-enter the thread (docs/ultratab.md, "Composed cards"). A helper
// (apps/ultratab/compose/compose.py) reads the conversation and asks a model;
// this side decides when to compose and owns runtime/ultratab_cards.json and
// runtime/ultratab_compose.jsonl.
namespace lapis::ultratab {
// "composer" in ~/.lapis/ultratab.json.
struct ComposerSettings {
    bool enabled{true};
    int debounce_ms{2000};
    int timeout_ms{120000};
    int max_running{2};
    QJsonObject helper; // passed to the helper: model, effort, endpoint
};
[[nodiscard]] ComposerSettings parse_composer(const QJsonValue& value);

// What a card is composed for: lapis's offer key when it has a guess, else
// the agent's last finished turn ("turn:<ms>").
[[nodiscard]] QString compose_key(const AgentState& state);
// The finished turn a key belongs to; a guess arrives for a turn already composed.
[[nodiscard]] qint64 compose_turn(const AgentState& state);

// Runs one composition. `done` runs once, on the caller's thread, with the
// helper's answer or why there is none (no conversation text).
class ComposeRunner {
  public:
    using Done = std::function<void(std::optional<QJsonObject> answer, const QString& failure)>;
    ComposeRunner() = default;
    virtual ~ComposeRunner() = default;
    ComposeRunner(const ComposeRunner&) = delete;
    ComposeRunner& operator=(const ComposeRunner&) = delete;
    ComposeRunner(ComposeRunner&&) = delete;
    ComposeRunner& operator=(ComposeRunner&&) = delete;
    virtual void start(const QJsonObject& job, std::chrono::milliseconds timeout, Done done) = 0;
};

// The helper as a process: `python3 compose.py compose`, the job on stdin, in
// its own process group so a timeout also ends the model call it started.
class ProcessComposeRunner final : public QObject, public ComposeRunner {
    Q_OBJECT
  public:
    ProcessComposeRunner(const QString& python, const QString& script, QObject* parent = nullptr);
    ~ProcessComposeRunner() override;
    ProcessComposeRunner(const ProcessComposeRunner&) = delete;
    ProcessComposeRunner& operator=(const ProcessComposeRunner&) = delete;
    ProcessComposeRunner(ProcessComposeRunner&&) = delete;
    ProcessComposeRunner& operator=(ProcessComposeRunner&&) = delete;
    void start(const QJsonObject& job, std::chrono::milliseconds timeout, Done done) override;

  private:
    QString python_;
    QString script_;
    QList<QProcess*> running_;
};

// Writes the helper and the next-prompt helper it imports into `folder`
// (owner-only); returns the helper's path, or empty when it could not.
[[nodiscard]] QString install_compose_helper(const QString& folder);
// An executable by name on PATH or where installers put them, or empty.
[[nodiscard]] QString find_tool(const QString& name);

class Composer final : public QObject {
    Q_OBJECT
  public:
    struct Paths {
        QString cards;   // runtime/ultratab_cards.json
        QString log;     // runtime/ultratab_compose.jsonl
        QString runtime; // lapis's runtime folder
        QString home;    // lapis's data folder
        QString claude;  // the Claude Code CLI
    };
    Composer(ComposeRunner& runner, Paths paths, ComposerSettings settings,
             QObject* parent = nullptr);

    void setPublished(const Published& published);
    // The agent whose card is in front while the overlay shows; empty while
    // it is hidden. Its card's content never changes under the person unless
    // its key does.
    void setHeld(const QString& agent_id);

    [[nodiscard]] QJsonObject cards() const;
    [[nodiscard]] qsizetype running() const { return running_.size(); }

    static constexpr qint64 max_file_bytes = qint64{512} * 1024;
    static constexpr qint64 max_card_bytes = qint64{64} * 1024;
    static constexpr qint64 max_log_bytes = qint64{1024} * 1024;

  signals:
    void cardsChanged();

  private:
    struct Entry {
        QJsonObject card;
        qint64 turn{-1};
    };
    struct Want {
        QString key;
        qint64 turn{};
        qint64 due_ms{};
    };
    struct Running {
        QString key;
        qint64 turn{};
    };
    void reconcile();
    void pump();
    void finished(const QString& id, const Running& run, const std::optional<QJsonObject>& answer,
                  const QString& failure, qint64 ms);
    void store(const QString& id, const QJsonObject& card, qint64 turn);
    void write();
    void log(const QJsonObject& event) const;
    [[nodiscard]] QJsonObject job(const Agent& agent, const AgentState& state,
                                  const QString& key) const;
    [[nodiscard]] QJsonObject fallback(const AgentState& state, const QString& key) const;
    [[nodiscard]] const Agent* agent(const QString& id) const;

    ComposeRunner& runner_;
    Paths paths_;
    ComposerSettings settings_;
    Published published_;
    QHash<QString, Entry> cards_;
    QHash<QString, Entry> held_back_; // results kept until the held card is let go
    QHash<QString, Want> wanted_;
    QHash<QString, Running> running_;
    QStringList order_; // waiting agents in deck order: the front card composes first
    QString held_;
    QTimer timer_;
    QElapsedTimer clock_;
    QByteArray written_;
};
} // namespace lapis::ultratab
#endif
