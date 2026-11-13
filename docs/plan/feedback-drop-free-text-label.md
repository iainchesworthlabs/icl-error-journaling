# Feedback: no "(Free Text)" label on the screen

Tester feedback: the log shows "(Free Text)" next to a note; people want the time and their words.

## Decision

- The screens drop the marker: the Log screen, the Review rows, the Last entry bar, the Edit entry preview and the
  example in the note template editor show "10:30 Played Morning Medley". The stored note (`kind = note`) is unchanged.
- The export keeps "(Free Text)". PRD export rule 4 specifies it ("Note lines are <time> (Free Text) <text>") and the
  golden files cover it (`[rule4]`, `[rule9]`). Copy all, Copy line and Share are the export wording.

## Open question for the owner

Should the export marker go too? That changes PRD rule 4 and every golden file with a note line, and the downstream
ticket indexer must be checked first (it reads the `T|` lines and may ignore the note lines, but that is not known
here). Until the owner decides, the export is unchanged.

## Tasks and tests

1. `export_format::without_note_marker` in core: `[export][rule4]` unit test (also shows the export still has the marker).
2. `TimelineModel` rows, Last line and Edit preview, and `SettingsModel::previewTemplateLine`, use it: the existing
   viewmodel and UI tests are updated (`[viewmodel][timeline]`, `[ui]`); Copy all and Copy line stay export-worded.
