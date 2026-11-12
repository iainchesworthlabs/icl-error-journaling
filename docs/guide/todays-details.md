# Today's details

The purple card at the top of the Log screen shows the unit, place, software version, date and your name. Tap it to open
**Today's details**. These fields head the log you share.

![Today's details: Facilitator, Date, Unit number, Environment, Location and Software version, and a Start logging button.](img/todays-details.png){ width="270" }

You can log errors before every field is filled in. While a field is empty the card says **Not filled in yet** and names it.

## The fields

- **Facilitator** and **Date** are filled in for you. Change the name in Settings.
- **Unit number:** type the number only. The prefix from Settings is added for you, so `27` shows as `UNIT-27`.
- **Environment:** tap to choose. **None** is the first choice. The rest come from **Unit environments** in Settings. It is
  saved with the unit number.
- **Location:** choose a place from your list, or **Other…** and type where you are.
- **Software version:** the version you are running, in the form `1.4.2` (numbers only).
- **Commit:** the short code (7 to 12 letters and numbers) that identifies the exact build. Leave it empty if you can't get it.

![The Environment sheet: None, prod and dev.](img/environment.png){ width="270" }

## When the software version changes

If the version changes during the day, for example an over-the-air update, change it in Today's details. The app asks
**What happened?**

- **It changed during the day:** adds a timed entry to the log, so the entries before it keep the old version and the ones
  after it use the new one. You can set the time it changed.
- **I typed it wrong:** corrects the value. Nothing is added to the log.

![The Software version sheet: the choice between It changed during the day and I typed it wrong, From and To versions, a commit and the line that will be added to the log.](img/software-version.png){ width="270" }

## Starting another log

At the bottom of Today's details, **Start a new log for another unit or site** keeps today's log in History and opens an
empty one. Only the facilitator carries over. Use it when you move to another unit or site on the same day. A new day
starts its own log (see [Sharing and the next day](sharing.md#the-next-day)).
