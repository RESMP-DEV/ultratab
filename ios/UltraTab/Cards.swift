import Foundation

// What the Mac publishes for Ultra Tab, as the gateway serves it
// (GET /api/deck, apps/remote/lapis_remote.py read_deck), and the composed
// cards in it, validated as apps/ultratab/src/cards.cpp validates them: a
// composed card replaces the plain one only while its key names the agent's
// current turn, and anything that fails validation is dropped.

struct DeckCategory: Equatable {
    let id: String
    let name: String
}

struct DeckAgent: Equatable {
    let id: String
    let title: String
    let category: String
    let directory: String
    let harness: String
}

struct Offer: Equatable {
    let key: String
    let text: String
    let said: String
    let seen: Bool
}

// The Mac window's view of one agent (agent_state.json version 1).
struct AgentState: Equatable {
    var status = "unknown"
    var unseen = false
    var requests = 0
    var request = ""
    var neededAtMs: Int64 = 0
    var turnAtMs: Int64 = 0
    var offer: Offer?
}

enum Block: Equatable {
    case text(String)
    case list([String])
    case table(columns: [String], rows: [[String]], numeric: [Bool], moreRows: Int)
    // Sanitized SVG and its own aspect ratio (width / height).
    case diagram(svg: String, aspect: Double)
    case link(label: String, url: URL)
}

struct ComposedCard: Equatable {
    let key: String
    let since: String
    let tldr: String
    let blocks: [Block]
    let prompt: String
}

struct Published: Equatable {
    var version = ""
    var categories: [DeckCategory] = []
    var agents: [DeckAgent] = []
    var states: [String: AgentState] = [:]
    var composed: [String: ComposedCard] = [:]
    var hasRegistry = false
    var hasState = false
    var writerRunning = false
    var problems: [String] = []
}

enum CardLimits {
    static let blocks = 3
    static let listItems = 8
    static let tableColumns = 6
    static let tableRows = 8
    static let svgBytes = 256 * 1024
    static let shortText = 300
    static let longText = 2000
    static let cellText = 200
    static let labelText = 120
    static let urlText = 2048
    static let stateVersion = 1
    static let cardsVersion = 1
}

enum PublishedParser {
    // The gateway's deck reply. Records without an id are skipped; a state
    // or cards file of another version is reported and left out.
    static func parse(_ data: Data) throws -> Published {
        guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw DeckError.unreadable
        }
        var published = Published()
        published.version = root["version"] as? String ?? ""
        published.problems = (root["problems"] as? [Any])?.compactMap { $0 as? String } ?? []
        published.writerRunning = root["writerRunning"] as? Bool ?? false
        if let categories = root["categories"] as? [[String: Any]] {
            published.hasRegistry = true
            published.categories = categories.map {
                DeckCategory(id: string($0["id"]), name: string($0["name"]))
            }
        }
        for object in root["agents"] as? [[String: Any]] ?? [] {
            let id = string(object["id"])
            guard !id.isEmpty else { continue }
            published.agents.append(DeckAgent(
                id: id, title: string(object["title"]), category: string(object["category"]),
                directory: string(object["directory"]),
                harness: (object["harness"] as? String) ?? "codex"))
        }
        if let state = root["state"] as? [String: Any] {
            if integer(state["version"]) == Int64(CardLimits.stateVersion) {
                published.hasState = true
                for object in state["agents"] as? [[String: Any]] ?? [] {
                    let id = string(object["id"])
                    if !id.isEmpty { published.states[id] = parseState(object) }
                }
            } else {
                published.problems.append("agent_state.json has another version")
            }
        }
        if let cards = root["cards"] as? [String: Any] {
            if integer(cards["v"]) == Int64(CardLimits.cardsVersion) {
                for (id, value) in cards["cards"] as? [String: Any] ?? [:] {
                    if let object = value as? [String: Any], let card = parseCard(object) {
                        published.composed[id] = card
                    }
                }
            } else {
                published.problems.append("ultratab_cards.json has another version")
            }
        }
        return published
    }

    static func parseState(_ object: [String: Any]) -> AgentState {
        var state = AgentState()
        state.status = (object["status"] as? String) ?? "unknown"
        state.unseen = object["unseen"] as? Bool ?? false
        state.requests = max(0, Int(integer(object["requests"])))
        state.request = string(object["request"])
        state.neededAtMs = integer(object["neededAtMs"])
        state.turnAtMs = integer(object["turnAtMs"])
        if let offer = object["offer"] as? [String: Any] {
            let text = string(offer["text"])
            if !text.isEmpty {
                state.offer = Offer(key: string(offer["key"]), text: text, said: string(offer["said"]),
                                    seen: offer["seen"] as? Bool ?? false)
            }
        }
        return state
    }

    static func parseCard(_ object: [String: Any]) -> ComposedCard? {
        let key = string(object["key"])
        guard !key.isEmpty else { return nil }
        var blocks: [Block] = []
        for value in object["blocks"] as? [Any] ?? [] {
            if blocks.count == CardLimits.blocks { break }
            if let block = (value as? [String: Any]).flatMap(parseBlock) { blocks.append(block) }
        }
        return ComposedCard(key: key, since: bounded(object["since"], CardLimits.shortText),
                            tldr: bounded(object["tldr"], CardLimits.shortText), blocks: blocks,
                            prompt: bounded(object["prompt"], CardLimits.longText))
    }

    static func parseBlock(_ object: [String: Any]) -> Block? {
        switch object["type"] as? String {
        case "text":
            let text = bounded(object["text"], CardLimits.longText)
            return text.isEmpty ? nil : .text(text)
        case "list":
            var items: [String] = []
            for item in object["items"] as? [Any] ?? [] {
                let text = bounded(item, CardLimits.shortText)
                if !text.isEmpty && items.count < CardLimits.listItems { items.append(text) }
            }
            return items.isEmpty ? nil : .list(items)
        case "table":
            return parseTable(object)
        case "diagram":
            guard let svg = SVGSanitizer.sanitize(string(object["svg"])) else { return nil }
            return .diagram(svg: svg.svg, aspect: svg.aspect)
        case "link":
            let text = string(object["url"]).trimmingCharacters(in: .whitespacesAndNewlines)
            guard text.count <= CardLimits.urlText, let url = URL(string: text), openable(url) else {
                return nil
            }
            var label = bounded(object["label"], CardLimits.labelText)
            if label.isEmpty { label = url.isFileURL ? url.lastPathComponent : (url.host ?? "") }
            return .link(label: label, url: url)
        default:
            return nil
        }
    }

    static func parseTable(_ object: [String: Any]) -> Block? {
        var columns: [String] = []
        for column in object["columns"] as? [Any] ?? [] where columns.count < CardLimits.tableColumns {
            columns.append(bounded(column, CardLimits.cellText))
        }
        guard !columns.isEmpty else { return nil }
        var rows: [[String]] = []
        var more = 0
        for value in object["rows"] as? [Any] ?? [] {
            if rows.count == CardLimits.tableRows {
                more += 1
                continue
            }
            var row: [String] = []
            for cell in value as? [Any] ?? [] where row.count < columns.count {
                row.append(bounded(cell, CardLimits.cellText))
            }
            while row.count < columns.count { row.append("") }
            rows.append(row)
        }
        guard !rows.isEmpty else { return nil }
        // A column is numeric when every filled cell in it is a number.
        let numeric = columns.indices.map { column -> Bool in
            let filled = rows.map { $0[column] }.filter { !$0.isEmpty }
            return !filled.isEmpty && filled.allSatisfy(numericCell)
        }
        return .table(columns: columns, rows: rows, numeric: numeric, moreRows: more)
    }

    // Only https: links with a host and absolute file: paths (files on the
    // Mac, named but not opened here) are kept.
    static func openable(_ url: URL) -> Bool {
        guard url.user == nil, url.password == nil else { return false }
        switch url.scheme?.lowercased() {
        case "https": return !(url.host ?? "").isEmpty
        case "file": return (url.host ?? "").isEmpty && url.path.hasPrefix("/")
        default: return false
        }
    }

    // Whether a cell reads as a number ("1,024", "-3.5%", "12 ms", "2.1x").
    static func numericCell(_ cell: String) -> Bool {
        let trimmed = cell.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.rangeOfCharacter(from: .decimalDigits) != nil else { return false }
        return trimmed.range(of: numberPattern, options: .regularExpression) != nil
    }

    private static let numberPattern =
        "^[~≈<>]?\\s*[-+−]?[$€£]?(\\d{1,3}(,\\d{3})+|\\d+)?(\\.\\d+)?\\s*" +
        "(%|x|×|ms|us|µs|ns|s|min|h|k|K|M|B|G|T|KB|MB|GB|TB|KiB|MiB|GiB|TiB|" +
        "TFLOPS|GFLOPS|tok/s|it/s)?$"

    // Trimmed, and cut with an ellipsis past `limit` characters.
    static func bounded(_ value: Any?, _ limit: Int) -> String {
        let text = string(value).trimmingCharacters(in: .whitespacesAndNewlines)
        guard text.count > limit else { return text }
        return String(text.prefix(limit - 1)).trimmingCharacters(in: .whitespacesAndNewlines) + "\u{2026}"
    }

    private static func string(_ value: Any?) -> String { value as? String ?? "" }

    private static func integer(_ value: Any?) -> Int64 {
        // JSON booleans are NSNumbers too; only numbers count.
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return 0 }
        return number.int64Value
    }
}

enum DeckError: LocalizedError {
    case unreadable
    var errorDescription: String? { "The Mac's deck could not be read" }
}

// The SVG when it is safe to draw, else nil: no script, foreignObject, event
// handlers, DTDs, processing instructions or references outside the
// document. A root without a fill is given a light one so unstyled shapes
// and text read on a dark card. Drawn afterwards only as an image (no script
// runs in an SVG image, and it loads nothing), behind a policy that allows
// no loads.
enum SVGSanitizer {
    struct Clean: Equatable {
        let svg: String
        let aspect: Double
    }

    static let defaultFill = "#c6d2e4"

    static func sanitize(_ svg: String) -> Clean? {
        guard !svg.isEmpty, svg.utf16.count <= CardLimits.svgBytes else { return nil }
        let lowered = svg.lowercased()
        // DTDs and entities, and any processing instruction but the leading
        // XML declaration.
        if lowered.contains("<!doctype") || lowered.contains("<!entity") { return nil }
        var rest = lowered.trimmingCharacters(in: .whitespacesAndNewlines)
        if rest.hasPrefix("<?xml ") || rest.hasPrefix("<?xml?") {
            guard let end = rest.range(of: "?>") else { return nil }
            rest = String(rest[end.upperBound...])
        }
        if rest.contains("<?") { return nil }
        guard let data = svg.data(using: .utf8) else { return nil }
        let parser = XMLParser(data: data)
        parser.shouldResolveExternalEntities = false
        parser.shouldProcessNamespaces = false
        let scan = Scan()
        parser.delegate = scan
        guard parser.parse(), scan.safe, scan.sawRoot, let aspect = scan.aspect, aspect > 0,
              aspect.isFinite else { return nil }
        var clean = svg
        if !scan.rootFill, let at = clean.range(of: "<svg", options: .caseInsensitive) {
            clean.insert(contentsOf: " fill=\"\(defaultFill)\" color=\"\(defaultFill)\"", at: at.upperBound)
        }
        return Clean(svg: clean, aspect: aspect)
    }

    private static let forbidden: Set<String> = [
        "script", "foreignobject", "iframe", "object", "embed", "audio", "video", "handler",
        "listener",
    ]

    static func safeStyle(_ css: String) -> Bool {
        let lowered = css.lowercased()
        if lowered.contains("@import") || lowered.contains("javascript:") || lowered.contains("expression(") {
            return false
        }
        return localReferences(css)
    }

    // Every url(...) stays in the document: url(#id).
    static func localReferences(_ value: String) -> Bool {
        guard let pattern = try? NSRegularExpression(
            pattern: "url\\s*\\(\\s*['\"]?\\s*([^)'\"\\s]*)", options: [.caseInsensitive]) else {
            return false
        }
        let range = NSRange(value.startIndex..., in: value)
        for match in pattern.matches(in: value, range: range) {
            guard let target = Range(match.range(at: 1), in: value) else { return false }
            if !value[target].hasPrefix("#") { return false }
        }
        return true
    }

    private final class Scan: NSObject, XMLParserDelegate {
        var safe = true
        var sawRoot = false
        var rootFill = false
        var aspect: Double?
        private var inStyle = false

        func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?,
                    qualifiedName: String?, attributes: [String: String] = [:]) {
            let name = local(elementName).lowercased()
            if (!sawRoot && name != "svg") || SVGSanitizer.forbidden.contains(name) {
                fail(parser)
                return
            }
            for (key, value) in attributes {
                let attribute = local(key).lowercased()
                if attribute.hasPrefix("on") ||
                    (attribute == "href" && !value.trimmingCharacters(in: .whitespaces).hasPrefix("#")) ||
                    !SVGSanitizer.safeStyle(value) {
                    fail(parser)
                    return
                }
            }
            if !sawRoot {
                sawRoot = true
                rootFill = attributes["fill"] != nil
                aspect = Self.aspect(attributes)
            }
            inStyle = name == "style"
        }

        func parser(_ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?,
                    qualifiedName: String?) {
            inStyle = false
        }

        func parser(_ parser: XMLParser, foundCharacters string: String) {
            if inStyle && !SVGSanitizer.safeStyle(string) { fail(parser) }
        }

        func parser(_ parser: XMLParser, foundCDATA CDATABlock: Data) {
            if inStyle && !SVGSanitizer.safeStyle(String(decoding: CDATABlock, as: UTF8.self)) { fail(parser) }
        }

        func parser(_ parser: XMLParser, foundProcessingInstructionWithTarget target: String, data: String?) {
            fail(parser)
        }

        func parser(_ parser: XMLParser, foundInternalEntityDeclarationWithName name: String, value: String?) {
            fail(parser)
        }

        func parser(_ parser: XMLParser, foundExternalEntityDeclarationWithName name: String,
                    publicID: String?, systemID: String?) {
            fail(parser)
        }

        private func fail(_ parser: XMLParser) {
            safe = false
            parser.abortParsing()
        }

        private func local(_ name: String) -> String {
            name.split(separator: ":").last.map(String.init) ?? name
        }

        // From the viewBox, else the width and height.
        private static func aspect(_ attributes: [String: String]) -> Double? {
            if let box = attributes["viewBox"] {
                let numbers = box.split(whereSeparator: { $0 == " " || $0 == "," }).compactMap { Double($0) }
                if numbers.count == 4, numbers[2] > 0, numbers[3] > 0 { return numbers[2] / numbers[3] }
            }
            func length(_ key: String) -> Double? {
                guard let raw = attributes[key] else { return nil }
                return Double(raw.trimmingCharacters(in: CharacterSet(charactersIn: "px ")))
            }
            if let width = length("width"), let height = length("height"), width > 0, height > 0 {
                return width / height
            }
            return nil
        }
    }
}
