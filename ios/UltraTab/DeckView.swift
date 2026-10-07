import SwiftUI

// The deck: categories on top, one card in front with the next peeking
// behind, and the three answers below it. Swipe right sends the proposed
// reply, swipe left skips, and the voice button annotates.
struct DeckView: View {
    @Environment(DeckStore.self) private var store
    @Environment(Deck.self) private var deck
    @Environment(\.scenePhase) private var phase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @State private var drag: CGSize = .zero
    @State private var leaving: Double = 0 // the answered card's slide
    @State private var settings = false

    var body: some View {
        VStack(spacing: 0) {
            topBar
            rail
            if !deck.notice.isEmpty {
                Text(deck.notice)
                    .font(.footnote)
                    .foregroundStyle(Theme.quiet)
                    .padding(.horizontal, 20)
                    .padding(.top, 6)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .accessibilityIdentifier("notice")
            }
            stack
                .padding(.horizontal, 16)
                .padding(.top, 10)
            Text(deck.message)
                .font(.footnote)
                .foregroundStyle(deck.message.hasPrefix("Not sent") ? Theme.skip : Theme.quiet)
                .lineLimit(2)
                .frame(maxWidth: .infinity, minHeight: 20)
                .padding(.horizontal, 20)
                .padding(.vertical, 4)
                .accessibilityIdentifier("message")
            AnnotateBar(answer: answer)
                .padding(.horizontal, 16)
                .padding(.bottom, 8)
        }
        .background(Theme.background.ignoresSafeArea())
        .sheet(isPresented: $settings) { SettingsSheet() }
        .onChange(of: phase, initial: true) { _, now in
            if now == .active { store.follow() } else { store.pause() }
        }
    }

    private var topBar: some View {
        HStack(spacing: 10) {
            Image(systemName: "arrow.right.to.line")
                .font(.headline.weight(.heavy))
                .foregroundStyle(Theme.gold)
            Text("Ultra Tab")
                .font(.headline)
            Spacer()
            Text(deck.visible.isEmpty ? "" : "\(deck.visible.count) waiting")
                .font(.footnote.monospacedDigit())
                .foregroundStyle(Theme.quiet)
                .accessibilityIdentifier("count")
            Button { settings = true } label: {
                Image(systemName: "gearshape").foregroundStyle(Theme.quiet)
            }
            .accessibilityIdentifier("settings")
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 10)
    }

    private var rail: some View {
        ScrollView(.horizontal) {
            HStack(spacing: 8) {
                ForEach(deck.rail, id: \.id) { entry in
                    let selected = entry.id == deck.category
                    Button {
                        deck.category = entry.id
                    } label: {
                        HStack(spacing: 6) {
                            Text(entry.name)
                            if entry.count > 0 {
                                Text("\(entry.count)").monospacedDigit().foregroundStyle(Theme.gold)
                            }
                        }
                        .font(.footnote.weight(selected ? .semibold : .regular))
                        .foregroundStyle(selected ? .white : Theme.quiet)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(selected ? Theme.raised : .clear, in: .capsule)
                        .overlay(Capsule().strokeBorder(selected ? Theme.gold.opacity(0.6) : Theme.edge))
                    }
                    .accessibilityIdentifier("category-\(entry.name)")
                }
            }
            .padding(.horizontal, 20)
        }
        .scrollIndicators(.hidden)
    }

    @ViewBuilder private var stack: some View {
        ZStack {
            if let behind = deck.behind {
                CardView(card: behind, refusal: deck.refusals[behind.key])
                    .scaleEffect(0.94)
                    .offset(y: 14)
                    .opacity(0.5)
                    .allowsHitTesting(false)
                    .accessibilityHidden(true)
            }
            if let front = deck.front {
                let offset = store.pose.map { $0 == .accept ? 90.0 : -90.0 } ?? (drag.width + leaving)
                CardView(card: front, refusal: deck.refusals[front.key], drag: offset)
                    .id(front.key)
                    .offset(x: offset, y: drag.height * 0.15)
                    .rotationEffect(.degrees(offset / 22))
                    .simultaneousGesture(swipe)
                    .accessibilityIdentifier("card")
                    .accessibilityAction(named: "Send the proposed reply") { answer(.accept) }
                    .accessibilityAction(named: "Skip") { answer(.skip) }
                    .transition(.identity)
            } else {
                empty
            }
        }
        .frame(maxHeight: .infinity)
    }

    private var empty: some View {
        VStack(spacing: 12) {
            Image(systemName: "checkmark.circle")
                .font(.system(size: 44, weight: .light))
                .foregroundStyle(Theme.gold)
            Text("Nothing waits on you.")
                .font(.title3)
                .accessibilityIdentifier("empty")
            if !deck.running.isEmpty {
                VStack(spacing: 4) {
                    Text("AT WORK").font(.caption2.weight(.bold)).tracking(1.2).foregroundStyle(Theme.quiet)
                    // Two agents can share a name; rows are told apart by place.
                    ForEach(Array(deck.running.prefix(12).enumerated()), id: \.offset) { _, name in
                        Text(name).font(.footnote).foregroundStyle(Theme.body)
                    }
                }
                .padding(.top, 10)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var swipe: some Gesture {
        DragGesture(minimumDistance: 16)
            .onChanged { value in
                guard abs(value.translation.width) > abs(value.translation.height) || drag != .zero else { return }
                drag = value.translation
            }
            .onEnded { value in
                let swipe = Swipe.from(dx: value.translation.width, dy: value.translation.height,
                                       predictedDx: value.predictedEndTranslation.width)
                if swipe == .none || !answer(swipe) {
                    withAnimation(.spring(duration: 0.25)) { drag = .zero }
                }
            }
    }

    // Answers the front card, sliding it away; false when the card cannot
    // take that answer (it springs back and the message says why).
    @discardableResult
    private func answer(_ swipe: Swipe) -> Bool {
        // One answer at a time: a second tap during the slide does nothing.
        if leaving != 0 { return true }
        guard let front = deck.front else { return false }
        if swipe == .accept, !front.canAccept {
            deck.accept() // only says why
            return false
        }
        let key = front.key
        let direction = swipe == .accept ? 1.0 : -1.0
        let finish = {
            // The card swiped, never one that came forward meanwhile.
            if deck.front?.key == key { deck.apply(swipe) }
            drag = .zero
            leaving = 0
        }
        if reduceMotion {
            finish()
            return true
        }
        withAnimation(.easeIn(duration: 0.18)) {
            leaving = direction * 600 - drag.width
        } completion: {
            finish()
        }
        return true
    }

    // The annotate button and the typed field act on the front card too.
    private func answer(text: String) -> Bool {
        if case .sending = deck.send(text) { return true }
        return false
    }

    private var answer: AnnotateBar.Actions {
        AnnotateBar.Actions(
            skip: { answer(.skip) },
            accept: { answer(.accept) },
            send: { answer(text: $0) })
    }
}

// Below the card: the annotation field, the big voice button between Skip
// and Send-proposal, and Send for the annotation. Tap the voice button to
// start and again to stop, or hold it while speaking; the words land in the
// field, editable, and nothing is sent until Send.
struct AnnotateBar: View {
    struct Actions {
        let skip: () -> Void
        let accept: () -> Void
        let send: (String) -> Bool
    }

    let answer: Actions
    @Environment(DeckStore.self) private var store
    @Environment(Deck.self) private var deck
    @State private var text = ""
    @State private var listening = false
    @State private var heldSince: Date?
    @State private var before = "" // the field's text when listening began
    @FocusState private var typing: Bool

    var body: some View {
        let front = deck.front
        VStack(spacing: 10) {
            HStack(alignment: .bottom, spacing: 8) {
                TextField(listening ? "Listening…" : "Annotate: hold the mic or type",
                          text: $text, axis: .vertical)
                    .lineLimit(1...5)
                    .focused($typing)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(Theme.raised, in: .rect(cornerRadius: 18))
                    .overlay(RoundedRectangle(cornerRadius: 18)
                        .strokeBorder(listening ? Theme.gold : Theme.edge, lineWidth: 1))
                    .accessibilityIdentifier("annotation")
                if !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    Button {
                        stopListening()
                        if answer.send(text) {
                            text = ""
                            typing = false
                        }
                    } label: {
                        Image(systemName: "arrow.up.circle.fill")
                            .font(.system(size: 34))
                            .foregroundStyle(Theme.gold)
                    }
                    .disabled(front?.canType != true)
                    .accessibilityLabel("Send annotation")
                    .accessibilityIdentifier("send")
                }
            }
            HStack {
                Button(action: answer.skip) {
                    Label("Skip", systemImage: "arrow.left")
                        .labelStyle(.titleAndIcon)
                        .font(.callout.weight(.medium))
                        .foregroundStyle(Theme.skip)
                        .frame(width: 96, height: 44)
                }
                .disabled(front == nil)
                .accessibilityIdentifier("skip")
                Spacer()
                mic(enabled: front?.canType == true)
                Spacer()
                Button(action: answer.accept) {
                    Label("Send", systemImage: "arrow.right")
                        .labelStyle(.titleAndIcon)
                        .font(.callout.weight(.medium))
                        .foregroundStyle(front?.canAccept == true ? Theme.accept : Theme.quiet)
                        .frame(width: 96, height: 44)
                }
                .disabled(front?.canAccept != true)
                .accessibilityLabel("Send the proposed reply")
                .accessibilityIdentifier("accept")
            }
        }
        .onChange(of: front?.key) { _, _ in
            // A note is for the card it was written on; a refused send keeps
            // the same key, so the draft stays for another try.
            stopListening()
            text = ""
        }
    }

    private func mic(enabled: Bool) -> some View {
        ZStack {
            Circle()
                .fill(listening ? Theme.gold : Theme.raised)
                .frame(width: 84, height: 84)
                .overlay(Circle().strokeBorder(Theme.gold, lineWidth: 2))
                .shadow(color: listening ? Theme.gold.opacity(0.5) : .clear, radius: 14)
            Image(systemName: listening ? "waveform" : "mic.fill")
                .font(.system(size: 32, weight: .semibold))
                .foregroundStyle(listening ? Color.black : Theme.gold)
                .symbolEffect(.variableColor.iterative, isActive: listening)
        }
        .opacity(enabled ? 1 : 0.35)
        .contentShape(Circle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { _ in
                    guard enabled, heldSince == nil else { return }
                    heldSince = .now
                    if listening { stopListening() } else { startListening() }
                }
                .onEnded { _ in
                    // A hold stops at release; a tap keeps listening until
                    // the next tap.
                    if let since = heldSince, Date.now.timeIntervalSince(since) > 0.4, listening {
                        stopListening()
                    }
                    heldSince = nil
                })
        .accessibilityElement()
        .accessibilityLabel(listening ? "Stop dictating" : "Dictate an annotation")
        .accessibilityAddTraits(.isButton)
        .accessibilityAction {
            guard enabled else { return }
            if listening { stopListening() } else { startListening() }
        }
        .accessibilityIdentifier("mic")
    }

    private func startListening() {
        before = text.trimmingCharacters(in: .whitespacesAndNewlines)
        listening = true
        typing = false
        Task {
            await store.transcriber.start(
                text: { heard in
                    guard listening else { return }
                    text = before.isEmpty ? heard : before + " " + heard
                },
                failed: { why in
                    listening = false
                    store.transcriber.stop()
                    deck.say(why)
                })
        }
    }

    private func stopListening() {
        guard listening else { return }
        listening = false
        store.transcriber.stop()
    }
}

struct SettingsSheet: View {
    @Environment(DeckStore.self) private var store
    @Environment(\.dismiss) private var dismiss
    @State private var host = ""

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    TextField("mac.tailnet.ts.net", text: $host)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .keyboardType(.URL)
                        .accessibilityIdentifier("host")
                } header: {
                    Text("Mac")
                } footer: {
                    Text("The lapis gateway on your Mac, over Tailscale or ZeroTier, as the lapis app uses it.")
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        store.host = host
                        dismiss()
                    }
                }
            }
        }
        .onAppear { host = store.host }
    }
}
