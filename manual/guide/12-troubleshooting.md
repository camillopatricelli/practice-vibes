# 12. Troubleshooting

---

## I can't hear my instrument
- Is **Monitor input** on? It starts off every time (Rig tab, chapter 2).
- Does the **input meter** in the Rig tab move when you play? If not, check the input in **Audio settings** and its
  **Active input channels**, and the input box in the Rig tab.
- Meter moves but still silence: check **Output level**, and that the right **Output** is chosen in Audio settings.
- Is a plugin in the chain silent or badly set? **Bypass** each one to find out.

## There's a delay between playing and hearing
That's latency. Use a smaller **Audio buffer size**, wired headphones and, on Windows, a lower-latency device mode
(chapter 2). Bluetooth headphones always add a long delay.

## Crackles, clicks or dropouts
The device can't keep up: use a bigger buffer, close other apps, plug in your laptop, and check heavy plugins
(chapter 2 and 10).

## The practice log counts time I wasn't playing (or misses time I was)
- It listens to the instrument input. With the built-in microphone it also hears the room: use your audio interface.
- A very low **Input gain** can make soft playing go unnoticed.
- Correct any wrong entry by tapping it (chapter 3).

## Overdubs in the looper sound late or "flammed"
Run **Calibrate...** on the Looper tab again (chapter 7), especially after changing interface or buffer size.

## The chords look wrong
Chord recognition is an automatic guess. Busy mixes, distorted guitars and unusual chords can come out simplified.
Trust your ears.

## A plugin is missing from the list
- **Windows / Mac**: install it as VST3 (or AU on Mac), then **Scan VST3**.
- **iPad**: tap **Refresh list**. Check the plugin shows up in another app (e.g. GarageBand).

## Changes from my other device don't show up
- Both devices must use the **same folder** in iCloud Drive (or the same synced folder between computers).
- Wait for the cloud to sync, then **Library folder...** › **Check for changes now**.
- On the iPad, only iCloud Drive can be used for a shared library.

## A YouTube video won't play
The video's owner may not allow playing it in other apps: use **Open on YouTube**. On the iPad, tap the video itself
once for the first play.

## Windows: "Windows protected your PC"
Click **More info** › **Run anyway** (chapter 1). It only appears the first time you open a new version.

## Mac: "Practice Vibes can't be opened" or "was blocked"
The app isn't notarised by Apple. Allow it once in **System Settings › Privacy & Security › Open Anyway** (macOS 15
or later), or right-click the app › **Open** (macOS 12–14). Chapter 1 has the steps; a new build may ask again.

## iPad: the app won't open any more
It's probably been more than 7 days since SideStore last signed it. Turn on **LocalDevVPN**, open **SideStore** ›
**My Apps** › **Refresh All**. Your data is still there. If refreshing fails with a pairing or connection error,
see "Good to know" in chapter 1.

---

## The log file

When reporting a problem, the app's log helps a lot. It records what the app did and any errors.

- **Inside the app**: **Audio settings** › **Show the Log tab**. The **Log** tab appears; **Copy log** copies it so you
  can paste it into a message.
- **The file**:
  - iPad: Files › On My iPad › Practice Vibes
  - Windows: `%APPDATA%\Practice Vibes` (paste it into the Explorer address bar)
  - Mac: `~/Library/Application Support/Practice Vibes/PracticeVibes.log` (Finder › Go › Go to Folder…)

Please describe what you did, what you expected and what happened, and attach the log.

## Reporting a problem

Open an issue on the project's GitHub page (github.com/camillopatricelli/practice-vibes/issues), or write to
practicevibes.app@gmail.com. First copy the diagnostic info: **Audio settings** › **About Practice Vibes** ›
**Copy diagnostic info**, then paste it into your report. It has the version and build number, your device and your
audio setup.

The **About** card also shows the version, links to the privacy statement, the licence and the source code, and
**Open source licences**: the licences of the libraries, fonts and sounds the app is built with.
