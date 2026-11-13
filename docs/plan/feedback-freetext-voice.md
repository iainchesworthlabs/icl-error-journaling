# Feedback: a bigger free-text box, and optional voice

Task breakdown and test list for the tester feedback "the free-text box is too small". Requirement IDs refer to the
[PRD](../prd.md). Two pull requests: **A** (the box) first, **B** (voice) second.

**Status:** draft for owner review.

## Decisions taken from the owner's words

- The box must be multi-line and hold more text (testers).
- The board must not be squashed or pushed down to make room.
- Voice is optional, off by default, and uses what the phone already has: a microphone icon inside the box, at its
  trailing edge (the left edge in a right-to-left layout), that starts the platform's own dictation.

## Findings

- The note limit is already 10,000 code points (`MAX_NOTE_CODE_POINTS`, decision 7). No limit changes and no format
  change are needed. The journal stores the text as written, so line breaks are kept in storage, the draft and undo.
- **Export rule 9** (PRD, Export format; `export_renderer.cpp`; test tag `[rule9]`) collapses line breaks in a note to
  one space so that every entry is one line, which the downstream ticket indexer relies on. This plan **keeps rule 9**:
  the shared text stays one line per entry. The on-screen log, the Review screen and the edit sheet show the line
  breaks. The owner confirmed this (option A). Carrying the line breaks into the export (continuation lines indented) stays
  an owner decision: it needs a PRD amendment, new golden files and a check against the indexer.
- NOTE-05 says "The app adds no speech recognition of its own" and NOTE-01 says Return adds the note. Part A changes
  NOTE-01 (Return is a new line, Add commits); part B amends NOTE-05 (see below).

## A. The box

### Design: collapsed at rest, opens over the board while typing

A tall box that is always there would shrink the board (`roomBelowComposer`). Instead:

- **At rest** the box is today's one-line height. The layout under it does not change, so nothing moves.
- **When it has focus** it opens downwards, over the board, to the height of the text, up to 5 lines, then scrolls
  inside. The keyboard already covers most of the board then. The card's layout height stays that of the collapsed box,
  so the tiles do not jump. It folds back when focus leaves.
- A draft with line breaks shows its first line at rest; the box is otherwise unchanged (chips, Add button, draft
  binding, skeleton, text scale).
- Return inserts a line break. Add commits (NOTE-01 amended). The Add button stays level with the first line.
- Text scale: line height follows the font, so the 5-line cap scales with Dynamic Type; the open box never exceeds half
  of the screen height.

### Tasks

| # | Task |
|---|---|
| A1 | `NoteComposer.qml`: replace `TextField` by a `TextArea` in a `Flickable`; collapsed height; open height; `z` above the board while open; same `noteInput` name, `draft` binding rules and signals |
| A2 | Return gives a line break; Add commits; `canSubmit` unchanged |
| A3 | Check the keyboard work (`ios-keyboard-layout`) still keeps the field fully in view when it opens over the board |
| A4 | Docs: PRD NOTE-01, user guide page for notes, changelog fragment |

### Tests (label `ui` unless stated)

| Test | Checks |
|---|---|
| `composer.multiline` | typing Return keeps a line break in the field, the draft and the `textEdited` text; Return does not submit |
| `composer.grow` | at rest the card height equals the single-line height; with focus the field is taller for 3 lines and capped at 5; the card (layout) height does not change |
| `composer.scroll` | text beyond the cap scrolls inside; the cursor stays visible |
| `composer.collapse` | losing focus folds the box back and keeps the text |
| `composer.add` (updated) | Add button submits text with line breaks intact; Return does not |
| `composer.cross_cutting` (existing) | names, targets, text scale 3.1, dark, Reduce Motion |
| log screen tests (existing, updated) | the board does not move when the box opens |
| `unit`: journal round trip | a note of 10,000 code points with `\n` and `\r\n` survives encode, decode and recovery |
| `unit`: export | a multi-line note still exports as one line (rule 9, unchanged) |

## B. Voice (optional)

**Not to be implemented until the owner has reviewed this section.** Part A lands first as its own pull request.

### Requirement

Add **NOTE-07** and amend **NOTE-05**: the box has a microphone button, off by default (a switch in Settings, "Voice
input"). Tapping it starts the phone's own dictation into the box. The app does not record, keep or send audio.

### What the platforms give us

| Platform | Mechanism | On device? |
|---|---|---|
| iOS | `SFSpeechRecognizer` with `requiresOnDeviceRecognition = true`, started from the button | Yes when the language supports it; if not, the button says dictation is not available offline and does nothing (never a silent fallback to Apple's servers) |
| Android | `SpeechRecognizer.createOnDeviceSpeechRecognizer` (API 31+) with `EXTRA_PREFER_OFFLINE` | Yes on API 31+ with the language pack; below that the button is hidden |
| Desktop rig | none; the button is hidden | n/a |

Open point for the owner: iOS's keyboard dictation key remains available to every user whatever we do; the button adds
a one-tap start and the on-device guarantee. The rule "no `#if` on a platform" holds: a `dictation` interface in
`libs/ios_bridge/include`, one implementation per `src/platform/<os>/`.

### Privacy and permissions

- Permission strings: iOS `NSSpeechRecognitionUsageDescription` and `NSMicrophoneUsageDescription`; Android
  `RECORD_AUDIO`, asked on the first tap, never at launch.
- Denied or restricted: a plain message under the box ("Voice input is off for this app in Settings"), the button stays,
  nothing else changes.
- `docs/privacy.md`, the privacy manifest and the store answers (`docs/plan/stage-11.md`) are updated: microphone use,
  on device, nothing stored or sent.

### Tasks and tests

To be broken down after A merges; the interface is tested with a fake (label `unit`/`ui`): partial results replace the
dictated span, a final result commits it, denial shows the message, the button is hidden when unsupported, off by
default, RTL puts the button on the left.

## Not run on a device

Nothing in A or B has run on an iPhone, iPad or Android phone. A is checked on the desktop rig only. B's platform code
is built in CI only (no Mac on the developer machine).
