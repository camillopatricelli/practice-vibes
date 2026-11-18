# Practice Vibes

A practice app for guitarists, bassists and other musicians, for **iPad, Windows and macOS**.
Free software under the [GNU AGPLv3](LICENSE): no price, no account, no ads, no tracking.

It puts the things you'd normally need several apps for in one place: a song player that slows songs down and loops
them, chord recognition, a metronome, a looper with a drum machine, a tuner, your effect plugins, a MIDI footswitch,
and a place for your lessons and practice notes. It also keeps track of how much you really play.

> **Status: in active development.** The downloads below are automatic test builds, not stable releases. Expect rough
> edges, and please [report what you find](../../issues).

<!-- Screenshots to add (see the screenshot list): Today tab, Practice tab with chords and loop, Looper with drums,
     Tuner, Topics, Rig with plugins. -->

## Who it's for

- **Students practising alone**: practice time is logged automatically while you play, with a rotation of topics,
  session plans, metronome and loops.
- **Teachers**: send a lesson (notes, songs, assignments) as one file and see what comes back.
- **Musicians who want one tool**: looper and drum machine, tuner, stem separation, plugin rig, footswitch.

## What it does

- **Song player**: WAV, AIFF, FLAC, MP3, M4A and YouTube videos. Speed 25-150 % without pitch change, pitch ±12
  semitones, A-B loop with snap to bars, waveform zoom.
- **Stems** (Windows, macOS): split a song into vocals, guitar, bass, drums, piano and other, then mute, solo or
  rebalance them.
- **Chords, beats and time signatures** detected automatically and shown as you play.
- **Metronome** with subdivisions, swing, several sounds, a speed trainer, and "follow the song's beats".
- **4-track looper** locked to the metronome, with overdub and undo, latency calibration, and a built-in **drum
  machine** (acoustic or electronic kit, step editor).
- **Tuner**: needle or strobe, guitar and bass tunings, strum all strings at once.
- **Practice log**: counts only the minutes you actually play (audio or MIDI keyboard), with history, rotation and
  session plans. No streaks, no notifications.
- **Topics**: your lessons with notes, tags, songs, PDFs and links, in a folder you choose (put it in iCloud Drive or
  another cloud folder to use it on all your devices). Share a topic as one file.
- **Rig**: your own effect and instrument plugins (AUv3 on iPad, VST3 on Windows, Audio Units and VST3 on macOS).
- **Footswitch**: any MIDI footswitch over USB or Bluetooth, each switch with a tap and a hold action.
- Dark and light themes; English and Italian.

## Download

From the [latest build](../../releases/tag/latest-build) page:

| Platform | File |
|---|---|
| Windows 10/11 (64-bit) | [PracticeVibes-windows.zip](../../releases/download/latest-build/PracticeVibes-windows.zip) |
| macOS (Apple Silicon and Intel) | [PracticeVibes-macos.dmg](../../releases/download/latest-build/PracticeVibes-macos.dmg) |
| iPad | [PracticeVibes.ipa](../../releases/download/latest-build/PracticeVibes.ipa) (install with SideStore, see below) |

The app is not signed with paid developer certificates, so each system warns you the first time. That is expected:

**Windows.** Unzip the folder anywhere and run `Practice Vibes.exe`. If SmartScreen says "Windows protected your PC",
click **More info**, then **Run anyway**. The in-app viewer needs Microsoft Edge WebView2, which comes with Windows 11
(on Windows 10, install the WebView2 Runtime from Microsoft if documents don't open).

**macOS.** Open the .dmg and drag Practice Vibes to Applications. The app is not notarised by Apple, so the first
launch is blocked: on macOS 15 or later go to System Settings > Privacy & Security and click **Open Anyway**; on
macOS 12-14 right-click the app and choose **Open**. The `READ ME FIRST.txt` in the .dmg has the details.

**iPad.** The app isn't on the App Store. It is installed with [SideStore](https://sidestore.io), which signs apps
with your own free Apple ID:
1. Set up SideStore by following [its official guide](https://docs.sidestore.io) (needs a computer once).
2. On the iPad, open the [latest build](../../releases/tag/latest-build) page in Safari and download `PracticeVibes.ipa`.
3. In SideStore, go to **My Apps**, tap **+** and pick the downloaded file.

Apps installed with a free Apple ID must be refreshed every 7 days; SideStore does this from the iPad.

On first launch, allow microphone access: it is needed for your instrument input. An audio interface works best; the
device's built-in microphone also hears the room.

## Problems and ideas

Please open an [issue](../../issues). In the app, **Audio settings > About Practice Vibes > Copy diagnostic info**
copies the version and device details to paste into your report. The log file is in:
- Windows: `%APPDATA%\Practice Vibes\PracticeVibes.log`
- macOS: `~/Library/Application Support/Practice Vibes/PracticeVibes.log`
- iPad: Files app > On My iPad > Practice Vibes

See [CONTRIBUTING.md](CONTRIBUTING.md) for more.

## Privacy

The app has no accounts and sends nothing to the developer. Your data stays in the folder you choose.
Details in [PRIVACY.md](PRIVACY.md).

## Building from source

Builds for all platforms run on GitHub Actions (see `.github/workflows/build.yml`).

**Windows**: Visual Studio 2022 Build Tools (C++ workload), CMake 3.22+, Ninja and Git. JUCE and the other
dependencies are downloaded automatically. From an "x64 Native Tools" command prompt:

```
cmake --preset windows
cmake --build --preset windows-release
```

The offline tests are `PracticeVibesTests` in the same build folder.

**macOS / iOS**: Xcode, then `cmake --preset macos` or `cmake --preset ios` (see the workflow file for the exact
build commands).

## Licence and credits

Practice Vibes is free software: you can use, study, share and change it under the
[GNU Affero General Public License v3](LICENSE). It comes with no warranty.

It is built on [JUCE](https://juce.com) (used under its AGPLv3 option),
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch),
[ONNX Runtime](https://onnxruntime.ai), Meta's [Demucs](https://github.com/facebookresearch/demucs) model,
[Lucide](https://lucide.dev) icon shapes, the Outfit and DM Sans fonts and the
[Big Rusty Drums](https://github.com/sfzinstruments/karoryfer.big-rusty-drums) samples by Karoryfer Samples.
The full list with licences is in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md), and in the app under
About > Open source licences.

Practice Vibes is made by a guitarist, Camillo Patricelli, by *vibe coding*: the idea, the design, the decisions and
the testing are his; the code is written by [Claude](https://www.anthropic.com/claude) (Anthropic's AI) through
[Claude Code](https://claude.com/claude-code). The name is a nod to that process.

The name "Practice Vibes" and the app icon belong to the author. The licence covers the code, not the name: if you
publish a modified version, please give it a different name and icon.

Contact: [practicevibes.app@gmail.com](mailto:practicevibes.app@gmail.com)
