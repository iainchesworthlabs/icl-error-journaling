# Rolling out to a team

This page is for whoever sets Field Test up for other people: a team lead, or someone who looks after the tools. The aim is
that everyone starts with the same board, places, notes and settings, and nobody has to type them.

## The idea

You set up one phone, export a **setup file**, and send it to the others. Each person imports it. A setup file holds the
board, the places, the note templates and the settings. It holds no logs and no people.

## Prepare the file

1. On one phone, set up the board: error types, places, note templates, unit prefix, time format, error prefix and whether
   the shared log ends with a totals line. [Setting up the board](setup.md) has the steps.
2. The file holds the tester's name and the unit prefix (they are required in the file). Every phone that imports it shows
   that name until its owner changes it in **Settings**, so put a placeholder name in, or tell people to change it.
3. In **Settings**, **Import or export**, tap **Export this setup…** and save or send the file.

If you prefer to write the file by hand, or to change it with a script, every key is described on
[The setup file](setup-file.md). The reader is strict: a key it does not know is an error, so a typo is found and not
ignored.

## Check it before you send it

1. Import the file on a second phone. The app lists what is in it and asks you to confirm.
2. If it lists problems, it changes nothing. Each message says where the problem is in the file
   ([When the app refuses a file](setup-file.md#when-the-app-refuses-a-file)).
3. Share a log from that phone and read the text. Check the error prefix, the times and the totals line against what the
   tool that reads the log expects ([The shared log format](shared-log-format.md)).

## Send it

Send the file the way you send any file: a chat app, Mail, AirDrop or a shared folder. A setup file ends in `.ejconfig`.
On the other phone, opening the file offers to import it in Field Test.

Do not post a real setup file in a public place, or attach it to a support request. It names your places and your units. The
[sample setup file]({{ site_url }}demo/demo-setup.ejconfig) has made-up names and is safe to share.
[Keeping the file out of the wrong hands](setup-file.md#keeping-the-file-out-of-the-wrong-hands) says more.

## What import changes, and what it keeps

| Changes | Keeps |
|---|---|
| The board, locations, note templates and preferences (including the name and the unit prefix) | Every log, past and present |
| The observation types, if the file has them | The people list |

Importing **replaces** the setup on that phone. The app shows what will be replaced and asks first. If someone replaced
their setup by mistake, [Troubleshooting](troubleshooting.md#i-replaced-my-setup-by-mistake) says what to do.

## Update the team

Change the file, export again, and send the new file. Each person imports it. Keep the `id` of each entry as it was: the
app keeps an entry's id when you export and import again, and a new id is a new entry.

## Check that a phone has it

On the phone, open **Settings** and look at the board and the lists. The **About** page lists the version of the app and,
under **This build**, the commit.

## Limits to remember

A setup file is at most 1 MiB and each list holds at most 500 entries. The board has 11 tiles and the fixed **Other…** tile.
The whole list is on [The setup file](setup-file.md#the-rules-for-the-whole-file).
