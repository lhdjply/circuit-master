# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the version
numbering follows [Semantic Versioning](https://semver.org/).
The single source of truth for the version is the `VERSION` file at the
repository root (same form as the tag, e.g. `v0.4.0`).

## [Unreleased]

### Added

- **Update packages are verified with SHA-256**: every package's hash goes into the update
  manifest (`assets[].sha256` in `latest.json`) — there is no separate `hashes.sha256`
  file to ignore. The client hashes the download as it streams, then compares before
  installing: on a mismatch the file is discarded and the next mirror is tried, and if all
  of them disagree it reports both the expected and the actual digest.
  Nothing was checked before — a mirror could have served any file at all. Releases that
  predate this still install, but the UI says the package was not verified.
- **Paper, orientation and scale are adjustable when printing**: "Paper" is a dropdown
  (A5 … A1, defaulting to the sheet's own setting), "Orientation" switches landscape /
  portrait and "Scale" offers fit-to-sheet, actual size (1:1) or a custom percentage —
  any change redraws the preview immediately. When the paper matches the sheet,
  fit-to-sheet *is* 1:1 (both render pixel-for-pixel the same); when the paper is
  smaller, fit-to-sheet squeezes the drawing onto one page while 1:1 keeps its real size
  and spills across pages.
- **Colour or black-and-white**: two radio buttons next to "Orientation". In black and
  white the sheet is converted to grayscale before drawing rather than relying on the
  device, so the preview and the printed job cannot diverge.
- **Anything that does not fit spills onto more pages**: printing an A3 drawing 1:1 onto
  A4 tiles it across pages; the preview gained Previous / Next buttons and a "Page N / M"
  readout, and the printed job has the same pages.
- A5 was added to the page setup as well.
- **Printing and print preview**: the File menu gained "Print Preview…" (Ctrl+Alt+P) and
  "Print…" (Ctrl+P). Paper size and orientation follow the page setup, and the printed area matches
  "Fit to Sheet"; the preview *is* the printed output, so nothing shifts between the two.
- **PDF export**: "Save As" gained a PDF file type. It exports the **whole sheet** — the
  PDF page takes the size from the page setup (A4 / A3 / A2 / A1 / custom, orientation
  included) and the drawing fills it, i.e. exactly what "Fit to Sheet" shows. The grid is
  not printed and selection highlights are left out.
- **Page setup**: right-click on empty canvas → "Page Setup…", or *File → Page Setup*
  (default **Ctrl+Shift+P**, remappable in Settings), picks a paper size (A4, A3,
  A2, A1 or custom) and an orientation (landscape or portrait); a custom size takes
  millimetres. The canvas *is* that sheet: the grid is drawn inside it and its edge is a
  **double solid frame** — outer line 5 mm in from the paper edge, inner line 10 mm,
  as on an engineering drawing — with no white fill and no size caption. Changing the paper
  re-centres the view. A new sheet defaults to **A4 landscape**; the paper is stored in
  the `.csm` under `"page"`, and files without that section open on A4. All **eight
  examples had their drawing re-centred on the sheet** (labels included, nothing sticking
  out); the flyback drawing is larger than A4, so that one uses A3 landscape. When a
  drawing is larger than the sheet, the canvas grows to cover it so the part outside the
  sheet stays reachable.
- **Fit to Sheet**: the toolbar gained a button right after "Fit to Window" (also in the
  View menu, default **Ctrl+Shift+0**) that zooms so the whole sheet fills the view — handy
  for seeing where the frame sits on a large drawing. "Fit to Window" still targets the
  drawing itself.
- **Right-drag pans the canvas**: hold the right button and the drawing moves with the
  cursor — the cursor becomes a hand on press and returns to an arrow on release, the
  same feel as middle-drag. A right click without moving still opens the usual context
  menu (and in wire mode the right button still finishes the wire).
- **Zener diode**: the Semiconductors category gained a DZ part. Forward it behaves like
  an ordinary diode (with an adjustable drop); reverse-biased it conducts at its
  breakdown voltage and clamps there. The property panel takes the breakdown voltage Vz
  (5.1 V by default). Measured with Vz = 5.1 V and a 1 kΩ series resistor: a 20 V supply
  clamps at 5.09 V, a 3 V supply (below breakdown) passes 3.00 V unchanged, and the
  3.3 V / 12 V parts line up too.

### Changed

- **A diode's forward drop is now adjustable**: the property panel gained a
  "Forward drop Vf" row (0.7 V by default) and the canvas label shows it too (it used
  to be a hard-coded `1N4148`). The model calibrates the saturation current from the
  drop at 20 mA and keeps the 1N4148 emission coefficient of 1.752 — changing the drop
  shifts the whole I-V curve without changing its slope. Measured on a half-wave
  rectifier: Vf of 0.3 / 0.7 / 1.2 / 3.0 V gives output peaks of
  4.76 / 4.37 / 3.87 / 2.10 V.

### Fixed

- **A failed crash-recovery copy was reported nowhere**: the timer writes a recovery copy
  every two minutes, but the return value of `Autosave::write()` was thrown away — with a
  full disk, a read-only home directory or an exhausted quota no copy existed at all while
  the UI still claimed crash recovery was on, and after a real crash the only message was
  "nothing to recover", indistinguishable from "nothing was ever saved". Failures now go to
  the log with their reason (the timer runs every few minutes, so it reports once when the
  state flips from good to bad). `Autosave::write()` also gained an `error` out-parameter
  which is cleared on success — reusing one string used to keep the previous message.
- **Loading a file no longer silently changes the circuit**: `.csm` was read with no
  validation at all, and three cases altered the circuit without a word —
  duplicate component ids made the netlist's per-id pin table overwrite one entry, so two
  parts ended up on the same node (a 5 V divider measured 0 V instead of 2.5 V); an
  unrecognised type tag quietly became a "0 Ω resistor"; and a non-finite number put inf
  into the matrix, so the solve returned nan and the user only saw "did not converge".
  Duplicate ids are now renumbered, unknown types are skipped, non-finite values fall back
  to their defaults, and each case leaves a note in the log. Ground / multimeter / gate
  modes are clamped to their valid range too (they used to be cast straight from any
  integer, leaving switches without a matching branch).
- **Editing a parameter mid-run reset the capacitor voltage to the DC operating point**:
  while the live scope was running, changing a resistor or capacitor value re-solved the
  DC operating point and wrote it back into the capacitor voltage, so a half-charged
  capacitor jumped straight to its steady-state value (RC with a 5 V supply and τ = 1 ms:
  3.17 V → 5 V at t = 1 ms). Now, when the topology is unchanged and only values moved,
  the capacitor voltages and inductor currents are kept as they were and the next step
  continues with the new values; only a real topology change restarts from the operating
  point. Guarded by liveParameterChangeKeepsCapacitorVoltage (the difference across the
  edit must stay under 0.01 V).
- **Page tiling was wrong (a strip went missing between pages, and the two scales
  disagreed when paper matched the sheet)**: the geometry now lives in `planPrint()`
  with unit tests — with paper equal to the sheet, fit-to-sheet and actual size produce
  the same plan (one page, same position, same scale); with smaller paper the drawing is
  tiled by the **printable area** (tiling by the whole page loses the 4.2 mm that falls
  into the printer margin, so neighbouring pages no longer line up). Each page is also
  clipped explicitly, so pages no longer bleed into one another in the preview.
- **The library background differed from the property panel**: the list used Base
  (pure white) while the panel next to it used the window colour, so the two looked like
  different layers. The library now uses the window colour too, with accent-tinted hover
  and selection, and follows theme changes.
- **The transformer's property panel was cut off**: the two "Windings" entries carried
  "(1 primary + 1 secondary)" and "(6 pins)", which pushed the whole panel wider than the
  dock — the right-hand fields were clipped and a horizontal scrollbar appeared. The
  entries are now "Single winding" and "Primary + auxiliary + secondary", and every combo
  in the panel is sized by minimum contents length so a long entry can no longer widen it.

## [v0.6.0] - 2026-11-27

This release turns the op-amp from an ideal part into a usable model — saturation,
bandwidth, slew rate, offset, bias current and both port resistances are adjustable —
and halves the size of `.csm` files, plus a batch of transformer, optocoupler and
live-scope fixes.

### Added

- **The op-amp is no longer ideal**: the property panel gained eight parameters — output
  swing high / low, gain-bandwidth product, slew rate, input offset, input bias current,
  input resistance and output resistance. Defaults match a general-purpose part; leave
  them alone and you get the old ideal model back. Without these, comparators,
  oscillators, clipping and loop stability cannot be simulated correctly.
- **Save As can write a PNG**: it renders the whole schematic (not just the viewport)
  at 2× pixels with the grid background.

### Changed

- **`.csm` only stores the parameters a component actually uses**: every element used to
  record all forty-odd fields (a resistor carried the op-amp's offset voltage), now only
  its own. Zero rotation, no mirroring and untouched labels are left out too. The sample
  file went from 12.9 KB to 3.9 KB; old files still load.

### Fixed

- **Op-amp outputs were wrong at the limit**: a follower with a ±7 V limit produced
  thousands of volts, and a tight limit could leave the output stuck on the rail. It now
  sits firmly on the rail and leaves saturation in a single step.
- **Transformer currents read as 0**: the report and the waveforms showed `I(T1)` as
  zero even though the circuit solved correctly. Multi-winding transformers now get one
  current per winding.
- **Live-scope test points read 0 V**: in circuits with a transformer, every test point
  after it was shifted.
- **The optocoupler's AC analysis disagreed with the transient**: flyback loop sweeps
  came out systematically low; both now use the same model.
- **Live scope**: clicking Select All / Clear crashed the app; the test-point list gained
  checkboxes; the time base is picked from the fastest signal in the circuit (a 1 MHz
  circuit no longer packs into a solid block of ink); and switching the time base no
  longer aborts the run.
- **Double-clicking a broken `.csm` no longer closes the app**: it shows the reason and
  keeps the window open.
- **Saving is safer**: a failed write leaves the original file untouched (it used to be
  emptied first).
- **Dialogs no longer take an extra taskbar slot**, and the update checker uses the same
  self-drawn title bar as everything else.

## [v0.5.2] - 2026-11-26

Every window now draws its own title bar, plus a couple of small fixes.

### Changed

- **Custom title bar**: the main window and every dialog (save-changes prompt, Quick
  Start, About, Changelog, settings, waveform monitor, multimeter) drop the system title
  bar. On the main window the top row carries the app icon, the menus and the
  minimise / maximise / close buttons, giving the content about 34 px more height.
  The **whole top row can be dragged** and double-clicking it maximises or restores;
  windows that stay resizable get eight edge / corner drag zones, while the multimeter
  is fixed-size with no maximise. The trade-off is losing the system window menu and
  edge snapping.

### Fixed

- **A circuit opened after discarding a tab would not simulate**: closing a tab with
  Discard tears down that document's live scope, and the example loaded next reuses the
  same document without rebinding it — the live simulation was skipped, so the button
  stayed on Run and the waveform monitor stayed greyed out.
- **Invisible input borders in the dark theme**: the numeric fields in the property panel
  had no visible edge, because Fusion draws frames by darkening the background. QLineEdit
  now gets an explicit hairline; combo boxes and the rest are left alone.

## [v0.5.1] - 2026-11-21

This release adds autosave and crash recovery, customisable shortcuts, and an icon
for every menu command.

### Added

- **Autosave and crash recovery**: two independent switches, each with its own
  interval, under Settings → Saving. Autosave (default 10 minutes) writes unsaved
  changes **back to the `.csm`**, exactly like pressing Save; crash recovery
  (default 2 minutes) keeps a separate copy covering untitled circuits and
  whatever changed between two saves, and offers to restore it on the next start.
- **Customisable shortcuts**: rebind any command under Settings → Shortcuts —
  typing a combination another command already uses warns you and snaps the row
  back; the × at the end of a row clears it and Restore Defaults brings everything
  back. The waveform monitor defaults to **F6** and Esc (cancel wiring / clear
  selection) is in the table too; the key names shown in the status bar, the
  startup tip and Quick Start follow them.
- **Every menu command has an icon**: the five menus, the Align and Distribute and
  Example Circuits submenus, and the three panel toggles — 46 entries in total,
  recoloured automatically in the dark theme.

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
