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

## B. Voice

**Status:** approved by the owner (13 Nov 2026), including the amendment of NOTE-05. The owner's reading of NOTE-05:
"no speech recognition of its own" means do not invent recognition or add a speech library; use the phone's facilities.

### What it is

- A microphone inside the note box on the first line, at the trailing edge (the left edge in a right-to-left layout).
  One tap starts dictation at the cursor, the microphone shows it is listening, the words appear as they are heard (each
  guess replaces the one before it), and a second tap, **60 seconds of silence**, the app going to the background, or an
  error ends it. The text is ordinary text afterwards.
- There is no Settings switch: nothing listens until the person taps the microphone, and where the phone cannot
  recognise speech on the phone for the person's language the microphone is not shown (the owner's wording).
- PRD: NOTE-05 amended, NOTE-07 added.

### What the platforms give us

| Platform | Mechanism | On device? |
|---|---|---|
| iOS | `SFSpeechRecognizer` fed by `AVAudioEngine`, `requiresOnDeviceRecognition = true` | Yes; `supportsOnDeviceRecognition` decides `available()`. Never the server |
| Android 13 and later (API 33) | `SpeechRecognizer.createOnDeviceSpeechRecognizer`, `EXTRA_PREFER_OFFLINE` | Yes; `isOnDeviceRecognitionAvailable` decides `available()` |
| Android 9 to 12 (the app's minimum is 28) | none: the ordinary recogniser may use a server | No: the microphone is not shown. Said here rather than shipping cloud transcription silently |
| Desktop | none | The stub is unavailable |

The platform code is in `libs/ios_bridge/src/platform/<os>/dictation.*` behind `Dictation` (`dictation.hpp`), with no
`#if`. The portable part (`QmlBridge`: one run at a time, restart when a recogniser ends a run by itself, the 60 second
silence, the 1.5 second wait for the last words after the second tap, stop on the background) is tested with a fake.

### Permissions and privacy

- iOS: `NSMicrophoneUsageDescription` and `NSSpeechRecognitionUsageDescription` in `Info.plist.in`; `check_ios_plist.py`
  and `verify_app.sh` now require these two and refuse any other usage text. Android: `RECORD_AUDIO`, the only
  permission `check_android_package.py` allows. Both are asked for at the first tap, never at launch.
- Refusal: a sentence under the box ("Voice input is turned off for this app..."); typing is unaffected, the microphone
  stays so the person can try again after changing the setting.
- `docs/privacy.md`, `docs/plan/stage-11.md` (App Privacy and Data safety answers), the user guide and the changelog are
  updated. App Privacy stays "Data Not Collected": nothing leaves the phone.

### Tasks and tests

| # | Task | Tests |
|---|---|---|
| B1 | `Dictation` interface, desktop stub, iOS and Android implementations, `PlatformServices` | `unit`: platform layout check; CI builds iOS and Android |
| B2 | `QmlBridge`: `dictationAvailable`, `dictating`, `startDictation`, `stopDictation`, signals | `unit` `dictation.*` (12 cases): unavailable, words, stop with last words, no answer after stop, restart, no spin, silence, refusals, stale callbacks, background |
| B3 | `NoteComposer` microphone, text insertion, message; `LogScreen` wiring | `ui` `composer.mic`, `composer.dictation_text`, `composer.dictation_message`, RTL, cross-cutting; `log.dictation` |
| B4 | Package checks: plist, manifest | `unit` Python: `test_ios_plist.py`, `test_android_package.py` |
| B5 | Docs: PRD, privacy, stage 11, guide, changelog | `check_help_links`, precheck |

## Not run on a device

Nothing in A or B has run on an iPhone, iPad or Android phone. A is checked on the desktop rig only. B's platform code
is built in CI only (no Mac on the developer machine); the microphone permission prompts, the recogniser's accuracy, the
language check and the on-device guarantee need a real iPhone and a real Android 13+ phone.
