import Foundation

// The lapis gateway on the Mac (apps/remote/lapis_remote.py), reached over
// Tailscale or ZeroTier like the lapis phone app: it admits only the Mac
// owner's devices, so there is nothing to sign in to. Ultra Tab uses two of
// its routes: GET /api/deck (read-only) and POST /api/agents/<id>/submit.
struct DeckGateway: Sender {
    static let defaultPort = 7349

    let base: URL
    let session: URLSession

    enum Failure: LocalizedError, Equatable {
        case invalidHost
        case unsupportedScheme(String)
        case refused(Int, String)
        case unreadable

        var errorDescription: String? {
            switch self {
            case .invalidHost: "Set the Mac's address in Settings"
            case .unsupportedScheme(let scheme): "\(scheme): addresses are not supported"
            case .refused(_, let message) where !message.isEmpty: message
            case .refused(let status, _): "The Mac answered \(status)"
            case .unreadable: "The Mac's answer could not be read"
            }
        }
    }

    // "mac.tailnet.ts.net", "mac.tailnet.ts.net:7349" or an http(s) URL.
    init(host: String, session: URLSession = DeckGateway.requests) throws {
        var text = host.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty { throw Failure.invalidHost }
        if !text.contains("://") { text = "http://" + text }
        guard var parts = URLComponents(string: text), let name = parts.host, !name.isEmpty else {
            throw Failure.invalidHost
        }
        guard let scheme = parts.scheme?.lowercased(), scheme == "http" || scheme == "https" else {
            throw Failure.unsupportedScheme(parts.scheme ?? "")
        }
        parts.scheme = scheme
        parts.port = parts.port ?? DeckGateway.defaultPort
        parts.path = ""
        parts.query = nil
        guard let url = parts.url else { throw Failure.invalidHost }
        base = url
        self.session = session
    }

    static let requests: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        // The deck waits up to eight seconds for a change.
        configuration.timeoutIntervalForRequest = 20
        configuration.waitsForConnectivity = false
        return URLSession(configuration: configuration)
    }()

    func request(_ path: String, query: [URLQueryItem] = []) -> URLRequest {
        var parts = URLComponents(url: base.appendingPathComponent(path), resolvingAgainstBaseURL: false)!
        if !query.isEmpty { parts.queryItems = query }
        var request = URLRequest(url: parts.url!)
        request.setValue("ultratab-ios", forHTTPHeaderField: "X-Lapis-Client")
        request.cachePolicy = .reloadIgnoringLocalCacheData
        return request
    }

    private static func check(_ response: URLResponse, _ data: Data) throws {
        guard let http = response as? HTTPURLResponse else { throw Failure.unreadable }
        guard http.statusCode == 200 else {
            let body = try? JSONDecoder().decode([String: String].self, from: data)
            throw Failure.refused(http.statusCode, body?["error"] ?? "")
        }
    }

    // With the version the phone has, the gateway answers once the Mac's
    // registry, state or cards change, or after at most eight seconds.
    func deck(after version: String? = nil) async throws -> Published {
        let query = version.map { [URLQueryItem(name: "after", value: $0)] } ?? []
        let (data, response) = try await session.data(for: request("api/deck", query: query))
        try DeckGateway.check(response, data)
        return try PublishedParser.parse(data)
    }

    func submit(agentID: String, text: String) async -> String? {
        var request = request("api/agents/\(agentID)/submit")
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.httpBody = try? JSONSerialization.data(withJSONObject: ["text": text])
        do {
            let (data, response) = try await session.data(for: request)
            try DeckGateway.check(response, data)
            return nil
        } catch {
            return error.localizedDescription
        }
    }
}
