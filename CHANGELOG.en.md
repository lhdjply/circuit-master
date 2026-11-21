# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version
numbering follows [Semantic Versioning](https://semver.org/).
The single source of truth for the version is the `VERSION` file at the
repository root (same form as the tag, e.g. `v0.4.0`).

## [Unreleased]

### Added

- **Waveform monitor defaults to F6**: F5 runs, F6 shows the traces; Esc (cancel
  wiring / clear selection) is in the shortcut table too and can be rebound.
- **Customisable shortcuts**: a new Shortcuts tab in Edit → Settings lets you rebind
  every command, clear a shortcut with the × at the end of its row, or restore the
  defaults in one click.
  Two commands on the same key combination are refused with a message. The choices
  are saved with your user settings and survive a restart, and the key names shown
  in the status bar, the startup tip and Quick Start follow them instead of being
  hard-coded.

## [v0.5.0] - 2026-11-21

This release adds a measurement probe and a signal generator, and gives the
sources and probes consistent + / − polarity marks.

### Added

- **Measurement probe**: drop it on a wire and a pale-yellow readout box appears
  (like Multisim's probe) — voltage and current peak-to-peak / RMS / DC for that
  segment, plus the frequency when a period can be estimated. The arrow runs
  along the wire, current is positive along it and rotating 180° flips the sign;
  the readings refresh while the live simulation runs.
- **Signal generator**: a new two-terminal part under Sources with **sine /
  square (adjustable duty) / triangle / sawtooth** waveforms, plus peak amplitude,
  frequency, phase and DC offset. It is drawn as a rounded instrument body, easy
  to tell apart from the round voltage source.

### Changed

- **Voltage source symbols**: DC is now drawn as a battery (long plate positive,
  short plate negative) while sine and pulse keep their waveform inside the
  circle; all three show + / −, in the same place and size as the multimeter.
- **Current source symbol**: the arrow inside the circle now runs along the wire
  and points in the reference direction of the current.
- **Current probe**: now shows + / − polarity, in the multimeter's position.

### Fixed

- **Editing shortcuts did nothing on a new tab**: they were only attached to the
  first tab's view.
- **Dragging a part dragged unrelated wires along**: only wires attached at the
  moment of the press follow now.

## [v0.4.1] - 2026-11-20

A settings window, light and dark themes, a Chinese/English interface, and wider
oscilloscope ranges.

### Added

- **Settings window**: Edit → Settings (Ctrl+,) collects the theme, grid size and
  interface language in one place. All three apply when you press OK; Cancel
  changes nothing.
- **Light and dark themes**: Follow System / Light / Dark. The canvas, symbols,
  wires, waveform charts, dialogs and hint text each get their own dark values,
  and switching applies immediately. Toolbar icons keep their original colours
  in light mode and are hue-preserving brightened in dark mode.
- **Interface language: Chinese / English**: switching prompts for a restart.
  Qt's own widget strings (line-edit context menus and so on), the built-in
  example names, the changelog and the update window all follow the setting.
- **Adjustable grid size**: 1 / 2 / 5 / 10 px (10 by default). It sets the canvas
  grid, the snap step for parts and wires, and the netlist's "are these two pins
  connected" test.
- **Scope ranges up to 1000 V and 10 A**: previously capped at 100 V/div and
  1 A/div, which flattened the rectified mains rail (around 300 V), the flyback
  drain spike (above 500 V) and the primary peak current (1–2 A) into flat tops.

### Changed

- **The live scope takes its step size the way a real oscilloscope does**: the
  time base only decides how wide a window you look at, while the integration
  step follows the circuit's dynamics. Coarsening it with the time base used to
  skip thousands of switching cycles at 1 s/div, collapsing a flyback output from
  ±300 V to 0; a steady 30 V / 12 V envelope is visible at 1 s/div now.
- **Each document owns its own live scope**: background sheets keep advancing,
  "sheet A simulating, sheet B not" are independent states, and the engine stops
  only when the document is closed.
- **Library and example menus lost their redundant suffixes**: no more
  "Resistor R" or "RC charge and discharge (transient)".

### Fixed

- **Follow System was sampled only at startup**, so switching the desktop theme
  mid-session left a dark canvas inside a light interface. The system colour
  scheme is now watched, with the system palette as a fallback when the platform
  does not report one.
- **The .deb only installed on Deepin**: packages are now built on Debian 12 and
  install on Debian 12/13, Ubuntu 24.04+, Linux Mint 22 and Kali.
- **Switching tabs interrupted the simulation**: see "each document owns its own
  live scope" above.

### Development

- UI strings are English source plus `.ts` catalogues (421 entries);
  `scripts/update-translations.py` extracts them and checks for untranslated
  ones. This file has a Chinese counterpart, `CHANGELOG.md`.

## [v0.4.0] - 2026-11-19

Logic gates, a momentary push button, canvas layout tools, the license, and the
start of the Android port.

### Added

- **Logic gates**: AND / OR / NOT / XOR / XNOR, under the "Digital logic"
  category. Inputs are high-impedance; the output behaves as "level + 100 Ω
  series resistance" and can drive LEDs, MOSFETs and transistors directly. The
  only parameter is the output high level (3.3 V by default).
- **Push button SB**: a momentary button — it acts while held during simulation
  and resets on release, normally open or normally closed. Plain switches still
  toggle on click, and a left click flips them while the simulation runs.
- **Align and distribute**: left / centre horizontally / right / top / centre
  vertically / bottom, plus horizontal and vertical distribution. Alignment uses
  the symbol body, so label text length makes no difference; attached wires move
  along and the whole thing is undoable.
- **Built-in changelog**: `CHANGELOG.md` is compiled into the program and opened
  from the About window, offline.
- **License added**: the project is released under GPL-3.0-or-later, with the
  full `LICENSE` text at the repository root.

### Android (experimental)

The code is on the mainline, but **the installer is only published in continuous
builds (pre-releases); tagged releases do not include it** — touch interaction
is still being polished.

- Builds an APK: CMake packaging (package `io.github.lhdjply.circuitmaster`,
  versionCode derived from `VERSION`), verified by CI
- Mobile shell: menu bar and status bar hidden, actions moved to an icon-only
  bar at the bottom; the five menus live in a left drawer opened by ☰
- Long press = right click: rotate / flip / clipboard / edit parameters
- Pinch to zoom; dialogs go full screen and the category filter became a row of
  buttons — working around Qt 6.7.3's broken popup handling on Android
  (QTBUG-135253 / QTBUG-137407)
- All of it is wrapped in `#if defined(Q_OS_ANDROID)`; the desktop build never
  compiles it

### Changed

- **The multimeter window only opens while the live simulation runs**: without a
  running simulation every reading is a dash.
- **The waveform monitor lists probes only (TP / IP)**: multimeter readings go to
  their own seven-segment window.
- **Ground rules relaxed**: the reference node is picked as GND → any ground
  kind → multimeter negative terminal, so a schematic with only AGND, or with a
  single multimeter, simulates too.

### Fixed

- **Redundant Library / Properties buttons on the desktop toolbar**: the panel
  toggles already exist under View; the two icon-less text buttons tucked after
  "Fit to Window" were just noise.

## [v0.3.3] - 2026-11-18

Editing got a lot better: multiple tabs in one window, opening a second `.csm`
reuses the running instance, the live scope no longer stutters or jumps ranges,
and the message boxes got a consistent look.

### Added

- **Multiple tabs**: one circuit per tab, each with its own canvas, undo history,
  analysis settings and last simulation result. New / Open / Load example all
  open a tab; Ctrl+W closes, Ctrl+Tab switches, tabs can be reordered and show a
  dirty marker. Closing the window asks about unsaved documents one by one.
- **Single instance**: double-clicking a `.csm` again does not start a second
  window; the file is handed to the running process and opened as a new tab.

### Improved

- **Live scope no longer stutters**: the step used to be dictated by the
  steepest pulse edge (a 1 µs edge demanded 200 ns), so a 5 s window needed 25
  million steps. It is now constrained by pulse width instead — the same circuit
  went from 200 ns to 10 ms and fills the window within a second.
- **Live scope ranges behave**: a manually chosen volt/div is no longer
  overridden, changing time/div no longer re-picks the voltage range, and Auto
  Scale now adapts the time base too (fitting a few signal periods) and lives in
  the live section.
- **Message boxes restyled**: circular accent-coloured badges instead of stock
  icons, system-theme button icons removed, consistent radius and spacing. The
  About window was rebuilt with an icon, a version pill and a short blurb.

### Fixed

- The waveform monitor's vertical axis could clip peaks (-891…587 mV squeezed
  into -1000…500 mV).
- The property panel's range combo was widened by long entries until the right
  half of the text was cut off.

## [v0.3.2] - 2026-11-17

A batch of "that looks wrong" fixes, and the SPICE netlist export was removed.

### Fixed

- A 1 kΩ resistance range displayed as 100.00 Ω (rounding shifted the decimal
  point by a factor of ten).
- Clicks landed in the wrong place after rotating a component — test points
  especially — and label hit areas were tightened to the text itself.
- Net labels rotated in the opposite direction to every other component.
- The property panel's empty-state hint was off-centre, with a stray scrollbar on
  the right.
- The multimeter window ignored range changes and kept showing the previous
  reading after the circuit was edited.

### Changed

- Removed "Export SPICE netlist" and the code behind it (previously Ctrl+E).

## [v0.3.1] - 2026-11-17

Update checking and downloading got fixed: mirrors are ranked by measured
download speed, a stalled download switches mirrors, and the UI shows the
current mirror and live speed.

### Fixed

- **Downloads hanging**: a mirror was only abandoned when the request finished
  with an error, so a mirror that connected but barely produced data meant
  minutes of waiting. Now 10 s without data, or under 200 KB in 8 s, switches to
  the next mirror.
- Net-label names appeared intermittently in reports (unordered QHash iteration);
  the report now shows them reliably.

### Improved

- Mirrors are ordered by measured **package download** speed: v4.gh-proxy.org →
  ghfast.top → gh-proxy.org → cdn.gh-proxy.org. The perpetually timing-out
  ghproxy.net and axisnow were dropped.
- The download view shows the **current mirror and live speed** (downloaded /
  total, time remaining), and the address can be selected and copied.

## [v0.3.0] - 2026-11-17

Multimeters and net labels, movable labels, and a reorganised component library.

### Added

- **Multimeter XMM**: five ranges (volts / amps / ohms / AC volts / AC amps); a
  seven-segment readout window opens on click during simulation, 3½ to 6½ digits,
  units shown with engineering prefixes.
- **Net label NET**: labels with the same name become one net, connecting two
  places without drawing a wire.
- **macOS support**: build, tests and packaging in CI, shipping a `.dmg`.

### Improved

- Reference designators and values can be dragged independently and snap back on
  rotate / flip.
- The library is grouped: Basic / Semiconductors / Analog ICs / Sources /
  Switches and Grounds / Measurement.

## [v0.2.2] - 2026-11-17

Three new part types (ground kinds, transformers, a switching controller),
free-angle rotation, manual junctions, and a complete flyback example.

### Added

- **Ground kinds**: GND / AGND / DGND / PGND / EARTH, each kind forming its own
  net, all five isolated from each other; the library still has a single Ground
  entry and the kind is chosen in the property panel.
- **Voltage probes can pick a reference ground**: choosing AGND shows the
  differential V(probe, reference).
- **Transformer**: single winding or primary + auxiliary + secondary, turns ratio
  written as N1:N2(:N3), dot polarity can be inverted.
- **OB2362A switching controller**: 65 kHz peak-current mode, with FB bias
  (starts at about 4.5 V open circuit), 17 V / 7.3 V VCC undervoltage and 28 V
  overvoltage clamp.

### Improved

- The property panel accepts **any rotation angle** (15°/45°/60°); pins snap to a
  10 px grid so angled parts still connect.
- Crossing wires are not connected by default and no junction dot is drawn; the
  context menu adds a junction explicitly, and automatic merging only happens
  where there is no crossing.
- A "Snap wires to grid" entry in the Edit menu fixes offsets left by pasting in
  older versions.
- The example became "220 V to 12 V flyback supply (OB2362)": rectifier, RCD
  snubber, peak-current mode, TL431 + optocoupler feedback, 12 V output.
- Update checking now offers "Download and install"; Qt's own widgets (context
  menus, standard buttons) are localised.

### Changed

- Sine source amplitude is now the **RMS** value: 220 V means mains 220 V. Older
  projects open with the same waveforms.

### Fixed

- Pasting snapped components to the grid but only translated wires, leaving
  crooked connections; the whole block now moves by whole 10 px steps.
- The auxiliary winding pins of a three-winding transformer sat off-grid, so no
  wire could be drawn to them.
- A step coarser than the switching dynamics (1 ms at 65 kHz, say) produced no
  log warning at all — all you saw was "the output is 0 V".
- On Windows, a package missing Qt's translation files left context menus in
  English without any build-time error.

## [v0.2.1] - 2026-11-16

A patch release: TL431 added, file association wired up, and a few rough edges
in the operating-point analysis and ground symbols fixed.

### Added

- **TL431 adjustable reference** (REF / anode / cathode): once REF exceeds the
  reference voltage (2.495 V by default) the cathode starts sinking current and
  holds Vka at the set value. REF tied to the cathode gives a 2.5 V reference,
  and with a divider it becomes an adjustable regulator; the reference voltage
  is edited in the property panel.
- **Example "TL431 5 V regulator"**: 12 V through a 470 Ω limiter and a 10 k/10 k
  divider, regulated to 5 V with a 1 kΩ load and probes. Open it and press F5 to
  see Vout = 4.99 V.
- **`.csm` file association**: double-clicking a `.csm` opens it after installing
  the Windows package (also listed under "Open with"); Linux registers the
  `application/x-circuit-master` MIME type; `CircuitMaster circuit.csm` works
  from the command line.

### Improved

- Rotation is now **90° counter-clockwise**, matching common EDA tools.
- Ground symbols can be rotated, with the label placed outside the symbol:
  horizontal when pointing down or up, vertical when pointing left or right — it
  never covers a wire and is never upside down.
- The operating-point report no longer interrupts with a dialog: the result goes
  to the log, the static waveform page shows the full report, and "Run again"
  refreshes in place.

### Fixed

- After an operating-point analysis the button stayed on "Run Simulation",
  looking as if clicking it stopped things. The cause was `applyProbeSignals`
  returning false when there is no time series, which was read as "the circuit
  has no probes" and prevented even the continuous simulation from starting.
- Desktop and Start-menu shortcuts and the uninstall entry were named
  "Circuit Master" in English; they are now consistently "电路仿真大师" (the
  install directory keeps an ASCII path).

## [v0.2.0] - 2026-11-16

Drawing circuits got much more complete: usable semiconductors, editable wires,
and update checking that can actually download the new version.

### Added

- **Transistors** (NPN / PNP), **MOSFETs** (N / P channel), **IGBTs**: drag them
  out of the Semiconductors category; parameters (β, Vth, K, Vf) are edited in the
  property panel.
- **LEDs**: they really light up when the simulation runs — brightness follows the
  forward current, and both the static and live views update.
- **Optocoupler**: four pins (LED anode/cathode, output collector/emitter);
  defaults come from LTV-817S-TA1-C (Vf = 1.2 V, CTR = 3.0, Vce(sat) = 0.2 V).
  High CTR saturates properly instead of pushing the emitter above the
  collector.
- **Polarized capacitors**: when enabled they are drawn as electrolytics (flat
  plus curved plate with a + sign). More than 1 V of reverse voltage glows red
  and then blows up into a mushroom cloud that stays until the simulation ends.
- **Waveform monitor window**: static waveforms and the live scope are two tabs
  of one independent window that can be dragged to another screen. Running a
  simulation no longer pops it open; click Waveform Monitor, and closing the
  window leaves the simulation running.
- **Update checking can download**: the right package is chosen for your system
  and install style (deb for a deb install, tar.gz for an unpacked portable
  build, and likewise on Windows); pick a save path and download.

### Improved

- A single toolbar button runs and stops the simulation, switching its label to
  "Stop Simulation".
- Editing the circuit during a live simulation (flipping switches, changing
  values) takes effect immediately without resetting the time axis.
- Wire editing is much stronger: wires follow a dragged component, keep
  orthogonal, can be dragged by individual endpoints, merge when they meet
  end-to-end, and only the clicked segment is highlighted.
- Added Cut (Ctrl+X); pasting lands at the mouse position instead of a fixed
  offset.
- A category combo at the top of the library (Basic / Semiconductors / Sources /
  Switches and Grounds / Probes).
- Rotation is **counter-clockwise** (Space or the rotate menu), matching common
  EDA tools.
- Space is the rotate shortcut, so repeated presses keep rotating the selection.
- Analysis settings (kind, stop time, step, sweep range) moved into the static
  waveform page; press "Run again" after changing them. The scope's time/div and
  volt/div are saved with the project.

### Fixed

- The optocoupler input used the wrong clamp model, so the input current was tens
  of µA and the downstream LED never lit.
- The optocoupler output had no saturation, computing 10 V at high CTR (above the
  supply).
- Static waveforms collapsed to a flat line hugging the bottom in purely DC
  circuits.
- Wires could not be selected or deleted with Del, and flashed red and vanished.

## [v0.1.0] - 2026-11-14

The first release. The goal was simple: a usable Multisim replacement on Linux —
draw a schematic, press run, look at the waveforms.

### What it does

- **Schematic capture**: drag parts from the library on the left; wires snap to
  pins. Parts can be rotated, flipped horizontally or vertically, copied and
  pasted, and dragged as a group after a rubber-band selection; `Ctrl+Z` undoes
  and `Ctrl+Shift+Z` redoes.
- **Parts**: resistors, capacitors, inductors, diodes, op-amps, switches, voltage
  and current sources, grounds, plus voltage and current probes.
- **Values**: type `10kΩ`, `4.7µF`, `5V`, `1kHz`; without a unit the component
  default applies, and a wrong unit is flagged in red immediately.
- **Analyses**: DC operating point, transient, AC sweep (Bode plot).
- **Waveforms**: a static plot, or a scrolling oscilloscope; time/div, volt/div
  and amp/div are all adjustable. Only the probes you placed are plotted.
- **Files**: projects are saved as `.csm` (JSON, editable by hand) together with
  the analysis settings; SPICE netlists can be exported for cross-checking with
  ngspice.
- **Getting started**: Simulate → Example Circuits has six ready-made examples,
  including RC charging, diode rectification, RLC oscillation and an inverting
  op-amp stage.
- **Update check**: Help → Check for Updates, and the program also looks once at
  startup.

### Fixed

- Wires drawn as a T junction (one end landing in the middle of another wire)
  were sometimes not considered connected, leaving an op-amp feedback path
  floating and the simulation failing.
- The scope showed a flat 0 V line at some time bases.
- After a rubber-band selection, dragging by a wire did not move the group.
- Closing the window right after editing a component value occasionally crashed.
- Toolbar icons appeared blank on some systems.

### Known limitations

- No transistors, MOSFETs, controlled sources or transformers yet.
- Transient analysis uses a fixed step; adaptive stepping is not available.
- A wire cannot be re-shaped by dragging a single endpoint afterwards; delete and
  redraw it.

### Downloads

- **Debian / Ubuntu / Deepin**: download the `.deb` and run
  `sudo apt install ./circuit-master_0.1.0_*.deb`
- **Other Linux**: unpack the `.tar.gz` and run `circuit-master` inside
- **Windows**: run `*-setup.exe`, or unpack the `.zip` and run
  `CircuitMaster.exe`
