# Glossary

The words Field Test uses, in alphabetical order.

**Attached note**
: A note that goes with an error, on the same line, after a dash. Hold a tile, or type in the note field and then hold.
  See [A day of logging](logging.md#log-an-error-with-a-note-hold).

**Board**
: The grid of tiles on the Log screen. It has 11 tiles you choose and the fixed **Other…** tile.
  See [Setting up the board](setup.md#your-board).

**Commit**
: The short code (7 to 12 letters and numbers) that names the exact build of the software under test. Optional, in
  [Today's details](todays-details.md#the-fields).

**Double-tap**
: Two quick taps on a tile. It logs the error. One tap only shows a hint.

**Error prefix**
: The marker before the error name on each error line of a shared log. It is `T|` by default. The ticket indexer reads it.
  See [The shared log format](shared-log-format.md#an-error-line).

**Error type**
: One kind of error you log, such as Wrong Response. Each has a name and a colour, and is a tile on the board or in
  **Other…**. See [Setting up the board](setup.md#error-types).

**Facilitator**
: The name the setup file uses for the tester. In the app it is **Tester**.

**History**
: The list of earlier days' logs. See [History](history.md).

**Journal**
: The file of records the app only adds to, kept beside the database so that no saved entry is lost.
  See [Where your data is kept](your-data.md#how-an-entry-is-saved).

**Location**
: The place named in the header of a log. You keep a list of places in Settings and pick one in Today's details.

**Marker**
: The text after the time on a line that says what kind of line it is: `T|` for an error, `(Free Text)` for a note,
  `(Observation)` for an observation.

**Note template**
: A chip above the note field. It asks a question and writes the line of the note for you.
  See [Setting up the board](setup.md#note-templates).

**Number suffix**
: A word after the unit number, such as `prod` or `dev`, giving `UNIT-27-prod`. It is also called an environment in a setup
  file (`unit_environments`).

**Observation**
: An entry on the second board: something you did or saw, as opposed to something that went wrong.
  See [Observations](observations.md).

**Other…**
: The last tile. It lists every error type, with search, and lets you add a new one.

**Quarantine**
: The folder where the app puts a file it replaced while repairing a log, instead of deleting it.
  See [Where your data is kept](your-data.md#when-the-app-did-not-close-properly).

**Retire**
: Take an error type, observation type or person out of use without losing the entries that name them.

**Review**
: Today's log in the words you will share, where you can change, copy or delete an entry.
  See [Sharing and the next day](sharing.md#review).

**Setup file**
: A text file (`.ejconfig`) that holds the board, places, note templates and preferences. It holds no logs and no people.
  See [The setup file](setup-file.md).

**Shared log**
: The plain text the app writes when you share or copy a log. See [The shared log format](shared-log-format.md).

**Tester**
: Your name in the header of a log, set on first launch and changed in Settings.

**Tile**
: One button on a board.

**Today's details**
: The header of the day's log: tester, date, unit, location and software version.
  See [Today's details](todays-details.md).

**Totals line**
: An optional last line of a shared log with the count of each error type.

**Undo**
: A button on the message after you log. It removes the entry completely, for five seconds.
