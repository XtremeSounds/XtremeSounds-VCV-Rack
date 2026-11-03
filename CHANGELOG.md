## 2.2.3
- Refined Fire OLED modulation-meter spacing to match the hardware display more closely without changing the established text size.
- Added more breathing room between the percentage text and the 0–100% progress bar, with slightly larger side margins.

## 2.2.1

## 2.2.2
- Sanity-check fix: blank Fire OLED, pads, and button LEDs before MIDI output disconnect, improving shutdown reliability when VCV tears down MIDI ports before module destruction.

- FireHost now blanks the OLED and turns off pad/button LEDs when the module is destroyed, including normal VCV Rack shutdown.

## 2.2.0

- FireHost: unassigned Select encoder now cycles root note.
- FireHost: unassigned Grid Left / Grid Right buttons cycle scales backward / forward.
- Assigned Select / Grid controls remain modulation-only and do not change root or scale.
- Root/scale changes immediately refresh pad LEDs and OLED feedback.

# 2.1.9

- Fire Host: OLED text enlarged by 75%; long scale names use compact display labels so the larger type still fits the 128x64 screen.
- Fire Host: assigned encoder turns now drive a live 0-100% white progress bar, and touching an assigned encoder shows its current amount.
- Fire Host: assigned buttons display 100% while pressed and 0% on release with the same progress bar.
- Fire Host: removed residual screw-edge halo/sliver artifacts and cleaned panel perimeter color artifacts.
- Fire Host: removed stray dark-theme marks above the Xtreme Sounds footer logo.

# 2.1.7

- Fixed malformed FireHost OLED source insertion that embedded literal `\n` sequences and broke the C++ class during compilation.
- Retains the 2.1.6 FireHost OLED, root-note white, screw, and shared logo updates.

# 2.1.6

- Fire Host: optional white root-note pads in Play mode.
- Fire Host: Akai Fire OLED now shows active root/scale and temporary assigned-encoder modulation strength feedback.
- Fire Host: corrected corner screw artwork so no panel-colored halo surrounds the screws.
- S8 / Fire Host: standardized the Xtreme Sounds footer logo to a lower, slightly smaller shared placement.

## 2.1.5

- FireHost: added saved right-click Pad Color selection with 16 RGB choices using the Fire pad SysEx protocol.
- FireHost: added saved Moss-style Root Note and Scale settings for Play mode; Piano and Drum64 mappings are unchanged.
- FireHost: Play-mode pad mapping now follows the selected root/scale and highlights root pads at full brightness.
- FireHost: resized XTREME SOUNDS branding using the exact S8 logo geometry at the same physical Rack size.
- FireHost: darkened Smoke and Dark panel substrates to match the S8 theme family more closely.
- S8: retained the no-ring shared knob asset from 2.1.4.

## 2.1.4

- S8: removed the bright outer silver ring from the shared knob asset so Dark and Smoke transition directly from black knob hardware to panel color.
- FireHost: note-off now lowers gate without clearing the latched pitch/velocity, preventing release tails from jumping to 0V and sounding like a retrigger.
- FireHost: poly output channel count remains stable for allocated voice slots after release, avoiding downstream voice reassignment during envelope tails.
- FireHost: resized XTREME SOUNDS panel branding to the same physical size as S8 across Light, Silver, Smoke, and Dark themes.

## 2.1.3

- Fixed FireHost C++11 enum assignments for PanelTheme so the merged VCV Rack plugin compiles without invalid int-to-enum conversions.

# 2.1.2

- Replaced the multi-runner CI with a focused VCV Rack 2 Windows x64 validation build using the official Rack SDK 2.6.6 and standard `make` / `make dist`.
- Kept the merged XtremeSounds plugin identity and S8/FireHost module identities unchanged.
- Added build-log artifact upload for failed VCV Rack validation runs.

# Changelog

## 2.1.0

- Initial combined **Xtreme Sounds** VCV Rack plugin collection.
- Merged S8 Quantizer 2.9.15 and Fire Host 2.2.4 into one plugin package.
- Preserved module slugs `S8` and `FireHost`.
- Namespaced module resources under `res/S8/` and `res/FireHost/`.
- Registered both modules from one `XtremeSounds` plugin entry point.
- Standardized the combined collection on GPL-3.0-or-later for open-source VCV Library submission.
- Added four-platform GitHub Actions packaging for Windows x64, Linux x64, macOS x64, and macOS ARM64.

Historical module-specific development before this combined collection is documented in the source releases from which this repository was assembled: S8 2.9.15 and Fire Host 2.2.4.
