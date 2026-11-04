// SPDX-License-Identifier: GPL-3.0-or-later
// macOS launcher: pick resolution, anti-aliasing, graphics, volume and music options,
// remember them in private/launcher.json, then exit 0 so launcher.sh starts
// the game. Mirrors tools/launcher.ps1 without the Music folder button: custom
// soundtracks need Media Foundation. Exits 1 if the window is closed.
import AppKit

let root = URL(fileURLWithPath: CommandLine.arguments.count > 1
    ? CommandLine.arguments[1] : FileManager.default.currentDirectoryPath)
let settingsURL = root.appendingPathComponent("private/launcher.json")

let heights = [480, 720, 1080, 1440, 2160]
let samples = [1, 2, 4, 8]
let volumes = [100, 75, 50, 25, 0]

var settings: [String: Any] = [:]
if let data = try? Data(contentsOf: settingsURL),
   let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
    settings = json
}
func intSetting(_ key: String, _ fallback: Int) -> Int {
    (settings[key] as? NSNumber)?.intValue ?? fallback
}
func boolSetting(_ key: String, _ fallback: Bool = false) -> Bool {
    (settings[key] as? NSNumber)?.boolValue ?? fallback
}
// What the game does when its title screen is left idle; see docs/title-idle.md.
let idleValues = ["default", "no-cutscene", "cpu-exhibition"]
let idleNames = ["Default", "No cutscene", "CPU Exhibition"]
let screenHeight: Int = {
    guard let screen = NSScreen.screens.first ?? NSScreen.main else { return 1080 }
    return Int(screen.frame.height * screen.backingScaleFactor)
}()

final class Launcher: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 376),
                          styleMask: [.titled, .closable], backing: .buffered, defer: false)
    let resolution = NSPopUpButton()
    let msaa = NSPopUpButton()
    let volume = NSPopUpButton()
    let idle = NSPopUpButton()
    let smaa = NSButton(checkboxWithTitle: "SMAA", target: nil, action: nil)
    let shuffle = NSButton(checkboxWithTitle: "Shuffle music", target: nil, action: nil)
    let unlock = NSButton(checkboxWithTitle: "Unlock swimsuits", target: nil, action: nil)
    let demoCut = NSButton(checkboxWithTitle: "Shorter demo cuts", target: nil, action: nil)
    let freeCamera = NSButton(checkboxWithTitle: "Free camera", target: nil, action: nil)
    let sunShadows = NSButton(checkboxWithTitle: "Sun shadows", target: nil, action: nil)
    // Graphics enhancements; see docs/graphics-options.md.
    let sunSoft = NSButton(checkboxWithTitle: "Soft sun shadows", target: nil, action: nil)
    let hdr = NSButton(checkboxWithTitle: "HDR output (HDR displays)", target: nil, action: nil)
    let anisotropy = NSButton(checkboxWithTitle: "Anisotropic filtering", target: nil, action: nil)
    let occlusion = NSButton(checkboxWithTitle: "Ambient occlusion", target: nil, action: nil)
    let bloom = NSButton(checkboxWithTitle: "Bloom and sun glare", target: nil, action: nil)
    let skinSoft = NSButton(checkboxWithTitle: "Soft skin", target: nil, action: nil)
    let skinSheen = NSButton(checkboxWithTitle: "Skin sheen", target: nil, action: nil)
    var launched = false

    func addHeader(_ title: String, _ x: CGFloat) {
        let header = NSTextField(labelWithString: title)
        header.font = NSFont.boldSystemFont(ofSize: NSFont.systemFontSize)
        header.frame = NSRect(x: x, y: 342, width: 228, height: 20)
        window.contentView!.addSubview(header)
    }

    func addRow(_ title: String, _ popup: NSPopUpButton, _ items: [String], _ index: Int, _ y: CGFloat,
                _ x: CGFloat = 16) {
        let label = NSTextField(labelWithString: title)
        label.frame = NSRect(x: x, y: y + 3, width: 84, height: 20)
        popup.frame = NSRect(x: x + 88, y: y, width: 140, height: 26)
        popup.addItems(withTitles: items)
        popup.selectItem(at: max(0, index))
        window.contentView!.addSubview(label)
        window.contentView!.addSubview(popup)
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        window.title = "DOAXBV"
        window.delegate = self
        // Left: graphics. Right: the game.
        addHeader("Graphics", 16)
        addHeader("Game", 276)
        let savedHeight = intSetting("height", screenHeight)
        let nearest = heights.lastIndex(where: { $0 <= savedHeight }) ?? 0
        addRow("Resolution", resolution,
               heights.map { $0 == 480 ? "480p (windowed)" : "\($0)p" }, nearest, 306)
        let savedMsaa = intSetting("msaa", 1)
        addRow("MSAA", msaa, samples.map { $0 == 1 ? "Off" : "\($0)x" },
               samples.firstIndex(of: savedMsaa) ?? 0, 274)
        smaa.frame = NSRect(x: 104, y: 248, width: 160, height: 20)
        smaa.title = "SMAA (unsupported)"
        smaa.state = .off
        smaa.isEnabled = false
        // Off keeps the game's original shadows; see docs/sun-shadow.md.
        sunShadows.state = boolSetting("sun_shadows") ? .on : .off
        sunSoft.state = boolSetting("sun_soft") ? .on : .off
        hdr.state = boolSetting("hdr") ? .on : .off
        anisotropy.state = boolSetting("anisotropy", true) ? .on : .off
        occlusion.state = boolSetting("ambient_occlusion") ? .on : .off
        bloom.state = boolSetting("bloom") ? .on : .off
        skinSoft.state = boolSetting("skin_soft") ? .on : .off
        skinSheen.state = boolSetting("skin_sheen") ? .on : .off
        for (i, box) in [sunShadows, sunSoft, anisotropy, occlusion, bloom, skinSoft, skinSheen, hdr].enumerated() {
            box.frame = NSRect(x: 16, y: 218 - CGFloat(i) * 26, width: 236, height: 20)
        }
        // Soft sun shadows soften the sun shadows; without them there is nothing to soften.
        sunSoft.isEnabled = sunShadows.state == .on
        sunShadows.target = self
        sunShadows.action = #selector(sunShadowsChanged)
        addRow("Volume", volume, volumes.map { $0 == 0 ? "Mute" : "\($0)%" },
               volumes.firstIndex(of: intSetting("volume", 100)) ?? 0, 306, 276)
        shuffle.state = boolSetting("shuffle") ? .on : .off
        unlock.state = boolSetting("unlock_swimsuits") ? .on : .off
        demoCut.state = boolSetting("demo_cut_hold") ? .on : .off
        freeCamera.state = boolSetting("free_camera") ? .on : .off
        for (i, box) in [shuffle, unlock, demoCut, freeCamera].enumerated() {
            box.frame = NSRect(x: 276, y: 274 - CGFloat(i) * 26, width: 228, height: 20)
        }
        let idleLabel = NSTextField(labelWithString: "Title screen idle behavior")
        idleLabel.frame = NSRect(x: 276, y: 162, width: 228, height: 20)
        idle.frame = NSRect(x: 276, y: 134, width: 228, height: 26)
        idle.addItems(withTitles: idleNames)
        idle.selectItem(at: idleValues.firstIndex(of: settings["title_idle"] as? String ?? "") ?? 0)

        let play = NSButton(title: "Play", target: self, action: #selector(play))
        play.frame = NSRect(x: 416, y: 12, width: 88, height: 30)
        play.keyEquivalent = "\r"
        for view in [smaa, shuffle, unlock, demoCut, freeCamera, sunShadows, sunSoft, hdr, anisotropy, occlusion, bloom,
                     skinSoft, skinSheen, idleLabel, idle, play] { window.contentView!.addSubview(view) }

        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc func sunShadowsChanged() {
        sunSoft.isEnabled = sunShadows.state == .on
    }

    @objc func play() {
        // Keys this window does not show (the in-game Graphics menu's amounts) are kept.
        var chosen: [String: Any] = settings
        let shown: [String: Any] = [
            "height": heights[resolution.indexOfSelectedItem],
            "msaa": samples[msaa.indexOfSelectedItem],
            "smaa": false,
            "volume": volumes[volume.indexOfSelectedItem],
            "shuffle": shuffle.state == .on,
            "unlock_swimsuits": unlock.state == .on,
            "demo_cut_hold": demoCut.state == .on,
            "free_camera": freeCamera.state == .on,
            "sun_shadows": sunShadows.state == .on,
            "sun_soft": sunSoft.state == .on,
            "hdr": hdr.state == .on,
            "anisotropy": anisotropy.state == .on,
            "ambient_occlusion": occlusion.state == .on,
            "bloom": bloom.state == .on,
            "skin_soft": skinSoft.state == .on,
            "skin_sheen": skinSheen.state == .on,
            "title_idle": idleValues[idle.indexOfSelectedItem],
        ]
        for (key, value) in shown { chosen[key] = value }
        do {
            try FileManager.default.createDirectory(
                at: settingsURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            let data = try JSONSerialization.data(
                withJSONObject: chosen, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: settingsURL)
        } catch {
            NSAlert(error: error).runModal()
            return
        }
        launched = true
        NSApp.terminate(nil)
    }

    func windowWillClose(_ notification: Notification) {
        if !launched { exit(1) }
    }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
let launcher = Launcher()
app.delegate = launcher
app.run()
exit(launcher.launched ? 0 : 1)
