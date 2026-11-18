# The shared log format

This page is for the people who read a shared log with a tool, or who want to know exactly what Field Test writes. The
text is plain, in UTF-8, with one entry to a line. The examples use made-up names.

The format is fixed so that a downstream tool can depend on it. The app's tests compare its output, byte for byte, with
files of examples.

## A whole log

```
Tester: Sam Example
Date: 7 Nov 2026
UNIT #: UNIT-27
Location: North Site
Software Version: v1.4.2

10:05 T| Wrong Response
10:20 T| Stopped Responding – caller said wait
10:30 (Free Text) Device restarted
11:02 T| Slow Response – asked about the schedule
13:15 Software Version changed: v1.4.2 → v1.5.0
13:40 T| Connection Lost

Totals: Wrong Response 1, Stopped Responding 1, Slow Response 1, Connection Lost 1
```

A log is three parts: the **header**, a blank line, and the **timeline**. A **totals** line follows after another blank
line, only if you turned **Include a totals line** on.

- Lines end with a line feed (`\n`). There is no line feed after the last line.
- The text is the same whether you tap **Share log**, **Copy log** or **Copy** on one entry, except that **Copy** carries
  just that entry's line.

## The header

Five lines, always in this order:

| Line | What it holds |
|---|---|
| `Tester: <name>` | Your name from Settings |
| `Date: <day> <month> <year>` | The day of the log, with a three-letter English month: `7 Nov 2026`. The month does not follow the phone's language |
| `<prefix> #: <unit>` | The unit prefix from Settings, then ` #:`, then the unit with its number and suffix: `UNIT #: UNIT-27-prod` |
| `Location: <place>` | The place chosen in Today's details |
| `Software Version: <version>` | The version, and the commit in brackets if you gave one: `v1.4.2 (a1b2c3d)` |

A field that is not filled in prints its label and the colon only: `Location:`.

Other versions you added in Today's details follow as further lines, one for each, written `<name> Version: <version>`, for
example `Influencer Version: v4.5.0`. A version with no number yet reads `not set`.

## The timeline

Each live entry is one line: `<time> <what>`. Entries are in the order they happened, not in the order of the clock face,
so a log that runs past midnight reads `23:50`, then `00:10`. An entry you deleted is not shown.

### The time

By default a time is a 24-hour clock without seconds: `09:07`. Settings can change it, and the choice applies to every
log, earlier days too:

| Choice | Looks like |
|---|---|
| 24-hour | `13:15` |
| 24-hour with seconds | `13:15:59` |
| 12-hour | `01:15 PM` |
| 12-hour with seconds | `01:15:59 PM` |

On a 12-hour clock the setup file can also choose lower-case `am` and `pm` (`meridiem`). A time is written in the time
zone the entry was logged in.

### An error line

`<time> <prefix> <error type>`, then ` – <note>` (an en dash with a space on each side) if the entry has a note:

```
10:05 T| Wrong Response
10:20 T| Stopped Responding – caller said wait
```

- The **prefix** is `T|` unless you change it in Settings, or `error_prefix` in a setup file. It is up to 12 characters.
  If you make it empty, the space after it goes too: `10:05 Wrong Response`.
- This marker is what the downstream ticket indexer reads. Do not edit it in a log you have shared.
- A note that has several lines is folded to one line.

### A note on its own

An entry with a note and no error is marked `(Free Text)`:

```
10:30 (Free Text) Device restarted
```

A note made from a note template is a note like any other: the template writes the words, and the log shows them.

### A change to the header

When a header field changes during the day (see [Today's details](todays-details.md#when-the-software-version-changes)), the log records it:

```
13:15 Software Version changed: v1.4.2 → v1.5.0
12:00 UNIT # changed: UNIT-28 → UNIT-31
```

The line is the field's label, ` changed: `, the old value, an arrow `→` and the new value. The entries before it keep the
old value. The header at the top shows the value the day started with. A change that fills a field that was empty
reads `Location changed:  → Back Room Annex`, with the old value empty.

### An observation

With the second board on, an observation is a line marked `(Interaction)`, with no error prefix, so the ticket indexer
does not read it:

```
10:12 (Interaction) Paired with A.S.
10:40 (Interaction) Paired with A.S., J.P. and P.S.I.
11:10 (Interaction) Paired with someone
11:20 (Interaction) Paired with (person not set)
```

- The marker is `(Interaction)` by default. The app keeps it as a preference (`interaction_marker`); an empty value drops
  the marker and its space. Whether Settings offers it was not checked for this page.
- A person's name follows **Names in shared logs**: initials (the default), the full name, or `someone`. A person who is
  forgotten reads `someone`. A type that asks for a person that was never chosen reads `(person not set)`.
- A note attached to the observation follows after ` – `, as on an error line.
- The people list itself is never in a log.

## The totals line

If **Include a totals line** is on and the log has at least one error, a blank line and this line close the log:

```
Totals: Wrong Response 3, Stopped Responding 1, Slow Response 2, Connection Lost 1
```

- It counts errors only, not notes, header changes or observations.
- Error types are in board order, then in the order of the full list; a type that is no longer in the list comes last.

## What this page does not promise

The totals wording (`Totals: `, a comma between types and a space before each count) is the app's own and is not agreed
with a downstream tool. If a tool depends on it, test it with a real log before you rely on it.

## See also

- [Sharing and the next day](sharing.md): how to share and what **Copy** does.
- [The setup file](setup-file.md): `error_prefix`, `time_format`, `include_totals`.
- [Settings](settings.md): the settings that change the text.
