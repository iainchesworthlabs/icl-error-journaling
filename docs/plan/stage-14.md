# Stage 14: the interactions board and people

This page is the design, task breakdown and test list for a second board for **interactions**, with an optional list of people that
are typed once and picked later. It is for the owner's review. It writes no production code (the stage gate in
`AGENTS.md`). Requirement IDs refer to the [PRD](../prd.md). The tester's stand-in is **Sam Example**, as everywhere in the repository (NFR-10). The stand-in for a saved name
on this page is **Alex Sample**, a second fictional name the owner approved on 13 Nov 2026 (package I-0 added it to
`AGENTS.md` and `docs/`).

**Source:** primary-user test feedback. Testers need a second board for what they *did* with or around the thing under
test, including, when it applies, the name of a person involved. **Interactions are not only about people:** a tester
may be doing actions against equipment with nobody named. So the board is "Interactions", the name feature is optional
and general (a **person** in the model and in the app's words, "People" in Settings), and nothing in the app says "resident". Both boards write to the same log in time order; a fifth icon in
the bottom navigation switches between them; the entries from both appear interleaved in the log.

**Owner decision, 13 Nov 2026: this moves from v2 into v1.** The app supports up to two boards. The second one is
**optional and off by default**: a tester who does not want it sees the app as it is today (four tabs, one board). A
switch in Settings turns it on; the fifth tab appears; the same switch turns it off again. Turning it off hides the
board and keeps every interaction entry and person (section 4.1). Sections 2 to 4 are written to that decision.

**Exit gate (proposed):**

- the owner approves this page and its open questions;
- packages I-1 to I-9 are merged, each with its labelled tests passing in CI;
- no acknowledged entry is lost in the kill-point tests with interactions in the workload (DS-07);
- the privacy page and the store privacy answers say what the name list holds, where it goes and where it does not;
- the owner has tried the board on a phone, an iPad and a foldable.

## 1. What the documents already say

The owner believed parts of this were described for v2. They are. The work below pulls them forward rather than
redesigning them.

| Where | What it says |
|---|---|
| PRD, scope | "The interactions board (v2, issue #105)" is out of scope for v1. |
| PRD, open items | "Note templates: ... Played song and Visitor interaction are confirmed", "A second board for logging interactions, with its own tab and its own interaction types, set up like the error board (#105). Raised by the tester; deferred on 9 Nov 2026." |
| Implementation plan | 9 Nov 2026: "The interactions board (#105) stays in v2." |
| Issue #105 | A second bingo board with **its own tab, so the tab bar has five tabs: Log errors, Log interactions, Review, History and Settings**. Set up exactly like the error board, with interaction types in place of error types. Free-text notes stay. The "Played song: {value}" style of prompt may no longer be needed. The tester thought interaction lines should **not** carry the `T\|` ticket marker: what matters is that the time of each event is exact, and the rest need only be enough to recall what happened. |
| Issue #105, open questions | How the user switches quickly (is a tab enough?); the export format of interaction lines and whether they count in the totals line; whether the setup file gets an `interactionTypes` list next to the error types. |
| PRD, export rules | Notes carry no ticket marker (rule 4) and render as `<time> (Free Text) <text>`. Templated notes render through the template's line (NOTE-04). |

**New in this feedback, and not in v2:** people (typed once, picked later), a name slot in an interaction's line,
and the privacy handling of those names. The rest of the page designs those, and settles the open questions of #105 as
recommendations.

**What exists in the code today** (read for this page, not changed): `entry.kind` is a `CHECK` over `error`, `note`,
`header_change` in `libs/storage/src/sqlite_log_repository.cpp`; an error entry stores the type's id **and a label
snapshot** (`error_type_id`, `error_type_label`), so a renamed type does not rewrite history; journal records are
logical, carry their own `v`, and `edit_entry` carries the whole entry; the setup file has `schema_version` 1
(`CONFIG_SCHEMA_VERSION`) and refuses a higher one.

## 2. Decisions this page proposes

1. An interaction is a **new entry kind**, `interaction`, in the same `entry` table and the same journal as errors and
   notes. Interleaving in time order then needs no new code: the log already sorts by `captured_utc, seq`.
2. The interactions board is defined **exactly like the error board**: a master list of interaction types with a
   position 1 to 11 or none, tile 12 always Other, a retired flag. (BRD, PRF-04 and PRF-05 apply by analogy.)
3. People live in **their own small store** (a table and its own journal), and entries refer to a person
   **by id only**. Names never enter the log journals, the database's entry tables, the export of the setup (PRF-08) or
   a shared log unless the user chooses so. This is the privacy design (section 5).
4. Tapping a tile that needs a name **logs first and asks second**: the entry is committed at the moment of the
   double-tap, with its exact time and no person; the name picker then opens and fills the person in with an
   `edit_entry`. The time is exact and the Stage 2 guarantee holds even if the picker is dismissed.
5. The second board is **optional and off by default** (`interactions_board_enabled`, false). When on, the fifth icon is a
   tab and the app reopens on the board last used. When off, nothing of it is visible and the app is as in v1 today.

## 3. Data model

### 3.1 Entities

New or changed entities, in the style of the PRD's table:

| Entity | Key fields | Notes |
|---|---|---|
| Entry (changed) | kind gains `interaction` | Common fields unchanged. |
| InteractionEntry | entry_id, interaction_type_id, interaction_type_label, person_ids (zero or more, in order), value_text (nullable), attached_note | kind = interaction. `interaction_type_label` is a snapshot like `error_type_label`. **No person name here.** |
| InteractionType | id, name, retired, board_position (1-11 or null), line_pattern (nullable), asks | Master list for the second board. Retired types keep their history. |
| Person | id, display_name, retired, location_id (nullable), last_used_utc, created_utc | The name list. Own store (3.4). |
| Preferences (changed) | adds `interactions_board_enabled` (default false), `name_export` (3.6), `interaction_marker` | Single row. |

`asks` is one of `nothing`, `person`, `text`. `line_pattern` is the line as the log shows it and may hold `{person}`
and `{value}`, at most one of each (`{person}` stands for one **or several** people), in the style of NOTE-04's pattern: `Spoke with {person}`, `Played {value}`,
`Gave {person} {value}`. The editor shows them as [person] and [answer] and has Insert person and Insert answer
buttons, as PRF-06 does. `asks` is derived from the pattern (`person` if it holds `{person}`, and so on) and is
stored only so a reader need not parse; the editor keeps the two equal. A type with no pattern renders as its name.
With several people the slot renders as a list in the order picked, `A, B and C` (two: `A and B`); at the `initials` level each name is shortened separately, at `hidden` the slot reads `someone` whatever the number. Two slots at once (`Gave {person} {value}`) asks for the person first, then the text.

Examples, with stand-ins (they would live in `config/sample-config.json`, whose real values the owner supplies):

| Type | Pattern | Asks |
|---|---|---|
| Greeted | (none) | nothing |
| Spoke with | `Spoke with {person}` | person |
| Played song | `Played {value}` | text |
| Visitor interaction | (none) | nothing |
| Alex Sample asked for help | not a type: a name is data, not a type | |

(The last row is deliberate: a name is never a board type, or the board would grow a tile per person.)

### 3.2 Journal format and version bump

The journal line keeps `{"v":..,"seq":..,"op":..,"log":..,"body":..,"crc":..}`. The operations are unchanged:
`add_entry` and `edit_entry` carry the whole `Entry`, so an interaction needs **no new operation**, only a new body
variant and `"v":2` on a record that carries one. Records of kinds already shipped stay `"v":1`, so a journal that never
saw an interaction is byte-for-byte what an old build wrote.

Body of an interaction entry, in the style of section 2.5 of [Stage 2](stage-2.md):

```text
{"v":2,"seq":43,"op":"add_entry","log":"…","body":{"entry":{"id":"…","kind":"interaction",
  "captured_utc":…,"captured_tz":"…","type_id":"…","type_label":"Spoke with","person_ids":[],
  "value":null,"note":null}},"crc":"…"}
```

The log-first flow of decision 4 is then two records: `add_entry` (no people), then `edit_entry` with the people
id and the edit time. **A name is never in a log journal.**

Backward and forward compatibility:

| Case | Behaviour | Why |
|---|---|---|
| New build, old journal and old database | Reads every `v:1` record. The migration adds the new tables; no row is rewritten. | "The reader understands every version ever shipped" (2.5). |
| New build, new journal | Reads `v:1` and `v:2`. | |
| Old build, database or journal written by the new build | **Not supported.** The database is refused because `user_version` is higher (2.9, M5) and nothing is written. | Same rule as every schema step. |
| Downgrade by a tester (TestFlight) | Unsupported, as above. The in-app update path is forward only. | |

### 3.3 Database schema step

One migration step, N to N+1 (the next free `user_version`; the product's steps are in `migrator.hpp`):

- `entry.kind` is a `CHECK (kind IN (...))`, which SQLite cannot alter in place. The step therefore **rebuilds the
  `entry` table** (create new, copy, drop, rename) inside the migration's transaction, with `foreign_key_check` and
  `integrity_check` after, as the Stage 2 framework does (2.9). This is the riskiest part of the work and gets its own
  package (I-2) and the migration tests M1 to M5 repeated for it, including a kill in the middle.
- New tables `interaction_entry`, `interaction_type`, `person` and the join `interaction_entry_person` (entry, person, position), because an interaction can name a group (up to 20 people; longer lists are refused).
- The DS-06 backup copy and rollback apply unchanged.

### 3.4 The person store and why it is separate

The log journals are append-only, kept for finished logs ("a finished log's journal is kept") and never deleted by the
app. A name written into them could never be erased. So:

- Names are held in **`people.jsonl`**, a second small journal with its own `seq`, CRC and exclusive lock, written
  by the same `JournalWriter` code with a different record set: `add_person`, `rename_person`, `retire_person`,
  `touch_person` (last used; not durable-synced, a miss costs only the sort order), `forget_person`.
- The database's `person` table is a cache of that journal, rebuilt the same way as a log is.
- **Forget** removes the name: the record is appended, the table row's name is blanked, and the journal is rewritten
  atomically without that person's earlier records (the one place an app action rewrites a journal; it works on a copy
  and renames, like recovery). Entries keep the id. Everything that shows or exports the entry then says "someone".
- Because entries hold ids, **renaming** someone corrects every past entry, like a header correction. Tester typos
  are fixable once.

### 3.5 The Stage 2 guarantee

| Rule | How it holds |
|---|---|
| Acknowledged means durable (DS-01, DS-02) | An interaction goes through the same write path (2.6): journal, `durable_sync`, then database commit, then the toast. |
| Log first, name second | The entry is committed and acknowledged **before** the picker opens. Dismissing the picker, a kill with the picker open, or a full disk at the second write loses no entry; at worst the entry reads "Spoke with (person not set)" and shows a marker in the Review list. |
| A new name, typed in the picker | The `add_person` record is made durable **before** the `edit_entry` that refers to it. If the second write fails, the person is in the list and the entry still has no name. A kill between them leaves someone with no entries, which is harmless. |
| Corrupt or missing database | The log is rebuilt from the log journals as before. The person table is rebuilt from `people.jsonl`. |
| Corrupt or missing **person journal** | Entries are intact and show "someone" where a name was; the report says `people_lost: N`. This is a degraded display, not a lost entry, and not a P1. The journal is also quarantined, not deleted. |
| Recovery report | Gains `people_recovered` and `people_lost` counts. Counts only, never names (the report is safe in CI output). |
| Forget | Never touches a log journal. A kill during the atomic rewrite leaves the old file or the new file. |
| Kill tests (DS-07) | The workload (`kill_workload.cpp`) gains interactions and people; the assertion is unchanged. |

### 3.6 Export, copy and sharing (PRD rules 3 to 5, NFR-09)

Interaction lines in the export:

```text
10:05 T| Wrong Response
10:12 (Interaction) Spoke with Alex Sample
10:20 T| Mute Delay
10:31 (Interaction) Played Morning Medley
10:48 (Interaction) Visitor interaction – asked about the schedule
```

- `(Interaction)` is a label in the style of `(Free Text)`. **No `T|` marker**: the tester thought not, and the
  downstream ticket indexer reads `T|`. The line is therefore treated like a note. The owner settled on 13 Nov 2026: no
  marker, label kept. (Whether the indexer ignores an unknown bracket tag is for the indexer's owner to confirm before
  release; it is the same as `(Free Text)` today.)
- The label is the preference `interaction_marker`, default `(Interaction)`; an empty value drops it, as an empty error
  prefix does.
- **Totals** (PRF-10): unchanged and for error types only. The owner decided on 13 Nov 2026 that interactions are not
  counted in totals; they can be added later without a format change to anything stored.

**People's names in shared text** are a preference, `name_export`, with these values:

| Value | Renders Alex Sample as |
|---|---|
| `full` | Alex Sample |
| `initials` | S.E. |
| `hidden` | someone |

The **recommended default is `initials`**, so a log pasted into Slack does not carry a full name unless the owner turns it
on. It applies to Share, to Copy line and Copy all (REV-04, REV-05) and to a past log re-shared (DAY-04). The Review
screen and the board show the full name, because they never leave the device. The setting is on the Settings screen
under a one-sentence explanation, and the Share sheet's preview shows what will leave. Decided: initials.

Everything else about names:

- **Setup file (PRF-08):** carries `interaction_types` and `interaction_templates` (the patterns), **never people**.
  A name list is a different kind of data from a setup, and a setup file is passed around to prepare other devices.
  **No export of the name list at all** (owner decision, 13 Nov 2026): not in the setup file and not as a separate file.
- **Backup:** the database, the people journal and the draft are in the app sandbox and so in a device backup, as
  today. `docs/privacy.md` and the store privacy answers (`docs/plan/stage-11.md`) say so.
- **Network:** none (NFR-09). No name is in a crash report, a log line or a CI artifact. The recovery report has counts only.
- **Repository:** only stand-ins (NFR-10). Tests build people from a fixed list of fictional names in a test header.

## 4. UI design

The phone mock-ups are in `docs/design/screens/` (screens 69 to 76 of [the design package](../design/README.md#interactions-board-and-people-draft-november-2026)): `Interactions`, `InteractionsPicker`, `SecondBoardSettings`, `SecondBoardSettingsOff`, `People`, `PersonEdit`, `InteractionTypeEdit` and `ReviewInteractions`, each in light and dark. The iPad and fold layouts are not drawn: `tools/visual/` writes those from rules tied to numbers in Stage 8 and 13, so they are package I-8. The wireframes below use the phone's numbers.

One change the mock-up forced: the board is 440 pt tall and a title strip does not fit under it, so on this board tiles are 96 pt (the 88 pt minimum of NFR-02 still holds) and the strip is 28 pt.

### 4.1 The fifth icon (phone)

```text
 ┌──────────────────────────────┐
 │  header card (shared)        │   unit, site, version, date, tester
 │  note composer (shared)      │   chips, field, Add
 │  Last bar                    │
 │ ┌──────┬──────┬──────┐       │
 │ │      │      │      │       │   interactions board: 3 × 4, same size
 │ ├──────┼──────┼──────┤       │   as the error board; tile 12 Other
 │ │      │      │      │       │
 │ └──────┴──────┴──────┘       │
 │  ⚠ Errors  💬 Interactions  ☰ Review  🕘 History  ⚙ Settings │
 └──────────────────────────────┘
```

- The tabs are, in order: **Errors**, **Interactions**, Review, History, Settings. The first tab, today called Log, is
  renamed Errors so that the two boards read as a pair. Icons: a warning mark for Errors and a speech bubble for
  Interactions, both drawn in the design package's style.
- The header card, the note composer (with its chips) and the Last bar are **one shared component above both boards**.
  This keeps one note draft (NOTE-03), keeps BRD-07 (hold a tile to log it with a note) working on both, and means the
  Last bar shows the latest entry of either kind. Only the 3 × 4 grid and its tab differ.
- Five tabs on 375 pt are 75 pt each. The longest label, Interactions, at the largest text scale the tab bar allows
  must still fit on one line or the tab bar scales its text down to a cap (the same cap rule as tile labels, NFR-07).
  The design package decides the cap; the test T-I5-3 checks it.
- **Telling the boards apart.** The two grids are the same shape, so a tap on the wrong one would log the wrong kind. The
  board gets a title strip ("Interactions") in the place the Errors board has none, and its tiles use a **second hue
  family** from the design package (a new group in `tokens.json`, checked by the existing contrast test in both schemes).
  The label still carries the meaning (NFR-07). Review stays black and white (REV-01).
- **Switching quickly.** A tab is one tap and is where the thumb already is. The app reopens on the last board used
  (NFR-04: no extra step). A horizontal swipe between boards is **not** proposed: a swipe on the board is too close to a
  hold-and-drag and to a missed tap. Decided, see section 8.
- **Optional second board.** `interactions_board_enabled` is **false by default**. Settings has a row "Second board:
  Interactions" with a switch and a one-sentence explanation. Turning it on shows the fifth tab at once and, if there are
  no interaction types yet, opens the setup of the board (empty positions say "Add interaction type", as PRF-05). Turning
  it off hides the tab and the board; if it was showing, the app goes to the Errors board. **Nothing is deleted by
  turning it off:** interaction entries stay in the log and in Review, export and history (so a log never changes
  because of a switch), and the types and people are kept for turning it on again. A setup file (PRF-08) can carry
  the switch, so a prepared device arrives with the board on. This also keeps every earlier package invisible to a tester
  until the whole thing works, with no `#if`.
- **Switching with the board on.** The tab (one tap, where the thumb is). The owner suggested a swipe as well; see open
  question 6, which now recommends an optional edge swipe (below).

### 4.2 iPad, foldables

Rules from Stage 8 and 13 are reused:

| Class | Where the tabs are | The second board |
|---|---|---|
| Phone portrait, iPad compact | tab bar at the foot | as 4.1 |
| iPad portrait (Regular) | tab bar in the 680 pt column | the board takes the Errors board's place; same tile sizes |
| iPad landscape (Wide) | tab bar in the log column | the left pane shows the chosen board; the log pane on the right shows both kinds, newest in view; no change on switching |
| Fold open, landscape | tabs in a rail on the trailing edge | five items instead of four; the rail has the room |
| Fold open, portrait | tab bar at the foot of the column | as the phone |
| Tabletop (fold and flip) | tabs in the upright half | the board stays on the flat half; the grid is the same height as the Errors grid |

In Wide the log pane already lists both kinds, so **an interaction appears next to the error it followed** without the
user leaving either board; this is the best view of the feature and is why the Review tab stays as drawn (Stage 8
decision 3).

### 4.3 Tapping a tile

- A tile is logged the way an error tile is: **double-tap** (BRD), count badge, undo toast, and **hold for a note**
  (BRD-07). The double-tap rule is kept on the interactions board unless the owner says an interaction is worth a single
  tap (decided: double-tap is kept).
- A tile with `asks` of `nothing` behaves exactly like an error tile.
- A tile with `asks: person` logs, shows the toast "Spoke with … Add people" (the toast stays 5 s), and **opens the
  person picker at once** (4.4). `asks: text` opens the existing question sheet used for note templates (NOTE-04),
  also after the entry is logged.
- The Other tile opens the list of interaction types not on the board, as on the Errors board.
- Undo removes the entry. If the picker was open, it closes.

### 4.4 Typing a name once and picking it later

The picker is a bottom sheet, within reach of the thumb:

```text
 ┌──────────────────────────────┐
 │ Spoke with …   Skip   Done(2)│
 │ [ Search or type a name   ]  │
 │ Recent                       │
 │ ( Alex Sample ) ( … ) ( … )  │   chips, last-used first, at least 44 pt tall
 │ All people                   │
 │  Alex Sample                 │
 │  …                           │
 │  + Add "sam" as a new person │  appears when the text matches nobody
 └──────────────────────────────┘
```

- **A group.** Tapping a chip or row toggles it (a tick), so one interaction can name several people ("Spoke with
  Alex Sample, Jordan Placeholder and Pat Stand-in"); **Done (n)** commits them in one `edit_entry`. A single tap on a
  name followed by Done is the common one-person case. **Everyone** is a row that ticks all people at the current
  location (it stores the people, so a later rename or Forget still works). The limit is 20 people.
- Typing filters the list. When the typed text matches nobody the first row is **Add "…" as a new person**; when it
  differs from an existing name only by case, spacing or accents, the sheet shows that name instead and does not create
  a duplicate.
- Names are trimmed, internal spaces are collapsed, they are at most 60 characters and are never empty. A name is shown
  as it was typed.
- The order is last used first, then alphabetical; people of the current location come first when a person has a
  location (decided: a person may belong to a location).
- **Skip** leaves the entry as logged. A skipped entry shows "(person not set)" in the Last bar and Review, and Review
  can set it later in the Edit entry sheet (which gains the same picker).
- VoiceOver: each chip is a button with the name, the typed row is announced as an action, the sheet is modal.
- The keyboard is the system one, with the system microphone (NOTE-05); the app adds no recognition.

### 4.5 Settings

- **Interaction types**, **Interaction board layout** and **Interaction templates** are the same screens as the error
  versions, with the name changed, and share their view-models by parameter (PRF-04, PRF-05, PRF-06 apply).
- **People**: a list with add, rename, retire, **Forget** (a confirmation, "Forget Alex Sample? Entries stay but show 'someone'"), and the location. Retired people leave the picker and stay on past entries.
- **People's names in shared logs**: the three-value choice of 3.6 with an example line.

### 4.6 Review, edit, history

- Review shows `(Interaction) Spoke with Alex Sample` in the same black-and-white wording as the export, with the full name.
- The Edit entry sheet (REV-02) for an interaction has: type, person (the picker), answer text, time and attached note.
- Swipe, copy, delete and undo (REV-03, REV-04) are unchanged. A deleted interaction does not count.
- History and past logs show interactions the same way and re-share by the current name-export setting.

### 4.7 Interaction with the two sessions changing the same area

| Session | Touches | Effect on this design |
|---|---|---|
| Free-text box (bigger, multi-line, optional voice) | The shared composer above the board | The composer is shared by both boards (4.1), so the board does not duplicate it and the change lands once. A taller composer shortens the board area; the Stage 8 rule (the Last bar gives way before tiles go under 88 pt) applies to the second board unchanged. If voice adds a button to the composer, the picker's search field uses the system keyboard microphone and nothing else. Hold-to-note (BRD-07) keeps working because the note and the draft are the same. |
| Unit and version header | The header card | Shared by both boards. No field of this design reads the header except the log's own location, which the optional person location uses; that is read through `HeaderHistory` as every entry's header is, so whatever the other session changes about how a header is held is picked up in one place. |

**Order:** I-1 to I-4 (core, storage, setup file, export) do not touch the UI and can merge while those two sessions
finish. I-5 onward (shell, board, picker) must rebase on both. The review of I-5 checks the composer height at the
largest text scale.

## 5. Privacy summary (NFR-09, NFR-10)

| Question | Answer |
|---|---|
| Where are names stored? | `people.jsonl` and the `person` table in the app sandbox, under iOS Data Protection. Not in a log journal, not in the entry tables. |
| Who can remove one? | The user, per person, with Forget. Entries show "someone" afterwards. |
| What leaves the device? | Only what the user shares or copies, at the chosen name level (default initials), and the device backup. |
| In the setup file? | No. |
| In the repository, tests, docs, CI output, crash output? | No real names. Stand-ins only; counts only in reports. |
| Is a name a "person name" under NFR-10? | NFR-10 keeps real names out of the *repository*. It does not forbid the app storing names the user types. The privacy page and the store answers are extended (an "If you change X" row). |

## 6. Task breakdown

Each package is one pull request into `develop`, independently shippable: nothing is visible to a tester until I-5, and
the board stays off until `interactions_board_enabled`. Every package updates the guide, `docs/privacy.md`, a
`changelog.d/` fragment and `docs/testing.md` where the table in `AGENTS.md` says so. The full labels are those of
`docs/testing.md`.

| Package | Content | Depends on |
|---|---|---|
| **I-1 Core model** | `Entry` gains the `interaction` body; `InteractionType`, `Person`, name rules (trim, collapse, case/accent match, 60 limit); line rendering with `{person}` and `{value}`; name levels (`full`, `initials`, `hidden`); journal record encoding for the new body at `v:2`. No storage. | none |
| **I-2 Storage and migration** | Schema step N to N+1 including the `entry` rebuild; `interaction_*` and `person` tables; repository calls; memory repository parity; `people.jsonl` and its writer; recovery and report counts; the reader check of 3.2. | I-1 |
| **I-3 Setup file** | `interaction_types` and `interaction_templates` in the reader, writer and `config/sample-config.json`; `CONFIG_SCHEMA_VERSION` handling (a file with only the old keys still reads); `docs/guide/setup-file.md` (the check `check_setup_doc.py` fails until they agree). | I-1 |
| **I-4 Export and sharing** | Interaction lines, `interaction_marker`, `name_export`, copy and share paths; `docs/privacy.md` and `stage-11.md` store answers. | I-1, I-2 |
| **I-0 Scope** | Move the interactions board from "out of scope for v1" to in scope in `docs/prd.md` (new requirement IDs BRD-13 onwards, PRF-14 onwards, NFR for privacy) and `docs/implementation-plan.md`; close the v2 label on #105. Adds the second stand-in (Alex Sample) to `AGENTS.md` and the docs, and the Stage 14 entry. Docs only, after the owner approves this page. | approval |
| **I-5 Shell and fifth tab** | `interactions_board_enabled` (default off) and its Settings row, five tabs when on, rail and bar layouts for all classes, last-board memory, icon assets, tab label cap, tokens for the second hue family and the contrast table. Board area shows the shared header, composer, Last bar and an empty-state board. | I-1 |
| **I-6 Interactions board and picker** | The board (tiles, count badges, double-tap, hold, undo, Other), log-first flow, person picker sheet, question sheet for `asks: text`, VoiceOver. | I-2, I-3, I-5 |
| **I-7 Settings** | Interaction types, layout, templates and People screens; Forget; name-export setting; help topics and `check_help_links.py`. | I-2, I-4, I-5 |
| **I-8 Wide layouts and mock-ups** | iPad and fold layouts of the second board, mock-ups through `tools/visual/`, owner review of the pictures. | I-5, I-6 |
| **I-9 Review, edit, history** | Rendering and the Edit entry sheet for interactions, "(person not set)" markers, guide page and screenshots (`docs/guide/screenshots.json`). | I-6 |

### Tests, by package

| Test | Label | Package | What it checks |
|---|---|---|---|
| T-I1-1 `interaction.name_rules` | unit | I-1 | Trim, collapse, 60 limit, empty refused, "alex sample" matches "Alex Sample", accents. |
| T-I1-2 `interaction.line_render` | unit | I-1 | Patterns with none, one and both slots; a missing name renders "(person not set)"; a pattern with two `{person}` is refused. |
| T-I1-3 `interaction.name_levels` | unit | I-1 | `full`, `initials` (two words, one word, hyphen, non-Latin), `hidden`. |
| T-I1-5 `interaction.group_render` | unit | I-1 | One, two and three people render `A`, `A and B`, `A, B and C` in pick order; initials per name; hidden gives `someone`; 21 people refused; a duplicate pick is ignored. |
| T-I1-4 `journal.v2_roundtrip` | unit | I-1 | Encode and decode the new body; CRC; a `v:1` record still decodes; record under `MAX_RECORD_BYTES` at the longest note, answer and label together. |
| T-I2-1 `migrate.interaction_step` | integration | I-2 | M1 to M5 for the step: rows kept, entry table rebuilt, failing step restores, newer schema refused. |
| T-I2-2 `migrate.kill_mid_step` | integration | I-2 | Kill inside the rebuild: old or new, never half (M3). |
| T-I2-3 `repo.interaction_parity` | unit | I-2 | The memory and SQLite repositories give the same results for add, edit, delete, restore, void. |
| T-I2-4 `people.journal` | unit | I-2 | Records, CRC, torn tail, gaps; `touch_person` loss costs only order. |
| T-I2-5 `people.forget` | integration | I-2 | Forget rewrites atomically; no earlier record of that name remains in the file; a kill during it leaves old or new; entries keep the id. |
| T-I2-6 `recovery.interactions` | integration | I-2 | Database gone: log and people rebuilt; person journal gone: entries intact, `people_lost` counted, file quarantined; no name in the report text. |
| T-I2-7 `kill.interaction_workload` | system | I-2 | The kill driver with interactions and people in the workload: no acknowledged entry lost (DS-07). |
| T-I2-8 `journal.no_names` | unit | I-2 | After a workload that adds, renames and forgets people, no log journal and no entry table contains any person's name bytes. |
| T-I2-9 `journal.unknown_v` | unit | I-2 | The reader's behaviour on a `v` it does not know, as the shipped reader has it (3.2). |
| T-I3-1 `config.interaction_types` | unit | I-3 | Read and write; absent keys give an empty list; duplicate names refused; positions 1 to 11 only. |
| T-I3-2 `config.no_people` | unit | I-3 | A written file never holds a person; a file that has a `people` key is refused with a message. |
| T-I3-3 `config.sample_still_valid` | unit | I-3 | The sample file reads; `check_setup_doc.py` passes. |
| T-I4-1 `export.interaction_lines` | unit | I-4 | Lines as 3.6, no `T|`, label preference, empty label; interleaved by time with errors and notes; a deleted interaction is absent. |
| T-I4-2 `export.name_levels` | integration | I-4 | Share, Copy line and Copy all use the setting; a past log re-shared uses the current one; Review shows the full name. |
| T-I5-1 `shell.five_tabs` | ui | I-5 | Five tabs in order on phone, iPad compact, Regular; the rail in fold-open. |
| T-I5-2 `shell.last_board` | ui | I-5 | The app reopens on the board last used. |
| T-I5-5 `shell.default_off` | ui | I-5 | A fresh install and an upgrade show four tabs and the v1 screens unchanged; the Settings row is off. |
| T-I5-6 `shell.toggle` | ui | I-5 | On: the tab appears and an empty board offers setup. Off while on the board: the app goes to Errors. Off keeps entries, types and people, and Review, export and history still show interaction entries. On again restores the board as it was. |
| T-I5-7 `config.board_switch` | unit | I-5 | The setup file can carry the switch; a file without it leaves the setting as it is. |
| T-I5-8 `shell.title_strip_swipe` | ui | I-5 | A swipe from the title strip switches boards; a swipe on a tile does not (if the owner chooses the swipe). |
| T-I5-3 `shell.tab_label_cap` | ui | I-5 | At the largest allowed scale the five labels do not truncate at 375 pt. |
| T-I5-4 `theme.interaction_hues` | unit | I-5 | The new colour group passes the contrast test in both schemes and the colour-blind separation note is recorded. |
| T-I6-1 `board.log_first` | ui | I-6 | Double-tap logs the entry before the picker is shown; dismissing the picker keeps the entry; Undo removes it and closes the picker. |
| T-I6-2 `board.hold_note` | ui | I-6 | BRD-07 on the second board, with the shared composer. |
| T-I6-6 `picker.group` | ui | I-6 | Tick several, Done (n) commits one edit; Everyone ticks the current location's people; the limit of 20 is explained, not silent; Skip with ticks unsaved leaves the entry as logged. |
| T-I6-3 `picker.search_and_add` | ui | I-6 | Filter, add a new name, duplicate by case offered as the existing name, recent first, Skip. |
| T-I6-4 `picker.write_order` | integration | I-6 | A new name is durable before the `edit_entry`; a failing second write leaves the entry and the person. |
| T-I6-5 `board.voiceover` | ui | I-6 | Tile names, picker chips, the Add row, modal sheet. |
| T-I7-1 `settings.interaction_types` | ui | I-7 | Add, rename, retire, move, swap on the miniature board. |
| T-I7-2 `settings.people` | ui | I-7 | Rename (history follows), retire, Forget with confirmation, location. |
| T-I7-3 `help.links` | unit | I-7 | `check_help_links.py` passes for the new topics. |
| T-I8-1 `ipad.second_board_geometry` | ui | I-8 | Tiles ≥ 88 pt, board and log panes in Wide, the tab bar in the 680 pt column; the same on the fold classes and tabletop. |
| T-I8-2 `fold.state_kept` | ui | I-8 | Folding with the picker open loses no state (T13-6 style). |
| T-I9-1 `review.interaction_rows` | ui | I-9 | Wording equals the export at full names; swipe, copy, delete, undo. |
| T-I9-2 `edit.interaction` | ui | I-9 | Change type, person, answer, time, note; Save and Cancel explicit; set the people on a skipped entry. |
| T-I9-3 `guide.screens` | unit | I-9 | `check_guide_screenshots.py` passes. |

CI covers all of these on Linux and the Qt offscreen platform; what only a device can show (haptics on the second board,
the picker with the iOS keyboard and dictation, tab labels at the largest Dynamic Type) is listed in each pull request
as not run on a device, per `AGENTS.md`.

## 7. Risks

| Risk | Handling |
|---|---|
| The `entry` table rebuild in the migration loses or reorders rows | I-2 alone, migration tests, kill-in-the-middle, DS-06 backup copy. |
| A tester downgrades after interactions were written | Unsupported and stated; see 3.2. |
| The wrong board is tapped | Title strip, second hue family, plain-text wording in Review, Undo. |
| Five tabs crowd a small phone | Label cap and T-I5-3; Review and History could share a menu if the cap fails (owner call then). |
| Names of people leak in a shared log | Default initials; the setting is on the Share preview; names never in the setup file. |
| Person journal damaged | Degrades to "someone"; never touches entries; counted in the report. |

## 8. Owner decisions (13 Nov 2026) and what is still open

| # | Question | Decision |
|---|---|---|
| 1 | Second fictional stand-in | Yes. **Alex Sample**; I-0 adds it to `AGENTS.md` and `docs/`. |
| 2 | Default name level in shared text | Initials (`initials`). |
| 3 | Export of the name list | None, in any form. Not in the setup file, no separate export. |
| 4 | Line label and ticket marker | No `T\|` marker. The label `(Interaction)` stands. Naming: the board is **Interactions**, not "resident interactions"; names are an optional saved list for testers who want it, and the app never says "resident". |
| 5 | Totals | None for interactions. Add later if wanted. |
| 6 | Switching | Tab, plus a swipe from the title strip only. |
| 7 | Double-tap on the interactions board | Kept. |
| 8 | Names tied to a location | Yes, optional. |
| 9 | Journal `v:2` against shipped builds | No concern: only the owner's and one tester's phones have the app and everyone updates forward. Downgrade stays unsupported. |
| 10 | Groups | Yes: one interaction can name several people (up to 20), with a multi-select picker and an Everyone row. |
| 11 | Ids or a name snapshot | Ids only. A rename fixes history; Forget works. |
| 13 | "Played song" and "Visitor interaction" | Move to interaction types in the sample setup. Nothing is migrated automatically either way. |

**Decided after the first round:**

- **12. Plan placement:** yes, a new Stage 14, added to `docs/implementation-plan.md` and the site navigation by I-0.
- **14. "Everyone":** ticks the people of the **current location** only. The limit of 20 stands.
- **15. Wording:** "Names" is wrong, because the feature is for equipment with humans in the loop (personal equipment,
  robots interacting with people). The list is **People** in Settings and the picker, the slot is [person], and the
  tab stays **Interactions**. An interaction with nobody involved never mentions people at all: the People row in
  Settings is shown only when the second board is on, and a type with no `{person}` slot never opens the picker.

**Still open:** nothing.
