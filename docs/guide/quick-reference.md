# Quick reference

The gestures, times and limits of Field Test on one page. Each row links to the page that explains it.

## Gestures

| Do this | To do this | More |
|---|---|---|
| **Double-tap** a tile | Log that error with the time of the tap | [A day of logging](logging.md#log-an-error-double-tap) |
| **Tap** a tile once | Nothing is logged. The hint "Double-tap to log" shows | [A day of logging](logging.md#log-an-error-double-tap) |
| **Hold** a tile for half a second | Log the error with a note, as one entry | [A day of logging](logging.md#log-an-error-with-a-note-hold) |
| Double-tap the same tile again while the message shows | Count a repeat: the message reads ×2, ×3 and so on | [A day of logging](logging.md#undo-and-repeats) |
| Tap **Undo** on the message | Take the entry back completely | [A day of logging](logging.md#undo-and-repeats) |
| Tap **Other…** | Log an error that is not on the board, with search | [A day of logging](logging.md#other-errors-that-are-not-on-the-board) |
| Tap the **Last** bar | Open Review | [A day of logging](logging.md#the-last-bar) |
| Tap an entry in **Review** | Change its error, time or note | [Sharing and the next day](sharing.md#review) |
| **Swipe an entry left** in Review | Copy it or delete it | [Sharing and the next day](sharing.md#review) |
| **Hold and drag** a tile in the board layout | Move it | [Setting up the board](setup.md) |
| Swipe along the **Observations** strip | Change board (only on the strip) | [Observations](observations.md) |

## Times

| What | How long |
|---|---|
| Undo is offered | 5 seconds after you log |
| A hold logs with a note | Half a second |
| Dictation stops | When you tap the microphone, after a minute of silence, when you leave the app, or when you tap Add |

## Limits

| What | Limit |
|---|---|
| Tiles on a board | 11, and the fixed **Other…** tile |
| Error prefix | Up to 12 characters, on one line; may be empty |
| Unit prefix | Up to 20 characters, without `-` or `#` |
| A person's name | Up to 60 characters; two people cannot share a name |
| A setup file | Up to 1 MiB, and up to 500 entries in each list |

The limits of a setup file are listed in full on [The setup file](setup-file.md#the-rules-for-the-whole-file).

## What the shared log looks like

```
Tester: Sam Example
Date: 7 Nov 2026
UNIT #: UNIT-27
Location: North Site
Software Version: v1.4.2

10:05 T| Wrong Response
10:20 T| Stopped Responding – caller said wait
10:30 (Free Text) Device restarted
```

[The shared log format](shared-log-format.md) describes every line.

## Where to look

- Something went wrong: [Troubleshooting](troubleshooting.md).
- A word you do not know: [Glossary](glossary.md).
- A setting: [Settings](settings.md).
- What is kept, and where: [Where your data is kept](your-data.md).
