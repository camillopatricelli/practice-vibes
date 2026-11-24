# 10. Rig: your effects

The **Rig** tab is your pedalboard. You plug in amp simulators, pedals and other effects you already own as
**plugins**, and your instrument goes through them before you hear it, record it or loop it.

Practice Vibes doesn't come with its own amp or effects: it uses plugins, so you can pick the sounds you like. Many
good ones are free. You don't need any: with an empty chain your instrument goes straight through. That's often all a
keyboard, an electronic drum kit or a microphone needs.

![Rig with two plugins in the chain](../../docs/screens/en/rig.png)

---

## How it works

> Think of it as a cable run:
>
> **instrument → interface input → Input gain → plugin 1 → plugin 2 → … → Output level → headphones**
>
> - **Input gain** is like the input knob on an amp: how strongly the signal enters. Set it so the input meter
>   moves well up when you play hard, without hitting the top. Too low and the effects sound weak and noisy; too high
>   and it distorts harshly before reaching them.
> - The **Effects chain** is your pedalboard: plugins run top to bottom, like pedals from guitar to amp. Order
>   matters, just like with real pedals: e.g. a wah or compressor before the amp, delay and reverb after it.
> - **Output level** is the master volume of your instrument in your headphones, compared to songs, loops and the
>   metronome.
> - **Monitor input** must be on to hear it at all (chapter 2).
>
> The songs, the metronome and the loops are mixed in after the chain: they don't go through your effects (except
> loops recorded "Before FX", chapter 7).

---

## Plugin types

Plugins are separate apps or programs from other makers that work inside host apps like this one:
- **iPad**: **AUv3** (Audio Unit) plugins: install them from the App Store like any app (search for "AUv3 guitar",
  "AUv3 bass amp" or "AUv3 reverb"). They show up in the list by themselves; if one is missing, tap **Refresh list**.
- **Windows**: **VST3** plugins. Install them with their own installer, then tap **Scan VST3** once.
- **Mac**: **Audio Units** (listed by themselves) and **VST3** (tap **Scan VST3** once).

If a plugin crashes while being scanned, it's skipped and noted in the log; the app carries on.

---

## Building the chain

1. **Available plugins** lists what's installed. Select one and tap **Add to chain** (or double-tap it).
2. The plugin appears in **Effects chain**. Select it to:
   - **Open UI** (window icon): show the plugin's own window, with its knobs and presets,
   - **Bypass** (power icon): switch it off without removing it (like stepping on a pedal),
   - **Up** / **Down** (arrows): change its place in the chain,
   - **Remove** (bin icon): take it out.
3. Play and adjust.

The chain, and every plugin's settings, are saved and come back next time.

> **Tip:** plugins use processing power. If you hear crackles after adding one, try a bigger buffer (chapter 2), or
> turn off the plugin's "high quality" / oversampling mode. Some amp simulators also add their own delay.

A footswitch can bypass the selected plugin (chapter 11).

---

## Scenes: change the sound while you play

Going from a clean sound to a crunchy one usually means opening a plugin's window and turning knobs. **Scenes** save
up to four sounds of the same chain, so you can switch with one tap, or with your foot (chapter 11).

A scene remembers every plugin's settings, which plugins are on or bypassed, and the **Output level** (so a lead sound
can be louder).

1. Set up a sound: open the plugins, turn the knobs, bypass what you don't need.
2. Under **Scenes**, tap an empty scene (**1 empty**): the current sound is saved there.
3. Set up the next sound and tap the next empty scene. And so on.
4. Tap a saved scene to switch to it.

The **...** button next to the scenes:
- **Save the current sound to**: update a scene after you've changed the knobs (tapping a saved scene only switches
  to it).
- **Rename**: give a scene a name, e.g. *Clean* or *Crunch*.
- **Clear**: empty a scene.
- *Plugin*: **scenes restore its full state**: see below.

Once a scene is saved, the top bar shows the current scene on every tab. Tap it to switch scenes, or to put the
footswitch in Looper or Scene mode.

> **Some sounds don't come back?** To switch quickly, a scene sets the knobs a plugin shows to the app (its
> parameters). Some plugins keep part of the sound elsewhere: an amp model picked from a menu, a loaded IR or
> capture file. For those, select the plugin in the chain, then **...** › *plugin*: **scenes restore its full
> state**. That works with any plugin, but it's slower and the sound may drop out for a moment when switching. The
> chain marks such plugins with **(full scene recall)**.

Good to know:
- A plugin you add after saving a scene is left as it is by that scene. Save the scene again to include it.
- Moving plugins up or down doesn't break your scenes.
- Delay and reverb tails may cut off when you switch. That's fine for practising.

---

## Play a MIDI keyboard through an instrument plugin

A MIDI keyboard (or pad) makes no sound by itself. Add an **instrument** plugin (a piano, a synth, a drum sampler) and
the app plays it from your keyboard: you hear it, and the looper and recorded takes include it, just like a guitar
through the effects.

1. Connect the keyboard (USB, or Bluetooth MIDI on the iPad).
2. In **Available plugins**, instruments are marked **[Instrument]**. Select one and tap **Add to chain**.
3. It goes to the top of the chain as **Instrument:** *name*. Effects stay below it and process its sound (a reverb
   on a piano, say). There's one instrument at a time: adding another replaces it.
4. Turn on **Monitor input** to hear it (the chain column reminds you), then play.

Good to know:
- **Open UI** shows the instrument's own window (sounds, presets). **Bypass** silences it, **Remove** takes it out.
- Your audio input still plays along with the instrument. If you only play the keyboard, turn **Input gain** down
  to keep the interface's hiss out.
- A footswitch's own messages never play notes.
- The looper's **Before FX** mode records the audio input only, without the instrument: use **After FX** (the
  default) to loop the keyboard.
- Free instruments to start with: on Windows and Mac, e.g. a free piano player plugin; on the iPad, AUv3 instrument
  apps from the App Store (search "AUv3 piano" or "AUv3 synth"). Sampled pianos can use a lot of memory, which
  matters on older iPads.
- Delay: the instrument adds no input delay of its own beyond the audio buffer (chapter 2), so the same buffer
  settings that suit a guitar suit the keyboard. A digital piano's own speakers are still quicker: if you'd rather
  hear those, you don't need an instrument plugin at all (the notes still count as practice time, chapter 3).

Next: **[11. Footswitch](11-footswitch.md)**
