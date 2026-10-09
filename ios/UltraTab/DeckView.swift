import SwiftUI

// The deck: categories on top, one card in front with the next peeking
// behind, and the note field below it. Swipe right sends the proposed reply,
// swipe left skips. Nothing else: the app's name, a count and settings earn no
// space on a screen read in seconds; settings open from the notice or the
// empty deck, where a connection problem shows.
struct DeckView: View {
    @Environment(DeckStore.self) private var store
    @Environment(Deck.self) private var deck
    @Environment(\.scenePhase) private var phase
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    @State private var drag: CGSize = .zero
    @State private var leaving: Double = 0 // the answered card's slide
    @State private var settings = false
    @FocusState private var typing: Bool
    @AppStorage("tutorialSeen") private var tutorialSeen = false

    var body: some View {
        VStack(spacing: 0) {
            rail
                .padding(.top, 6)
            if !deck.notice.isEmpty {
                Button(action: openSettings) {
                    HStack(spacing: 6) {
                        Text(deck.notice)
                        Image(systemName: "gearshape")
                    }
                    .font(.footnote)
                    .foregroundStyle(Theme.quiet)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .padding(.horizontal, 20)
                .padding(.top, 6)
                .accessibilityIdentifier("notice")
            }
            stack
                .padding(.horizontal, 16)
                .padding(.top, 10)
                // Touching the card puts the keyboard away.
                .simultaneousGesture(TapGesture().onEnded { typing = false })
            Text(deck.message)
                .font(.footnote)
                .foregroundStyle(deck.message.hasPrefix("Not sent") ? Theme.skip : Theme.quiet)
                .lineLimit(2)
                .frame(maxWidth: .infinity, minHeight: 20)
                .padding(.horizontal, 20)
                .padding(.vertical, 4)
                .accessibilityIdentifier("message")
            AnnotateBar(send: { answer(text: $0) }, typing: $typing)
                .padding(.horizontal, 16)
                .padding(.bottom, 8)
        }
        .background(Theme.background.ignoresSafeArea())
        .overlay {
            if !tutorialSeen { Tutorial { tutorialSeen = true } }
        }
        .sheet(isPresented: $settings) { SettingsSheet() }
        .onChange(of: phase, initial: true) { _, now in
            if now == .active { store.follow() } else { store.pause() }
        }
    }

    private func openSettings() {
        typing = false
        settings = true
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
                            // Something waits here; how many is not the point,
                            // but a person who cannot see the dot still hears it.
                            if entry.count > 0 {
                                Circle()
                                    .fill(Theme.gold)
                                    .frame(width: 6, height: 6)
                                    .accessibilityLabel("\(entry.count) waiting")
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
            Button(action: openSettings) {
                Image(systemName: "gearshape").foregroundStyle(Theme.quiet)
            }
            .padding(.top, 16)
            .accessibilityLabel("Settings")
            .accessibilityIdentifier("settings")
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

    // The note field acts on the front card too.
    private func answer(text: String) -> Bool {
        if case .sending = deck.send(text) { return true }
        return false
    }
}

// Below the card: the note field. Typing (or the keyboard's own dictation,
// such as Wispr Flow) lands here, editable; nothing is sent until the arrow.
// Accept and skip are the card's swipes, so there are no buttons for them.
struct AnnotateBar: View {
    let send: (String) -> Bool
    // Owned by the deck, so touching the card or opening settings puts the
    // keyboard away and nothing brings it back on its own.
    var typing: FocusState<Bool>.Binding
    @Environment(Deck.self) private var deck
    @State private var text = ""

    var body: some View {
        let front = deck.front
        HStack(alignment: .bottom, spacing: 8) {
            TextField("Add a note", text: $text, axis: .vertical)
                .lineLimit(1...5)
                .focused(typing)
                .padding(.horizontal, 14)
                .padding(.vertical, 10)
                .background(Theme.raised, in: .rect(cornerRadius: 18))
                .overlay(RoundedRectangle(cornerRadius: 18).strokeBorder(Theme.edge, lineWidth: 1))
                .disabled(front?.canType != true)
                .accessibilityIdentifier("annotation")
            if typing.wrappedValue {
                Button {
                    typing.wrappedValue = false
                } label: {
                    Image(systemName: "keyboard.chevron.compact.down")
                        .font(.system(size: 22))
                        .foregroundStyle(Theme.quiet)
                        .frame(width: 34, height: 40)
                }
                .accessibilityLabel("Hide the keyboard")
                .accessibilityIdentifier("hide-keyboard")
            }
            if !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                Button {
                    if send(text) {
                        text = ""
                        typing.wrappedValue = false
                    }
                } label: {
                    Image(systemName: "arrow.up.circle.fill")
                        .font(.system(size: 34))
                        .foregroundStyle(Theme.gold)
                }
                .disabled(front?.canType != true)
                .accessibilityLabel("Send note")
                .accessibilityIdentifier("send")
            }
        }
        .onChange(of: front?.key) { _, _ in
            // A note is for the card it was written on; a refused send keeps
            // the same key, so the draft stays for another try.
            text = ""
        }
    }
}

// Shown once, at the first launch: the two swipes and the note field.
struct Tutorial: View {
    let done: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 22) {
            Text("Each card is an agent waiting on you.")
                .font(.title3.weight(.semibold))
                .foregroundStyle(.white)
                .accessibilityIdentifier("tutorial")
            row("arrow.right", Theme.accept, "Swipe right", "sends the proposed reply")
            row("arrow.left", Theme.skip, "Swipe left", "skips it; nothing is sent")
            row("square.and.pencil", Theme.gold, "Add a note", "type or dictate your own reply")
            Button(action: done) {
                Text("Got it")
                    .font(.headline)
                    .foregroundStyle(.black)
                    .frame(maxWidth: .infinity, minHeight: 50)
                    .background(Theme.gold, in: .rect(cornerRadius: 14))
            }
            .accessibilityIdentifier("tutorial-done")
        }
        .padding(28)
        .background(Theme.card, in: .rect(cornerRadius: 26))
        .overlay(RoundedRectangle(cornerRadius: 26).strokeBorder(Theme.edge, lineWidth: 1))
        .padding(20)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black.opacity(0.6).ignoresSafeArea())
    }

    private func row(_ symbol: String, _ color: Color, _ title: String, _ detail: String) -> some View {
        HStack(spacing: 14) {
            Image(systemName: symbol)
                .font(.title2.weight(.semibold))
                .foregroundStyle(color)
                .frame(width: 34)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.headline).foregroundStyle(.white)
                Text(detail).font(.callout).foregroundStyle(Theme.body)
            }
        }
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
