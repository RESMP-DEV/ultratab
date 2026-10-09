import SwiftUI

@main
struct UltraTabApp: App {
    @State private var store = DeckStore()

    var body: some Scene {
        WindowGroup {
            DeckView()
                .environment(store)
                .environment(store.deck)
                .preferredColorScheme(.dark)
                .tint(Theme.gold)
        }
    }
}

enum Theme {
    static let gold = Color(red: 0xE8 / 255, green: 0xB9 / 255, blue: 0x31 / 255)
    static let background = Color(red: 0x0A / 255, green: 0x0B / 255, blue: 0x0F / 255)
    static let card = Color(red: 0.075, green: 0.082, blue: 0.11)
    static let raised = Color(red: 0.11, green: 0.12, blue: 0.16)
    static let edge = Color(white: 0.2)
    static let body = Color(red: 0xC6 / 255, green: 0xD2 / 255, blue: 0xE4 / 255)
    static let quiet = Color(white: 0.55)
    static let accept = Color(red: 0.30, green: 0.85, blue: 0.55)
    static let skip = Color(red: 0.95, green: 0.42, blue: 0.38)
}

// Follows the Mac's deck while the app is active: one request waits for the
// next change (or eight seconds), then the next one starts.
@MainActor @Observable
final class DeckStore {
    static let hostKey = "gatewayHost"

    let deck: Deck
    var host: String {
        didSet {
            UserDefaults.standard.set(host, forKey: DeckStore.hostKey)
            if host != oldValue { restart() }
        }
    }
    var gateway: DeckGateway? { try? DeckGateway(host: host) }
    // A drag pose shown without a finger, for the UI tests' captures
    // (-swipePose accept|skip).
    let pose: Swipe?

    private var following: Task<Void, Never>?

    init() {
        let defaults = UserDefaults.standard
        // The UI tests' first launch (-resetTutorial YES) shows the tutorial
        // again; Got it is remembered afterwards, as it is for a person.
        // A launch argument of the form -key value is how they say it, so
        // read those as well as a stored default.
        if defaults.bool(forKey: "resetTutorial") || Self.argument("resetTutorial") {
            defaults.removeObject(forKey: "tutorialSeen")
        }
        let bundled = Bundle.main.object(forInfoDictionaryKey: "LapisDefaultHost") as? String
        let host = defaults.string(forKey: DeckStore.hostKey) ?? bundled ?? ""
        self.host = host
        pose = switch defaults.string(forKey: "swipePose") {
        case "accept": .accept
        case "skip": .skip
        default: nil
        }
        deck = Deck(sender: GatewaySender(host: host))
    }

    // Whether the process was launched with `-name YES`, `true` or `1`.
    static func argument(_ name: String) -> Bool {
        let arguments = ProcessInfo.processInfo.arguments
        guard let index = arguments.firstIndex(of: "-\(name)") else { return false }
        let value = arguments[(index + 1)...].first
        return ["YES", "true", "1"].contains(value)
    }

    func follow() {
        guard following == nil else { return }
        following = Task { [weak self] in await self?.run() }
    }

    func pause() {
        following?.cancel()
        following = nil
    }

    private func restart() {
        deck.retarget(GatewaySender(host: host))
        pause()
        follow()
    }

    private func run() async {
        var version: String?
        while !Task.isCancelled {
            guard let gateway else {
                deck.connection = "Set the Mac's address in Settings."
                try? await Task.sleep(for: .seconds(2))
                continue
            }
            do {
                let published = try await gateway.deck(after: version)
                if Task.isCancelled { return }
                deck.connection = ""
                if published.version != version || version == nil {
                    deck.setPublished(published)
                }
                version = published.version
            } catch {
                if Task.isCancelled { return }
                deck.connection = "Cannot reach the Mac: \(error.localizedDescription)"
                version = nil
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }
}

// Sends through whichever Mac address is set when the answer is given.
struct GatewaySender: Sender {
    let host: String

    func submit(agentID: String, text: String) async -> String? {
        do {
            return try await DeckGateway(host: host).submit(agentID: agentID, text: text)
        } catch {
            return error.localizedDescription
        }
    }

    func submit(agentID: String, text: String, label: AnswerLabel) async -> String? {
        do {
            return try await DeckGateway(host: host).submit(agentID: agentID, text: text, label: label)
        } catch {
            return error.localizedDescription
        }
    }

    func skipped(agentID: String, label: AnswerLabel) async {
        await (try? DeckGateway(host: host))?.skipped(agentID: agentID, label: label)
    }
}
