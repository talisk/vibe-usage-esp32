// Optional macOS regression: decode actual renderer output with Apple Vision.
// Usage: swift tests/host/decode_about_qr.swift build/ui-previews/*-qr.png
import Foundation
import Vision

precondition(CommandLine.arguments.count == 5, "Pass all four About QR PNG files")
for path in CommandLine.arguments.dropFirst() {
    let request = VNDetectBarcodesRequest()
    request.symbologies = [.qr]
    try VNImageRequestHandler(url: URL(fileURLWithPath: path)).perform([request])
    guard let payload = request.results?.first?.payloadStringValue else {
        fatalError("QR decode failed: \(path)")
    }
    let expected = path.contains("repository")
        ? "https://github.com/talisk/vibe-usage-esp32" : "https://x.com/SwainTalisk"
    precondition(payload == expected, "Unexpected QR target")
    print("\(URL(fileURLWithPath: path).lastPathComponent): decoded exact URL PASS")
}
