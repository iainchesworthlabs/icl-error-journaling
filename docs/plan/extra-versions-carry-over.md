# Other versions that carry over

Task breakdown and test list for carrying the names of other software versions (HDR-10) from one log to the next. One pull request of
code into `develop` after the owner has reviewed this page. It is not a stage of the [implementation plan](../implementation-plan.md): the
owner asked for it after using the build ("If a user adds an additional version, the version type should be carried over until they
remove it, ie ask them for its version every logging day. If I add an app version as a second version, I shouldn't need to add 'app
version' every time, just set the value, until I remove app version from my additional versions").

**Status:** approved by the owner (with the two open points answered: the banner lists a carried version with no value, and the export prints the `not set` line); built in the same pull request.

## Behaviour

1. A new log (DAY-01, DAY-02) starts with the names of the other versions the log it follows had when it ended, each with **no value
   yet**. HDR-05 stays true for everything else: the robot version, its commit, the unit number and the location are still cleared.
2. A name stays on every later log until the person removes it (Remove on its card in the Other versions sheet, then Done). A name that
   was never given a value still carries on.
3. A carried name shows on Today's details in the Other versions row and its sheet, with an empty Version field to fill in. The person
   only types the number.
4. While a version has no value the log, the change lines and the export print it as **not set** (decision 2), for example
   `Influencer Version: not set`. Typing the number replaces it. Nothing blocks logging, sharing or leaving the screen.

## Decisions

1. **The names are copied from the previous log, not kept in Settings** (the owner's choice). There is no new preference, no setup-file
   key and no new thing stored, so `docs/privacy.md` does not change. Removing a name in a log ends it for the logs after that one; the log
   it was removed in and the earlier logs keep what they had.
2. **An unset version prints as "not set"** (the owner's choice), so a reader of the log sees that the version exists and was not filled
   in, and not that it was left out.
3. **"Previous log" means the log being archived by Start a new log**, the one open when the person chooses it, in its state at that
   moment. A first launch has none and starts with no extra versions, as now.
4. **A name with an empty Version field is valid in the sheet** (Done saves it as not set). Before, an empty version on a card was
   refused. A typed version is checked as in HDR-04, as before.

## What changes

| Part | Where | Change |
|---|---|---|
| The value | `libs/core/include/error_journaling/extra_versions.hpp` | `LabelledVersion` can hold no value (a flag; the class stays copyable and free of strings, see its header comment). `==`, `summarize_extra_versions` ("Influencer not set"), `extra_version_line_label` unchanged |
| The stored text | `extra_versions.cpp` | A line `label=` with nothing after the sign is a name with no value; `parse_extra_versions` reads it, `stored_extra_versions` writes it. Older text reads as before; the text stays one `label=version` per line, so the SQLite column and the journal record need no schema change |
| The new log | `LogSession::attempt_new_log`, `tester_for_new_log` area in `log_session.cpp` | The header of the new log gets the labels of the open log, each unset |
| The sheet | `ExtraVersionsSheet.qml`, `LogSession::read_extra_rows` | A card may have an empty Version; the placeholder is the example number; Done is allowed with empty versions |
| The export | `export_renderer.cpp` | A name with no value prints `<label> Version: not set` |
| Today's details | `HeaderScreen.qml` | The row's value shows "Influencer not set"; the footer sum-up leaves them out until set (it names the versions in force) |
| Docs | `docs/prd.md` (HDR-05, HDR-10, the rendering rules), `docs/guide/todays-details.md`, `changelog.d/` | The new rule and the new line |

## Answers of the owner

- **The banner** lists a carried version with no value, as "Influencer version" (the key is `extra_version:<name>` in `missingFields`).
- **The export** prints `Influencer Version: not set`.

## Tests

| Case or test | Label | What |
|---|---|---|
| `extra_versions.carry_over` | unit | The names go on, in order, with no value |
| `extra_versions.unset` | unit | A name with no value: stored as `label=`, parsed back equal, summarised, checked (no duplicate, not Robot) |
| `extra_versions.stored_old_text` | unit | Text written before this change reads as it did |
| `storage.extra_versions_unset` | integration | Written and read back through SQLite and through the journal record, and recovered after a crash |
| `session.new_log_carries_names` | unit | Start a new log: the banner names each; the names are there with no value; the robot version, commit, unit and location are empty; the tester is kept |
| `session.new_log_drops_removed` | unit | A name removed in a log is not on the next one; the earlier log still has it |
| `session.new_log_first_launch` | unit | With no previous log there are no extra versions |
| `export.unset_version_line` | unit | `Influencer Version: not set`; with a value the line is as before; a log with none prints exactly as before |
| `sheet.carried_cards` | ui | A carried name has an empty Version field; Done with it empty saves not set; typing a bad number is refused; Remove takes the name off |
| `header_card.extra_version` | ui | The strip names a carried version with no value by its name |
| `versions.sheet_carried` | ui | A name with no number is allowed, Done saves it as not set |

## On a device

Not run (there is no Mac). To check on the phone: add an app version, log, start a new log, open Today's details and see the name with an
empty Version field; type the number; remove the name; start another log and see it gone.
