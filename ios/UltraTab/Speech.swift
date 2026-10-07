import AVFoundation
import Foundation
import Speech

// The voice path behind the annotate button. Kept to this small protocol so
// another audio model can replace Apple's recognizer: `start` delivers the
// transcript so far (replacing the previous one) until `stop`, and reports
// a failure through `failed`.
@MainActor
protocol Transcriber: AnyObject {
    func start(text: @escaping @MainActor (String) -> Void,
               failed: @escaping @MainActor (String) -> Void) async
    func stop()
}

// Apple's SFSpeechRecognizer, on the device when the recognizer supports it.
@MainActor
final class AppleTranscriber: Transcriber {
    private let engine = AVAudioEngine()
    private var request: SFSpeechAudioBufferRecognitionRequest?
    private var task: SFSpeechRecognitionTask?
    private let recognizer = SFSpeechRecognizer()

    func start(text: @escaping @MainActor (String) -> Void,
               failed: @escaping @MainActor (String) -> Void) async {
        guard await Self.authorized() else {
            failed("Allow speech recognition and the microphone for Ultra Tab in Settings")
            return
        }
        guard let recognizer, recognizer.isAvailable else {
            failed("Speech recognition is not available right now")
            return
        }
        stop()
        let request = SFSpeechAudioBufferRecognitionRequest()
        request.shouldReportPartialResults = true
        if recognizer.supportsOnDeviceRecognition { request.requiresOnDeviceRecognition = true }
        do {
            let audio = AVAudioSession.sharedInstance()
            try audio.setCategory(.record, mode: .measurement, options: .duckOthers)
            try audio.setActive(true, options: .notifyOthersOnDeactivation)
            let input = engine.inputNode
            let format = input.outputFormat(forBus: 0)
            input.installTap(onBus: 0, bufferSize: 1024, format: format) { buffer, _ in
                request.append(buffer)
            }
            engine.prepare()
            try engine.start()
        } catch {
            stop()
            failed("The microphone could not start: \(error.localizedDescription)")
            return
        }
        self.request = request
        task = recognizer.recognitionTask(with: request) { result, error in
            let heard = result?.bestTranscription.formattedString
            let final = result?.isFinal ?? false
            let problem = error?.localizedDescription
            Task { @MainActor in
                if let heard { text(heard) }
                if let problem, heard == nil, !final { failed(problem) }
            }
        }
    }

    func stop() {
        if engine.isRunning {
            engine.stop()
            engine.inputNode.removeTap(onBus: 0)
        }
        request?.endAudio()
        request = nil
        task?.finish()
        task = nil
        try? AVAudioSession.sharedInstance().setActive(false, options: .notifyOthersOnDeactivation)
    }

    private static func authorized() async -> Bool {
        let speech = await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { continuation.resume(returning: $0 == .authorized) }
        }
        guard speech else { return false }
        return await AVAudioApplication.requestRecordPermission()
    }
}

// A transcript said word by word, for the simulator's UI tests (launch
// argument -scriptedSpeech "words"); no microphone is used.
@MainActor
final class ScriptedTranscriber: Transcriber {
    private let words: [String]
    private var running: Task<Void, Never>?

    init(_ script: String) {
        words = script.split(separator: " ").map(String.init)
    }

    func start(text: @escaping @MainActor (String) -> Void,
               failed: @escaping @MainActor (String) -> Void) async {
        running?.cancel()
        let words = words
        running = Task { @MainActor in
            for count in 1...max(1, words.count) {
                try? await Task.sleep(for: .milliseconds(120))
                if Task.isCancelled { return }
                text(words.prefix(count).joined(separator: " "))
            }
        }
    }

    func stop() {
        running?.cancel()
        running = nil
    }
}
