# Feedback: filter the entries of a log

Owner request, 19 Nov 2026, for v1.0.0: the person reading a log should be able to narrow it, for example to the error
lines (the ones that carry the `T|` marker), or to the Observations, or to the entries that contain a word. This page is
the scope for the owner to agree before any code (AGENTS.md, "Gates"). It writes no production code. The stand-ins are
Sam Example and Alex Sample, as everywhere in the repository (NFR-10).

## What exists today

- The Review tab shows every entry of the open log in time order, in the words of the export (REV-01). The same screen,
  `Review.qml`, serves a past log opened from History, with a back control and "Share log again".
- Its rows come from `TimelineModel` (`apps/app/src/viewmodel/timeline_model.cpp`). A row has `entryId`, `timeText`,
  `lineText`, `fullLine`, `kind`, `prefixText` and `restText`. `kind` already tells an error from a note, a header
  change and an Observation.
- **Copy all** and **Share log** render the whole log from the document, not from the rows (`copyAll` calls
  `export_text`; the share service does the same). Copy line copies one row.
- There is no search or filter anywhere in the app. Nothing about it is in the user guide.

## Decisions this page proposes

1. **A filter changes what Review shows, never what the log is.** Copy all, Share log, Share log again, the entry count
   in the title, the unshared state (REV-05) and the exported text always cover the whole log. A person who filters to
   errors and taps Share still sends the complete day, because the downstream ticket indexer and the facilitator both
   rely on the shared text being whole. The filter bar says so in one line while a filter is on ("Share and Copy all
   still use the whole log").
2. **Two controls, combined with AND.**
   - *Kinds*: toggle chips **Errors**, **Notes**, **Observations**, **Changes** (header changes). None on means all.
     More than one on means any of them (Errors + Observations). The chip is called Errors, not `T|`, because the
     marker is a preference (PRF-13) and may be empty. A note with a template counts as a note; an error with an
     attached note counts as an error. The Observations chip appears only when the second board is on or the log
     already holds an Observation, so a tester who never turned the board on sees three chips.
   - *Keyword*: a text field. It matches the text of the row as Review shows it (time excluded), ignoring case, accents
     and extra spaces, every word of the field having to appear (so `wrong song` finds "Wrong Response ... song"). It
     searches the full names that Review shows, which never leave the device (PRF-17); what is typed is not stored.
3. **Same screen, both logs.** Because one `Review.qml` serves the open log and a past log, the filter is built once and
   is in both. Each log has its own filter state; leaving a log, starting a new log or restarting the app clears it.
   Nothing is stored (no change to `docs/privacy.md`).
4. **Rows keep their identity.** Tap to edit, swipe to copy or delete, Undo and the Edit entry sheet work on the shown
   rows as now, through `entryId`. If an edit makes an entry stop matching, it leaves the shown list when Save
   completes. An entry added from the Log tab while a filter is set is shown when it matches.
5. **Where it sits.** One row under the header card, one line high when idle: a **Filter** button that shows how many
   rows are shown ("Filter · 4 of 27" when on). Tapping it opens the chips and the field in place (a sheet on a phone;
   in place on iPad and the wide layout). The timeline does not move down by more than that one row, so one-handed
   reading is not worse for the person who never filters.
6. **States.** With a filter on and no match: "No entries match" with a **Clear filter** button. With nothing logged yet
   the filter control is hidden. A change of the shown count is announced to a screen reader ("4 of 27 entries").

## Open questions for the owner

1. **Copy what is shown?** The proposal is no: Copy all stays whole (decision 1). The alternative is a second action,
   "Copy shown lines", which is handy for pasting only the errors but puts a partial log on the clipboard that can be
   mistaken for the day's log. Recommended: not in v1.0.0.
2. **Filter by error type** (only Wrong Response)? The proposal is that the keyword covers it. A picker of types would
   be a third control and more mock-ups. Recommended: not in v1.0.0.
3. **Time range** (from 10:00 to 11:00)? Not proposed.
4. **Does the filter also belong in History** (search across logs)? The proposal is no: it is per log, as above. A search
   across days is "reporting across days", which is out of scope for v1.

## Tasks

Design first, as for the other screens: the owner approves the mock-ups before the QML.

1. Design: `docs/design/screens/ReviewFilter.dc.html` (and `.dark`), the idle row, the open state, the no-match state,
   the same on iPad portrait and landscape. Check at the largest text size and with the chips wrapping.
2. `TimelineFilterModel` in `apps/app/src/viewmodel/`: a filtering proxy over `TimelineModel` with `kinds`, `text`,
   `active`, `shownCount` and `clear()`. It maps a shown row back to the source row for `copyLine` and keeps
   `entryId`. It answers `kind` from the row, never from parsing text.
3. The keyword fold (case, accents, spaces) as a small function with its own tests, so Review and any later screen
   agree on what matches.
4. `Review.qml`: the Filter row, the open state, the empty state and the whole-log line; the rows read from the proxy.
   `copyAll`, share and `entryCount` keep reading the document. Past-log use passes the same property.
5. Accessibility: chips are buttons with a selected state, the field has a label, the count is announced, targets are at
   least 44 pt (NFR-02), text follows Dynamic Type.
6. In-app help and guide: the Review section of `docs/guide/logging.md` and the past-log section of
   `docs/guide/history.md`; a new screenshot entry in `docs/guide/screenshots.json` and its picture; the in-app help
   topic for Review (`HelpTopics.qml`), kept in step by `tools/ci/check_help_links.py`.
7. A fragment in `changelog.d/` (`log-filter.added.md`) saying that it is view-only and what it has not run on.
8. Nothing is stored, sent or backed up, so `docs/privacy.md` and the store privacy answers stay as they are.

## Tests

| Label | What |
|---|---|
| `unit` | Kind filter: each chip alone, two together, none = all; header changes and templated notes land in the right kind; an error with an attached note is an error. |
| `unit` | Keyword: case, accents, extra spaces, several words in any order, a word that appears only in a name (full name on screen), the time not matched, an empty field = no filter. |
| `unit` | Kinds and keyword combined with AND; clearing restores every row. |
| `unit` | A shown row maps to the right source row: `copyLine` on a filtered row copies that entry's full line; delete by `entryId` removes that entry. |
| `integration` | With a filter on, `copyAll` and the share text are byte-identical to the unfiltered text; `entryCount`, the title and the unshared state do not change. |
| `integration` | An entry added or edited while filtered appears or leaves the shown rows correctly; a new log and a restart clear the filter. |
| `ui` | Idle row, open state, empty state and Clear filter; chip selected state; the Observations chip is absent with the board off and no Observations; past log shows the same control. |
| `ui` | Largest text size: chips wrap, no clipping; the announce of "n of m entries" fires once per change. |
| `perf` | A 5,000-entry log: typing in the field updates the rows within a frame budget (no per-key work above the row count). |

Not run on a device: everything here is verified in CI and on the desktop build; the owner tries it on a phone and an
iPad before 1.0.0.
