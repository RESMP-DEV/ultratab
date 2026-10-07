#ifndef LAPIS_ULTRATAB_DECK_HPP
#define LAPIS_ULTRATAB_DECK_HPP
#include "published.hpp"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lapis::ultratab {
// One agent that needs you, as a card.
struct Card {
    QString key; // changes when the agent has something new for you
    QString agent_id;
    QString name;
    QString folder;
    QString category_id;
    QString category_name;
    QString line;     // what happened, one sentence
    QString proposal; // the proposed reply (composed, else lapis's guess); may be empty
    bool request{};   // a request (such as a permission prompt) is pending
    int tier{};       // lapis's Tab tier (attention_order.hpp)
    qint64 needed_at_ms{};
    int position{}; // registry order
    // The composer's card for this turn, when it is current.
    std::optional<ComposedCard> composed;
};

// Sanitized diagrams by content hash, read by the overlay's image provider
// (possibly off the GUI thread).
class DiagramStore {
  public:
    void replace(QHash<QString, QString> svgs);
    [[nodiscard]] QString svg(const QString& key) const;
    [[nodiscard]] static QString key_for(const QString& svg);

  private:
    mutable QMutex mutex_;
    QHash<QString, QString> svgs_;
};

// Agents that need you, in the order lapis's Tab visits them: its tiers,
// then the one waiting longest. An agent at work, or one whose state is
// unknown, is never a card; one with a pending request always is.
[[nodiscard]] std::vector<Card> waiting_cards(const Published& published);
// The first sentence of `text` on one line, ending in an ellipsis past
// `limit` characters.
[[nodiscard]] QString one_sentence(const QString& text, qsizetype limit = 160);

// Sends text to an agent's session as if typed and submitted with Return.
// `done` runs once, on the caller's thread, with whether the agent's
// session service admitted it and a reason when it did not.
class Sender {
  public:
    using Done = std::function<void(bool admitted, const QString& message)>;
    Sender() = default;
    virtual ~Sender() = default;
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;
    Sender(Sender&&) = delete;
    Sender& operator=(Sender&&) = delete;
    virtual void submit(const Agent& agent, const QString& text, Done done) = 0;
};

// The deck the overlay shows. Every card takes the same four answers: accept
// (send lapis's guess), type (send what was typed), speak (a placeholder
// until a voice path exists) and skip (drop the card; it stays in history).
// Answering hides the card at once; a send the service refuses brings it back.
class Deck final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap front READ front NOTIFY changed)
    Q_PROPERTY(QVariantMap behind READ behind NOTIFY changed)
    Q_PROPERTY(QVariantList rail READ rail NOTIFY changed)
    Q_PROPERTY(QVariantList running READ running NOTIFY changed)
    Q_PROPERTY(QVariantList history READ history NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // Why the deck may be incomplete: no workspace, no published state, or
    // lapis closed since it last published.
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
  public:
    explicit Deck(Sender& sender, QObject* parent = nullptr);

    void setPublished(const Published& published);
    // Whether the window that publishes the state still runs (a test seam).
    void setWriterCheck(std::function<bool(qint64)> running) { running_ = std::move(running); }
    // How a link is opened (a test seam; default: the system's handler).
    void setLinkOpener(std::function<void(const QUrl&)> opener) { opener_ = std::move(opener); }
    [[nodiscard]] const std::shared_ptr<DiagramStore>& diagrams() const { return diagrams_; }

    [[nodiscard]] QVariantMap front() const;
    [[nodiscard]] QVariantMap behind() const;
    [[nodiscard]] QVariantList rail() const;
    [[nodiscard]] QVariantList running() const;
    [[nodiscard]] QVariantList history() const;
    [[nodiscard]] const QString& message() const { return message_; }
    [[nodiscard]] QString notice() const;
    [[nodiscard]] bool listening() const { return listening_; }
    // The cards shown, in order, for the selected category.
    [[nodiscard]] std::vector<Card> visible() const;

    // Tab: send the front card's guess. False when it has none.
    Q_INVOKABLE bool accept();
    // Enter: send typed text to the front card's agent. False when nothing
    // was sent (no card, a request, or empty text).
    Q_INVOKABLE bool send(const QString& text);
    // Left arrow or Delete: drop the front card without sending anything.
    Q_INVOKABLE bool skip();
    // Holding the speak key. Voice input is not built yet; this only shows
    // that the deck is listening.
    Q_INVOKABLE void setListening(bool on);
    // Whether an Option key is down right now (the summoning chord may still
    // be held when the overlay takes the keyboard).
    Q_INVOKABLE static bool optionDown();
    // Command-[ and Command-]: the previous or next category on the rail.
    Q_INVOKABLE void nextCategory(int delta);
    // Escape with nothing typed.
    Q_INVOKABLE void dismiss() { emit dismissRequested(); }
    // Command-click on a link chip (`index` among the front card's links), or
    // Command-O for the first. Opens it with the system's handler; the card
    // stays. False when there is no such link.
    Q_INVOKABLE bool openLink(int index);

    static constexpr int history_limit = 200;
    static constexpr int running_limit = 12;

  signals:
    void changed();
    void dismissRequested();

  private:
    struct Answer {
        quint64 id{};
        QString name;
        QString how; // accepted, typed, skipped
        QString text;
        QString outcome; // sending, sent, not sent, skipped
    };
    [[nodiscard]] const Agent* agent(const QString& id) const;
    [[nodiscard]] static QVariantMap describe(const Card& card);
    [[nodiscard]] static QVariantList describe_blocks(const ComposedCard& composed);
    bool answer(const Card& card, const QString& how, const QString& text);
    void remember(Answer answer);
    Sender& sender_;
    Published published_;
    std::vector<Card> cards_;
    QSet<QString> answered_; // card keys hidden by an answer
    std::deque<Answer> history_;
    quint64 answers_{};
    QString category_; // empty: every category
    QString message_;
    bool listening_{};
    std::function<bool(qint64)> running_;
    std::function<void(const QUrl&)> opener_;
    std::shared_ptr<DiagramStore> diagrams_{std::make_shared<DiagramStore>()};
};
} // namespace lapis::ultratab
#endif
