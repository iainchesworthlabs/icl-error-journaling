# Questions and answers

## Logging

**Why does one tap do nothing?**
A tile logs on a **double-tap**, so a touch by accident never makes an entry. One tap shows the hint **Double-tap to log**.
See [A day of logging](logging.md#log-an-error-double-tap).

**I logged the wrong error. Can I take it back?**
Tap **Undo** on the message within five seconds. After that, open the **Review** tab: tap the entry to change its error,
or swipe it left and tap **Delete**. See [Sharing and the next day](sharing.md#review).

**How do I put a note on an error?**
Hold the tile for half a second. If you typed a note first, the error is logged with it. If the note field is empty, the
entry opens so you can type one. You can also open any entry in Review and type a note there.

**Can I log before I fill in Today's details?**
Yes. The card at the top says what is not filled in yet. Fill it in when you can: it heads the log you share.

**The error I need is not on the board.**
Tap **Other…**. It lists every error type and has a search field. If the type does not exist yet, tap **Add a new error
type**.

**The software changed during the day. What do I do?**
Open Today's details and change the **Software version**, then choose **It changed during the day**. The entries before
the change keep the old version. See [Today's details](todays-details.md#when-the-software-version-changes).

**Can I log without a connection?**
Yes. The app does not use the network ([Privacy](../privacy.md)).

## Sharing

**What does the shared log look like, and what reads it?**
A header, then one line per entry: the time, the marker `T|`, the error type, and the note after a dash. The downstream
ticket indexer reads the `T|` marker. See [Sharing and the next day](sharing.md#share-the-log).

**Does copying the log count as sharing it?**
No. Only **Share log** marks a log as shared. Copying leaves it as **Not shared**.

**Why does History say "Changed since sharing"?**
You edited or deleted an entry, or added one, after you shared. Share it again to send the new version.

**The app asked to start a new log. What happens to yesterday's?**
It stays in History. If it had not been shared, the prompt says so and offers **Share … log first**. If you are still
working past midnight, tap **Keep … log open**.

## Setup

**How do I add or change an error type, a place or a template?**
In **Settings**. See [Setting up the board](setup.md).

**What is the difference between retiring a type and deleting it?**
You retire a type. It leaves the board and Other, and stays on the entries already logged, so no old log changes.

**How do I move my setup to another phone?**
On the first phone, **Settings**, **Import or export**, **Export this setup…**. Send the file to the other phone, open it
in CS Bingo Board and confirm. A setup file has no logs, so logs do not move. See
[Setting up the board](setup.md#importing-and-exporting-a-setup-file).

**Can I change `T|`?**
Yes: **Settings**, **Error prefix**. Change it only if the tool that reads your log expects something else.

**Where can I get a sample setup to try?**
There is a sample setup file on the [home page](../index.md), with made-up names.

## Your data

**Where are my logs?**
Only on this device, until you share one. Nothing is sent by the app itself ([Privacy](../privacy.md)).

**How do I delete a log?**
In **History**, swipe the log left and tap **Delete**, then confirm. A deleted log can't be brought back.

**Something else is wrong.**
See [Troubleshooting](troubleshooting.md), or [Support](../support.md).
