# Stage 1: core library

Task breakdown and test list for Stage 1 of the [implementation plan](../implementation-plan.md). Each work package is
one pull request into `develop`. Requirement IDs refer to the [PRD](../prd.md).

**Exit gate:** all core tests pass with TSan and the sanitisers on, and the golden export matches the PRD example.

**Status:** approved by the owner; implemented in work packages 1.1 to 1.7. Exit-gate result: see pull request for 1.7.

## Scope and conventions

Everything lives in `ej_core` (C++23, QtCore only) under `libs/core/`. There is no UI, no database and no QML. Stage 1 adds
no dependency.

- Headers in `libs/core/include/error_journaling/`, namespace `ej`, sources in `libs/core/src/`, tests in
  `libs/core/tests/`.
- Text is UTF-8 `std::string`. The core uses `std::chrono` for time. `QTimeZone` appears only in one implementation file
  (see 1.3), so the rest of the core builds and tests without Qt types.
- Recoverable errors are `std::expected<T, Error>` with a small enum error code per component. Nothing throws across the
  library boundary. Validation that reports several problems at once (Configuration) returns a list.
- Everything is a value type or a pure function unless stated. No global state, no clock reads: the caller passes the
  time. This keeps TSan runs trivial and tests deterministic.
- Tests use Catch2 through CTest. Every test carries one label. Stage 1 uses `unit` for everything, including the golden
  tests, because none touches the file system beyond reading fixtures.
- Sketches below show shape, not final signatures. Names follow `snake_case` functions, `PascalCase` types,
  `m_PascalCase` members, `UPPER_SNAKE_CASE` enumerators.

## Components

### UnitNumber (HDR-02)

```cpp
class UnitNumber
{
public:
    enum class Error { EMPTY, NOT_DIGITS, LEADING_ZERO, TOO_LONG };
    static std::expected<UnitNumber, Error> parse(std::string_view digits);
    std::string_view digits() const;                       // "27"
    std::string format(std::string_view prefix) const;     // "UNIT-27"
    friend bool operator==(const UnitNumber&, const UnitNumber&) = default;
};
std::string unit_label(std::string_view prefix);           // "UNIT #"
```

Tests (`unit`):

- Accepts `1`, `27`, `100`, `123456789`.
- Rejects `027` and `00` (`LEADING_ZERO`), the empty string, `2 7`, ` 27`, `27 `, `-27`, `+27`, `2.7`, `UNIT-27`, and
  Unicode digits such as `２７` (`NOT_DIGITS`).
- Rejects ten or more digits (`TOO_LONG`).
- `format("UNIT")` gives `UNIT-27`; a different prefix is used verbatim; `unit_label("UNIT")` gives `UNIT #`.

### SoftwareVersion (HDR-04)

```cpp
class SoftwareVersion
{
public:
    enum class Error { EMPTY, WRONG_COMPONENT_COUNT, NOT_DIGITS, LEADING_ZERO, TOO_LONG };
    static std::expected<SoftwareVersion, Error> parse(std::string_view text);
    std::string format() const;                            // "1.4.2"
    friend auto operator<=>(const SoftwareVersion&, const SoftwareVersion&) = default;
};
```

Tests (`unit`):

- Accepts `1.4.2`, `0.0.1`, `10.20.30`, `1.5.0`.
- Rejects `1.4` and `1.4.2.1` (`WRONG_COMPONENT_COUNT`), `1.4.2-beta`, `1.4.2+3`, `v1.4.2`, `1..2`, `1.4.`, ` 1.4.2`
  (`NOT_DIGITS`), `01.4.2` (`LEADING_ZERO`), the empty string.
- Ordering is numeric per component: `1.10.0 > 1.9.9`, `1.4.2 < 1.5.0`, equal values compare equal, and `parse` then
  `format` round-trips.

### Timestamp and TimeZoneProvider (Data model, risk "platform time zone support")

```cpp
struct Timestamp
{
    std::chrono::sys_time<std::chrono::milliseconds> m_Utc;
    std::string m_Zone;                                    // IANA name, e.g. "Australia/Melbourne"
};

struct LocalTime { std::chrono::year_month_day m_Date; std::chrono::seconds m_SinceMidnight; };

class TimeZoneProvider
{
public:
    enum class Error { UNKNOWN_ZONE };
    virtual ~TimeZoneProvider() = default;
    virtual std::expected<LocalTime, Error> to_local(const Timestamp&) const = 0;
    virtual std::expected<std::chrono::sys_time<std::chrono::milliseconds>, Error>
        to_utc(const LocalTime&, std::string_view zone) const = 0;   // for edited times; picks the earlier
                                                                      // instant in a repeated hour
};

std::unique_ptr<TimeZoneProvider> make_qt_time_zone_provider();     // the only file that includes QTimeZone
```

`to_utc` exists because the review screen edits the local time (REV-02). The fake provider used by other tests is a
table of fixed offsets that lives in `libs/core/tests/`.

Tests (`unit`, against the Qt implementation, run on Linux CI with the system tz data):

- `Australia/Melbourne` at 2026-10-03 and 2026-10-05 gives +10:00 then +11:00; the local hour 02:xx on 2026-10-04 does not
  exist, and `to_utc` for 02:30 that day returns a defined result (the instant after the gap) instead of an error.
- At the April change (2026-04-05, 03:00 AEDT back to 02:00 AEST) the local time 02:30 occurs twice; `to_local` of the two
  UTC instants gives 02:30 both times and `to_utc` picks the first.
- `Australia/Perth`, `Australia/Sydney` and `UTC` give the expected local time, so travel works.
- An unknown zone name returns `UNKNOWN_ZONE`.
- A local date and time that differs from the UTC date (UTC 13:30 on 6 Nov is 00:30 on 7 Nov in Melbourne).

### TimeFormatter (PRF-07)

```cpp
enum class TimeStyle { H24, H12 };
enum class MeridiemCase { UPPER, LOWER };                // AM and PM, or am and pm; only used with H12
struct TimeFormat
{
    TimeStyle m_Style = TimeStyle::H24;
    bool m_Seconds = false;
    MeridiemCase m_Meridiem = MeridiemCase::UPPER;
};

std::string format_time(const LocalTime&, TimeFormat);   // "10:05", "10:05:09", "10:05 AM", "10:05:09 am"
```

Pure formatting of an already-localised time. Output is locale-independent so the export never changes with the phone's
language.

Tests (`unit`):

- 24-hour: `00:00`, `00:05`, `09:07`, `12:00`, `23:59`.
- 12-hour, upper case (the 12-hour default): `12:00 AM`, `12:05 AM`, `09:07 AM`, `12:00 PM`, `01:15 PM`, `11:59 PM`. The hour
  keeps its leading zero.
- 12-hour, lower case: the same times with `am` and `pm`.
- With seconds: `09:07:03`, `09:07:03 AM` and `09:07:03 am`.
- All eight combinations of style (2), seconds (2) and meridiem case (2) are covered; the case option has no effect on 24-hour.
- Zero-padding of minutes and seconds; no locale dependency (the test sets a non-English `LC_ALL` and expects identical
  output).

### Log, Entry and entry kinds (Data model, NOTE-02, REV-03)

```cpp
using EntryId = Uuid;                                      // 16 bytes, formatted lower-case 8-4-4-4-12

struct ErrorEntry        { ErrorTypeId m_ErrorType; std::optional<std::string> m_AttachedNote; };
struct NoteEntry         { std::optional<NoteTemplateId> m_Template; std::string m_Value; };   // value text when free
struct HeaderChangeEntry { HeaderField m_Field; std::string m_NewValue; };
enum class HeaderField { UNIT_NUMBER, LOCATION, SOFTWARE_VERSION };

struct Entry
{
    EntryId m_Id;
    Timestamp m_Captured;
    std::optional<std::chrono::sys_time<std::chrono::milliseconds>> m_EditedUtc;
    bool m_Deleted = false;
    std::variant<ErrorEntry, NoteEntry, HeaderChangeEntry> m_Body;
};

class Log
{
public:
    enum class Error { UNKNOWN_ENTRY, DUPLICATE_ID };
    const HeaderValues& initial_header() const;
    std::span<const Entry> entries() const;                // insertion order; time order via sorted_entries()
    std::vector<const Entry*> sorted_entries(bool include_deleted = false) const;  // by Captured.utc, ties by insertion
    std::expected<void, Error> add(Entry);
    std::expected<void, Error> set_deleted(EntryId, bool deleted);                 // soft delete and restore
    std::expected<void, Error> replace(EntryId, Entry);                            // review edits; sets m_EditedUtc
    std::size_t count_for(ErrorTypeId) const;              // live error entries only, for badges and totals
};
```

`HeaderValues` holds the log's initial unit number, location (a location id or free text) and software version, each
optional, plus facilitator and local date. Entries are never physically removed in the core; Stage 2 decides what
leaves the database.

Tests (`unit`):

- Entries added out of order come back sorted by UTC time; equal times keep insertion order.
- Soft delete hides an entry from `sorted_entries()` and from `count_for()`, and restore brings it back.
- `set_deleted` and `replace` on an unknown id return `UNKNOWN_ENTRY`; adding a duplicate id returns `DUPLICATE_ID`.
- `replace` changes the time, re-sorts, and records `m_EditedUtc`; the captured zone is kept unless the edit gives one.
- Counting ignores notes, header changes and deleted entries and counts each error type separately.
- A `Log` is copyable and two copies diverge independently (no shared mutable state).

### HeaderHistory (HDR-05, HDR-07, DAY-03)

Computes the header in force at any time from the log's initial values and its live header-change entries.
**Correct** fixes a typo and writes no entry. **Change** records a new value at a point in time (an entry).

```cpp
class HeaderHistory
{
public:
    explicit HeaderHistory(const Log&);
    HeaderValues at(std::chrono::sys_time<std::chrono::milliseconds>) const;   // in force at that instant
    HeaderValues start() const;                                                // what the export header block shows
    std::optional<std::string> previous_value(const Entry& change) const;     // the "old" side of a change line
};

// Operations on the log, so both behaviours are testable apart from any UI.
std::expected<void, HeaderError> change_header(Log&, HeaderField, std::string new_value, Timestamp, EntryId);
std::expected<void, HeaderError> correct_header(Log&, HeaderField, std::string value);
std::expected<void, HeaderError> correct_header_change(Log&, EntryId change, std::string new_value);
```

`correct_header` fixes the value currently in force: the latest live change for that field if there is one, otherwise the
initial value. The "old" value on a change line is derived from the history (`previous_value`), not stored, so a
correction flows into every later line without rewriting entries. The owner confirmed this reading of the PRD's data model. `change_header` validates through `UnitNumber` and `SoftwareVersion` for those fields and
rejects a change to the value already in force.

Tests (`unit`):

- Entries before a change see the initial header; entries after it see the new value; a second change on the same field
  stacks; changes to different fields are independent.
- A change at exactly the same millisecond as an entry applies to that entry (the change is in force at its own time).
- Correct with no changes rewrites the initial value and adds no entry.
- Correct a change entry: later lines show the corrected value, and the next change line's "old" side follows it.
- A change then a correction: `1.4.2` → `1.5.0` then correct to `1.5.1`; the line reads `1.4.2 → 1.5.1`, the header block
  still shows `1.4.2`.
- A correction then a change: correct `1.4.2` to `1.4.3`, then change to `1.5.0`; the line reads `1.4.3 → 1.5.0` and the
  header block shows `1.4.3`.
- Soft-deleting a change reverts the field to its previous value from that time on; restoring brings it back.
- A change whose new value equals the value in force is rejected; an invalid version or unit number is rejected with the
  component's error.
- A change that is edited earlier than a prior change (review screen) reorders correctly.
- Empty initial fields: a first change from empty renders its old side as empty (see golden `header-change-from-empty`).

### NoteTemplate (NOTE-04, PRF-06)

```cpp
struct NoteTemplate
{
    NoteTemplateId m_Id;
    std::string m_Name;            // "Played song"
    std::string m_ValuePrompt;     // "Which song?"
    std::string m_OutputPattern;   // "Played {value}"
    int m_SortOrder = 0;
};

enum class PatternError { MISSING_PLACEHOLDER, UNBALANCED_BRACE, UNKNOWN_PLACEHOLDER };
std::expected<void, PatternError> validate_pattern(std::string_view pattern);
std::string render_note(const NoteTemplate&, std::string_view value);   // "Played Morning Medley"
```

Only `{value}` is a placeholder. A literal brace is written `{{` or `}}`. The pattern must contain `{value}` at least once;
more than once substitutes each.

Tests (`unit`):

- `Played {value}` with `Morning Medley` gives `Played Morning Medley`; `Visitor: {value}` and `{value} played twice`.
- Pattern with two placeholders substitutes both; escaped braces `{{x}}` come out as `{x}`.
- Rejects a pattern with no placeholder, `{value`, `value}`, `{name}`.
- The value is inserted verbatim: a value containing `{value}` or `{` is not re-expanded.
- Surrounding whitespace in the value is trimmed; an all-whitespace value renders as an empty value (the UI blocks
  committing it, the core stays total).

### ExportRenderer (REV-01, REV-04, REV-05, PRF-10, PRD "Export format" rules 1 to 9)

```cpp
struct ExportContext
{
    std::string_view m_UnitPrefix;
    TimeFormat m_TimeFormat;
    bool m_IncludeTotals = false;
    const TimeZoneProvider& m_Zones;
    std::function<std::string_view(ErrorTypeId)> m_ErrorTypeName;        // retired types still resolve
    std::function<std::string_view(LocationId)> m_LocationName;
    std::function<const NoteTemplate*(NoteTemplateId)> m_Template;
};

enum class RenderError { UNKNOWN_ERROR_TYPE, UNKNOWN_LOCATION, UNKNOWN_TEMPLATE, UNKNOWN_ZONE };

std::expected<std::string, RenderError> render_export(const Log&, const ExportContext&);   // the whole log
std::expected<std::string, RenderError> render_header(const Log&, const ExportContext&);   // header block only
std::expected<std::string, RenderError> render_entry_line(const Log&, EntryId, const ExportContext&);
```

`render_entry_line` serves REV-04 (copy one line) and the timeline in Stage 3, so the review screen and the export cannot
drift apart (REV-01 requires the same wording). The two format constants that were provisional (the header date and
the space after `T|`, now accepted) are each defined once in `export_format.hpp`, so a change is a one-line edit followed by regenerating
the affected golden files.

Rules as implemented (the numbering is this plan's; it covers PRD rules 1 to 9 and adds line endings):

1. The header block is `Facilitator`, `Date`, `<prefix> #`, `Location`, `Software Version`, one per line, from
   `HeaderHistory::start()`. The date is the log's local date (`7 Nov 2026`), not derived from entries.
2. An empty field prints its label and a colon with no trailing space.
3. Error line: `<time> T| <name>`, then ` – <attached note>` (en dash, one space each side) when a note is attached.
4. Note line: `<time> (Free Text) <text>`, where the text is the rendered template or the free text.
5. Header-change line: `<time> <field label> changed: <old> → <new>`, with the unit-number label from the prefix.
6. Times in the zone each entry was captured in, formatted by `TimeFormatter`.
7. Deleted entries are skipped. With totals on, a blank line and a totals line follow the timeline.
8. Header block, blank line, timeline. LF line endings, UTF-8, no trailing whitespace on any line.
9. Free text containing line breaks is collapsed to single spaces, so one entry is always one line (confirmed).

Tests (`unit`): the golden files below, plus:

- Each rule in isolation with a hand-written expected string: rules 1 to 9 each have at least one named test case.
- `render_entry_line` of every entry in a log equals the matching line of `render_export`.
- Unknown error type, location, template or zone returns the matching `RenderError` and no partial text.
- Rendering is deterministic: two renders of the same log are byte-identical, and rendering does not change the log.
- The output contains no tab, no carriage return, and no character outside printable text plus `–`, `→`, `\n`.

### Configuration (PRF-01 to PRF-09)

```cpp
inline constexpr int CONFIG_SCHEMA_VERSION = 1;

struct Configuration
{
    std::string m_Facilitator;
    std::string m_UnitPrefix;
    TimeFormat m_TimeFormat;
    bool m_IncludeTotals = false;
    std::vector<ErrorType> m_ErrorTypes;     // id, name, retired, board position 1..11 or none
    std::vector<Location> m_Locations;       // id, name, sort order, retired
    std::vector<NoteTemplate> m_Templates;
};

struct ConfigError { enum class Code { MALFORMED_JSON, WRONG_TYPE, MISSING_FIELD, UNKNOWN_FIELD,
                                       UNSUPPORTED_VERSION, DUPLICATE, OUT_OF_RANGE, BAD_PATTERN, BAD_ID };
                     Code m_Code; std::string m_Path;      // JSON pointer, e.g. "/error_types/3/name"
                     std::string m_Message; };

std::expected<Configuration, std::vector<ConfigError>> import_configuration(std::string_view json);
std::string export_configuration(const Configuration&);    // stable key order, 2-space indent, LF, version first
```

The file shape is `{ "schema_version": 1, "preferences": {...}, "error_types": [...], "locations": [...],
"note_templates": [...] }`. Day logs are not part of it (PRD data model). JSON parsing uses `QJsonDocument` inside one
source file; the header exposes only strings and the structs above.

Validation, collected rather than stopping at the first problem: version present and not greater than the supported one;
required fields and types; ids are UUIDs and unique; names are non-empty and unique among live entries (case-insensitive);
unit prefix is non-empty and has no `-` or `#`; at most 11 board positions, each 1 to 11 and unique; note patterns pass
`validate_pattern`. The message text is plain English written for the person who prepared the file, naming the path and the
fix, and never echoes a whole value longer than 40 characters.

Tests (`unit`):

- Round trip: import of an exported configuration equals the original; export of an import is byte-identical to the input
  when the input was itself an export.
- `config/sample-config.json` (from work package 0.3) imports without errors. If 0.3 has not merged, the test uses a copy
  in `libs/core/tests/fixtures/` and the swap is noted in the PR.
- Rejects: not JSON, a JSON array at the top, missing `schema_version`, `schema_version: 2` (`UNSUPPORTED_VERSION`, message
  says to update the app), `schema_version: 0` or a string.
- Rejects: duplicate ids, duplicate names, two types on board position 3, position 0 and 12, a template without
  `{value}`, a malformed UUID, an empty facilitator, a prefix of `UNIT-`.
- Several faults in one file are all reported, in file order.
- A retired type keeps its board position out of the layout, and still round-trips.
- Unknown keys are rejected with `UNKNOWN_FIELD` (decided).
- `time_format` is `{ "style": "24h"|"12h", "seconds": bool, "meridiem": "upper"|"lower" }`; all eight combinations round-trip,
  an omitted `meridiem` takes the default, and any other value is `OUT_OF_RANGE`.
- Unicode names round-trip; a file with a byte-order mark is accepted.
- Limits: files over 1 MiB, or with over 500 error types, are rejected before parsing deeply (no unbounded work on an
  imported file).

### DayBoundary (DAY-01, DAY-02)

```cpp
enum class DayPrompt { NONE, OFFER_NEW_LOG };

std::expected<DayPrompt, TimeZoneProvider::Error>
day_prompt(std::chrono::year_month_day open_log_date,
           Timestamp now,
           const TimeZoneProvider&);

std::chrono::year_month_day local_date(const Timestamp&, const TimeZoneProvider&);
```

The prompt is offered when the device-local calendar date of `now` is later than the open log's local date. There is no
grace period; a log that runs past midnight stays open until the facilitator accepts the prompt (declining is the UI's
job, and the next foreground raises it again). The unshared flag is Stage 3 (`DayWatcher`), because it needs
`last_shared_utc`.

Tests (`unit`):

- Same date: `NONE`. Next date at 00:00:00 local: `OFFER_NEW_LOG`. 23:59:59.999 on the log's date: `NONE`.
- A log that runs past midnight: entries at 23:50 and 00:10 stay in the log, and the prompt is raised at 00:10.
- Several days later: `OFFER_NEW_LOG`. An earlier date than the log (clock set back): `NONE`.
- Daylight saving: Melbourne 2026-10-04 (23-hour day) and 2026-04-05 (25-hour day) roll over at local midnight, not at 24
  hours of elapsed time.
- The traveller case: a log opened in Melbourne, then `now` in Perth at 22:30 local on the same UTC day, gives `NONE`;
  at 00:30 Perth time it gives `OFFER_NEW_LOG`.

## Golden export files

Stored in `libs/core/tests/golden/<name>.txt`, one per case, each built from a scenario function in
`libs/core/tests/golden_scenarios.cpp` that also names the context (prefix, time format, totals, zones). The test renders,
reads the file as bytes and compares exactly; a failure writes the actual output beside the expected file in the build
directory and prints the first differing line. A `.gitattributes` rule marks `libs/core/tests/golden/*.txt` as `-text
eol=lf` so a Windows checkout cannot convert line endings. Names, ids and zones are fictional (Sam Example, UNIT, North
Site, Wrong Response, and the other error types of the proposed board).

"Date" means the header date format (accepted: `7 Nov 2026`). "T\|" means the space after the marker
(accepted: `HH:mm T| <error type>`). Every golden has a `Date:` line, so a change to the date format
regenerates all of them; only the goldens with error lines depend on the marker spacing. The totals-line wording is not in
the PRD at all and is a third provisional item (open question 1).

| File | Covers | Date | T\| | Totals wording |
|---|---|---|---|---|
| `prd-example.txt` | The PRD's example, byte for byte: five header lines, blank line, three errors, one free-text note, one mid-day version change, with `–` and `→` as UTF-8. | yes | yes | no |
| `empty-log.txt` | Full header and no entries: header block only, no trailing blank line. | yes | no | no |
| `empty-header.txt` | Every header field empty, plus a few entries (header banner case, HDR-06): `UNIT #:`, `Location:`, `Software Version:` with no trailing space. | yes | yes | no |
| `partial-header.txt` | Unit number set, location empty, version set; and location as free text from Other. | yes | yes | no |
| `attached-note.txt` | An error with an attached note after the en dash; an error with a note and a note-only line on the same minute; an attached note containing the characters `–` and `→`. | yes | yes | no |
| `templated-notes.txt` | Both confirmed templates (`Played {value}`, `Visitor interaction: {value}` as fictional patterns), a template with the placeholder mid-pattern, and a free-text note, all under `(Free Text)`. | yes | no | no |
| `version-change-midday.txt` | Software version changed mid-day, and a unit-number change and a location change in the same log; the header block keeps the start values. | yes | yes | no |
| `header-change-corrected.txt` | A change then a correction, and a correction then a change (the two `HeaderHistory` cases), showing the corrected "old" side. | yes | yes | no |
| `header-change-from-empty.txt` | First change on a field that started empty: empty old side. | yes | yes | no |
| `totals-off.txt` | A busy log with totals disabled; identical timeline to `totals-on.txt`. | yes | yes | no |
| `totals-on.txt` | The same log with totals on: blank line, then the totals line; includes a type used three times, once, and a retired type. | yes | yes | yes |
| `totals-on-empty.txt` | Totals on with no errors (decides whether a totals line appears at all). | yes | no | yes |
| `deleted-entries.txt` | A deleted error, a deleted note and a deleted header change are excluded; the totals and header history ignore them. | yes | yes | no |
| `twelve-hour.txt` | 12-hour time, upper and lower case, including noon, midnight and a PM change line. | yes | yes | no |
| `with-seconds.txt` | 24-hour with seconds, and 12-hour with seconds in both cases. | yes | yes | no |
| `custom-prefix.txt` | Prefix `DEMO`: label `DEMO #:` and value `DEMO-9`, and the same prefix on the unit-number change line. | yes | yes | no |
| `same-minute-order.txt` | Several entries in the same minute and the same millisecond: ordering by UTC, ties by insertion. | yes | yes | no |
| `past-midnight.txt` | Log dated 7 Nov with entries at 23:50 and 00:10 the next day; `Date:` stays 7 Nov. | yes | yes | no |
| `melbourne-dst-spring.txt` | Entries across 2026-10-04 02:00 → 03:00, including one edited into the missing hour. | yes | yes | no |
| `melbourne-dst-autumn.txt` | Entries across 2026-04-05 03:00 → 02:00: two entries both showing `02:30`, ordered by UTC. | yes | yes | no |
| `mixed-zones.txt` | Entries captured in Melbourne, Perth and UTC in one log, each rendered in its own zone. | yes | yes | no |
| `multiline-and-whitespace.txt` | Free text with line breaks, tabs, double spaces and trailing spaces collapsed to one line. | yes | no | no |
| `retired-type.txt` | An entry whose error type was retired later still prints its name. | yes | yes | no |

The first golden to be written, reviewed and merged is `prd-example.txt` (work package 1.2), before any other renderer
code. If the owner changes either accepted format, the change is a constant in `export_format.hpp` plus a
regeneration of the files in the table, reviewed as a diff.

## Work packages

One pull request each, in this order. 1.1 must merge first; 1.2 to 1.4 are independent of each other after it and may go
in parallel, but not two writers in the same files.

### 1.1 Foundations

Directory layout, `Uuid`, ids, `HeaderField`, the error-code conventions, the Catch2 helper for golden comparison,
`.gitattributes` for goldens, and the `docs/testing.md` section on golden tests and test fixtures. Update
`docs/architecture.md` for the layout. No behaviour yet; one trivial test per helper.

### 1.2 Validators and the first golden

`UnitNumber`, `SoftwareVersion`, `NoteTemplate` pattern handling, and a minimal `Log` with `Entry` kinds, enough to build
`prd-example.txt`. The golden file and a skeleton `ExportRenderer` that renders the header block only land here, with the
timeline test marked as expected to fail until 1.5, so the owner can review the contract bytes early.

### 1.3 Time

`Timestamp`, `TimeZoneProvider` with the Qt implementation and the fake, `TimeFormatter`, `DayBoundary`. The daylight-saving
and traveller tests land with it.

### 1.4 Log aggregate and header history

Complete `Log` (sorting, soft delete, replace, counts) and `HeaderHistory` with `change_header`, `correct_header` and
`correct_header_change`.

### 1.5 Export renderer and goldens

`render_export`, `render_header`, `render_entry_line`, totals, and every golden in the table. Removes the expected-to-fail
marker from 1.2. This is the only package that touches `export_format.hpp`.

### 1.6 Configuration

Schema, import with collected errors, export, limits, and the round-trip and rejection tests. Uses the sample file if 0.3
has merged.

### 1.7 Exit-gate pass

No new features. Run `linux-debug`, `linux-asan-ubsan` and the TSan job over the whole library, raise
`tools/coverage-floors.json` for `ej_core` to what the tests reach, confirm `tools/preprocessor-baseline.json` has not
risen, and record the result in the pull request.

## Cross-cutting

- **Documentation:** `docs/architecture.md` (layout, in 1.1), `docs/testing.md` (golden tests, in 1.1). No CMake option or
  preset changes, so `docs/building.md` is unchanged. No user-visible behaviour yet, so no `changelog.d/` fragment.
- **Licence headers:** every new source file carries the SPDX header copied from an existing file.
- **No platform `#if`:** the Qt time zone implementation is selected by CMake like any other source; there is no
  per-platform code in Stage 1 other than that.
- **Sanitisers:** `linux-debug` and `linux-asan-ubsan` locally before each pull request, as in AGENTS.md; TSan in CI.
- **Fictional data only (NFR-10):** every name, zone, prefix and note in tests and goldens comes from the fictional
  stand-ins; the confidentiality check from 0.3 runs over them.

## What cannot be verified here

The development machine has no Mac, so Stage 1 runs on Linux (and Windows where it builds) locally. Time-zone behaviour
depends on the platform's tz database: the Melbourne tests run on Linux CI against its tz data, and the same cases run again
on the macOS runner when 0.2 adds that leg. Nothing in Stage 1 touches iOS; the iOS-specific tz risk is checked on device in
Stage 5.

## Decisions and open questions

**Decided by the owner (7 Nov 2026)**

- The `T|` spacing (`10:05 T| Wrong Response`) and the header date format (`7 Nov 2026`) are accepted as written. They stay
  constants in `export_format.hpp`, so a later finding from the indexer is a one-line change and a golden regeneration.
- Correct fixes the value currently in force, and a change line's "old" value is derived from history, not stored.
- Digit limits stand: nine digits for unit numbers and for each version component; `0` is not a unit number.
- Every part of the clock format is a user preference (PRF-07), and the export uses whatever is set: 24-hour or 12-hour,
  seconds on or off, and `AM`/`PM` or `am`/`pm`. The defaults are 24-hour `HH:mm`, no seconds, and upper-case meridiem
  when 12-hour is chosen. The hour keeps its leading zero (`09:07 AM`). The meridiem case is an addition to PRF-07, so
  the PRD wording needs one phrase added and `time_format` in the configuration file gains a `meridiem` key.
- Unknown keys in a configuration file are rejected.
- Free-text line breaks are collapsed to a single space. Confirmed: the indexer never needs the line breaks.
- A trailing newline after the last export line does not matter; the plan keeps none.
- An empty header field prints its label and colon with no trailing space. Confirmed fine; the UI shows the missing-field banner (HDR-06).
- The C++ namespace is `ej` (headers stay under `include/error_journaling/`, per the template layout).

**Still open**

1. **Totals line wording.** The PRD does not give it. Proposal: `Totals: Wrong Response 3, Mute Delay 1`, in board order and
   then master-list order, types with zero left out, and no totals line at all when there are no errors. The owner will ask
   whether the downstream ticket indexer reads this line. Blocks only the totals goldens (`totals-on.txt`,
   `totals-on-empty.txt`); work package 1.5 can land the rest first.
