import Foundation
import XCTest

// Ultra Tab's logic: deck order, card parsing and sanitizing, what a swipe
// means, and the send path against a fake service. scripts/check_ultratab_ios.py
// compiles these with the app's logic sources (Cards, Deck, DeckGateway).

private let agentA = "aaaaaaaa-0000-4000-8000-000000000001"
private let agentB = "bbbbbbbb-0000-4000-8000-000000000002"
private let agentC = "cccccccc-0000-4000-8000-000000000003"
private let agentD = "dddddddd-0000-4000-8000-000000000004"

private func deckJSON(states: [[String: Any]], cards: [String: Any]? = nil,
                      version: String = "v1") -> Data {
    var root: [String: Any] = [
        "version": version,
        "categories": [["id": "build", "name": "Build"], ["id": "later", "name": "Later"]],
        "agents": [
            ["id": agentA, "title": "kernels", "category": "build", "directory": "/w/kernels", "harness": "claude"],
            ["id": agentB, "title": "", "category": "build", "directory": "/w/docs", "harness": "codex"],
            ["id": agentC, "title": "parked", "category": "later", "directory": "/w/parked", "harness": "claude"],
            ["id": agentD, "title": "busy", "category": "later", "directory": "/w/busy", "harness": "codex"],
        ],
        "state": ["version": 1, "pid": 1, "agents": states],
        "writerRunning": true,
        "problems": [],
    ]
    if let cards { root["cards"] = ["v": 1, "cards": cards] }
    return try! JSONSerialization.data(withJSONObject: root)
}

private func published(states: [[String: Any]], cards: [String: Any]? = nil) -> Published {
    try! PublishedParser.parse(deckJSON(states: states, cards: cards))
}

private func block(_ object: [String: Any]) -> Block? { PublishedParser.parseBlock(object) }

final class DeckOrderTests: XCTestCase {
    func testTiersThenLongestWaitingThenRegistryOrder() {
        let deck = published(states: [
            // A turn finished unseen: tier 1.
            ["id": agentA, "status": "finished", "unseen": true, "neededAtMs": 300, "turnAtMs": 300],
            // A guess not yet seen: tier 0, first despite waiting less.
            ["id": agentB, "status": "idle", "neededAtMs": 900,
             "offer": ["key": "g1", "text": "go on", "said": "Done. More text"]],
            // A guess already seen: tier 2.
            ["id": agentC, "status": "finished", "neededAtMs": 100,
             "offer": ["key": "g2", "text": "retry", "seen": true]],
            // At work: never a card.
            ["id": agentD, "status": "working", "unseen": true, "neededAtMs": 50],
        ])
        let cards = DeckOrder.cards(deck)
        XCTAssertEqual(cards.map(\.agentID), [agentB, agentA, agentC])
        XCTAssertEqual(cards.map(\.tier), [0, 1, 2])
        XCTAssertEqual(cards[0].name, "docs") // no title: the folder
        XCTAssertEqual(cards[0].line, "Done.")
        XCTAssertEqual(cards[0].proposal, "go on")
        XCTAssertEqual(cards[1].line, "Finished a turn.")
        XCTAssertEqual(cards[1].categoryName, "Build")
    }

    func testWithinATierTheOneWaitingLongestComesFirst() {
        let deck = published(states: [
            ["id": agentA, "status": "finished", "unseen": true, "neededAtMs": 500],
            ["id": agentB, "status": "finished", "unseen": true, "neededAtMs": 200],
            ["id": agentC, "status": "finished", "unseen": true, "neededAtMs": 200],
        ])
        XCTAssertEqual(DeckOrder.cards(deck).map(\.agentID), [agentB, agentC, agentA])
    }

    func testRequestsAreAlwaysCardsAndUnknownStatesNever() {
        let deck = published(states: [
            ["id": agentA, "status": "working", "requests": 1, "request": "Allow rm -rf build? It asks.",
             "neededAtMs": 10, "offer": ["key": "g", "text": "yes"]],
            ["id": agentB, "status": "unknown", "unseen": true],
            ["id": agentC, "status": "finished"], // nothing new
        ])
        let cards = DeckOrder.cards(deck)
        XCTAssertEqual(cards.map(\.agentID), [agentA])
        XCTAssertTrue(cards[0].request)
        XCTAssertEqual(cards[0].line, "Allow rm -rf build?")
        XCTAssertEqual(cards[0].proposal, "") // a request takes no reply here
        XCTAssertFalse(cards[0].canAccept)
        XCTAssertFalse(cards[0].canType)
    }

    func testAComposedCardOnlyWhileItsKeyIsCurrent() {
        let state: [String: Any] = ["id": agentA, "status": "finished", "unseen": true,
                                    "neededAtMs": 40, "turnAtMs": 50]
        let current = published(states: [state], cards: [
            agentA: ["key": "turn:50", "tldr": "Kernel is 2x faster", "since": "2 turns since",
                     "prompt": "Ship it", "blocks": [["type": "text", "text": "Details"]]],
        ])
        let card = DeckOrder.cards(current)[0]
        XCTAssertEqual(card.headline, "Kernel is 2x faster")
        XCTAssertEqual(card.proposal, "Ship it")
        XCTAssertEqual(card.composed?.blocks, [.text("Details")])

        // The card's own key, without the agent id, also names it.
        let keyed = published(states: [state], cards: [agentA: ["key": "50|40||0", "tldr": "Keyed"]])
        XCTAssertEqual(DeckOrder.cards(keyed)[0].headline, "Keyed")

        let stale = published(states: [state], cards: [agentA: ["key": "turn:49", "tldr": "Old"]])
        XCTAssertNil(DeckOrder.cards(stale)[0].composed)
        XCTAssertEqual(DeckOrder.cards(stale)[0].headline, "Finished a turn.")
    }

    func testOneSentence() {
        XCTAssertEqual(DeckOrder.oneSentence("## **Done.** Next I will"), "Done.")
        XCTAssertEqual(DeckOrder.oneSentence("> quoted   text\nmore"), "quoted text more")
        let long = String(repeating: "word ", count: 60)
        let cut = DeckOrder.oneSentence(long)
        XCTAssertLessThanOrEqual(cut.count, 160)
        XCTAssertTrue(cut.hasSuffix("\u{2026}"))
    }
}

final class CardParsingTests: XCTestCase {
    func testLimits() {
        let text = String(repeating: "a", count: 2500)
        guard case .text(let bounded)? = block(["type": "text", "text": text]) else { return XCTFail() }
        XCTAssertEqual(bounded.count, 2000)
        XCTAssertTrue(bounded.hasSuffix("\u{2026}"))
        XCTAssertNil(block(["type": "text", "text": "   "]))
        XCTAssertNil(block(["type": "video", "text": "x"]))

        guard case .list(let items)? = block(["type": "list", "items": (1...12).map { "item \($0)" } + [""]]) else {
            return XCTFail()
        }
        XCTAssertEqual(items.count, 8)

        let rows = (1...11).map { ["r\($0)", "\($0 * 10) ms", "x"] }
        guard case .table(let columns, let kept, let numeric, let more)? =
            block(["type": "table", "columns": ["a", "b", "c", "d", "e", "f", "g"], "rows": rows]) else {
            return XCTFail()
        }
        XCTAssertEqual(columns.count, 6)
        XCTAssertEqual(kept.count, 8)
        XCTAssertEqual(kept[0].count, 6) // padded to the columns
        XCTAssertEqual(more, 3)
        XCTAssertEqual(numeric, [false, true, false, false, false, false])
        XCTAssertNil(block(["type": "table", "columns": [], "rows": [["1"]]]))
        XCTAssertNil(block(["type": "table", "columns": ["a"], "rows": []]))
    }

    func testAtMostThreeBlocksAndBadOnesDropped() {
        let card = PublishedParser.parseCard([
            "key": "k",
            "blocks": [["type": "text", "text": "1"], ["type": "nope"], ["type": "list", "items": ["2"]],
                       ["type": "text", "text": "3"], ["type": "text", "text": "4"]],
        ])
        XCTAssertEqual(card?.blocks, [.text("1"), .list(["2"]), .text("3")])
        XCTAssertNil(PublishedParser.parseCard(["tldr": "no key"]))
    }

    func testNumericCells() {
        for cell in ["1,024", "-3.5%", "12 ms", "2.1x", "~40 GB", "$12", "1.2 TFLOPS"] {
            XCTAssertTrue(PublishedParser.numericCell(cell), cell)
        }
        for cell in ["n/a", "v2 release", "", "abc 12"] {
            XCTAssertFalse(PublishedParser.numericCell(cell), cell)
        }
    }

    func testOnlyHttpsAndMacFileLinks() {
        guard case .link(let label, let url)? = block(["type": "link", "url": "https://example.com/r"]) else {
            return XCTFail()
        }
        XCTAssertEqual(label, "example.com")
        XCTAssertEqual(url.host, "example.com")
        guard case .link(let file, _)? = block(["type": "link", "url": "file:///w/report.html"]) else {
            return XCTFail()
        }
        XCTAssertEqual(file, "report.html")
        for bad in ["http://example.com", "javascript:alert(1)", "file://host/x", "https://u:p@example.com",
                    "data:text/html,x", "https://", "relative/path"] {
            XCTAssertNil(block(["type": "link", "url": bad]), bad)
        }
    }

    func testOtherVersionsAreReported() throws {
        var root = try JSONSerialization.jsonObject(with: deckJSON(states: [])) as! [String: Any]
        root["state"] = ["version": 2, "agents": []]
        root["cards"] = ["v": 9, "cards": [:]]
        let parsed = try PublishedParser.parse(JSONSerialization.data(withJSONObject: root))
        XCTAssertFalse(parsed.hasState)
        XCTAssertEqual(parsed.problems.count, 2)
    }
}

final class SVGSanitizerTests: XCTestCase {
    private let good = #"<svg viewBox="0 0 200 100" xmlns="http://www.w3.org/2000/svg"><rect width="10" height="10" fill="url(#g)"/><text x="5" y="20">ok</text></svg>"#

    func testASafeDiagramGetsAFillAndItsAspect() throws {
        let clean = try XCTUnwrap(SVGSanitizer.sanitize(good))
        XCTAssertEqual(clean.aspect, 2)
        XCTAssertTrue(clean.svg.hasPrefix(##"<svg fill="#c6d2e4" color="#c6d2e4" viewBox"##))
        let filled = #"<?xml version="1.0"?><svg fill="red" width="30" height="10"/>"#
        XCTAssertEqual(SVGSanitizer.sanitize(filled), .init(svg: filled, aspect: 3))
        guard case .diagram(_, let aspect)? = block(["type": "diagram", "svg": good]) else { return XCTFail() }
        XCTAssertEqual(aspect, 2)
    }

    func testAnythingThatRunsOrReachesOutIsRefused() {
        let unsafe = [
            #"<svg viewBox="0 0 1 1"><script>alert(1)</script></svg>"#,
            #"<svg viewBox="0 0 1 1" onload="alert(1)"/>"#,
            #"<svg viewBox="0 0 1 1"><a href="https://x.test"><text>x</text></a></svg>"#,
            #"<svg viewBox="0 0 1 1" xmlns:xlink="http://www.w3.org/1999/xlink"><image xlink:href="https://x.test/a.png"/></svg>"#,
            #"<svg viewBox="0 0 1 1"><rect style="fill:url(https://x.test/p)"/></svg>"#,
            #"<svg viewBox="0 0 1 1"><style>@import url(#a);</style></svg>"#,
            #"<svg viewBox="0 0 1 1"><style><![CDATA[rect{fill:url(http://x.test)}]]></style></svg>"#,
            #"<svg viewBox="0 0 1 1"><foreignObject><div/></foreignObject></svg>"#,
            #"<!DOCTYPE svg [<!ENTITY x "y">]><svg viewBox="0 0 1 1">&x;</svg>"#,
            #"<svg viewBox="0 0 1 1"><?php echo 1 ?></svg>"#,
            #"<html><svg viewBox="0 0 1 1"/></html>"#,
            #"<svg viewBox="0 0 1 1"><rect"#,
            #"<svg/>"#, // no size to draw it at
            "",
            "<svg viewBox=\"0 0 1 1\">" + String(repeating: " ", count: 300 * 1024) + "</svg>",
        ]
        for svg in unsafe {
            XCTAssertNil(SVGSanitizer.sanitize(svg), String(svg.prefix(80)))
        }
    }
}

final class SwipeTests: XCTestCase {
    func testDistanceFlickAndDirection() {
        XCTAssertEqual(Swipe.from(dx: 130, dy: 10, predictedDx: 130), .accept)
        XCTAssertEqual(Swipe.from(dx: -130, dy: 10, predictedDx: -130), .skip)
        XCTAssertEqual(Swipe.from(dx: 40, dy: 0, predictedDx: 300), .accept)  // a flick
        XCTAssertEqual(Swipe.from(dx: -40, dy: 0, predictedDx: -300), .skip)
        XCTAssertEqual(Swipe.from(dx: 60, dy: 0, predictedDx: 120), .none)    // springs back
        XCTAssertEqual(Swipe.from(dx: 120, dy: 200, predictedDx: 400), .none) // a scroll
    }
}

// A session service that admits or refuses each paste, as the gateway reports.
private actor FakeService: Sender {
    var sent: [(agent: String, text: String)] = []
    var refuse: String?

    func refuseNext(_ why: String?) { refuse = why }

    nonisolated func submit(agentID: String, text: String) async -> String? {
        await record(agentID, text)
    }

    private func record(_ agent: String, _ text: String) -> String? {
        sent.append((agent, text))
        defer { refuse = nil }
        return refuse
    }
}

@MainActor
final class SendPathTests: XCTestCase {
    private func makeDeck(_ service: FakeService) -> Deck {
        let deck = Deck(sender: service)
        deck.setPublished(published(states: [
            ["id": agentA, "status": "finished", "neededAtMs": 1, "offer": ["key": "g", "text": "run the sweep"]],
            ["id": agentB, "status": "finished", "unseen": true, "neededAtMs": 2],
            ["id": agentC, "status": "waiting", "requests": 1, "request": "Allow?", "neededAtMs": 3],
        ]))
        return deck
    }

    func testAcceptSendsTheProposalAndSkipSendsNothing() async {
        let service = FakeService()
        let deck = makeDeck(service)
        XCTAssertEqual(deck.front?.agentID, agentA)
        XCTAssertEqual(deck.apply(.accept), .sending)
        XCTAssertEqual(deck.front?.agentID, agentB) // hidden at once
        await deck.waitForSends()
        let sent = await service.sent
        XCTAssertEqual(sent.map(\.agent), [agentA])
        XCTAssertEqual(sent.map(\.text), ["run the sweep"])
        XCTAssertEqual(deck.message, "Sent to kernels")
        XCTAssertEqual(deck.history.first?.outcome, "sent")

        XCTAssertEqual(deck.apply(.skip), .skipped)
        XCTAssertEqual(deck.front?.agentID, agentC)
        let after = await service.sent
        XCTAssertEqual(after.count, 1)
        XCTAssertNil(deck.apply(.none))
    }

    func testARefusedSendBringsTheCardBackWithTheReason() async {
        let service = FakeService()
        await service.refuseNext("rejected: a request is pending")
        let deck = makeDeck(service)
        deck.accept()
        XCTAssertEqual(deck.front?.agentID, agentB)
        await deck.waitForSends()
        XCTAssertEqual(deck.front?.agentID, agentA)
        XCTAssertEqual(deck.refusals[deck.front!.key], "rejected: a request is pending")
        XCTAssertEqual(deck.message, "Not sent to kernels: rejected: a request is pending")
        XCTAssertEqual(deck.history.first?.outcome, "not sent")
        // Answering again clears the reason.
        deck.accept()
        await deck.waitForSends()
        XCTAssertTrue(deck.refusals.isEmpty)
    }

    func testAnnotationsAreTrimmedAndCardsWithoutAProposalOnlyTakeThem() async {
        let service = FakeService()
        let deck = makeDeck(service)
        deck.skip()
        XCTAssertEqual(deck.front?.agentID, agentB)
        XCTAssertEqual(deck.accept(), .refused("No proposed reply; dictate or type one"))
        XCTAssertEqual(deck.send("   "), .refused("Nothing to send"))
        XCTAssertEqual(deck.send("  add a benchmark \n"), .sending)
        await deck.waitForSends()
        let sent = await service.sent
        XCTAssertEqual(sent.map(\.text), ["add a benchmark"])
        XCTAssertEqual(sent.map(\.agent), [agentB])
    }

    func testARequestTakesNoAnswerButCanBeSkipped() async {
        let service = FakeService()
        let deck = makeDeck(service)
        deck.skip()
        deck.skip()
        XCTAssertEqual(deck.front?.agentID, agentC)
        XCTAssertEqual(deck.accept(), .refused("Answer this request in lapis; skip it here"))
        XCTAssertEqual(deck.send("yes"), .refused("Answer this request in lapis; skip it here"))
        XCTAssertEqual(deck.skip(), .skipped)
        XCTAssertNil(deck.front)
        let sent = await service.sent
        XCTAssertTrue(sent.isEmpty)
    }

    func testAnAnsweredCardStaysAwayUntilTheAgentHasSomethingNew() {
        let deck = Deck(sender: FakeService())
        let first: [[String: Any]] = [["id": agentA, "status": "finished", "unseen": true, "neededAtMs": 1, "turnAtMs": 1]]
        deck.setPublished(published(states: first))
        deck.skip()
        deck.setPublished(published(states: first))
        XCTAssertNil(deck.front)
        deck.setPublished(published(states: [["id": agentA, "status": "finished", "unseen": true,
                                               "neededAtMs": 1, "turnAtMs": 2]]))
        XCTAssertEqual(deck.front?.agentID, agentA)
    }

    func testCategoriesFilterTheDeck() {
        let deck = makeDeck(FakeService())
        deck.nextCategory(2) // all, Build, Later
        XCTAssertEqual(deck.category, "later")
        XCTAssertEqual(deck.visible.map(\.agentID), [agentC])
        XCTAssertEqual(deck.rail.map(\.count), [3, 2, 1])
    }
}

// The gateway's two routes, against a fake Mac answering through URLProtocol.
final class GatewayTests: XCTestCase {
    final class FakeMac: URLProtocol {
        nonisolated(unsafe) static var requests: [URLRequest] = []
        nonisolated(unsafe) static var bodies: [Data] = []
        nonisolated(unsafe) static var answer: (Int, Data) = (200, Data())

        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
        override func startLoading() {
            FakeMac.requests.append(request)
            if let stream = request.httpBodyStream {
                stream.open()
                var data = Data()
                var buffer = [UInt8](repeating: 0, count: 4096)
                while stream.hasBytesAvailable {
                    let count = stream.read(&buffer, maxLength: buffer.count)
                    if count <= 0 { break }
                    data.append(buffer, count: count)
                }
                stream.close()
                FakeMac.bodies.append(data)
            } else {
                FakeMac.bodies.append(request.httpBody ?? Data())
            }
            let response = HTTPURLResponse(url: request.url!, statusCode: FakeMac.answer.0,
                                           httpVersion: "HTTP/1.1", headerFields: nil)!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: FakeMac.answer.1)
            client?.urlProtocolDidFinishLoading(self)
        }
        override func stopLoading() {}
    }

    private func gateway() throws -> DeckGateway {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FakeMac.self]
        FakeMac.requests = []
        FakeMac.bodies = []
        return try DeckGateway(host: "mac.example.ts.net", session: URLSession(configuration: configuration))
    }

    func testHosts() throws {
        XCTAssertEqual(try DeckGateway(host: "mac.example.ts.net").base.absoluteString, "http://mac.example.ts.net:7349")
        XCTAssertEqual(try DeckGateway(host: "https://mac:9000/x?y").base.absoluteString, "https://mac:9000")
        XCTAssertThrowsError(try DeckGateway(host: " "))
        XCTAssertThrowsError(try DeckGateway(host: "ftp://mac"))
    }

    func testTheDeckIsReadWithTheClientHeaderAndTheVersionHeld() async throws {
        let gateway = try gateway()
        FakeMac.answer = (200, deckJSON(states: [], version: "v2"))
        let deck = try await gateway.deck(after: "v1")
        XCTAssertEqual(deck.version, "v2")
        let request = try XCTUnwrap(FakeMac.requests.first)
        XCTAssertEqual(request.httpMethod, "GET")
        XCTAssertEqual(request.url?.path, "/api/deck")
        XCTAssertEqual(request.url?.query, "after=v1")
        XCTAssertNotNil(request.value(forHTTPHeaderField: "X-Lapis-Client"))
    }

    func testASubmitIsOnePostAndARefusalCarriesTheReason() async throws {
        let gateway = try gateway()
        FakeMac.answer = (200, Data(#"{"ok":true}"#.utf8))
        let admitted = await gateway.submit(agentID: agentA, text: "run it")
        XCTAssertNil(admitted)
        XCTAssertEqual(FakeMac.requests.last?.httpMethod, "POST")
        XCTAssertEqual(FakeMac.requests.last?.url?.path, "/api/agents/\(agentA)/submit")
        let body = try JSONSerialization.jsonObject(with: try XCTUnwrap(FakeMac.bodies.last)) as? [String: String]
        XCTAssertEqual(body, ["text": "run it"])

        FakeMac.answer = (409, Data(#"{"error":"rejected: a request is pending"}"#.utf8))
        let refused = await gateway.submit(agentID: agentA, text: "run it")
        XCTAssertEqual(refused, "rejected: a request is pending")
        XCTAssertEqual(FakeMac.requests.count, 2) // never retried
    }
}
