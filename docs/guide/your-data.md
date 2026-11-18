# Where your data is kept

This page says what Field Test keeps on your phone, how it protects an entry from being lost, and what happens when the app
did not close properly. For what leaves the phone, and what does not, read [Privacy](../privacy.md); this page does not
repeat it.

## What is on the phone

Everything is in the space the system gives the app, on your phone or tablet. Other apps cannot read it. The app keeps:

- **Your logs**: each day's entries and the day's details.
- **Your setup**: the error types, places, note templates, observation types and preferences.
- **The people list**, if you use Observations, in a file of its own.
- **A note you started and did not add**, so that it is there when you come back.

The files use the system's data protection: they stay locked after a restart until the phone is first unlocked. A log
leaves the phone only when you share it or copy it.

## How an entry is saved

The app keeps each log in two forms: a database, and a **journal**, a file of records that the app only adds to. When you
double-tap a tile, the app writes the entry's record to the journal and makes sure it has reached the phone's storage
**before** it writes the entry to the database. It shows the entry as logged only when both are done.

That order is why the app never shows an entry as logged before it is saved, and why [“… wasn't saved”](troubleshooting.md)
appears if the write fails: nothing was added, and you tap the tile again.

## When the app did not close properly

A phone that runs out of battery, or an app that is stopped in the middle of a write, can leave the database and the journal
out of step. Every time the app starts, before the board appears, it checks them against each other:

| What the app finds | What it does | What you see |
|---|---|---|
| Nothing wrong | Nothing | Nothing |
| The database is behind the journal | Brings the database up to date from the journal | Possibly [“Your log was repaired”](troubleshooting.md#your-log-was-repaired) |
| The database is damaged or missing | Builds a new one from the journals, and puts the damaged files in a quarantine folder | “Your log was repaired” |
| A journal ends with half a record | Cuts off the half record | Nothing, or “Your log was repaired” |
| Some records are damaged | Rebuilds from the records that are good, and says how many were skipped | A message that says some entries could not be recovered |
| The open log cannot be recovered | Starts a new, empty log | A message that says so |

The files that the app replaces go to a folder called `quarantine` in the app's storage. The app does not delete a file
from a quarantine folder one by one. It keeps the newest few quarantine folders and removes older ones. The folders stay
on the phone and are not sent anywhere.

After a repair, open **Review** and read the last few entries against what you remember. The app rebuilds from what it had
saved, so an entry that was never saved cannot come back.

## Deleting

- **A day's log:** delete it in History. A deleted log cannot be brought back.
- **Everything:** delete the app. Its logs, setup and settings go with it.

## Backups

The app's files are part of the phone's normal backup, if you have one on, under your own account. The developer cannot see
it. A copy in a backup stays until the backup is replaced or deleted. See [Privacy](../privacy.md#your-device-backup).

## What was not checked for this page

- Whether any file is left out of the phone's backup. The page says only what [Privacy](../privacy.md) says.
- How many quarantine folders are kept on a given version (the code's default is the newest five).
- The exact wording of every message after a partial recovery. The pictures and messages in
  [Troubleshooting](troubleshooting.md) are the ones to follow.
