#include "deck.hpp"
#include "attention_order.hpp"

#include <QDir>
#include <QFileInfo>
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
        return reason.isEmpty() ? QStringLiteral("Asks for your answer in lapis.") : reason;
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
    // Markdown the agents write: headings, bold and code ticks.
    static const QRegularExpression markup(QStringLiteral("[*`#]+|^>\\s*"));
    line.remove(markup);
    line = line.replace(spaces, QStringLiteral(" ")).trimmed();
    if (const auto found = end.match(line); found.hasMatch())
        line.truncate(found.capturedStart() + 1);
    if (line.size() > limit) {
        auto cut = limit - 1;
        if (line.at(cut - 1).isHighSurrogate())
            --cut;
        const auto space = line.lastIndexOf(QLatin1Char(' '), cut);
        line = line.left(space > limit / 2 ? space : cut).trimmed() + QChar(0x2026);
    }
    return line;
}

Deck::Deck(Sender& sender, QObject* parent)
    : QObject(parent), sender_(sender), running_(&writer_running) {}

void Deck::setPublished(const Published& published) {
    published_ = published;
    cards_ = waiting_cards(published_);
    // Forget answers to cards whose agent is gone; keep the rest so an
    // answered card stays hidden until the agent has something new.
    QSet<QString> ids;
    for (const auto& agent : published_.agents)
        ids.insert(agent.id);
    for (auto key = answered_.begin(); key != answered_.end();)
        key = ids.contains(key->section(QLatin1Char('|'), 0, 0)) ? std::next(key)
                                                                 : answered_.erase(key);
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
    return shown;
}

QVariantMap Deck::describe(const Card& card) {
    return {{QStringLiteral("key"), card.key},
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
        return published_.problem.isEmpty() ? QStringLiteral("No lapis workspace found.")
                                            : published_.problem;
    if (!published_.has_state)
        return QStringLiteral("This lapis does not publish its agents' state yet.");
    if (!running_(published_.writer_pid))
        return QStringLiteral("lapis is closed; this is what it last showed.");
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
    return answer(shown.front(), QStringLiteral("typed"), trimmed);
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
