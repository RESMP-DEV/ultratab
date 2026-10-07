import XCTest

// Drives Ultra Tab against the gateway, session services and published deck
// that scripts/check_ultratab_ios.py starts on the Mac; it passes
// ULTRATAB_HOST. The deck, in order: "kernels" (text, list and table; a
// proposed reply), "docs" (diagram, text and a link to a file on the Mac),
// "approve" (a pending request) and "parked" (a proposed reply whose session
// is not running, so sending is refused).
final class UltraTabUITests: XCTestCase {
    private var app: XCUIApplication!
    private var host: String { ProcessInfo.processInfo.environment["ULTRATAB_HOST"] ?? "127.0.0.1:7351" }

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    private func launch(_ extra: [String] = []) {
        app = XCUIApplication()
        app.launchArguments = ["-gatewayHost", host, "-scriptedSpeech", "Make the arrows gold"] + extra
        app.launch()
    }

    private func snap(_ name: String) {
        let attachment = XCTAttachment(screenshot: app.screenshot())
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private func element(_ identifier: String) -> XCUIElement {
        app.descendants(matching: .any)[identifier].firstMatch
    }

    private func waitForFront(_ name: String, timeout: TimeInterval = 20) {
        let label = app.staticTexts.matching(identifier: "card-name").firstMatch
        let predicate = NSPredicate(format: "label == %@", name)
        let expectation = XCTNSPredicateExpectation(predicate: predicate, object: label)
        XCTAssertEqual(XCTWaiter().wait(for: [expectation], timeout: timeout), .completed,
                       "the front card never became \(name)")
    }

    // A finger drag across the card, as a person swipes.
    private func swipeCard(right: Bool) {
        let card = element("card")
        XCTAssertTrue(card.waitForExistence(timeout: 10))
        let start = card.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.3))
        let end = start.withOffset(CGVector(dx: right ? 260 : -260, dy: 0))
        start.press(forDuration: 0.05, thenDragTo: end, withVelocity: .fast, thenHoldForDuration: 0)
    }

    func testDeal() throws {
        launch()
        waitForFront("kernels")
        for block in ["block-text", "block-list", "block-table", "proposal", "since"] {
            XCTAssertTrue(element(block).waitForExistence(timeout: 5), "kernels shows \(block)")
        }
        XCTAssertEqual(element("count").label, "4 waiting")
        snap("1-card-text-list-table")

        // Swipe right: the proposed reply goes to kernels (the check reads
        // it on the Mac side) and the next card comes forward.
        swipeCard(right: true)
        waitForFront("docs")
        for block in ["block-diagram", "block-text", "block-link"] {
            XCTAssertTrue(element(block).waitForExistence(timeout: 5), "docs shows \(block)")
        }
        XCTAssertTrue(element("no-proposal").exists)
        XCTAssertFalse(element("accept").isEnabled)
        let message = element("message")
        XCTAssertTrue(message.waitForExistence(timeout: 5))
        let sent = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Sent to kernels"),
                                             object: message)
        XCTAssertEqual(XCTWaiter().wait(for: [sent], timeout: 10), .completed, "kernels admitted the reply")
        sleep(1) // the diagram's image finishes drawing
        snap("2-card-diagram-text-link")

        // Annotate: dictate (scripted on the simulator), edit, then send.
        let mic = element("mic")
        mic.tap()
        let field = element("annotation")
        let heard = XCTNSPredicateExpectation(
            predicate: NSPredicate(format: "value == %@", "Make the arrows gold"), object: field)
        XCTAssertEqual(XCTWaiter().wait(for: [heard], timeout: 10), .completed, "dictation fills the field")
        mic.tap() // stop listening
        // Edit at the end of what was heard, as a person taps after it.
        field.coordinate(withNormalizedOffset: CGVector(dx: 0.97, dy: 0.5)).tap()
        field.typeText(" please")
        snap("3-annotate")
        element("send").tap()
        waitForFront("approve")

        // A request takes no answer here; it can be skipped.
        XCTAssertTrue(element("request").exists)
        XCTAssertFalse(element("accept").isEnabled)
        snap("4-card-request")
        swipeCard(right: true) // springs back
        waitForFront("approve")
        swipeCard(right: false)
        waitForFront("parked")

        // Sending to an agent whose session is not running is refused, and
        // the card comes back with the reason.
        swipeCard(right: true)
        XCTAssertTrue(element("refusal").waitForExistence(timeout: 15), "the refused card comes back")
        waitForFront("parked")
        snap("5-refused")

        element("skip").tap()
        XCTAssertTrue(element("empty").waitForExistence(timeout: 5))
        snap("6-empty")
    }

    // The front card held mid-swipe, for the captures.
    func testSwipePoses() throws {
        launch(["-swipePose", "accept"])
        waitForFront("kernels")
        snap("pose-accept")
        app.terminate()
        launch(["-swipePose", "skip"])
        waitForFront("kernels")
        snap("pose-skip")
    }
}
