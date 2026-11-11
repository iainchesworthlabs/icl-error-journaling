# The setup file

A setup file holds everything about how you work that is not in a day's log: your name, the unit prefix, the error types
and where they sit on the board, your places, your note templates and your time format. It is a plain text file with the
extension `.ejconfig`. You can export one from the app, change it in a text editor and import it again, or prepare one for
someone else.

A setup file has **no logs** in it. Importing one replaces your setup and leaves every past log as it is.

This page is for whoever prepares a file. If you only set up the board in the app, you do not need it.

## A complete example

The file below is the sample setup that the app's tests use. All the names in it are made up.

```json
{
  "schema_version": 1,
  "preferences": {
    "facilitator": "Sam Example",
    "unit_prefix": "UNIT",
    "time_format": {
      "style": "24h",
      "seconds": false,
      "meridiem": "upper"
    },
    "include_totals": false
  },
  "error_types": [
    {
      "id": "00000000-0000-4000-8000-0000000000a1",
      "name": "Wrong Response",
      "retired": false,
      "board_position": 1
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a2",
      "name": "Sequence Out of Order",
      "retired": false,
      "board_position": 2
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a3",
      "name": "Wrong Track Played",
      "retired": false,
      "board_position": 3
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a4",
      "name": "Track Not Found",
      "retired": false,
      "board_position": 4
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a5",
      "name": "Track Skipped",
      "retired": false,
      "board_position": 5
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a6",
      "name": "Audio Drop Out",
      "retired": false,
      "board_position": 6
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a7",
      "name": "Volume Jump",
      "retired": false,
      "board_position": 7
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a8",
      "name": "Mute Delay",
      "retired": false,
      "board_position": 8
    },
    {
      "id": "00000000-0000-4000-8000-0000000000a9",
      "name": "Speech Delay",
      "retired": false,
      "board_position": 9
    },
    {
      "id": "00000000-0000-4000-8000-0000000000aa",
      "name": "Track Delay",
      "retired": false,
      "board_position": 10
    },
    {
      "id": "00000000-0000-4000-8000-0000000000ab",
      "name": "Navigation Failure",
      "retired": false,
      "board_position": 11
    }
  ],
  "locations": [
    {
      "id": "00000000-0000-4000-8000-0000000000b1",
      "name": "North Site",
      "sort_order": 1,
      "retired": false
    },
    {
      "id": "00000000-0000-4000-8000-0000000000b2",
      "name": "South Site",
      "sort_order": 2,
      "retired": false
    }
  ],
  "note_templates": [
    {
      "id": "00000000-0000-4000-8000-0000000000c1",
      "name": "Played song",
      "value_prompt": "Which song?",
      "output_pattern": "Played {value}",
      "icon": "music",
      "sort_order": 1
    },
    {
      "id": "00000000-0000-4000-8000-0000000000c2",
      "name": "Visitor interaction",
      "value_prompt": "Who visited?",
      "output_pattern": "Visitor: {value}",
      "icon": "speech",
      "sort_order": 2
    }
  ]
}
```

## The rules for the whole file

- The file is text in UTF-8 and is JSON: names and text in double quotes, entries in `[ ... ]`, objects in `{ ... }`,
  commas between entries and none after the last one.
- The file can be at most 1 MiB, and each list can hold at most 500 entries.
- Every key is spelled exactly as below. **A key the app does not know is an error**, so a typo is found, not ignored.
- Every key marked *required* must be there. The three lists (`error_types`, `locations`, `note_templates`) are required
  but can be empty: `[]`.
- Text is never blank, and is at most the length in the tables.
- Every `id` is a unique identifier in the form `0f8fad5b-d9cb-469f-a165-70867728950e` (8-4-4-4-12 hexadecimal digits),
  not all zeros and used once in its list. The app keeps an entry's id when you export and import again, so do not change the ids in a file
  you exported. Make a new id for each new entry.
- Names must be different within a list, ignoring capitals, among entries that are not retired. A template's name must
  differ from every other template's.

| Limit | Value |
|---|---|
| <!-- limit:CONFIG_SCHEMA_VERSION --> The version number in `schema_version` | 1 |
| <!-- limit:CONFIG_MAX_BYTES --> Size of the file | 1,048,576 bytes (1 MiB) |
| <!-- limit:CONFIG_MAX_ENTRIES_PER_LIST --> Entries in one list | 500 |
| <!-- limit:CONFIG_BOARD_POSITIONS --> Tiles on the board (positions 1 to 11) | 11 |

## The top level

<!-- fields: top -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `schema_version` | Required | The version of the file format. | The number 1. A file with a higher number was made by a newer app; update the app. |
| `preferences` | Required | Your name, unit prefix and display choices. | An object, below. |
| `error_types` | Required | The error types. | A list, below. It may be empty. |
| `locations` | Required | The places you work in. | A list, below. It may be empty. |
| `note_templates` | Required | The quick notes. | A list, below. It may be empty. |

## Preferences

<!-- fields: preferences -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `facilitator` | Required | Your name, as it appears in the header of a log. | Text, up to 200 characters. |
| `unit_prefix` | Required | The letters before a unit's number. The app adds the dash and the number: `UNIT-27`. | Text, up to 20 characters, without `-` or `#`. |
| `unit_environments` | Optional | The environments the unit picker offers in Today's details, in the order shown. "None" is always the first choice and is not in the list. | A list of names, not empty, up to 500 of them, each up to 20 characters of lower-case letters, digits and dashes, not starting or ending with a dash, each used once. Without this key the choices are `prod` and `dev`. |
| `time_format` | Optional | How times are shown and shared. | An object, below. Without it: 24-hour clock, no seconds. |
| `include_totals` | Optional | Whether a shared log ends with a totals line, per error type. | `true` or `false`. Without it: `false`. |
| `error_prefix` | Optional | The text before the error name on each error line of a shared log. | Text, up to 12 characters, on one line; may be empty. Without it: `T|`. |

### Time format

<!-- fields: preferences.time_format -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `style` | Optional | The clock. | `"24h"` or `"12h"`. Without it: `"24h"`. |
| `seconds` | Optional | Whether times show seconds. | `true` or `false`. Without it: `false`. |
| `meridiem` | Optional | The capitals of AM and PM on a 12-hour clock. | `"upper"` or `"lower"`. Without it: `"upper"`. |

## Error types

Each entry in `error_types` is one kind of error. A type with a `board_position` has a tile on the board; the others are
found under Other.

<!-- fields: error_types -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `id` | Required | The type's identifier. | See the rules above. |
| `name` | Required | What the tile and the shared log call it. | Text, up to 200 characters. |
| `retired` | Optional | A retired type is one you no longer use. It has no tile on the board. | `true` or `false`. Without it: `false`. |
| `board_position` | Optional | Which of the 11 tiles on the board this type has. | A whole number from 1 to 11. Each position is used by one type that is not retired. A retired type keeps its number and is left out of the board. |

## Locations

<!-- fields: locations -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `id` | Required | The place's identifier. | See the rules above. |
| `name` | Required | What Today's details shows, for example "North Site". | Text, up to 200 characters. |
| `sort_order` | Optional | Where the place is in the list: the lowest first. | A whole number from -1,000,000 to 1,000,000. Without it: 0. |
| `retired` | Optional | A retired place is one you no longer use. | `true` or `false`. Without it: `false`. |

## Note templates

A template is a quick note. In the app you give it a name (the chip on the Log screen), a question to ask, and the line
that goes in the log, with an Insert answer button for the answer.

<!-- fields: note_templates -->

| Key | Needed | What it is | Allowed |
|---|---|---|---|
| `id` | Required | The template's identifier. | See the rules above. |
| `name` | Required | The text on the chip. In the app: Name. | Text, up to 200 characters. |
| `value_prompt` | Required | What you are asked when you tap the chip. In the app: Question to ask. | Text, up to 200 characters. |
| `output_pattern` | Required | The line that goes in the log. In the app: Line in the log. It must have `{value}` once, where the answer goes. | Text, up to 500 characters. Write `{{` or `}}` for a brace that is part of the line. No other placeholder is allowed. |
| `icon` | Optional | The picture on the chip. A name the app has no picture for shows the standard one. | Up to 32 characters of lower-case letters, digits and dashes. |
| `sort_order` | Optional | Where the chip is in the list: the lowest first. | A whole number from -1,000,000 to 1,000,000. Without it: 0. |

## When the app refuses a file

The app reads the whole file and lists every problem, in the order they appear in the file, and changes nothing until the
file has none. Each message names where the problem is and says how to fix it. The messages fall into these kinds.

<!-- messages: the kinds of problem, by the code the app gives them -->

| Kind | What it means | What to do |
|---|---|---|
| <!-- code:MALFORMED_JSON --> Not valid JSON | The file cannot be read as JSON, or is larger than 1 MiB. The message gives the place. | Open the file in a text editor and look at that spot for a missing comma, quote or bracket. Check that it is the right file. |
| <!-- code:WRONG_TYPE --> Wrong kind of value | A value is text where a number, a list or true/false is wanted, or the other way round. | Use the kind the tables above give for that key. |
| <!-- code:MISSING_FIELD --> A required key is missing | The key is not in the file or is `null`. | Add it. |
| <!-- code:UNKNOWN_FIELD --> A key the app does not use | A typo, or a key from another tool. | Remove it, or correct the spelling. |
| <!-- code:UNSUPPORTED_VERSION --> A newer file | `schema_version` is higher than the app reads. | Update the app, then import the file again. |
| <!-- code:DUPLICATE --> Used twice | Two entries have the same id or name, or two types on the board have the same position. | Give each entry its own id, name or position, or mark one as retired. |
| <!-- code:OUT_OF_RANGE --> Outside what is allowed | A value is blank, too long, too small or large, or not one of the choices. | Use a value that the tables above allow. |
| <!-- code:BAD_PATTERN --> A line that does not work | A template's `output_pattern` is wrong. | See the next table. |
| <!-- code:BAD_ID --> Not an identifier | An `id` is not 8-4-4-4-12 hexadecimal digits, or is all zeros. | Make a new one. |

Some of these problems the app also says in its own words, with the entry's number in its list:

| Problem | What the app says | What to do |
|---|---|---|
| <!-- problem:OTHER --> Anything else | The message above, as written. | Follow it. |
| <!-- problem:EMPTY --> Missing or blank | A required value is not there or is empty. | Fill it in. |
| <!-- problem:TOO_LONG --> Too long | A text is longer than its limit. | Shorten it to the limit in the tables. |
| <!-- problem:NAME_TAKEN --> Name used twice | Another entry has the same name. | Rename one, or retire one. |
| <!-- problem:POSITION_TAKEN --> Position used twice | Two types that are not retired have the same board position. | Give each its own position from 1 to 11. |
| <!-- problem:NO_PLACEHOLDER --> No answer in the line | A template's line has no `{value}`. | Put `{value}` where the answer should go, for example `Played {value}`. |
| <!-- problem:STRAY_BRACE --> A stray brace | A `{` or `}` is not closed or doubled. | Write `{{` or `}}` for a brace that is part of the line, and close every `{value}`. |
| <!-- problem:UNKNOWN_PLACEHOLDER --> An unknown placeholder | The line has a placeholder other than `{value}`. | Use only `{value}`. |

## Keeping the file out of the wrong hands

A real setup file names your people and places. Do not post it, attach it to a support request or put it in a public
place. The [demo setup file](https://iainchesworthlabs.github.io/icl-error-journaling/demo/demo-setup.ejconfig) has made-up names and is safe to share.
