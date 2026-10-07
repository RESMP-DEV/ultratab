import Foundation
import Observation

// One agent that needs you, as a card (apps/ultratab/src/deck.hpp).
struct Card: Equatable, Identifiable {
    var key: String // changes when the agent has something new for you
    var agentID: String
    var name: String
    var folder: String
    var categoryID: String
    var categoryName: String
    var harness: String
    var line: String     // what happened, one sentence
    var proposal: String // the proposed reply; may be empty
    var request: Bool    // a request (a permission prompt, say) is pending
    var tier: Int
    var neededAtMs: Int64
    var position: Int
    var composed: ComposedCard?

    var id: String { key }
    var headline: String {
        if let tldr = composed?.tldr, !tldr.isEmpty { return tldr }
        return line
    }
    var canAccept: Bool { !request && !proposal.isEmpty }
    var canType: Bool { !request }
}

enum DeckOrder {
    // lapis's Tab tiers (apps/desktop/src/attention_order.hpp): 0 a guess
    // not yet seen; 1 a turn that finished unseen, or a request; 2 a guess
    // already seen; -1 when the agent does not wait.
    static func tier(guessed: Bool, guessSeen: Bool, status: String, unseen: Bool, request: Bool) -> Int {
        let waiting = status == "finished" || status == "idle"
        if guessed && waiting && !guessSeen { return 0 }
        if unseen || request { return 1 }
        if guessed && waiting { return 2 }
        return -1
    }

    // Agents that need you, in the order lapis's Tab visits them: its tiers,
    // then the one waiting longest, then registry order. An agent at work,
    // or one whose state is unknown, is never a card; one with a pending
    // request always is.
    static func cards(_ published: Published) -> [Card] {
        var cards: [Card] = []
        for (position, agent) in published.agents.enumerated() {
            guard let state = published.states[agent.id],
                  let card = card(agent, state, position, published) else { continue }
            cards.append(card)
        }
        return cards.sorted {
            ($0.tier, $0.neededAtMs, $0.position) < ($1.tier, $1.neededAtMs, $1.position)
        }
    }

    static func card(_ agent: DeckAgent, _ state: AgentState, _ position: Int, _ published: Published) -> Card? {
        let request = state.requests > 0
        let waiting = state.status == "finished" || state.status == "idle"
        guard request || (waiting && (state.unseen || state.offer != nil)) else { return nil }
        let tier = tier(guessed: state.offer != nil, guessSeen: state.offer?.seen ?? false,
                        status: state.status, unseen: state.unseen, request: request)
        guard tier >= 0 else { return nil }
        let key = "\(agent.id)|\(state.turnAtMs)|\(state.neededAtMs)|\(state.offer?.key ?? "")|\(state.requests)"
        let name = agent.title.isEmpty ? folderLabel(agent.directory) : agent.title
        var card = Card(
            key: key, agentID: agent.id, name: name, folder: folderLabel(agent.directory),
            categoryID: agent.category,
            categoryName: published.categories.first { $0.id == agent.category }?.name ?? "",
            harness: agent.harness, line: whatHappened(state),
            proposal: request ? "" : (state.offer?.text ?? ""), request: request, tier: tier,
            neededAtMs: state.neededAtMs, position: position)
        if let composed = published.composed[agent.id],
           composed.key == composeKey(state) || keyCurrent(composed.key, cardKey: key) {
            card.composed = composed
            if !request && !composed.prompt.isEmpty { card.proposal = composed.prompt }
        }
        return card
    }

    // The composer's key for a turn (apps/ultratab/src/composer.cpp).
    static func composeKey(_ state: AgentState) -> String {
        if let offer = state.offer, !offer.key.isEmpty { return offer.key }
        return "turn:\(state.turnAtMs > 0 ? state.turnAtMs : state.neededAtMs)"
    }

    // A composed card is current when its key is the card's key, with or
    // without the leading "<agent id>|".
    static func keyCurrent(_ composedKey: String, cardKey: String) -> Bool {
        guard !composedKey.isEmpty else { return false }
        if composedKey == cardKey { return true }
        guard let bar = cardKey.firstIndex(of: "|"), bar > cardKey.startIndex else { return false }
        return composedKey == String(cardKey[cardKey.index(after: bar)...])
    }

    static func whatHappened(_ state: AgentState) -> String {
        if state.requests > 0 {
            let reason = oneSentence(state.request)
            return reason.isEmpty ? "Asks for your answer in its own window." : reason
        }
        if let said = state.offer.map({ oneSentence($0.said) }), !said.isEmpty { return said }
        return state.status == "idle" ? "Ready for you." : "Finished a turn."
    }

    static func folderLabel(_ directory: String) -> String {
        let trimmed = directory.hasSuffix("/") && directory.count > 1 ? String(directory.dropLast()) : directory
        if trimmed.range(of: "^/(Users|home)/[^/]+$", options: .regularExpression) != nil { return "~" }
        let name = (trimmed as NSString).lastPathComponent
        return name.isEmpty ? directory : name
    }

    // The first sentence of `text` on one line, ending in an ellipsis past
    // `limit` characters.
    static func oneSentence(_ text: String, limit: Int = 160) -> String {
        var line = text.replacingOccurrences(of: "[*`#]+|^>\\s*", with: "", options: .regularExpression)
        line = line.replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespaces)
        if let end = line.range(of: "[.!?](\\s|$)", options: .regularExpression) {
            line = String(line[..<line.index(after: end.lowerBound)])
        }
        if line.count > limit {
            let cut = limit - 1
            let head = String(line.prefix(cut))
            if let space = head.lastIndex(of: " "), head.distance(from: head.startIndex, to: space) > limit / 2 {
                line = String(head[..<space]).trimmingCharacters(in: .whitespaces) + "\u{2026}"
            } else {
                line = head.trimmingCharacters(in: .whitespaces) + "\u{2026}"
            }
        }
        return line
    }
}

// Sends text to an agent as if typed and submitted with Return: one paste
// and Return through a join that never replaces the Mac window's
// attachment. Returns nil when the agent's session admitted it, else why not.
protocol Sender: Sendable {
    func submit(agentID: String, text: String) async -> String?
}

// What a swipe on the front card means.
enum Swipe: Equatable {
    case accept // right: send the proposed reply
    case skip   // left: next card, nothing sent
    case none   // not far enough: the card springs back

    static let distance: Double = 110
    static let flick: Double = 260

    // A swipe counts past `distance`, or when a flick's predicted end is
    // past `flick`; mostly vertical drags are scrolls, never answers.
    static func from(dx: Double, dy: Double, predictedDx: Double) -> Swipe {
        guard abs(dx) > abs(dy) else { return .none }
        let reach = abs(dx) >= distance ? dx : (abs(predictedDx) >= flick ? predictedDx : 0)
        if reach > 0 { return .accept }
        if reach < 0 { return .skip }
        return .none
    }
}

// The deck the phone shows. Every card takes the same three answers: accept
// (send the proposed reply), annotate (send dictated or typed text) and skip.
// Answering hides the card at once; a send the service refuses brings it back
// with the reason.
@MainActor @Observable
final class Deck {
    struct Answer: Equatable, Identifiable {
        let id: Int
        let name: String
        let how: String // accepted, annotated, skipped
        let text: String
        var outcome: String // sending, sent, not sent, skipped
    }

    enum Outcome: Equatable {
        case sending
        case skipped
        case refused(String) // nothing sent, with why
    }

    private(set) var published = Published()
    private(set) var cards: [Card] = []
    private(set) var answered: Set<String> = []
    // Why the last send of a card was refused, by card key.
    private(set) var refusals: [String: String] = [:]
    private(set) var history: [Answer] = []
    private(set) var message = ""
    var category = "" // empty: every category
    var connection = "" // why the Mac cannot be reached; empty when it can

    private var sender: Sender
    private var answers = 0
    static let historyLimit = 200

    init(sender: Sender) {
        self.sender = sender
    }

    // A line under the card (a dictation problem, say).
    func say(_ text: String) {
        message = text
    }

    // Later answers go through `sender` (the Mac's address changed).
    func retarget(_ sender: Sender) {
        self.sender = sender
    }

    func setPublished(_ published: Published) {
        self.published = published
        cards = DeckOrder.cards(published)
        // Forget answers to cards whose agent is gone; keep the rest so an
        // answered card stays away until the agent has something new.
        let ids = Set(published.agents.map(\.id))
        answered = answered.filter { ids.contains(agentID(ofKey: $0)) }
        refusals = refusals.filter { key, _ in cards.contains { $0.key == key } }
        if !category.isEmpty && !published.categories.contains(where: { $0.id == category }) {
            category = ""
        }
    }

    var visible: [Card] {
        cards.filter { !answered.contains($0.key) && (category.isEmpty || $0.categoryID == category) }
    }

    var front: Card? { visible.first }
    var behind: Card? { visible.dropFirst().first }

    // Categories with how many cards wait in each, "all" first.
    var rail: [(id: String, name: String, count: Int)] {
        let waiting = cards.filter { !answered.contains($0.key) }
        var rows = [(id: "", name: "all", count: waiting.count)]
        for category in published.categories {
            rows.append((category.id, category.name, waiting.filter { $0.categoryID == category.id }.count))
        }
        return rows
    }

    var running: [String] {
        published.agents.filter { published.states[$0.id]?.status == "working" }
            .map { $0.title.isEmpty ? DeckOrder.folderLabel($0.directory) : $0.title }
    }

    // Why the deck may be incomplete.
    var notice: String {
        if !connection.isEmpty { return connection }
        if !published.hasRegistry { return "No agent workspace found on the Mac." }
        if !published.hasState { return "The lapis window on the Mac does not publish its agents' state yet." }
        if !published.writerRunning { return "The lapis window on the Mac is closed; this is what it last showed." }
        return ""
    }

    // Right swipe: send the front card's proposed reply.
    @discardableResult
    func accept() -> Outcome {
        guard let card = front else { return .refused("No card") }
        if card.request { return refuse("Answer this request in lapis; skip it here") }
        if card.proposal.isEmpty { return refuse("No proposed reply; dictate or type one") }
        return answer(card, how: "accepted", text: card.proposal)
    }

    // Annotate: send dictated or typed text to the front card's agent.
    @discardableResult
    func send(_ text: String) -> Outcome {
        guard let card = front else { return .refused("No card") }
        if card.request { return refuse("Answer this request in lapis; skip it here") }
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return refuse("Nothing to send") }
        return answer(card, how: "annotated", text: trimmed)
    }

    // Left swipe: drop the front card without sending anything.
    @discardableResult
    func skip() -> Outcome {
        guard let card = front else { return .refused("No card") }
        answered.insert(card.key)
        refusals[card.key] = nil
        remember(card.name, how: "skipped", text: "", outcome: "skipped")
        message = ""
        return .skipped
    }

    @discardableResult
    func apply(_ swipe: Swipe) -> Outcome? {
        switch swipe {
        case .accept: accept()
        case .skip: skip()
        case .none: nil
        }
    }

    func nextCategory(_ delta: Int) {
        let ids = [""] + published.categories.map(\.id)
        let current = ids.firstIndex(of: category) ?? 0
        category = ids[((current + delta) % ids.count + ids.count) % ids.count]
    }

    // The task finishes once the agent's session has answered.
    private var pending: [Task<Void, Never>] = []

    func waitForSends() async {
        let tasks = pending
        pending = []
        for task in tasks { await task.value }
    }

    private func refuse(_ why: String) -> Outcome {
        message = why
        return .refused(why)
    }

    private func answer(_ card: Card, how: String, text: String) -> Outcome {
        answered.insert(card.key)
        refusals[card.key] = nil
        let id = remember(card.name, how: how, text: text, outcome: "sending")
        message = "Sending to \(card.name)"
        let sender = sender
        pending.append(Task { [weak self] in
            let refused = await sender.submit(agentID: card.agentID, text: text)
            self?.finish(id: id, card: card, refused: refused)
        })
        return .sending
    }

    private func finish(id: Int, card: Card, refused: String?) {
        if let index = history.firstIndex(where: { $0.id == id }) {
            history[index].outcome = refused == nil ? "sent" : "not sent"
        }
        if let refused {
            // The card comes back for another answer.
            answered.remove(card.key)
            refusals[card.key] = refused
            message = "Not sent to \(card.name): \(refused)"
        } else {
            message = "Sent to \(card.name)"
        }
    }

    @discardableResult
    private func remember(_ name: String, how: String, text: String, outcome: String) -> Int {
        answers += 1
        history.insert(Answer(id: answers, name: name, how: how, text: text, outcome: outcome), at: 0)
        if history.count > Deck.historyLimit { history.removeLast(history.count - Deck.historyLimit) }
        return answers
    }

    private func agentID(ofKey key: String) -> String {
        String(key.prefix { $0 != "|" })
    }
}
