#ifndef LAPIS_ULTRATAB_SESSION_SENDER_HPP
#define LAPIS_ULTRATAB_SESSION_SENDER_HPP
#include "deck.hpp"

#include <QObject>

namespace lapis::ultratab {
// Sends a reply the way the phone gateway types: it joins the agent's session
// service as an extra view (wire v6 attach mode `join`), so the lapis window
// keeps its own attachment, acknowledges the first screen, then submits the
// text as one paste-and-Return transaction and leaves. It never resizes the
// terminal and never falls back to taking the agent over (discover mode); a
// service that cannot be joined is reported instead. The service refuses the
// submission while the agent has a pending request.
class SessionSender final : public QObject, public Sender {
    Q_OBJECT
  public:
    explicit SessionSender(QObject* parent = nullptr) : QObject(parent) {}
    // Bounds one whole submission, from connecting to the service's answer.
    static constexpr int timeout_ms = 5000;
    void submit(const Agent& agent, const QString& text, Done done) override;
};
} // namespace lapis::ultratab
#endif
