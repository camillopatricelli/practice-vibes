# 11. Footswitch

A **MIDI footswitch** lets you run the looper, record takes and more without taking your hands off your instrument. Any pedal
that sends MIDI works, over a USB cable or **Bluetooth MIDI**.

---

## Connecting the pedal

- **USB**: plug it in. It shows up by itself.
- **Bluetooth MIDI**:
  - **iPad / Mac**: tap the **Bluetooth** button in the top bar, pick your pedal in the list and connect.
    (iPad: the list opens in the app's own panel; **Close** when done.)
    (Mac: or use Audio MIDI Setup › Window › Show MIDI Studio › Bluetooth.)
  - **Windows**: pair the pedal in Windows Settings › Bluetooth & devices first. The **Bluetooth** button
    in the top bar explains this and has an **Open Bluetooth settings** button that takes you straight there.

To check it works: **Rig** tab › **MIDI footswitch** shows the last MIDI message received. Press a switch and
it should change.

---

## Setting it up

Open **Footswitch setup...** (Rig tab, or **Footswitch** on the Looper tab).

1. **Learn A-D in a row**, then press your pedal's switches one after the other, in the order you want them to be
   A, B, C, D. (Or **Learn** a single switch.) A lamp lights when the app sees a press. **Forget** unlearns a switch.
2. For each switch, choose a **Tap** action and, optionally, a **Hold 1 s** action.

Default: switches A–D = looper tracks 1–4. **Tap** records, overdubs or plays the track; **hold** clears it.

The actions:
- Track 1–4: record / overdub / play · clear · mute / unmute
- Undo last overdub · Mute / unmute all tracks · Clear all tracks
- Start / stop metronome
- Tuner on / off
- Record a take (start / stop)
- Mute / unmute one of the song's stems (vocals, guitar, bass, drums, piano or other: each has its own entry)
- Rig: bypass selected plugin

The tap action happens **on the press**, so a loop starts exactly when you stomp. The looper rows show which switch
controls them ("pedal A").

Your learned switches are kept, and the pedal works again next time without relearning.

---

## Tap and hold need a "momentary" pedal

To tell a tap from a hold, the app needs to know when you press **and** when you release the switch. Pedals can work
two ways:
- **Momentary**: one message on press, another on release. Tap and hold both work.
- **Toggle / latching**: one message per stomp. Only the tap action works, and the app shows a note about it.

Most pedals can be switched to momentary in their own settings or app. Learn the switches again afterwards.

> **Example – M-VAVE Chocolate:** in its CubeSuite app choose **Custom Control**, give each switch its own CC number
> and set **Visual behavior** to **Off** (momentary). Then learn the switches in Practice Vibes.

Next: **[12. Troubleshooting](12-troubleshooting.md)**
