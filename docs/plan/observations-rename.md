# Renaming Interactions to Observations

This page is the task breakdown, test list and decisions for renaming the optional second board from **Interactions** to
**Observations**, with an eye icon, **including the names the app stores and exchanges**. It is not a stage of the
[implementation plan](../implementation-plan.md); it changes what [Stage 14](stage-14.md) built. It is for the owner's review and
writes no production code (the stage gate in `AGENTS.md`). The stand-ins are **Sam Example** and **Alex Sample** (NFR-10).

**Owner decisions, 17 and 18 Nov 2026:**

1. The board, its tab and every word the user reads change from Interactions to **Observations**. The icon is an **eye**.
2. The **stored** names change too: setup-file keys, the journal entry kind, the database tables and columns, and the export
   wording (`(Observation)`; decided 18 Nov 2026), through a schema step that **still reads the old names** so that data
   written before the update opens. The old word is removed everywhere else: it survives only in that reading code, marked
   `legacy`, and in the old-format test fixtures.
3. The app ships as **1.0.0** to the App Store. The facilitator already uses a build with the old names, so her log, her
   journals, her database and her setup file must keep working after she updates. Nothing she holds is lost or rewritten
   into something an update cannot read.
4. There will be **another round of TestFlight builds for iPad and iPhone** (owner, 18 Nov 2026), so the rename is tried on real
   devices, with the facilitator's copied log, before 1.0.0 goes to review.

The word "people" (the person picker, `{person}`, People in Settings) is **not** renamed. Only the board and its entries are.

**Exit gate (proposed):**

- the owner approves this page and answers the open decisions in section 9;
- packages O-1 to O-8 are merged, each with its labelled tests passing in CI;
- every old-format fixture of section 6 (a setup file, a journal, a database written by the build the facilitator uses)
  opens in the new build with no entry lost, and the Stage 2 kill and corruption harness passes with the new names and with a
  store that started in the old format (no acknowledged entry lost, every corruption case recovers; losing an entry is a P1);
- no user-visible text, accessibility name, guide page, store text or review note in the repository says "interaction" for the
  board (the check in O-8 fails until it does), and the remaining uses of the old word are listed and are all in
  compatibility code;
- the owner has tried the app on an iPhone and an iPad (the next TestFlight round) with the facilitator's real log copied in, and the log reads the same.

## 1. Where the word is today

A survey of `interaction` (case-insensitive) on `origin/develop` at 05de140 finds about 2,400 occurrences in 256 files. Most are
names in code and tests. They fall into five groups; the **first two are the hard ones** because data already written depends on
them and cannot be changed by search and replace.

### 1.1 Stored or exchanged (must stay readable as it is)

| # | Where | Today | Notes |
|---|---|---|---|
| S-1 | Setup file, `libs/core/src/configuration.cpp` (`read_interaction_types`, the writer near line 1212, the section list near lines 140 and 1112), `docs/guide/setup-file.md`, `config/sample-config.json` | top-level key `interaction_types` (each item has `id`, `name`, `retired`, `board_position`, `line_pattern`, the derived `asks`); preference `interactions_board_enabled` under `preferences` | `CONFIG_SCHEMA_VERSION` is 1 and a higher one is refused. Error messages carry the section name `interaction_types` and the text "interaction type". `tools/ci/check_setup_doc.py` fails until the page matches the reader. |
| S-2 | Journal entry kind, `libs/core/src/journal_record.cpp` (writer near line 386, reader near line 836) | body `"kind":"interaction"`; the record is written at `"v":2` (`JOURNAL_VERSION_INTERACTIONS`) | Records are append-only and each has a CRC over its bytes, and "a finished log's journal is kept". A record already on disk is never rewritten by the app. The reader rejects a record whose version does not match its body (line 561). |
| S-3 | SQLite schema, `libs/storage/src/schema_v4.hpp`, `migrator.cpp` (`add_interactions`, step 3 to 4), `sqlite_log_repository.cpp` | `entry.kind CHECK (kind IN ('error','note','header_change','interaction'))`; tables `interaction_entry` (`interaction_type_id`, `interaction_type_label`), `interaction_entry_person`, `interaction_type`; `user_version` 4 | The CHECK cannot be altered in place, so a change of kind value needs the table rebuild again (see 4.3). The people table and journal keep their names (decision above). |
| S-4 | Small preference files, `board_state.cpp` and `export_preferences.cpp` (QSettings INI) | `boards/enabled`, `boards/last` (value `Interactions`), `export/interaction_marker`, `export/name_export` | Not the log or the setup, but they hold the switch the facilitator turned on. If the key is lost the board silently disappears from her app. |
| S-5 | Export, `libs/core/src/export_renderer.cpp`, `export_format.hpp` (`DEFAULT_INTERACTION_MARKER`), the golden files `libs/core/tests/golden/interactions-*.txt` | the line label `(Interaction)`, for example `10:12 (Interaction) Spoke with Alex Sample` | Leaves the device (Share, Copy, past logs). The downstream ticket indexer may read it (section 7). The stored marker preference may hold a user's own word, which is never rewritten. |
| S-6 | Recovery report and quarantine text | counts only, no names; mentions interactions in messages | Check the report keys the CI output reads. |
| S-7 | The sample file `config/sample-config.json` and any setup file already on a device or in a message | uses `interaction_types` | Read forever (4.1). Written new from now on. |

### 1.2 Code names

| Where | What |
|---|---|
| `libs/core` | `InteractionEntry`, `InteractionType`, `InteractionTypeId`, `InteractionPatternError`, `interaction.hpp/.cpp`, `render_interaction_*`, `m_InteractionTypes`, `m_InteractionsBoardEnabled`, `m_InteractionMarker` |
| `libs/storage` | repository calls, `INTERACTION_TABLES`, `add_interactions`, kill workload (`kill_workload.cpp`), `contract_cases.hpp`, `raw_db.hpp` |
| `apps/app/src/viewmodel` | `InteractionBoardModel`, `InteractionTypes`, `InteractionTypeListModel`, `BoardState` values `INTERACTIONS`, `SettingsModel`, `LogSession`, `TimelineModel`, `ExportPreferences` |
| `apps/app/qml` | `TabBar.qml` (`key: "Interactions"`, `interactionsEnabled`, `icon: "speech"`), `LogScreen.qml`, `InteractionTypes.qml`, `InteractionTypeEdit.qml`, `InteractionTilePicker.qml`, `InteractionOtherSheet.qml`, `EditEntry.qml`, `SettingsList.qml`, `Theme.qml` (`interactionAccent`, the second hue family), `Main.qml`, `AppShell.qml` |
| Tests and tools | `apps/app/tests/*` (about 700 mentions), `libs/*/tests/*`, `tools/visual/interactions_mockups.py`, `tools/ej_screenshots/screens.json`, `docs/design/tokens.json`, `docs/design/contrast.md` |

These rename freely (O-2, O-5, O-6). The rule: a name of **stored** data keeps its old spelling **only** inside the reader code
that handles old data, in one place per layer, marked `legacy`.

### 1.3 What the user sees and hears

Tab label and key, board title strip, Settings row ("Second board: Interactions"), the Interaction types, layout and templates
screens, the picker headline, toast text, Review rows, the Edit entry sheet, help topics (`HelpTopics.qml`, ids
`interactionTypes` and so on), the **accessibility names** of the tab, tiles, strip, picker and sheets (VoiceOver strings and the
`Accessible.name` values the QML tests check), and the app's tab-bar icon.

### 1.4 Documents

`docs/prd.md` (23), `docs/architecture.md` (13), `docs/privacy.md` (2), `docs/testing.md` (6), `docs/implementation-plan.md` (2),
`docs/guide/` (`setup.md` 17, `setup-file.md` 12, `sharing.md` 10, `logging.md` 5, `history.md` 1 and the picture list in
`screenshots.json`), `docs/design/` (README, the screen mock-ups `Interactions*.dc.html` and the interaction parts of `Main*`,
`Other*`, `Review*`, `People*`, `Recovery*` and others), `docs/plan/stage-11.md` (store privacy answers) and `stage-14.md`, the
`changelog.d/` fragments, and `mkdocs.yml` navigation.

### 1.5 Store text and review notes

`store/appstore`, `store/play` and `store/README.md` hold **no** occurrence of the word today (the survey found none), and
`docs/plan/stage-11.md` has two (the store privacy answer for names and the age-rating line "user interaction"). The 1.0.0
listing, the description, the screenshots' captions and the **App Review notes** are written in package O-8 with the new word, so
the reviewer who opens the board finds "Observations" and the eye. The age-rating answer "user interaction" is the store's own
questionnaire term, not ours, and stays.

## 2. Decisions this page proposes

1. **The new word is Observation (singular) and Observations (plural).** The singular is used for an entry ("an observation"),
   the Edit sheet ("Edit observation") and the export label if it changes (section 7).
2. **Read old, write new, never rewrite what is on disk without a copy.** Every reader accepts both spellings for ever. Every
   writer writes the new one. An old record is not edited in place, because the CRC and the append-only rule forbid it.
3. **One compatibility layer per layer**: `configuration.cpp` (setup file), `journal_record.cpp` (journal), `migrator.cpp`
   (database), `board_state.cpp` and `export_preferences.cpp` (preference files). Nothing above them knows the old word.
4. **The schema moves by one step**, 4 to 5, with the Stage 2 framework (backup copy, rollback, `foreign_key_check`,
   `integrity_check`); a build that sees a higher `user_version` refuses it, as every build does (Stage 2, 2.9).
5. **Downgrade is unsupported**, as in Stage 14 (decision 9). The facilitator updates forward. A **setup file** written by the new
   build is not read by an old build (it refuses the new keys); the old word is not kept to prevent that (4.1).
6. **The 1.0.0 build is the first to carry only the new names in new data.** There is no public release that has the old names;
   the old formats exist on the owner's, the facilitator's and one tester's devices. That limits the reader's lifetime question to
   those few files (section 9, decision O-4).

## 3. Data model changes

| Entity | Old | New |
|---|---|---|
| Entry kind | `interaction` | `observation` |
| Entry body type | `InteractionEntry` | `ObservationEntry` |
| Type master list | `InteractionType`, table `interaction_type` | `ObservationType`, table `observation_type` |
| Entry table | `interaction_entry` (`interaction_type_id`, `interaction_type_label`) | `observation_entry` (`observation_type_id`, `observation_type_label`) |
| Join | `interaction_entry_person` | `observation_entry_person` |
| Setup file | `interaction_types`, `preferences.interactions_board_enabled` | `observation_types`, `preferences.observations_board_enabled` |
| Preference files | `boards/enabled`, `boards/last = Interactions`, `export/interaction_marker` | `boards/last = Observations`, `export/observation_marker`; `boards/enabled` is unchanged (it does not carry the word) |
| Journal body | `"kind":"interaction"` at `"v":2` | `"kind":"observation"` at `"v":3` |

The version of a journal record is raised to 3 **only for a record that carries the new kind**, as `v:2` was for the first
interaction. A journal that never saw an observation stays byte for byte what the build before wrote. A record already at `v:2`
stays `v:2` and is read as an observation in memory.

The person tables and `people.jsonl` are unchanged. No record of the people journal names an interaction.

## 4. The migration

The facilitator's device holds four kinds of old data. Each is read by the new build and each moves forward by a path that has
**copy, then migrate, then restore on failure** (Stage 2, 2.9 and DS-06).

### 4.1 Setup file

- The reader (`configuration.cpp`) accepts `observation_types` **or** `interaction_types` (and `observations_board_enabled` or
  `interactions_board_enabled`). A file that has **both** spellings of the same key is refused with a plain message that names
  the two keys (an editing mistake, not a case to guess).
- The writer writes the new keys. `CONFIG_SCHEMA_VERSION` rises from 1 to 2 **only if** the owner wants the new key to be
  detectable by a version check (decision O-3); the recommendation is **not to raise it**: an old build ignores unknown top-level
  keys only if the reader does, which it does not (the section list near line 1112 is a known-key check), so an old build refuses a
  file with a new key either way. The version number adds nothing. A **setup file is written by the owner or by Export, and read
  by the same build**, so refusal by an old build is acceptable and the message should say "this file is from a newer app".
- Importing an old file converts in memory. When the person saves or exports a setup, the new keys are written.
- The sample `config/sample-config.json` is rewritten with the new keys. A second fixture keeps the old keys for the tests.
- `docs/guide/setup-file.md` documents the new keys and has one short section "Files from an earlier version" that lists the old
  keys as accepted. `check_setup_doc.py` is extended to read both lists, so the page cannot drift from the reader.

### 4.2 Journals

- A journal is **not rewritten** by the migration. The reader decodes `"kind":"interaction"` (at `v:2`) and
  `"kind":"observation"` (at `v:3`) to the same in-memory type. The writer writes the new one at `v:3`. An existing log
  therefore continues in the same file: old records above, new records below, each record valid on its own.
- `JOURNAL_VERSION_NEWEST` becomes 3. The check at `journal_record.cpp:561` (the version must match the body) is updated so that a
  `v:2` `interaction` and a `v:3` `observation` are both valid, and a `v:2` `observation` or a `v:3` `interaction` is a corrupt
  record (counted and quarantined, as any mismatch is).
- A journal that holds a `v:3` record cannot be read by the old build. It stops at that line and the log "needs recovery", which
  recovers up to that point (Stage 2). This is the documented downgrade cost, and it costs the new observations, never the old
  entries. Downgrade stays unsupported.
- *Alternative considered and not recommended:* rewrite every journal to the new names at migration time. It makes the files
  uniform but breaks "a finished log's journal is kept", puts every CRC and every kept file at risk in one step, and gains nothing
  the reader does not already give.

### 4.3 Database

One step, 4 to 5, run by the Stage 2 migrator (backup copy before, rollback and restore on failure, quarantine of the files it
replaces):

1. Rebuild `entry` with `CHECK (kind IN ('error','note','header_change','observation'))` and copy every row with
   `CASE kind WHEN 'interaction' THEN 'observation' ELSE kind END`.
2. Create `observation_entry`, `observation_entry_person` and `observation_type`; copy their rows from the old tables
   (`INSERT ... SELECT` with the column names mapped); drop the old tables.
3. Rebuild the indexes and triggers the old tables had under the new names.
4. `foreign_key_check` and `integrity_check`, then set `user_version` to 5. Any failure restores the copy.

**Reading across the rename, defensively:** a database file in which the step did not run (for example a restored backup of an
older app) is migrated on open, not read in place. The database is a cache of the journals in any case (Stage 2): a database that
cannot be migrated is rebuilt from the journals, which the new reader reads in both spellings. That is the second line of defence
and the reason the journals are left alone.

### 4.4 Small preference files

On load, `BoardState` reads `boards/last` with either word and writes the new one. `ExportPreferences` reads
`export/observation_marker` and falls back to `export/interaction_marker`, then writes the new key and leaves the old one in the file
unread. A stored marker the person typed is kept as typed. A stored value exactly equal to the old default `(Interaction)` was
never a choice, so it is read as the new default `(Observation)` and the new key is written.

### 4.5 What the migration does not touch

Archived logs' exports that someone already sent, the people journal, the draft file, the person ids and every entry id. Ids do
not change, so Undo history, the "(person not set)" markers and Review links keep working.

## 5. UI and icon

### 5.1 Words

`Observations` for the tab, strip, Settings row and screens; `Observation types`, `Observation board layout`, `Observation
templates`; `Edit observation`. The one-sentence explanations in Settings, the help topics (`observationTypes`; the old id is
dropped, nothing outside the app links to it), the toasts and the empty state change. Every `Accessible.name`, `Accessible.description`
and VoiceOver announcement is on the list in O-6; the QML tests that look strings up by name are updated with them.

### 5.2 The eye icon

The tab bar's `Icon.qml` gets an `eye`. States, the same as the other tabs (`TabBar.qml`):

| State | Drawing | Notes |
|---|---|---|
| Unselected, board off | not shown | The fifth tab does not exist while `observations_board_enabled` is false (Stage 14, 4.1). |
| Unselected, board on | outline eye, secondary text colour | A 1.5 pt stroke on the same 24 pt grid as the other four icons. |
| Selected | outline eye with a filled pupil, in the board's accent (`observationAccent`, renamed from `interactionAccent`) | The state is not colour alone (NFR-07): the pupil is filled only when selected, as the other icons gain their fill. |
| Pressed | the selected drawing at the pressed opacity of the other tabs | |
| Disabled | not used (a tab is never disabled) | |
| Large text, five labels | the label may shrink to the cap of T-I5-3; the icon does not | `Observations` is two characters longer than `Interactions`, so the cap check runs again. |
| High contrast and both colour schemes | stroke and pupil pass the contrast test in light and dark | `theme.interaction_hues` becomes `theme.observation_hues`. |
| VoiceOver | "Observations, tab, 2 of 5, selected" | The eye is decorative to the reader (the label carries the meaning). |

The **App icon** is not changed. The eye also appears in the Settings row of the second board and the empty state; the
mock-ups `Interactions*.dc.html` are re-drawn as `Observations*.dc.html` and the `tools/visual/` scripts follow (O-7).

## 6. Tests

All tests keep their labels from `docs/testing.md`. The new ones are below. Fixtures are **written by the build that exists today**
(the commit before the rename) and checked in under `libs/*/tests/fixtures/legacy-interactions/`, never written by hand after the
rename, so a fixture is what the facilitator really holds. They use only the stand-ins.

| Test | Label | Package | What it checks |
|---|---|---|---|
| T-O1-1 `config.read_old_keys` | unit | O-1 | A setup file with `interaction_types` and `interactions_board_enabled` reads to the same `Configuration` as the same file with the new keys. |
| T-O1-2 `config.both_spellings_refused` | unit | O-1 | A file with both spellings of one key is refused with a message that names both. |
| T-O1-3 `config.write_new_keys` | unit | O-1 | A written file has only the new keys; reading it back is equal; the old sample still reads. |
| T-O1-4 `config.error_sections` | unit | O-1 | Error messages name the section the file used (old or new), and `check_setup_doc.py` passes for both lists. |
| T-O2-1 `journal.read_v2_interaction` | unit | O-2 | A `v:2` `interaction` record decodes to an `ObservationEntry`; re-encoding it as an old record is not possible (the writer only writes new). |
| T-O2-2 `journal.v3_roundtrip` | unit | O-2 | Encode and decode `observation` at `v:3`; CRC; longest note, answer and label under `MAX_RECORD_BYTES`. |
| T-O2-3 `journal.version_mismatch` | unit | O-2 | `v:2` with `observation`, and `v:3` with `interaction`, are corrupt records. |
| T-O2-4 `journal.mixed_file` | integration | O-2 | A journal with old records and new records appended reads in order; edits of an old entry (`edit_entry`) at `v:3` replace it; nothing above the first new record changes byte for byte. |
| T-O3-1 `migrate.observation_step` | integration | O-3 | A database written by today's build (kind values, three tables, people rows) migrates 4 to 5: every row kept, kinds mapped, `foreign_key_check` and `integrity_check` clean. M1 to M5 for the step. |
| T-O3-2 `migrate.kill_mid_step` | integration | O-3 | Kill inside the rebuild at every crash point: the old database or the new one, never half; the backup copy restores. |
| T-O3-3 `migrate.failure_restores` | integration | O-3 | A forced failure in step 2 (and in the checks) restores the copy; the app opens the old database read-write on the old names and tells the person. |
| T-O3-4 `migrate.newer_refused` | integration | O-3 | `user_version` 6 is refused, nothing written. |
| T-O3-5 `recovery.old_db_new_reader` | integration | O-3 | The database is deleted: the log is rebuilt from a journal in the old spelling and from a mixed one; the same entries, the same order. |
| T-O3-6 `repo.observation_parity` | unit | O-3 | The memory and SQLite repositories give the same results for add, edit, delete, restore, void (the Stage 14 parity test, renamed). |
| T-O4-1 `prefs.read_old_files` | unit | O-4 | `boards/last = Interactions` opens the Observations board; the old marker key is read, the new one written; a user's own marker word is never changed. |
| T-O4-2 `prefs.default_marker_policy` | unit | O-4 | A stored marker exactly `(Interaction)` reads as `(Observation)`; any other stored word is kept as typed. |
| T-O5-1 `export.golden_observations` | unit | O-5 | The golden files renamed (`observations-full`, `-initials`, `-hidden`, `-no-label`) give `(Observation)` lines; a log made of old entries and a log made of new ones export identically. |
| T-O5-2 `export.indexer_contract` | integration | O-5 | The tickets-only view of the export (the lines that start `T\|`) is byte for byte what it was for the same log before the rename, and the observation lines carry no `T\|`. |
| T-O5-3 `export.label_empty` | unit | O-5 | An empty marker drops the label, as today. |
| T-O6-1 `ui.observations_words` | ui | O-6 | The tab, strip, Settings row, picker headline, Edit sheet and toasts say Observations; no QML text or `Accessible.name` contains `nteraction`. |
| T-O6-2 `ui.eye_states` | ui | O-6 | The five states of 5.2; the filled pupil only when selected; contrast in both schemes. |
| T-O6-3 `shell.tab_label_cap` | ui | O-6 | The five labels do not truncate at 375 pt at the largest allowed scale (T-I5-3 for the longer word). |
| T-O6-4 `ui.voiceover_names` | ui | O-6 | The tab reads "Observations, tab, 2 of 5"; tile, picker chips and sheets as in T-I6-5. |
| T-O6-5 `help.links` | unit | O-6 | `check_help_links.py` passes with the new topic ids. |
| T-O7-1 `docs.no_old_word` | unit | O-7, O-8 | A repository check (`tools/ci/check_observation_word.py`) fails on the old word anywhere except the allow-list of compatibility files and the plan pages that describe the history. |
| T-O7-2 `guide.screens` | unit | O-7 | `check_guide_screenshots.py` passes with the new pictures. |
| T-O9-1 `system.facilitator_upgrade` | system | O-9 | The end-to-end run of section 8, on the facilitator-style fixture set. |
| T-O9-2 `kill.observation_workload` | system | O-3, O-9 | The kill driver (`ej_kill_driver`) with a workload of observations and people, run **twice**: from an empty store, and from a store that started as today's format and was migrated. No acknowledged entry lost (DS-07). |
| T-O9-3 `corruption.cases` | system | O-9 | Every corruption case of Stage 2 (torn tail, bad CRC, gap, deleted database, corrupt people journal) on a store whose journal holds old and new records: all recover. |

CI runs all of these on Linux and the Qt offscreen platform. What only a device can show (the eye at the largest Dynamic Type, the
VoiceOver reading, the migration on the facilitator's own file) is listed in each pull request as not run on a device.

## 7. The downstream ticket indexer

**Decided 18 Nov 2026:** the old word is removed everywhere, so the export line becomes `10:12 (Observation) Spoke with Alex Sample`.
No setting keeps the old label, and a past log re-shared from the new build also reads `(Observation)` (the label is a preference,
not stored in the entry).

**The risk that remains.** The line still has no `T|` marker and a bracketed label in the style of `(Free Text)`. The downstream
ticket indexer reads `T|` lines. Stage 14 (3.6) left it *for the indexer's owner to confirm* that it ignores an unknown bracket tag.
This page cannot find that out from the repository. A new tag reaching it on the first day of 1.0.0 is harmless if it behaves as
it does for `(Free Text)`; if not, observation lines could be dropped or mistaken. Mitigations in the plan:

1. The owner (or the indexer's owner) checks the indexer with a sample export holding `(Observation)` lines **before O-5 merges**.
   The next TestFlight round is the natural place: send a real export of the test log through it.
2. T-O5-2 keeps the `T|` lines of any export byte for byte what they were, so the indexer's input does not move.
3. The label is one constant and one preference (`observation_marker`), so if the check fails the fix is a one-line change, not a
   rename of anything stored.

## 8. Order of work, small pull requests

Each package is one pull request into `develop`, on a branch prefixed `feature/`, with its labelled tests passing and a
`changelog.d/` fragment where a user can see the change. The rename is invisible to a tester until O-6, because the old names are
still what the UI shows until then.

| Package | Content | Depends on |
|---|---|---|
| **O-0 Plan** | This page, owner review. Docs only. | none |
| **O-1 Setup file** | The reader accepts both spellings, the writer writes the new, the sample file, the legacy fixture, `docs/guide/setup-file.md` and `check_setup_doc.py`. The code type is still `InteractionType` (only the file's words change). | O-0 |
| **O-2 Journal** | `observation` at `v:3` is read and written, `interaction` at `v:2` is read; the version check; mixed-file test. The in-memory type is still `InteractionEntry`. | O-0 |
| **O-3 Database** | Step 4 to 5, the rebuild and copy, restore on failure, the repository's SQL on the new names, recovery tests, the kill test from an old-format store. Nothing outside `libs/storage` and its tests. | O-2 |
| **O-4 Preference files** | `boards/last`, `observation_marker`, a stored `(Interaction)` read as the new default. | O-0 |
| **O-5 Export** | The `(Observation)` label, the golden files, the indexer contract test; merges after the indexer check of section 7. | O-2 |
| **O-6 Code and UI words** | Rename types, files, QML ids, models and tests in one mechanical pull request (nothing stored changes in it); the visible words, the eye icon and its states, accessibility names, help topics with the alias, `observationAccent`. Split in two if it exceeds review size: C++ names, then QML and words. | O-1 to O-5 |
| **O-7 Documents and mock-ups** | PRD, architecture, testing, guide pages and pictures, privacy page, design package and `tools/visual/`, `tools/ej_screenshots/screens.json`, the repository check for the old word. | O-6 |
| **O-8 Store text and review notes** | The listing text, the age and privacy answers in `docs/plan/stage-11.md`, the App Review notes for 1.0.0, in the new word. Docs only. | O-7 |
| **O-9 Upgrade rehearsal** | Not code: the system test T-O9-1, the kill and corruption harness runs, and the owner's trial with a copy of the facilitator's log; the exit-gate checklist. | O-1 to O-8 |

**Why this order.** The three packages that touch stored data (O-1 to O-3) come first and change only what is read and written;
a reviewer can check them against the fixtures without wading through a rename. The mechanical rename comes after, when the
compatibility layer exists and the old fixtures prove it. Words, pictures and store text come last so they are written once.
O-1, O-2 and O-4 are independent and can be reviewed in any order; O-3 follows O-2 so that a database rebuilt from journals reads
the new journal.

**Interaction with other work:** the stage 14 page, the guide pictures and the Stage 11 store pages are edited by the rename. A
package that edits the same file as an open pull request rebases on it. `docs/plan/stage-14.md` is **not rewritten**: it carries a
note at the top (added by O-7) that it describes the board under its old name and points here, so the history stays true.

## 9. Owner decisions

| # | Question | Recommendation | Decision |
|---|---|---|---|
| O-1 | Does the export label change from `(Interaction)` to `(Observation)`? | Yes. | **decided 18 Nov 2026**: the old word is removed everywhere. The indexer check of section 7 stays open. |
| O-2 | Singular and plural: "an observation" for an entry, "Observations" for the board. | Yes. | proposed |
| O-3 | Raise `CONFIG_SCHEMA_VERSION` to 2 for the new keys? | No. The reader refuses unknown keys either way; the version adds nothing and costs a migration of the number. | **open** |
| O-4 | How long does the reader keep the old names? | For ever, only in the reading code. The cost is a few lines per layer, and the facilitator's data can be any age. | **open** |
| O-5 | The setup file's old keys on **write**: only the new ones, or both for one release so an old build can still read a file the new one wrote? | Only the new. Setup files are prepared and read on the same build, and writing both makes the file confusing. | **open** |
| O-6 | Database step 4 to 5 now, or fold the rename into a later schema step? | Now, as its own step, so the rebuild is tested alone (the riskiest part of Stage 14 was this kind of step). | proposed |
| O-7 | Journals: leave old records as they are (recommended) or rewrite files to the new names? | Leave. | proposed |
| O-8 | Does the app icon or the splash change? | No; only the tab icon. | proposed |
| O-9 | Does the facilitator get the rename in a TestFlight build before 1.0.0 goes to review, so her real log is the test? | Yes: package O-9 is not done until her copy has opened. | **decided 18 Nov 2026**: yes, another round of TestFlight builds for iPad and iPhone. |

**Not done by this page:** no code, no test, no setup-file or journal change, no tag or release. The facilitator's real files are
never copied into the repository (NFR-10); the fixtures are made by the old build from stand-in data.
