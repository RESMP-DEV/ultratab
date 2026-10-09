import SwiftUI
import WebKit

// One card: which agent, what happened, the composed blocks and the proposed
// reply. Folder, category and when you last looked are left off: the rail
// shows the category, and the timing is the composer's input for framing the
// card, not something to read.
struct CardView: View {
    let card: Card
    let refusal: String?
    var drag: Double = 0 // horizontal offset, for the accept and skip stamps

    var body: some View {
        ZStack(alignment: .top) {
            ScrollView {
                VStack(alignment: .leading, spacing: 14) {
                    header
                    if let refusal {
                        Label("Not sent: \(refusal)", systemImage: "exclamationmark.triangle.fill")
                            .font(.footnote.weight(.medium))
                            .foregroundStyle(Theme.skip)
                            .padding(10)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Theme.skip.opacity(0.12), in: .rect(cornerRadius: 10))
                            .accessibilityIdentifier("refusal")
                    }
                    Text(card.headline)
                        .font(.title3.weight(.semibold))
                        .foregroundStyle(.white)
                        .fixedSize(horizontal: false, vertical: true)
                        .contentShape(Rectangle())
                        .accessibilityIdentifier("headline")
                    ForEach(Array((card.composed?.blocks ?? []).enumerated()), id: \.offset) { _, block in
                        BlockView(block: block)
                    }
                    reply
                }
                .padding(20)
            }
            .scrollIndicators(.hidden)
            .scrollDismissesKeyboard(.immediately)
            stamps
        }
        .background(Theme.card, in: .rect(cornerRadius: 26))
        .overlay(RoundedRectangle(cornerRadius: 26).strokeBorder(edge, lineWidth: 1))
        .accessibilityElement(children: .contain)
    }

    private var edge: Color {
        if drag > 20 { return Theme.accept.opacity(min(1, drag / Swipe.distance)) }
        if drag < -20 { return Theme.skip.opacity(min(1, -drag / Swipe.distance)) }
        return Theme.edge
    }

    private var header: some View {
        Text(card.name)
            .font(.subheadline.weight(.semibold))
            .foregroundStyle(Theme.quiet)
            .lineLimit(1)
            .accessibilityIdentifier("card-name")
    }

    @ViewBuilder private var reply: some View {
        if card.request {
            Label("Answer this one in lapis", systemImage: "lock.fill")
                .font(.callout.weight(.medium))
                .foregroundStyle(Theme.skip)
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.raised, in: .rect(cornerRadius: 14))
            .accessibilityIdentifier("request")
        } else if !card.proposal.isEmpty {
            // The gold edge marks the reply a right swipe sends.
            VStack(alignment: .leading, spacing: 6) {
                Text(card.proposal)
                    .font(.body.monospaced())
                    .foregroundStyle(.white)
                    .fixedSize(horizontal: false, vertical: true)
                    .accessibilityIdentifier("proposal")
            }
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.raised, in: .rect(cornerRadius: 14))
            .overlay(RoundedRectangle(cornerRadius: 14).strokeBorder(Theme.gold.opacity(0.45), lineWidth: 1))
        } else {
            // A right swipe would be refused; say where the note goes.
            Text("No proposed reply. Add a note below.")
                .font(.callout)
                .foregroundStyle(Theme.quiet)
                .accessibilityIdentifier("no-proposal")
        }
    }

    private var stamps: some View {
        HStack {
            stamp("SEND", color: Theme.accept, angle: -12)
                .opacity(max(0, min(1, drag / Swipe.distance)))
            Spacer()
            stamp("SKIP", color: Theme.skip, angle: 12)
                .opacity(max(0, min(1, -drag / Swipe.distance)))
        }
        .padding(.horizontal, 24)
        .padding(.top, 64)
        .allowsHitTesting(false)
    }

    private func stamp(_ text: String, color: Color, angle: Double) -> some View {
        Text(text)
            .font(.title.weight(.heavy))
            .tracking(3)
            .foregroundStyle(color)
            .padding(.horizontal, 12)
            .padding(.vertical, 4)
            .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(color, lineWidth: 3))
            .rotationEffect(.degrees(angle))
    }
}

struct BlockView: View {
    let block: Block

    var body: some View {
        switch block {
        case .text(let text):
            Text(text)
                .font(.callout)
                .foregroundStyle(Theme.body)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("block-text")
        case .list(let items):
            VStack(alignment: .leading, spacing: 6) {
                ForEach(Array(items.enumerated()), id: \.offset) { _, item in
                    HStack(alignment: .firstTextBaseline, spacing: 8) {
                        Text("•").foregroundStyle(Theme.gold)
                        Text(item)
                            .foregroundStyle(Theme.body)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    .font(.callout)
                }
            }
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("block-list")
        case .table(let columns, let rows, let numeric, let more):
            TableBlock(columns: columns, rows: rows, numeric: numeric, more: more)
                .accessibilityIdentifier("block-table")
        case .diagram(let svg, let aspect):
            DiagramView(svg: svg)
                .aspectRatio(aspect, contentMode: .fit)
                .frame(maxWidth: .infinity, maxHeight: 260)
                .background(Theme.raised, in: .rect(cornerRadius: 12))
                .clipShape(.rect(cornerRadius: 12))
                .accessibilityElement()
                .accessibilityLabel("Diagram")
                .accessibilityIdentifier("block-diagram")
        case .link(let label, let url):
            LinkChip(label: label, url: url)
        }
    }
}

struct TableBlock: View {
    let columns: [String]
    let rows: [[String]]
    let numeric: [Bool]
    let more: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            ScrollView(.horizontal) {
                Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 6) {
                    GridRow {
                        ForEach(columns.indices, id: \.self) { column in
                            Text(columns[column])
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(Theme.quiet)
                                .gridColumnAlignment(numeric[column] ? .trailing : .leading)
                        }
                    }
                    Divider().overlay(Theme.edge)
                    ForEach(rows.indices, id: \.self) { row in
                        GridRow {
                            ForEach(columns.indices, id: \.self) { column in
                                Text(rows[row][column])
                                    .font(numeric[column] ? .footnote.monospacedDigit() : .footnote)
                                    .foregroundStyle(Theme.body)
                            }
                        }
                    }
                }
                .padding(12)
            }
            .background(Theme.raised, in: .rect(cornerRadius: 12))
            if more > 0 {
                Text("\(more) more row\(more == 1 ? "" : "s")")
                    .font(.caption2)
                    .foregroundStyle(Theme.quiet)
            }
        }
        .accessibilityElement(children: .combine)
    }
}

// A link the agent wrote: a web address opens in the browser; a file is on
// the Mac and is only named here.
struct LinkChip: View {
    let label: String
    let url: URL

    var body: some View {
        if url.scheme?.lowercased() == "https" {
            Link(destination: url) { chip(target: url.host ?? "", symbol: "arrow.up.right.square") }
                .accessibilityIdentifier("block-link")
        } else {
            chip(target: "on the Mac: \(url.lastPathComponent)", symbol: "doc.text")
                .accessibilityElement(children: .combine)
                .accessibilityIdentifier("block-link")
        }
    }

    private func chip(target: String, symbol: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: symbol).foregroundStyle(Theme.gold)
            VStack(alignment: .leading, spacing: 1) {
                Text(label).font(.callout.weight(.medium)).foregroundStyle(.white).lineLimit(1)
                Text(target).font(.caption2.monospaced()).foregroundStyle(Theme.quiet).lineLimit(1)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Theme.raised, in: .capsule)
        .overlay(Capsule().strokeBorder(Theme.gold.opacity(0.35), lineWidth: 1))
    }
}

// A sanitized SVG drawn as an image (SVGSanitizer): an SVG image runs no
// script and loads nothing, JavaScript is off, the page's policy allows no
// loads but the image itself, nothing is stored, and no navigation leaves it.
struct DiagramView: UIViewRepresentable {
    let svg: String

    func makeCoordinator() -> Coordinator { Coordinator() }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .nonPersistent()
        configuration.defaultWebpagePreferences.allowsContentJavaScript = false
        configuration.dataDetectorTypes = []
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.isOpaque = false
        view.backgroundColor = .clear
        view.scrollView.isScrollEnabled = false
        view.isUserInteractionEnabled = false
        view.navigationDelegate = context.coordinator
        return view
    }

    func updateUIView(_ view: WKWebView, context: Context) {
        guard context.coordinator.loaded != svg else { return }
        context.coordinator.loaded = svg
        view.loadHTMLString(DiagramView.page(svg), baseURL: nil)
    }

    static func page(_ svg: String) -> String {
        let data = Data(svg.utf8).base64EncodedString()
        return """
        <!doctype html><html><head>\
        <meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src data:; style-src 'unsafe-inline'">\
        <meta name="viewport" content="width=device-width,initial-scale=1">\
        <style>html,body{margin:0;height:100%;background:transparent}\
        img{display:block;width:100%;height:100%;object-fit:contain}</style>\
        </head><body><img alt="" src="data:image/svg+xml;base64,\(data)"></body></html>
        """
    }

    final class Coordinator: NSObject, WKNavigationDelegate {
        var loaded: String?

        func webView(_ webView: WKWebView, decidePolicyFor action: WKNavigationAction,
                     decisionHandler: @escaping @MainActor (WKNavigationActionPolicy) -> Void) {
            // Only the page this view loads itself.
            decisionHandler(action.request.url?.absoluteString == "about:blank" ? .allow : .cancel)
        }
    }
}
