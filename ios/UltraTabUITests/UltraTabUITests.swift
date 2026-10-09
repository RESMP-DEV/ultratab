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
        app.launchArguments = ["-gatewayHost", host] + (extra.isEmpty || extra.first != "-resetTutorial"
            ? ["-tutorialSeen", "YES"] : []) + extra
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
        for block in ["block-text", "block-list", "block-table", "proposal"] {
            XCTAssertTrue(element(block).waitForExistence(timeout: 5), "kernels shows \(block)")
        }
        // The card is the screen: no title bar, count or since line.
        for gone in ["count", "since", "settings"] {
            XCTAssertFalse(element(gone).exists, "\(gone) is not shown over a card")
        }
        snap("1-card-text-list-table")

        // The keyboard comes up for the annotation and goes away on request.
        // (the hide button shows exactly while the field has focus).
        element("annotation").tap()
        XCTAssertTrue(element("hide-keyboard").waitForExistence(timeout: 5), "typing focuses the field")
        element("hide-keyboard").tap()
        let hidden = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"),
                                               object: element("hide-keyboard"))
        XCTAssertEqual(XCTWaiter().wait(for: [hidden], timeout: 5), .completed, "the keyboard hides")
        // Touching the card puts it away too.
        element("annotation").tap()
        XCTAssertTrue(element("hide-keyboard").waitForExistence(timeout: 5))
        element("headline").tap()
        let away = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"),
                                             object: element("hide-keyboard"))
        XCTAssertEqual(XCTWaiter().wait(for: [away], timeout: 5), .completed, "a touch on the card hides it")

        // Swipe right: the proposed reply goes to kernels (the check reads
        // it on the Mac side) and the next card comes forward.
        swipeCard(right: true)
        waitForFront("docs")
        for block in ["block-diagram", "block-text", "block-link"] {
            XCTAssertTrue(element(block).waitForExistence(timeout: 5), "docs shows \(block)")
        }
        XCTAssertFalse(element("proposal").exists, "no proposed reply, no reply box")
        XCTAssertTrue(element("no-proposal").waitForExistence(timeout: 5),
                      "a card with no proposal says where a note goes")
        for gone in ["accept", "skip", "mic"] {
            XCTAssertFalse(element(gone).exists, "no \(gone) button: the swipes answer")
        }
        let message = element("message")
        XCTAssertTrue(message.waitForExistence(timeout: 5))
        let sent = XCTNSPredicateExpectation(predicate: NSPredicate(format: "label == %@", "Sent to kernels"),
                                             object: message)
        XCTAssertEqual(XCTWaiter().wait(for: [sent], timeout: 10), .completed, "kernels admitted the reply")
        sleep(1) // the diagram's image finishes drawing
        snap("2-card-diagram-text-link")

        // A note: typed (the keyboard's own dictation lands the same way).
        let field = element("annotation")
        field.tap()
        field.typeText("Make the arrows gold please")
        snap("3-annotate")
        element("send").tap()
        waitForFront("approve")

        // A request takes no answer here; it can be skipped.
        XCTAssertTrue(element("request").exists)
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

        swipeCard(right: false)
        XCTAssertTrue(element("empty").waitForExistence(timeout: 5))
        snap("6-empty")
    }

    // The first launch explains the swipes once.
    func testTutorial() throws {
        launch(["-resetTutorial", "YES"])
        XCTAssertTrue(element("tutorial").waitForExistence(timeout: 10))
        snap("0-tutorial")
        element("tutorial-done").tap()
        let gone = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"),
                                             object: element("tutorial"))
        XCTAssertEqual(XCTWaiter().wait(for: [gone], timeout: 5), .completed, "Got it puts it away")
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
