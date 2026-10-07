#include "deck.hpp"
#include "composer.hpp"
#include "attention_order.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

namespace lapis::ultratab {
namespace {
QString folder_label(const QString& directory) {
    if (QDir::cleanPath(directory) == QDir::homePath())
        return QStringLiteral("~");
    const auto name = QFileInfo(directory).fileName();
    return name.isEmpty() ? directory : name;
}

QString category_name(const Published& published, const QString& id) {
    for (const auto& category : published.categories)
        if (category.id == id)
            return category.name;
    return {};
}

bool waits_for_prompt(const QString& status) {
    return status == QLatin1String("finished") || status == QLatin1String("idle");
}

QString what_happened(const AgentState& state) {
    if (state.requests > 0) {
        const auto reason = one_sentence(state.request);
        return reason.isEmpty() ? QStringLiteral("Asks for your answer in its own window.")
                                : reason;
    }
    if (state.offer)
        if (const auto said = one_sentence(state.offer->said); !said.isEmpty())
            return said;
    return state.status == QLatin1String("idle") ? QStringLiteral("Ready for you.")
                                                 : QStringLiteral("Finished a turn.");
}

std::optional<Card> card_for(const Agent& agent, const AgentState& state, int position,
                             const Published& published) {
    const bool request = state.requests > 0;
    if (!request && !(waits_for_prompt(state.status) && (state.unseen || state.offer)))
        return std::nullopt;
    const int tier = desktop::waiting_tier({.guessed = state.offer.has_value(),
                                            .guess_seen = state.offer && state.offer->seen,
                                            .status = state.status,
                                            .unseen = state.unseen,
                                            .request = request});
    if (tier < 0)
        return std::nullopt;
    Card card;
    card.key = QStringLiteral("%1|%2|%3|%4|%5")
                   .arg(agent.id)
                   .arg(state.turn_at_ms)
                   .arg(state.needed_at_ms)
                   .arg(state.offer ? state.offer->key : QString())
                   .arg(state.requests);
    card.agent_id = agent.id;
    card.name = agent.title.isEmpty() ? folder_label(agent.directory) : agent.title;
    card.folder = folder_label(agent.directory);
    card.category_id = agent.category;
    card.category_name = category_name(published, agent.category);
    card.line = what_happened(state);
    card.proposal = request || !state.offer ? QString() : state.offer->text;
    card.request = request;
    card.tier = tier;
    card.needed_at_ms = state.needed_at_ms;
    card.position = position;
    if (const auto composed = published.composed.cards.constFind(agent.id);
        composed != published.composed.cards.cend() &&
        (composed->key == compose_key(state) || key_current(*composed, card.key))) {
        card.composed = *composed;
        if (!request && !composed->prompt.isEmpty())
            card.proposal = composed->prompt;
    }
    return card;
}
} // namespace

std::vector<Card> waiting_cards(const Published& published) {
    std::vector<Card> cards;
    for (int position = 0; position < published.agents.size(); ++position) {
        const auto& agent = published.agents.at(position);
        const auto state = published.states.constFind(agent.id);
        if (state == published.states.cend())
            continue;
        if (auto card = card_for(agent, *state, position, published))
            cards.push_back(std::move(*card));
    }
    std::stable_sort(cards.begin(), cards.end(), [](const Card& left, const Card& right) {
        return std::tie(left.tier, left.needed_at_ms, left.position) <
               std::tie(right.tier, right.needed_at_ms, right.position);
    });
    return cards;
}

QString one_sentence(const QString& text, qsizetype limit) {
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    static const QRegularExpression end(QStringLiteral("[.!?](\\s|$)"));
    auto line = text;
    // Markdown the agents write: heading and quote markers, bold and code
    // ticks. "C#", "#12" and "2*3" keep their characters.
    static const QRegularExpression markup(QStringLiteral("^[ \\t]*(?:#+[ \\t]+|>+[ \\t]*)"),
                                           QRegularExpression::MultilineOption);
    line.remove(markup);
    line.remove(QStringLiteral("**"));
    line.remove(QLatin1Char('`'));
    line = line.replace(spaces, QStringLiteral(" ")).trimmed();
    if (const auto found = end.match(line); found.hasMatch())
        line.truncate(found.capturedStart() + 1);
    if (line.size() > limit) {
        if (limit < 2)
            return {};
        auto cut = limit - 1;
        if (line.at(cut - 1).isHighSurrogate())
            --cut;
        const auto space = line.lastIndexOf(QLatin1Char(' '), cut);
        line = line.left(space > limit / 2 ? space : cut).trimmed() + QChar(0x2026);
    }
    return line;
}

void DiagramStore::replace(QHash<QString, QString> svgs) {
    const QMutexLocker lock(&mutex_);
    svgs_ = std::move(svgs);
}

QString DiagramStore::svg(const QString& key) const {
    const QMutexLocker lock(&mutex_);
    return svgs_.value(key);
}

QString DiagramStore::key_for(const QString& svg) {
    return QString::fromLatin1(
        QCryptographicHash::hash(svg.toUtf8(), QCryptographicHash::Sha256).toHex().left(32));
}

Deck::Deck(Sender& sender, QObject* parent)
    : QObject(parent), sender_(sender), running_(&writer_running),
      opener_([](const QUrl& url) { QDesktopServices::openUrl(url); }) {}

void Deck::setPublished(const Published& published) {
    published_ = published;
    cards_ = waiting_cards(published_);
    QHash<QString, QString> svgs;
    for (const auto& card : cards_)
        if (card.composed)
            for (const auto& block : card.composed->blocks)
                if (block.type == Block::Type::diagram)
                    svgs.insert(DiagramStore::key_for(block.svg), block.svg);
    diagrams_->replace(std::move(svgs));
    // An answered card stays hidden until the agent has something new, which
    // comes with a new key; keys no card carries any more are forgotten.
    QSet<QString> waiting;
    for (const auto& card : cards_)
        waiting.insert(card.key);
    for (auto key = answered_.begin(); key != answered_.end();)
        key = waiting.contains(*key) ? std::next(key) : answered_.erase(key);
    if (!category_.isEmpty() &&
        std::none_of(published_.categories.cbegin(), published_.categories.cend(),
                     [this](const Category& category) { return category.id == category_; }))
        category_.clear();
    emit changed();
}

std::vector<Card> Deck::visible() const {
    std::vector<Card> shown;
    for (const auto& card : cards_)
        if (!answered_.contains(card.key) && (category_.isEmpty() || card.category_id == category_))
            shown.push_back(card);
    // A newer card never takes the front from the one being typed to.
    if (!drafting_.isEmpty()) {
        const auto pinned = std::find_if(shown.begin(), shown.end(), [this](const Card& card) {
            return card.agent_id == drafting_;
        });
        if (pinned != shown.end())
            std::rotate(shown.begin(), pinned, std::next(pinned));
    }
    return shown;
}

QVariantList Deck::describe_blocks(const ComposedCard& composed) {
    QVariantList blocks;
    int links = 0;
    for (const auto& block : composed.blocks) {
        switch (block.type) {
        case Block::Type::text:
            blocks.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                                      {QStringLiteral("text"), block.text}});
            break;
        case Block::Type::list:
            blocks.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("list")},
                                      {QStringLiteral("items"), block.items}});
            break;
        case Block::Type::table: {
            QVariantList rows;
            for (const auto& row : block.rows)
                rows.append(QVariant(row));
            QVariantList numeric;
            for (const bool number : block.numeric)
                numeric.append(number);
            blocks.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("table")},
                                      {QStringLiteral("columns"), block.columns},
                                      {QStringLiteral("rows"), rows},
                                      {QStringLiteral("numeric"), numeric},
                                      {QStringLiteral("more"), block.more_rows}});
            break;
        }
        case Block::Type::diagram:
            blocks.append(
                QVariantMap{{QStringLiteral("type"), QStringLiteral("diagram")},
                            {QStringLiteral("source"),
                             QStringLiteral("image://diagram/") + DiagramStore::key_for(block.svg)},
                            {QStringLiteral("aspect"), block.size.width() / block.size.height()}});
            break;
        case Block::Type::link:
            blocks.append(QVariantMap{
                {QStringLiteral("type"), QStringLiteral("link")},
                {QStringLiteral("label"), block.label},
                {QStringLiteral("target"), block.url.isLocalFile()
                                               ? QDir::toNativeSeparators(block.url.toLocalFile())
                                               : block.url.host()},
                {QStringLiteral("index"), links++}});
            break;
        }
    }
    return blocks;
}

QVariantMap Deck::describe(const Card& card) {
    const auto& composed = card.composed;
    const auto headline = composed && !composed->tldr.isEmpty() ? composed->tldr : card.line;
    return {{QStringLiteral("key"), card.key},
            {QStringLiteral("composed"), composed.has_value()},
            {QStringLiteral("since"), composed ? composed->since : QString()},
            {QStringLiteral("headline"), headline},
            {QStringLiteral("blocks"), composed ? describe_blocks(*composed) : QVariantList{}},
            {QStringLiteral("agent"), card.agent_id},
            {QStringLiteral("name"), card.name},
            {QStringLiteral("folder"), card.folder},
            {QStringLiteral("category"), card.category_name},
            {QStringLiteral("line"), card.line},
            {QStringLiteral("proposal"), card.proposal},
            {QStringLiteral("request"), card.request},
            {QStringLiteral("canAccept"), !card.request && !card.proposal.isEmpty()},
            {QStringLiteral("canType"), !card.request}};
}

QVariantMap Deck::front() const {
    const auto shown = visible();
    return shown.empty() ? QVariantMap{} : describe(shown.front());
}

QVariantMap Deck::behind() const {
    const auto shown = visible();
    return shown.size() < 2 ? QVariantMap{} : describe(shown.at(1));
}

QVariantList Deck::rail() const {
    QHash<QString, int> counts;
    int total = 0;
    for (const auto& card : cards_)
        if (!answered_.contains(card.key)) {
            ++counts[card.category_id];
            ++total;
        }
    QVariantList rail{QVariantMap{{QStringLiteral("id"), QString()},
                                  {QStringLiteral("name"), QStringLiteral("all")},
                                  {QStringLiteral("count"), total},
                                  {QStringLiteral("selected"), category_.isEmpty()}}};
    for (const auto& category : published_.categories)
        rail.append(QVariantMap{{QStringLiteral("id"), category.id},
                                {QStringLiteral("name"), category.name},
                                {QStringLiteral("count"), counts.value(category.id)},
                                {QStringLiteral("selected"), category.id == category_}});
    return rail;
}

QVariantList Deck::running() const {
    QStringList names;
    for (const auto& agent : published_.agents)
        if (published_.states.value(agent.id).status == QLatin1String("working"))
            names.append(agent.title.isEmpty() ? folder_label(agent.directory) : agent.title);
    QVariantList rows;
    for (const auto& name : names.mid(0, running_limit))
        rows.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("more"), false}});
    if (names.size() > running_limit)
        rows.append(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("+%1 more").arg(names.size() - running_limit)},
            {QStringLiteral("more"), true}});
    return rows;
}

QVariantList Deck::history() const {
    QVariantList rows;
    for (const auto& answer : history_)
        rows.append(QVariantMap{{QStringLiteral("name"), answer.name},
                                {QStringLiteral("how"), answer.how},
                                {QStringLiteral("text"), answer.text},
                                {QStringLiteral("outcome"), answer.outcome}});
    return rows;
}

QString Deck::notice() const {
    if (!published_.has_registry)
        return published_.problem.isEmpty() ? QStringLiteral("No agent workspace found.")
                                            : published_.problem;
    if (!published_.has_state)
        return published_.problem.isEmpty()
                   ? QStringLiteral("The agent window does not publish its agents' state yet.")
                   : published_.problem;
    if (!running_(published_.writer_pid))
        return QStringLiteral("The agent window is closed; this is what it last showed.");
    return {};
}

const Agent* Deck::agent(const QString& id) const {
    for (const auto& agent : published_.agents)
        if (agent.id == id)
            return &agent;
    return nullptr;
}

void Deck::remember(Answer answer) {
    history_.push_front(std::move(answer));
    while (history_.size() > static_cast<std::size_t>(history_limit))
        history_.pop_back();
}

bool Deck::answer(const Card& card, const QString& how, const QString& text) {
    const auto* target = agent(card.agent_id);
    if (target == nullptr)
        return false;
    answered_.insert(card.key);
    const auto id = ++answers_;
    remember({id, card.name, how, text, QStringLiteral("sending")});
    message_ = QStringLiteral("Sending to %1").arg(card.name);
    emit changed();
    sender_.submit(*target, text,
                   [deck = QPointer<Deck>(this), id, key = card.key,
                    name = card.name](bool admitted, const QString& why) {
                       if (!deck)
                           return;
                       for (auto& entry : deck->history_)
                           if (entry.id == id)
                               entry.outcome =
                                   admitted ? QStringLiteral("sent") : QStringLiteral("not sent");
                       if (admitted) {
                           deck->message_ = QStringLiteral("Sent to %1").arg(name);
                       } else {
                           // The card comes back for another answer.
                           deck->answered_.remove(key);
                           deck->message_ = QStringLiteral("Not sent to %1: %2").arg(name, why);
                       }
                       emit deck->changed();
                   });
    return true;
}

bool Deck::accept() {
    const auto shown = visible();
    if (shown.empty() || shown.front().request || shown.front().proposal.isEmpty())
        return false;
    return answer(shown.front(), QStringLiteral("accepted"), shown.front().proposal);
}

bool Deck::send(const QString& text) {
    const auto shown = visible();
    const auto trimmed = text.trimmed();
    if (shown.empty() || shown.front().request || trimmed.isEmpty())
        return false;
    if (!drafting_.isEmpty() && shown.front().agent_id != drafting_) {
        // The agent typed to stopped waiting. The text is kept, and stays
        // unsendable until it is cleared, so it never reaches another agent.
        message_ = QStringLiteral("That agent is no longer waiting; nothing was sent. "
                                  "Escape clears the text.");
        emit changed();
        return false;
    }
    drafting_.clear();
    return answer(shown.front(), QStringLiteral("typed"), trimmed);
}

void Deck::setDrafting(bool on) {
    if (!on) {
        if (!drafting_.isEmpty()) {
            drafting_.clear();
            emit changed();
        }
        return;
    }
    if (!drafting_.isEmpty())
        return;
    const auto shown = visible();
    if (!shown.empty())
        drafting_ = shown.front().agent_id;
}

bool Deck::skip() {
    const auto shown = visible();
    if (shown.empty())
        return false;
    const auto& card = shown.front();
    answered_.insert(card.key);
    remember({++answers_, card.name, QStringLiteral("skipped"), {}, QStringLiteral("skipped")});
    message_.clear();
    emit changed();
    return true;
}

bool Deck::openLink(int index) {
    const auto shown = visible();
    if (shown.empty() || !shown.front().composed || index < 0)
        return false;
    int links = 0;
    for (const auto& block : shown.front().composed->blocks)
        if (block.type == Block::Type::link && links++ == index) {
            // Checked again here: only what parsing admitted is ever opened.
            if (!openable(block.url))
                return false;
            opener_(block.url);
            message_ = QStringLiteral("Opened %1").arg(block.label);
            emit changed();
            return true;
        }
    return false;
}

bool Deck::optionDown() {
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::AltModifier);
}

void Deck::setListening(bool on) {
    if (listening_ == on)
        return;
    listening_ = on;
    emit changed();
}

void Deck::nextCategory(int delta) {
    QStringList ids{QString()};
    for (const auto& category : published_.categories)
        ids.append(category.id);
    const auto count = static_cast<int>(ids.size());
    const auto current = static_cast<int>(std::max<qsizetype>(0, ids.indexOf(category_)));
    category_ = ids.at(((current + delta) % count + count) % count);
    emit changed();
}
} // namespace lapis::ultratab
