# Observations

Observations is an optional second board. Use it for what you did around the unit, as well as what went wrong: who you
paired with, which task you began, which room you entered. It is **off** until you turn it on, and while it is off the app works as it
always has.

This page is the overview. The steps are in the pages it links to.

## How it differs from the Errors board

| | Errors board | Observations board |
|---|---|---|
| What it logs | What went wrong | What you did or saw |
| Tile types | Error types | Observation types, which you make yourself |
| After a double-tap | Logged at once | Logged at once, or asks who or for an answer if the type's line has a place for one |
| Line in the shared log | `10:05 T| Wrong Response` | `10:12 (Interaction) Paired with A.S.` |
| Read by the ticket indexer | Yes, by the `T|` marker | No: the line has no `T|` |
| In the totals line | Counted | Not counted |

The header card, the note box and the **Last** bar are the same on both boards, so there is one note draft, and **Last**
shows the latest entry of either kind.

## Turn it on

In **Settings**, turn on **Second board: Observations**. A fifth tab appears, and the first tab is now called **Errors**.
Turning it off hides the board and the tab. Nothing is deleted: the entries stay in the log, in Review and in History.
[Setting up the board](setup.md#the-second-board-observations-optional) has the steps.

The app has no observation types of its own. Until you make one, or import a setup file that brings some, the board is
empty.

## Observation types and their lines

Each type has a name and, if you want, a **line in the log**:

- `[person]` in the line asks who it was, after you double-tap. “Paired with [person]” writes “Paired with Alex Sample”.
- `[answer]` in the line asks for a word or two. “Began task [answer]” writes “Began task 1”.
- A line with neither never asks anything. With no line, the log shows the name of the type.

[Setting up the Observations board](setup.md#setting-up-the-observations-board) has the editor. A setup file can bring
observation types too, as `interaction_types` ([The setup file](setup-file.md#interaction-types)).

## People

The list of people is optional, and it stays on the phone. It is never part of a setup file, and the app never fills it
in. [People](setup.md#people) in the setup page covers adding, renaming, retiring and forgetting a person.

What a shared log shows for a person follows **Names in shared logs**:

| Setting | Writes |
|---|---|
| Initials (the default) | `A.S.` |
| Full name | `Alex Sample` |
| Someone | `someone` |

**Review** and the **Observations** board always show the full name, because they never leave the phone. A person you have
forgotten reads “someone”. A type that asks for a person, with none chosen, reads “(person not set)”.

## Review and sharing

An observation is a line of its own in [Review](sharing.md#observations-in-review). Tap it to change the observation, the
people, the answer, the time or the note. The shared text for observations is described in
[The shared log format](shared-log-format.md#an-observation).

## What it does not do

It does not keep a list of people outside the phone, count observations in the totals, or add anything to the lines the
ticket indexer reads. A log with no observations looks exactly as it did before the board existed.
