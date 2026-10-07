#include "session_sender.hpp"
#include "launch_spec.hpp"
#include "transport/local_protocol.hpp"

#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <exception>
#include <memory>
#include <utility>

namespace lapis::ultratab {
namespace wire = session::wire;
namespace {
constexpr quint64 kPasteRequest = 1;

session::AgentMode agent_mode(const Agent& agent) {
    if (agent.harness == QLatin1String("codex"))
        return session::AgentMode::codex;
    return agent.claude_mode ? session::AgentMode::claude : session::AgentMode::terminal;
}

// One join, one paste-and-Return, then gone. Owns its socket; deletes itself
// after reporting once.
class Submission final : public QObject {
  public:
    Submission(QByteArray fingerprint, QByteArray text, Sender::Done done, QObject* parent)
        : QObject(parent), fingerprint_(std::move(fingerprint)), text_(std::move(text)),
          done_(std::move(done)), socket_(new QLocalSocket(this)) {
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this,
                [this] { finish(false, QStringLiteral("the agent's session did not answer")); });
        connect(socket_, &QLocalSocket::connected, this, &Submission::attach);
        connect(socket_, &QLocalSocket::readyRead, this, &Submission::receive);
        connect(socket_, &QLocalSocket::errorOccurred, this,
                [this] { finish(false, QStringLiteral("the agent's session is not reachable")); });
        connect(socket_, &QLocalSocket::disconnected, this,
                [this] { finish(false, QStringLiteral("the agent's session closed")); });
    }
    void start(const QString& endpoint) {
        timer_.start(SessionSender::timeout_ms);
        socket_->connectToServer(endpoint);
    }

  private:
    enum class Stage : std::uint8_t { hello, screen, result, done };
    void attach() {
        write(wire::Kind::attach, wire::encode_attach({.mode = wire::AttachMode::join,
                                                       .fingerprint = fingerprint_,
                                                       .expected = {},
                                                       .hyperlinks = false,
                                                       .attention_phase = false,
                                                       .paste_transactions = true}));
    }
    void write(wire::Kind kind, const QByteArray& payload) {
        socket_->write(wire::frame(kind, payload));
    }
    void receive() {
        buffer_ += socket_->readAll();
        try {
            wire::Frame frame;
            while (stage_ != Stage::done && wire::take_frame(buffer_, frame))
                handle(frame);
        } catch (const std::exception& error) {
            finish(false, QString::fromUtf8(error.what()));
        }
    }
    void handle(const wire::Frame& frame) {
        if (frame.kind == wire::Kind::status) {
            const auto status = wire::decode_status(frame.payload);
            finish(false, stage_ == Stage::hello
                              ? QStringLiteral("this agent's session cannot be joined (%1); "
                                               "answer it in lapis")
                                    .arg(status.message)
                              : status.message);
            return;
        }
        if (stage_ == Stage::hello && frame.kind == wire::Kind::hello) {
            const auto hello = wire::decode_hello(frame.payload);
            if (!hello.paste_transactions) {
                finish(false, QStringLiteral("this agent's session predates submitted pastes; "
                                             "answer it in lapis"));
                return;
            }
            attachment_ = hello.attachment;
            stage_ = Stage::screen;
            return;
        }
        if (stage_ == Stage::screen && frame.kind == wire::Kind::snapshot) {
            const auto screen = wire::decode_snapshot_envelope(frame.payload);
            if (screen.attachment != attachment_)
                return;
            write(wire::Kind::ready, wire::encode_ready({attachment_, screen.sequence}));
            write(wire::Kind::paste_request,
                  wire::encode_paste_request({attachment_, kPasteRequest, true, text_}));
            stage_ = Stage::result;
            return;
        }
        if (stage_ == Stage::result && frame.kind == wire::Kind::paste_result) {
            const auto result = wire::decode_paste_result(frame.payload);
            if (result.attachment == attachment_ && result.request_id == kPasteRequest)
                finish(result.queued, result.message);
        }
        // Later screens and anything else a view receives are not needed.
    }
    void finish(bool admitted, const QString& message) {
        if (stage_ == Stage::done)
            return;
        stage_ = Stage::done;
        timer_.stop();
        disconnect(socket_, nullptr, this, nullptr);
        socket_->disconnectFromServer();
        if (auto done = std::exchange(done_, {}))
            done(admitted, message);
        deleteLater();
    }
    QByteArray fingerprint_;
    QByteArray text_;
    Sender::Done done_;
    QLocalSocket* socket_;
    QTimer timer_;
    QByteArray buffer_;
    wire::Attachment attachment_;
    Stage stage_{Stage::hello};
};
} // namespace

void SessionSender::submit(const Agent& agent, const QString& text, Done done) {
    const auto bytes = text.toUtf8();
    const auto fail = [this, &done](const QString& why) {
        // Report on the next turn, as a reply from the service would come.
        QTimer::singleShot(0, this, [done, why] { done(false, why); });
    };
    if (bytes.isEmpty() || bytes.size() > wire::max_paste_bytes)
        return fail(QStringLiteral("the reply is empty or too long"));
    QByteArray fingerprint;
    try {
        fingerprint =
            session::launch_fingerprint(session::validate_launch({.program = agent.program,
                                                                  .arguments = agent.arguments,
                                                                  .directory = agent.directory,
                                                                  .size = {100, 30},
                                                                  .agent = agent_mode(agent)}));
    } catch (const std::exception& error) {
        return fail(QStringLiteral("this agent's launch is not valid here (%1)")
                        .arg(QString::fromUtf8(error.what())));
    }
    auto* submission = new Submission(std::move(fingerprint), bytes, std::move(done), this);
    submission->start(agent.endpoint);
}
} // namespace lapis::ultratab
