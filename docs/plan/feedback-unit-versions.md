# Feedback: unit suffix and software versions

Task breakdown, test list and UX options for three findings from the primary user's test of Today's details. Requirement
IDs refer to the [PRD](../prd.md). The owner can redirect on the options below; the recommended one is built.

## Findings

1. The tester wanted to type "27-prod" in the unit field. On seeing the Environment row they said: "Oh, I see what
   you've done. I find the word environment confusing."
2. The software version should show a "v" in front of the number. Nobody should have to type it.
3. Two versions matter, not one: the robot's software and the "influencer" (a phone app that controls the robot). More
   may follow, so the model must be a list, without a long list on a phone.

## What is there today

- HDR-08: the environment is optional, picked from a list (PRF-11) and "never typed". It is stored inside the unit text
  (`27-prod`). The data model already matches what the tester typed; only the word and the way of entering it confuse.
- HDR-04, HDR-09: one `SoftwareVersion` (major.minor.patch, optional commit) per log, stored as `1.4.2+a1b2c3d`, with
  Correct and Change (HDR-07) through the journal's `correct_header` and `header_change` records. The journal is the
  truth; SQLite (schema 2) is a projection of it.

## Options

### 1. The unit suffix

| Option | What the person sees | Cost |
|---|---|---|
| **A. Relabel (recommended)** | The row reads "Number suffix" with a footer "Added after the number, like UNIT-27-prod". The sheet is titled the same; Settings calls the list "Number suffixes". The model and PRD HDR-08 (picked, not typed) are unchanged. | Wording only |
| B. Type it | The unit field accepts `27-prod`, splits off a known suffix and selects it in the row. | Needs a keyboard with letters, so the numeric keypad of HDR-02 goes; breaks the owner's "never typed" decision |
| C. Inline chips | Chips "none / prod / dev" next to the number field instead of a row and a sheet. | New control, wide on small phones |

### 2. The "v"

One option only. The field shows a fixed "v" before the text (the field's prefix, like "UNIT-" before the unit number).
`SoftwareVersion::parse` accepts a leading `v` or `V` and drops it, so a pasted `v1.2.3` is fine and never doubles. The
stored text stays `1.2.3`, so old data needs no migration. `display()` (the log text, change lines, shares and exports)
now prints `v1.2.3` and `v1.2.3 (a1b2c3d)`.

### 3. Several versions

| Option | What the person sees | Cost |
|---|---|---|
| **A. Summary row and a sheet (recommended)** | The first version keeps its field, now named "Robot software version" with the commit under it. Below it one row "Other versions" shows the rest as "Influencer v4.5" (or "None" / "Influencer v4.5 · Gateway v2.0") and opens a sheet: one row per version, a label and a version field each, remove, and "Add a version". | One new sheet |
| B. All in one sheet | One summary row "Robot v1.2.3 · Influencer v4.5" is the only version control; every version, the robot's too, is edited in the sheet. | Breaks the Software version sheet (Change) flow and every test that types in the version field |
| C. Fixed second field | A second field "Influencer version" on the screen. | Does not grow past two versions, which the owner ruled out |

Data model for A: `HeaderValues::m_ExtraVersions`, an ordered list of `{label, SoftwareVersion}` next to the existing
`m_SoftwareVersion`, which stays the robot's. Labels are 1 to 20 characters (letters, digits, spaces and dashes), unique
without regard to case, and not "Robot". Stored as the new header field `extra_versions`, a text of one
`label=version` per line. Because it is a `HeaderField`, Correct, Change, the history at a time of day, the change lines
in the log and the SQLite projection follow without a second mechanism. Export prints `Software Version: v1.2.3`
exactly where it did, then one `<Label> Version: v4.5.0` line per extra version, none when there are none.

## Migration (no entry may be lost, P1)

- Journal: `extra_versions` is read when present and absent otherwise; a log without it has an empty list. Records
  already written stay valid and are not rewritten. Older builds cannot read a log with extra versions (as with
  environments).
- SQLite: schema 2 to 3 adds `day_log.extra_versions TEXT` (NULL for existing rows) in the existing migrator, which
  copies the database before it runs.
- The single version of a log becomes the robot's version unchanged.

## Tasks

1. Core: tolerant `v` in `SoftwareVersion::parse`, `display()` with `v`; `ExtraVersions` (label rules, stored text,
   parse); `HeaderField::EXTRA_VERSIONS` through `header_values`, journal, history and export.
2. Storage: column, schema 3, migration step, projection read and write.
3. View model: `LogSession` properties and invokables for the extra versions; text of the summary.
4. QML: relabel (A), "v" prefix, summary row, `ExtraVersionsSheet`.
5. Docs: PRD (HDR-02, 04, 08, 09, a new HDR-10, data model), guide pages, design notes, changelog fragment.

## Test list

| Label | Test |
|---|---|
| unit | `v`, `V` accepted and dropped; `vv1.2.3`, `v` alone refused; `display()` has one `v`; stored text has none |
| unit | extra versions: label rules (empty, 21 characters, "robot", duplicate in other case, `=` and newline), round trip of stored text |
| unit | journal: a log with and without `extra_versions` reads; header correct and change of the field; history at a time |
| unit | export header and change lines with 0, 1 and 2 extra versions; `v` in the existing golden text |
| integration | SQLite: new database at schema 3; a schema 2 database migrates with its logs intact and an empty list; round trip |
| unit | `LogSession`: add, edit, remove, summary text, validation messages |
| ui | header screen: label "Number suffix", fixed `v` in the version field, typed `v1.2` not doubled, summary row text, sheet add / edit / remove, large text |

## What was built, and what was left

Built: option 1A (relabel, in the Header, its sheet and Settings; the setup file key stays `unit_environments`), the fixed
"v" (2) and option 3A (summary row and sheet).

Left out on purpose, to keep the change small:

- The extra versions are edited as a Correct only. The model accepts a Change (`changeHeader("extra_versions", ...)`)
  and the export prints its line ("Other Versions changed: ..."), but no screen offers it; a robot update during the day
  uses the existing Software version sheet.
- The Software version sheet's "To" row still has no fixed "v" in it (its From row and its preview print the "v").
- The sheet of option 3A has no mock-up. The Header mock-ups (phone, light and dark) are updated; the iPad and fold
  variants are only relabelled.
- The commit of an extra version is kept if it is there, but the sheet has no field for it.
