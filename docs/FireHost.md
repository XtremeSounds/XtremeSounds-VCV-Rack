# Fire Host

Fire Host connects an **Akai Fire / FL Studio Fire** controller to standalone VCV Rack. It translates pads, relative encoders, buttons, and mode controls into Rack polyphonic note outputs and assignable CV outputs.

## Hardware connection

1. Connect the Akai Fire by USB.
2. Close other applications that may already hold the Fire MIDI ports.
3. Add **Xtreme Sounds > Fire Host**.
4. Use **Find / reconnect Akai Fire** from the module menu when needed.
5. LINK indicates a recognized Fire connection; DATA shows MIDI activity.

## Pad outputs

- **V/OCT** — translated polyphonic pitch.
- **GATE** — polyphonic note gates.
- **VEL** — polyphonic velocity CV.

Fire pad IDs are translated through the physical 4x16 pad layout before musical output.

### Pad modes

- **NOTE** cycles Fire Host note layouts.
- **DRUM** selects the 64-pad drum layout.

### Velocity response

Right-click **Pad velocity response**: Soft, Medium, Firm, or Raw.

### Pad color

Right-click **Pad color** to choose one of 16 Fire RGB lighting colors: Red, Orange, Amber, Yellow, Lime, Green, Mint, Cyan, Sky, Blue, Indigo, Purple, Magenta, Pink, White, or Ice. The selected color is saved with the patch and sent to the Fire immediately. In Play mode root notes are shown brighter than the other scale degrees. Enable **Root notes white** to display every root-note pad in white while the remaining scale degrees keep the selected pad color.

### Root note and scale

Fire Host mirrors the DrivenByMoss Play-mode concept with saved **Root note** and **Scale** settings in the right-click menu. These settings affect **Play mode only**; Piano and Drum64 retain their own mappings. The Akai Fire OLED normally shows the active root note and scale.

Root notes: C, C#/Db, D, D#/Eb, E, F, F#/Gb, G, G#/Ab, A, A#/Bb, B.

Scales: Major, Natural Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor, Melodic Minor, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone, Diminished Half-Whole, Diminished Whole-Half, and Chromatic.

## Assignable CV outputs

Fire Host provides 20 assignable outputs. Click an assignment label, or right-click its jack, to choose the Fire control that drives the slot.

Continuous encoder sources include Volume, Pan, Filter, Resonance, and Select. Relative encoder motion is accumulated to 0-127 and converted to 0-10 V. When an assigned encoder is touched or moved, the Fire OLED temporarily shows that encoder and its current modulation percentage, then returns to the root/scale display.

Button assignments support Momentary, Toggle, and Trigger behavior.

## Themes

Right-click **Panel theme** for Light, Silver, Smoke, or Dark. Dark and Smoke use the same darker substrate family as S8.

## Independence notice

Fire Host is not affiliated with or endorsed by Akai Professional. Akai, Fire, and FL Studio are trademarks of their respective owners.

## OLED feedback

The Akai Fire OLED normally shows the active root note and scale. When an assigned continuous encoder is touched or turned, the OLED temporarily shows that control and a 0–100% white progress bar matching its current modulation/CV amount. Assigned buttons use the same display: 100% while pressed and 0% on release, then the OLED returns to root/scale.


## Shutdown behavior

When VCV Rack closes or FireHost is removed, FireHost blanks the Fire OLED and turns off its pad/button LEDs before releasing the MIDI output.
