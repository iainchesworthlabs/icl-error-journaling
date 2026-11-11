# User guide: outline

This page is the structure of the user guide that Stage 10 builds (`docs/plan/stage-10.md`). It is not published: the site
leaves it out (`exclude_docs` in `mkdocs.yml`). A page moves out of this outline and into the site when its content is
written, after the Stage 7 wording fixes have merged.

The guide is for facilitators and for whoever prepares their setup file. It is not for developers. It uses the app's own
words for buttons, the stand-in names only (Sam Example, UNIT, North Site, Wrong Response, "the robot", "the downstream
ticket indexer"), and one task per section.

| Page | File | Sections |
|---|---|---|
| Getting started | `guide/getting-started.md` | What the app is for. Install. First launch: open a setup file you were given, or set up by hand. Your first entry. |
| Setting up the board | `guide/setup.md` | Your name, unit prefix and time format. Board layout: place, move and change the type of a tile; empty positions. Error types: add, rename, retire; what Other holds. Locations. Note templates: name, question to ask, line in the log, example. Import and export of a setup file. |
| A day of logging | `guide/logging.md` | Double-tap a tile to log (and why one tap does nothing). Hold a tile to log it with a note. Other and its search. Typing a note and Add. Template chips. The message after a tap: ×N and Undo. The Last bar. |
| Today's details | `guide/todays-details.md` | Unit and environment. Place. Software version and its commit; changing the version during the day. Date and facilitator. |
| Sharing and the next day | `guide/sharing.md` | Review: the timeline, edit and delete an entry. Share: what the text looks like and what the downstream ticket indexer reads. The new-day prompt and what happens to a log you did not share. |
| History | `guide/history.md` | Open a past log. Share it again. Delete it. What is kept. |
| The setup file | `guide/setup-file.md` | A complete example. A field reference for every key, matching `libs/core`. The rules (ids, unique names, board positions). Every message the app can show on import, with the fix. (Work package 10.2.) |
| FAQ | `guide/faq.md` | Short questions with short answers. |
| Troubleshooting | `guide/troubleshooting.md` | Symptom, cause, fix: the write-failed banner, recovery after a crash, an import refused, a share that did not go, an empty board, a missing tile. |

Privacy and Support are already published: [privacy.md](../privacy.md), [support.md](../support.md).
