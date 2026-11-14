# Setting up the board

Everything that makes the app yours is in **Settings**: the tiles on the board, your error types, places and note
templates, and your name and unit prefix.

![The Settings screen. At the top is Your board, a miniature of the tiles. Below are Error types, Note templates, Locations, Number suffixes, Facilitator, Unit prefix and a switch for a totals line.](img/board-settings.png){ width="270" }

## Your board

**Your board** at the top of Settings is a small copy of the Log screen's tiles. The board holds 11 error types and the
fixed **Other…** tile.

- **Move a tile:** hold and drag it to another place. The type keeps its colour.
- **Change a tile's type:** tap the tile, then choose a type from the list. If that type is already on the board, the two
  swap places.
- **Fill an empty place:** an empty place shows **+**. Tap it and choose a type, or **New error type…** to make one.

![The Add error type sheet: New error type at the top, then the list of error types, each with its position on the board or Not on the board.](img/board-type-picker.png){ width="270" }

## Error types

**Error types** is the whole list of what you can log. Add one with **Add an error type**. The first 11 on the list fill
the board in the order shown. The rest are found under **Other** on the Log screen.

- **Order:** drag the handle, or use the arrows. The top 11 are the board.
- **Rename or recolour:** tap a type.
- **Retire:** a type you no longer use. It leaves the board and Other, and stays on entries already logged.

![The Error types list: Add an error type, then the types on the board with arrows to move them.](img/error-types.png){ width="270" }

## Locations and number suffixes

**Locations** are the places you work in, for example North Site. They are the choices for Location in Today's details.
**Number suffixes** are the choices for the Number suffix on Today's details, for example `prod` and `dev`; **None** is always the first choice.

## Note templates

A template is a quick note you log often. Tap its chip above the note field on the Log screen, answer its one question,
and the line goes in the log.

![The list of note templates: Played song, which asks Which song and logs Played answer, and Visitor interaction.](img/templates.png){ width="270" }

Each template has three parts:

- **Name:** what the chip on the Log screen says.
- **Question to ask:** what you are asked when you tap the chip, for example "Which song?".
- **Line in the log:** what gets written. Tap **Insert answer** to put `[answer]` where your answer goes, for example
  "Played [answer]". The **Example** at the bottom of the screen shows the line as it will appear.

![The template editor for Played song, with Name, Question to ask, Line in the log, an Insert answer button and an example of the line.](img/template-edit.png){ width="270" }

## Your name, prefix and totals

- **Facilitator** and **Unit prefix** are the two things asked on first launch. They head every log.
- **Include a totals line** adds a line at the end of a shared log with the count of each error type.
- **Time format** and **Error prefix** are further down the list. The error prefix is the marker before each error line
  (`T|` unless you change it). Change it only if the tool that reads your log expects something else.

## Importing and exporting a setup file

**Import or export** in Settings moves your setup between devices or people.

![The Import or export screen: Import a setup file and Export this setup, and a note that a setup file does not contain any logs.](img/import-export.png){ width="270" }

- **Export this setup…** makes a setup file and offers it to the share sheet or the file picker.
- **Import a setup file…** reads one. The app shows what is in it first.

![The Import this setup screen: counts of error types, locations and note templates, and a warning that this replaces your current setup, with a Replace my setup button.](img/import-confirm.png){ width="270" }

Importing **replaces your setup** (board, locations, templates and preferences). Past logs are not touched, and a setup file
has no logs in it.

If the file has problems, the app lists every one, in the order they appear in the file, and changes nothing.

![The import errors screen: a list of problems found in the file, each saying where it is and how to fix it.](img/import-errors.png){ width="270" }

[The setup file](setup-file.md) explains every key and every message, for whoever prepares the file.

## The second board: Interactions (optional)

The app can show a second board, **Interactions**, for what you did as well as what went wrong. It is **off** until you turn it
on, and while it is off the app looks and works as it always has: four tabs, and the first one is **Log**.

To turn it on, open **Settings** and turn on **Second board: Interactions** (the **Second board** group, above the list of
setup rows). A fifth tab, **Interactions**, appears at once, and the first tab is now called **Errors**. The tabs are
**Errors**, **Interactions**, **Review**, **History** and **Settings**.

- The header card, the note box and the **Last** bar at the top are the same on both boards, so there is one note draft and
  **Last** shows the latest entry of either kind.
- The Interactions board has a strip with its name, and its tiles are a different set of colours from the Errors board, so you
  can tell the boards apart. The tab, the strip and the label of each tile say which board you are on; do not rely on colour.
- To change board, tap the tab, or swipe along the strip that says **Interactions**. A swipe anywhere else does nothing, so a
  slip over a tile never changes board.
- The app opens on the board you used last.
- Until you have added interaction types, the board is empty and offers **Add interaction types**.

Turning the switch off hides the tab and the board. **Nothing is deleted:** the entries stay in the log and in Review and
History, and what you set up is kept for the next time. If you were on the Interactions board, the app goes to the Errors board.


### Setting up the Interactions board

With the second board on, **Settings** shows two more rows in the **Second board** group: **Interaction types** and
**People**. The **Interactions** tab leads there too: while the board is empty, **Add interaction types** opens the
editor at once, and turning the switch on with nothing set up opens it for you.

**Interaction types** works like **Error types**. Tap **Add an interaction type**, give it a name and, if you want, a
**Line in the log**:

- Tap **Insert person** to put **[person]** in the line, for example “Spoke with [person]”. A type with **[person]** asks who
  after you log it. A type without it never asks, so a tile such as “Played song” logs at once.
- Tap **Insert answer** to put **[answer]** in the line, for example “Played [answer]”. You are asked for the answer after you
  log it.
- With no line, the log shows the name of the type.

The first 11 types fill the board in the order shown; drag the handle, or use the arrows, to change it. The rest are under
**Other…**. A type you retire leaves the board and stays on the entries already logged.
