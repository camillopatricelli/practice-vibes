# 1. Installing Practice Vibes

Practice Vibes is free software (GNU AGPLv3): free to download, no account. All files are on the
[latest build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build) page:

| Platform | File |
|---|---|
| iPad (iPadOS 16 or later) | **PracticeVibes.ipa**, installed with SideStore |
| Windows 10 / 11 (64-bit) | **PracticeVibes-windows.zip** |
| Mac (Apple Silicon or Intel, macOS 12 or later) | **PracticeVibes-macos.dmg** |

The app isn't signed with paid developer certificates, so each system asks once before opening it. The steps below
show how.

What you'll want besides the app:
- **An audio interface** for your instrument (USB, or an iRig-type adapter on the iPad). The built-in microphone works for
  the tuner, but it hears the room as well, so the practice log would count background noise as playing.
- **Headphones.** If you listen to your instrument through the app on speakers while using a microphone, you'll get feedback.

More on what to plug in: **Audio setup** (AUDIO-SETUP.md).

---

## iPad

The app isn't on the App Store. It's installed with **SideStore**, a free tool that signs apps with your own Apple ID
(a free one is enough) and installs them on your iPad. You need a computer once, for the first setup; after that
everything happens on the iPad.

> SideStore's tools and steps change from time to time. If something below looks different, follow the official
> guide at [docs.sidestore.io](https://docs.sidestore.io).

### First time: set up SideStore (about 30 minutes, once)

**On the iPad**
1. Update iPadOS: **Settings › General › Software Update**.
2. Set a passcode, connect to Wi-Fi and sign in with your Apple ID. Two-factor authentication must be on.

**On the computer**
3. Windows: install Apple's device drivers with the **Apple Devices** app (Microsoft Store) or **iTunes**, otherwise
   Windows can't see the iPad. Mac: nothing to install.
4. Connect the iPad with a cable and unlock it. Tap **Trust This Computer** and enter your passcode.
5. Run the installer that SideStore's guide recommends (in 2026: **iloader**), sign in with your Apple ID and install
   SideStore on the iPad. This also creates the "pairing file" SideStore needs later.

**Back on the iPad**
6. **Settings › General › VPN & Device Management**: trust the developer profile with your Apple ID.
7. **Settings › Privacy & Security › Developer Mode**: turn it on. The iPad restarts. (This option only appears
   after the first sideloaded app is installed.)
8. Install **LocalDevVPN** from the App Store. SideStore needs it switched on to install and refresh apps.
9. Open **SideStore** and sign in with the same Apple ID.

You can unplug the iPad now.

### Install Practice Vibes

1. In Safari, open the [latest build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build)
   page and download **PracticeVibes.ipa** (it goes to **Files › Downloads**).
2. Turn on **LocalDevVPN**, open **SideStore**, go to **My Apps**, tap **+** and pick the file.
3. When it's installed, turn LocalDevVPN off and open Practice Vibes. Allow **Microphone** (your instrument input)
   and **Bluetooth** (footswitches) when asked.

### Every 7 days: refresh

Apps signed with a free Apple ID stop opening 7 days after they were last signed. Your data isn't lost, but the app
won't open until you refresh:
1. Turn on **LocalDevVPN**.
2. Open **SideStore** › **My Apps** › **Refresh All**.
3. Turn the VPN off again.

Refresh every few days to be safe.

### Updating to a new build

Download the new **PracticeVibes.ipa** and install it the same way (steps 1–3 above). **Install over the old app:
don't delete it first.** Installing over it keeps your settings and data; deleting it erases everything stored on
the iPad. A library kept in iCloud Drive is safe either way (see chapter 9). The build number is shown in **Audio
settings › About Practice Vibes**.

### Good to know
- A free Apple ID can have at most **3 sideloaded apps** at once (SideStore counts as one) and 10 new app IDs per
  week. Updating Practice Vibes doesn't use new ones.
- Only if SideStore stops working with a pairing or connection error (for example after an iPadOS update) do you need
  the computer again: connect the cable, run **iloader** and refresh the pairing file.

---

## Windows

1. Download **PracticeVibes-windows.zip** from the [latest build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build) page and unzip it to a folder of your choice (for example
   `Documents\Practice Vibes app`). Keep all the files together: `onnxruntime.dll` is needed for stem separation.
2. Double-click **Practice Vibes.exe**. If Windows shows "Windows protected your PC", click **More info** ›
   **Run anyway**. This only happens the first time.
3. Optional: right-click the .exe › **Show more options** › **Send to** › **Desktop (create shortcut)**.

Windows 10: documents and YouTube open in a built-in viewer that uses Microsoft Edge **WebView2**. It comes with
Windows 11; on Windows 10, if documents or videos stay blank, install the free **WebView2 Runtime** from Microsoft.

**Updating:** close the app, then unzip the new version over the old folder. Your settings and library are stored
elsewhere, so they aren't affected.

> **Tip:** install your audio interface's own driver from its maker's website if it has one. Chapter 2 explains how
> to get the delay between playing and hearing as low as possible.

---

## Mac

1. Download **PracticeVibes-macos.dmg** from the [latest build](https://github.com/camillopatricelli/practice-vibes/releases/tag/latest-build) page and open it.
2. Drag **Practice Vibes** onto the **Applications** folder in that window.
3. Open Practice Vibes from Applications. The app isn't notarised by Apple, so the first time macOS blocks it:
   - macOS 15 or later: **System Settings › Privacy & Security**, scroll down to "Practice Vibes was blocked" and
     click **Open Anyway**, then confirm.
   - macOS 12 to 14: right-click (or Control-click) the app › **Open** › **Open**.
   The **READ ME FIRST.txt** in the .dmg has the same steps.
4. Allow **microphone** access when asked. After an update, macOS may ask again.

**Updating:** drag the new version into Applications and choose **Replace**. A new build is a new unnotarised app,
so macOS may block it once more: allow it the same way (step 3).

---

## Where the app keeps its files

| | Settings and log | Your library (default) |
|---|---|---|
| iPad | inside the app | Files › On My iPad › Practice Vibes |
| Windows | `%APPDATA%\Practice Vibes` | chosen on first start |
| Mac | `~/Library/Application Support/Practice Vibes` | chosen on first start |

The library (topics, songs, takes, practice log, plans) is a normal folder, and you can move it into iCloud Drive to
share it between devices. See chapter 9.

Next: **[2. First setup](02-first-setup.md)**: connect your instrument and get the delay down.
