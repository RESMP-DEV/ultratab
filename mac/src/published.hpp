#ifndef LAPIS_ULTRATAB_PUBLISHED_HPP
#define LAPIS_ULTRATAB_PUBLISHED_HPP
#include "cards.hpp"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QtGlobal>
#include <optional>

class QFileSystemWatcher;

// What lapis publishes about its agents, read without taking its workspace
// lock and without writing anything of lapis's: the workspace registry
// (runtime/workspace.json) and the window's agent state
// (runtime/agent_state.json, AgentStatePublisher), and the composed cards
// written beside them (runtime/ultratab_cards.json, cards.hpp).
namespace lapis::ultratab {
struct Category {
    QString id;
    QString name;
};
// One registry record: who the agent is and how to reach its session service.
struct Agent {
    QString id;
    QString title;
    QString category;
    QString directory;
    QString harness;
    QString endpoint;
    QString program;
    QStringList arguments;
    bool claude_mode{}; // launched with lapis's Claude Code adapter
};
struct Offer {
    QString key;
    QString text;
    QString said; // the agent's last reply the guess answers
    bool seen{};
};
// The window's view of one agent (agent_state.json version 1).
struct AgentState {
    QString status; // SessionPreview::statusKind
    bool unseen{};
    int requests{};
    QString request;
    qint64 needed_at_ms{};
    qint64 turn_at_ms{};
    std::optional<Offer> offer;
};
struct Published {
    QList<Category> categories;
    QList<Agent> agents; // registry order
    QHash<QString, AgentState> states;
    qint64 writer_pid{};
    bool has_registry{};
    bool has_state{};
    ComposedCards composed; // runtime/ultratab_cards.json, when present
    QString problem;        // why a file could not be read; empty when all were
};

// Limits on what is read: lapis itself refuses a larger registry.
constexpr qint64 max_registry_bytes = qint64{1024} * 1024;
constexpr qint64 max_state_bytes = qint64{4} * 1024 * 1024;
constexpr int state_version = 1;

// LAPIS_HOME when set, else ~/.lapis once it has a workspace (the downloaded
// app), else empty: as the phone gateway decides.
[[nodiscard]] QString lapis_home();
// Reads the files in `runtime`. Records that fail validation (an endpoint
// outside `runtime`, a relative program) are skipped; the rest is kept.
[[nodiscard]] Published read_published(const QString& runtime);
[[nodiscard]] Published parse_published(const QString& runtime, const QByteArray& registry,
                                        const QByteArray& state);
// Whether the window that wrote the state still runs.
[[nodiscard]] bool writer_running(qint64 pid);

// Follows the three files: re-reads them off the GUI thread when the folder
// changes (and every `poll_ms` while polling), and emits the newest result.
class PublishedSource final : public QObject {
    Q_OBJECT
  public:
    explicit PublishedSource(QString runtime, QObject* parent = nullptr);
    ~PublishedSource() override;
    PublishedSource(const PublishedSource&) = delete;
    PublishedSource& operator=(const PublishedSource&) = delete;
    // Reads now (soon, off this thread).
    void reload();
    // A fallback for missed folder events, used while the overlay shows.
    void setPolling(bool on);
    [[nodiscard]] const QString& runtime() const { return runtime_; }

  signals:
    void loaded(const lapis::ultratab::Published& published);

  private:
    QString runtime_;
    QFileSystemWatcher* watcher_{};
    QTimer poll_;
    QThreadPool pool_; // one read at a time
    quint64 generation_{};
    bool reading_{};
    bool again_{};
};
} // namespace lapis::ultratab
Q_DECLARE_METATYPE(lapis::ultratab::Published)
#endif
