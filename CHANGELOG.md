# Changelog

All notable changes are listed here. Each release section is assembled from the
fragments in `changelog.d/`.

## [1.0.0] - 2026-11-19

### Added

- **There is an About page.** The last row of Settings, “About Field Test”, opens it. It shows the version, the build number, the commit, the branch or tag when there is one, the date of the commit (UTC), the platform, the Qt version and the storage; the licence of the app (GPL-3.0-or-later) with a row that opens the full text; the address of the source code, which opens in the browser; and the open-source components the app is built with, each with its version, its licence and a page with the licence text. The list is made by the build from what the app really links, and the build stops when a library has no licence entry. Five quick taps on the Version row show the launch and tap timings. Not yet run on a device.

- **The app can follow the system text size and Reduce Motion.** The bridge reads the text size (Dynamic Type), Reduce Motion and whether VoiceOver is running, and tells the screens when one changes. No screen uses them yet. Not yet run on a device.

- **Android app.** The app now builds for Android, for 64-bit and 32-bit phones and tablets (Android 9 and later), with the share sheet, haptics, "Open with" and "Share to" for the setup file, the same file protection and backup as on iOS, and the lifecycle and accessibility signals. It has run on an emulator only, not on a device, and it is not published anywhere yet.

- **The Android app has its own launcher icon.** It is the same "f" and "t" on a board of tiles as on iOS, as an adaptive icon: it fits a circle, a rounded square or any other shape the phone's launcher uses, and on Android 13 and later it takes the colours of the phone's theme when themed icons are turned on. There is one icon, as Android has no dark one. Not yet run on a device.

- **The app now keeps its entries in the real store.** On launch the app recovers and opens its on-disk log and configuration file, and builds the board, timeline, undo, sharing and history services on them, so what the placeholder window reports as its storage is now the real one instead of "none". The window is still the spike placeholder; Stage 4 replaces it with the screens. A headless full-day test drives a day from the header to the new-day prompt and History through the same wiring.

- **Field Test now has its screens.** The app opens on the Log board, with tabs for Log, Review, History and Settings. You can set up by hand or import a setup file on first launch, fill in today's details, tap the board to log errors, add notes, undo, review and edit the log, share it, and change the setup in Settings, including its error types, locations, note templates, import and export. The History tab is a placeholder until the New day and History screens are added. Import and export use a file dialog on a Mac or PC; the phone's document picker is not connected to these rows yet. Only run off-screen so far, not yet run on a device.

- **ICL Error Journaling repository.** Created from the organisation template with the requirements, implementation plan and approved screens in `docs/`.

- **Haptics are prepared ahead of the touch.** The engine is woken when the board is shown and when the app becomes active, never while the app is inactive, so the tap haptic fires on touch-down without a start-up delay. A repeating wake-up exists but is off until it is shown to be needed. Not yet run on a device.

- **Share sheet bridge.** The bridge can now share a log's text, or a temporary file such as the setup export, through the system share sheet, one sheet at a time, and reports whether it was shared. It is not on a screen yet. It has not run on a device.

- **Launch and tap timings can be read on a device.** The app marks when the board is usable and how long a tap takes to reach the haptic, for Instruments, and prints one `launch:` line with timings only to the system log. A development-signed build can be made for profiling, and a desktop tool makes a year of fictional data to test it with. Not yet run on a device.

- **Confidentiality guard.** CI fails when a confidential term appears in tracked files, commit messages or the pull request text, and when a real `*-config.json` is committed. The term list comes from the optional `CONFIDENTIAL_TERMS` secret; without it the term check warns and is skipped. `config/sample-config.json` holds fictional values only.

- **Configuration file in the core library.** `ej_core` can now read and write the versioned configuration file, listing every problem in a file at once and refusing unknown keys and files from a newer app. Nothing changes for the person using the app yet.

- **Export renderer.** `ej_core` can now render a whole log as the export text: header block, timeline of errors, notes and header changes in the zone each was captured in, and an optional totals line. Twenty-seven golden files pin the format. Nothing changes for the person using the app yet.

- **Core library foundations.** `ej_core` gains `Uuid`, typed ids, the `std::expected` conventions and a byte-for-byte golden test helper, ready for the Stage 1 domain model. Nothing changes for the person using the app yet.

- **Core log and header history.** `ej_core` gains the full log (time ordering, soft delete, review edits, per-type counts) and the header history behind the Correct and Change actions. Nothing changes for the person using the app yet.

- **Core validators and the first golden.** `ej_core` gains unit number and software version validation, note template patterns, the log and entry types, and the first golden export file (the PRD example). The export renderer is a skeleton that prints the header block only; the timeline follows. Nothing changes for the person using the app yet.

- **You can change the text in front of the error name.** The log writes each error as `10:05 T| Wrong Response`. Settings has an Error prefix row, below Time format, with a page to change `T|` (up to 12 characters, or nothing), an example line and Reset. The Last bar, Review, History, Copy and Share all follow it, and so do past logs. A setup file can set it with the optional `error_prefix` setting.

- **Help is in the app.** A question mark at the top of the board layout, Error types, Note templates, Today’s details, Review and Import or export opens a short help sheet for that screen, with “Read more in the guide” at the foot. Settings has a new Help group: Quick help (the same topics as a list, with no connection needed), and the User guide, Support and Privacy pages, which open in your browser. A refused setup file has a “How to fix a setup file” row that opens the guide’s page of messages, and the Error types list says in one line which types are on the board. On the first three days a card on the Log screen says how to log (double-tap), add a note (hold) and take it back (Undo); Close hides it for good. The help text works offline. Not yet run on a device.

- **The Interactions types screen now has a miniature board.** Tap a tile to change its type, tap an empty tile to add one, or hold and drag a tile to move it, as on the error board. The guide and help now say that a type's line is its template. Not yet run on a device.

- **You can choose how a name is written in a shared log.** With the second board on, **Names in shared logs** in Settings offers Initials (the default), Full name and Someone. It applies to Copy, Share and a re-shared past log; Review and the Interactions board always show the full name. Not yet run on a device.

- **You can set up the Interactions board in the app.** With the second board on, Settings shows **Interaction types** and **People** under the switch. The empty Interactions board's **Add interaction types** now opens the editor straight away, and turning the board on with nothing set up opens it too. Add, rename, reorder and retire types, and give each a line such as “Paired with [person]”: a type with [person] asks who after you log it, one with [answer] asks a question, and one with neither logs at once. Not yet run on a device.

- **There is an optional second board, Interactions, with its own tab.** It is off until you turn on **Second board: Interactions** in Settings; with it off, the app is exactly as before, with four tabs and the first one called Log. With it on, the tabs are Errors, Interactions, Review, History and Settings, the app opens on the board you used last, and a swipe along the strip that says Interactions (and only there) switches back to Errors. The header card, the note box and the Last bar are shared by both boards. The board has its own colours, and until interaction types can be added it is an empty board that says Add interaction types. Turning the switch off hides the tab and keeps everything. Not yet run on a device; the tab labels at the largest Dynamic Type size are not checked on one.

- **The Interactions board fits an iPad and a foldable.** In a Regular window (an iPad in portrait) the board uses the same tile sizes as the Errors board and the five tabs sit in the 680 pt column. In a Wide window (an iPad in landscape) the board is in the left pane with its title strip, the log with both kinds of entry is in the right pane, and the tiles fill the pane and are never under 88 pt. Folding or unfolding keeps the draft, the focus and the board you were on. Not yet run on an iPad or a foldable; the foldable layouts that need the Stage 13 posture code (tab rail, book and tabletop) are drawn in the mock-ups only.

- **The Interactions board works.** With **Second board: Interactions** on in Settings, the board shows the interaction types you made (the app has none of its own, as it has no error types of its own). Double-tap a tile to log it, as on the Errors board; the count badge, the undo message and the hold-to-add-a-typed-note all work the same, and the Other tile lists the types that are not on the board. A tile whose line has a **[person]** slot, such as “Paired with [person]”, opens a list of people as soon as it is logged: type a name once, tick one or several people (up to 20, or **Everyone here** for the people of the place in force), tap **Done**, and pick them from the list next time. **Skip** leaves the entry as logged. A tile with an answer, such as “Began task [answer]”, asks for it in a small sheet. The entry is in the log before either sheet opens, so nothing is lost if you skip or close them. Names are kept on the phone only. Not yet run on a device; Share and Copy of a log that holds an interaction come with the export package, and Review does not edit interactions yet.

- **Interactions can be shared and copied.** A log with interaction entries now shares, copies and re-shares: each interaction is a line of its own, `10:12 (Interaction) Paired with A.S.`, with no `T|` marker so the ticket indexer does not read it, and the error lines are unchanged. A person is written as initials by default, in Share, Copy and Copy log alike; the choice (`full`, `initials`, `someone`) is stored as the preference `name_export` and has its Settings row in the Settings package of the interactions board. Review and the board show the full name. The list of people is never exported. Not yet run on a device.

- **An interaction shows in Review in the same words as the export, and can be fixed afterwards.** With the second board on, an interaction line reads like `Paired with Alex Sample`, with the full name on the screen; an interaction with no person shows "(person not set)". Tap it to open Edit entry and change the type, the people, the answer, the time or the note, then Save (Cancel keeps it as it was); this is also how to add the people to an entry logged with Skip. Swipe, Copy, Delete and Undo work as for errors. Not yet run on a device.

- **The board and the log sit side by side on the iPad in landscape.** The composer and the board are on the left, and the header card and today's log, with Copy all and Share log, are on the right. A tap on an entry opens Edit entry, a swipe gives Copy and Delete, and the Undo bar is over the foot of the log. The Last bar is not shown, because the log shows the last entry. The iPad's share sheet points at the Share log button. Landscape is not turned on yet. Not yet run on an iPad.

- **The iPad app runs in portrait and landscape, and in Split View, Slide Over and Stage Manager windows.** The iPhone stays portrait only. On an iPad the launch screen shows the background colour only. Not yet run on an iPad.

- **The iPad window cannot be made smaller than a phone.** In Stage Manager the window stops at 375 by 720 points, the smallest the screens are laid out for. The window's own minimum size is also kept on a desktop. Not yet run on an iPad.

- **A note being typed is kept when the app goes to the background, and the day is checked on return.** When the app becomes inactive it writes the note draft and finishes any write in flight inside a short background task, and when it becomes active again it runs the new-day check (once the log has loaded). The same flush runs at quit. Not yet run on a device.

- **Hold a tile to log it with a note.** Type a note in the box and hold an error tile for half a second: the error is logged with that note on the same line, once, and the box clears. One Undo takes the entry away and puts the note back in the box. With the box empty, holding a tile logs the error and opens the Edit entry sheet on it with the cursor in the note; Save adds the note and Cancel leaves the entry as it was. A tap is unchanged: it logs the error alone and leaves a typed note where it is. With VoiceOver, while a note is typed each tile also has a “Log <name> with note” item. The note box now says “Type a note, then hold a tile…”. Not yet run on a device.

- **Dictate a note with the microphone.** The note field has a microphone at its trailing edge (the left in a right-to-left layout). Tap it and speak: the words appear in the note as you say them, and a second tap, a minute of silence or leaving the app ends it. Recognition is the phone's own and happens on the phone (iPhone and iPad: on-device speech recognition; Android 13 and later: the on-device recogniser), so no sound or text is sent anywhere, and the microphone is not shown where the phone cannot do that. The microphone and speech permissions are asked for at the first tap. Run on an iPhone and an iPad; not yet on Android.

- **You can add and manage the people of the Interactions board.** With the second board on, **People** in Settings opens a list where you add a name (with a location if you want), rename it, retire it, bring it back, or Forget it, which removes the name from the app and leaves the entries showing “someone”. Not yet run on a device.

- **The list of people can remember a change of location.** The people journal has a new record, `set_person_location`, that moves a person to another location or to none, so a person no longer has to be added again to change it. The screen for it follows. A journal that never changed a location is written exactly as before; one that did is not readable by an older build, which sets a copy aside and keeps the people it could read. From this build on, the journal reader skips a record of a kind it does not know and goes on. Not yet run on a device.

- **Privacy manifest.** The iOS app now carries Apple's privacy manifest. It declares no tracking and no collected data, and gives a reason for each system API that needs one. Every iOS build checks the manifest against the executable, and checks that the app has no network code. Not yet run on a device.

- **Setup files import and export on the iPhone.** Choosing "Import setup" shows the Files picker, a setup file opened from AirDrop or Files starts the same import, and "Export setup" opens the share sheet. A file that is too large or cannot be read shows the same banner as a failed save.

- **Empty Qt Quick application.** The application is now a Qt Quick window with a placeholder for the board, portrait only on iPhone, with the `ej_core`, `ej_storage` and `ej_ios_bridge` libraries behind it.

- **Setup file: interaction types and the Interactions board switch.** A setup file can now carry `interaction_types` (a name, a board position from 1 to 11 or none, and an optional line such as `Paired with {person}` or `Began task {value}`) and `interactions_board_enabled`. Both are optional: a file without them imports as before and leaves your interaction types and the switch as they are. A file with a `people` or `residents` key is refused, because people are never in a setup file. The sample file shows the new keys. Nothing uses them yet: the Interactions board comes in a later build.

- **Setup files open in the app and can be shared.** A `.ejconfig` setup file (or a plain `.json`) opened from AirDrop or Files is handed to the import, and the file is deleted afterwards whatever the result. The setup can be written to a temporary file and shared from the share sheet. The app declares the file type, never opens a file in place and does not turn on file sharing. The Settings screens that use these are not built yet. Not yet run on a device.

- **The History tab and the new-day prompt now work.** The History tab lists your past logs, opens one for review, and lets you share it again or delete it. When midnight passes with a log open, the app asks whether to start a new log, share the old one first, or keep it open. The foot of Settings now shows the version of the app, and five quick taps on it show or hide the timing line used to check launch and tap speed on a phone. Only run off-screen so far, not yet run on a device.

- **Share sheet and haptic spikes.** The empty app has a placeholder tile that fires a haptic on touch-down, a success haptic and a share button, behind `ej_ios_bridge` (a desktop stub does nothing). They have not run on a device yet; see the spikes plan.

- **Launch and tap timings can be read on the phone.** Five quick taps on the version text show the launch time and the last, median and worst of the recent tap-to-haptic times, so the speed targets can be checked from a TestFlight build without a Mac. The tap time stops when the haptic call returns and does not include the actuator.

- **A unit can have an environment.** On Today's details, a new Environment row under the unit number opens a short list (None, prod, dev) and the choice is saved with the unit, so the log shows "UNIT-27-prod" in its header, in the unit change lines and in History; None, the default for a new log, keeps "UNIT-27". The environment is picked and never typed. A setup file may replace the list with an optional `unit_environments` key under `preferences` (one or more names of lower-case letters, digits and dashes, none repeated); a file without it keeps prod and dev, and the export writes the key only when the list is not the default. Logs from earlier versions read as before, with no environment. An older build cannot read a log or a setup file that has an environment. Not yet run on a device.

- **You can edit the environments of a unit in Settings.** "Unit environments", under Locations, opens a list like the one of locations: add a name (lower-case letters, digits and dashes, up to 20 characters; capitals are lowered as you type), tap one to rename it, drag the handle to move it, or remove it. Today's details offers them in that order after None. The last one cannot be removed, and a log whose unit has a removed environment keeps it. A setup file can still replace the list.

- **A software version can carry the commit of the build.** Today’s details has an optional Commit field under the software version, and the Software version sheet has the same field, so a change during the day can take the commit too. A commit is 7 to 12 letters and numbers (a to f): it is lower-cased, a pasted full 40-character hash is shortened to 7, and anything else gets a plain message; empty is fine. The log shows it after the version in brackets, “Software Version: 1.4.2 (a1b2c3d)”, in the version change lines and in the entry footnote; with no commit nothing changes. While it is empty the Log card’s “Not filled in yet” strip lists it as a reminder that never blocks logging or sharing. The commit travels with the version, as the version does: a new log starts without either. Logs from earlier versions read as before, with no commit. An older build cannot read a log that has a commit. Not yet run on a device.

### Changed

- **The "f" of the app icon is made of tiles.** It was one solid white shape; it is now six white tiles like the dark "t", on iOS and Android. Not yet run on a device.

- **The app has its own icon.** The placeholder (a dark square on yellow) is replaced by the owner's drawing of an "f" and a "t" on a board of tiles: a white "f" and a dark "t" on leaf green, the same in light and dark appearance. The Google Play icon is made from it. Not yet run on a device.

- **Setting up the board is easier to find.** A board with no error types says so on the Log screen and offers "Add error types"; an empty tile shows a plus and opens the board setup at that position. In Settings an empty tile reads "Add error type" and opens the choice of a type at once, whose new "New error type…" row names a type and puts it on that tile in one step. A type added in Settings > Error types goes onto the first empty tile, and a banner says where. "Replace type" is now "Change error type". Not yet run on a device.

- **CI is cheaper and stops stale queue runs.** A pull request no longer runs coverage unless it carries the `deep` label (the merge queue still does), the Linux build jobs may run 60 minutes, runs of ejected merge-queue entries are cancelled, and the toolchain and Qt packages install from a cache.

- **The user guide, support and privacy pages have a new address.** The links in the app (the help sheet and the Settings rows that open the guide) now open the pages at iainchesworthlabs.com/field-test/. Not yet run on a device.

- **Help: the User guide row opens the guide's contents page.** It used to open the site's home page. The site no longer publishes the developer and planning pages, and the support and privacy pages say the app is for iPhone and iPad and that support is through GitHub issues.

- **Double-tap a tile to log an error.** One tap on an error tile no longer logs anything, so an accidental touch makes no entry. Two quick taps on the same tile (the second within 0.3 seconds of the first) log one error, with the buzz and the press at the second tap, and the undo toast as before. A single tap shows “Double-tap to log” for a moment (at most once every five seconds) and logs nothing. Press and hold still logs with a note, and now logs at the hold, after half a second: with a note typed it goes on the error’s line and the box clears, and one Undo gives it back; with the box empty, the Edit entry sheet opens on the new entry. The Other tile still opens on one tap. With VoiceOver one activation logs one error, as before. Not yet run on a device.

- **A note no longer shows "(Free Text)" on the screen.** The Log screen, the Review screen, the Last entry bar, the Edit entry preview and the example in the note template editor now show a note as its time and your words, such as "10:30 Began task 1". Copy all and Share still write "(Free Text)" in the exported text, because the downstream ticket indexer reads those lines. Not yet run on a device.

- **Error entries remember the name they were tapped with.** `ej_core` now prints an error entry under the name it had when it was tapped, so renaming an error type changes only later taps and a past export reads as it did when posted. Changing an entry's type takes a new name. Entries with no stored name still use the current one. Nothing changes for the person using the app yet.

- **The shared log's first header line now reads `Tester: …`, not `Facilitator: …`.** Everything else in the log is unchanged. The downstream ticket indexer should be checked if it reads that line. Nothing stored changes: setup files, journals and the database keep the name `facilitator` inside. Not yet run on a device.

- **Other versions carry over to the next log.** When you start a new log, the names of the other versions you had (such as the phone app) come with it, with no number yet. Type the number each day; remove a name to stop it carrying on. Until the number is typed it shows as not set, in the log as “Influencer Version: not set”, and the Not filled in yet strip names it. Not yet run on a device.

- **The person who runs the sessions is called the “tester”, not the “facilitator”.** The Settings row, the first-launch and Today's details screens, the messages when a setup file or an import is refused, the user guide, the help and the store text now say “tester”. Nothing stored changes: setup files, journals and the database still use the name `facilitator` inside, and the shared log still starts with the line `Tester: …`, so existing files and the downstream ticket indexer read as before. The pictures in the guide still show the old word until they are retaken on a device. Not yet run on a device.

- **The note field takes several lines.** Return now starts a new line (tap Add to log the note). At rest the field is one line high, so the board does not move; while you type it opens over the board to five lines and then scrolls, and folds back when you leave it. Line breaks are kept in the draft, the stored log and the Review screen; the shared text still prints each note on one line. Not yet run on a device.

- **Holding an interaction tile with an empty note box opens the Edit entry sheet**, as it does on the Errors board, with the cursor in the note. The sheet also has the people and the answer, so the person list does not open as well. Not yet run on a device.

- **The help for Review and sharing tells you how interactions are shared.** An interaction is a line of its own with no `T|`, a person's name leaves the phone as initials unless you choose the full name or `someone`, and Review shows the full name. The guide pages for Review and History say the same. A test now checks that an error line is the same bytes whatever the name level, the label or the interactions beside it.

- **Names in shared interactions are checked end to end.** A test now copies and shares a log of real people at each name level and checks that a name is never written as a list. Not yet run on a device.

- **The board is larger on the iPad.** On an iPad in portrait the tiles are larger (about 218 by 152 points on an 11-inch iPad), with 17 pt labels, larger count badges and a 12 pt gap, in the same places as on the phone. The undo bar sits under the board, so it no longer covers the bottom row. In a window shorter than a phone, such as Split View on an iPad mini, the tiles get shorter so that the bottom row stays above the tab bar, and the Last bar gives way if they still do not fit. Not yet run on an iPad.

- **The iPad keeps the screens in a readable column.** On an iPad in portrait, the Log screen, Review, History, Today's details, Settings and its editors, and first launch sit in a centred column with wider margins, instead of stretching across the whole screen. The tabs share the same column. A phone, and a narrow Split View window, look as before. Not yet run on an iPad.

- **Sheets are cards on the iPad.** On an iPad, the Other picker, Edit entry, the version and time sheets and the other sheets open as a 560 point card in the middle of the screen, above the keyboard when it is up, and dismiss with a tap outside them or their Cancel button. The new-day and delete alerts are centred. A phone, and a narrow Split View window, keep the bottom sheet. Not yet run on an iPad.

- **On an iPad in landscape, the board and the log now sit side by side on every iPad.** The two-pane layout began at 1100 points wide, which left out the 10.2-inch iPad (1080 wide in landscape) and the iPads that are 1024 wide; it now begins at 1024. Portrait and Split View keep the single column. The phone is unchanged. Not yet run on a device.

- **On an iPad, Today's details lines up with its column, and the log pane's title no longer overlaps its count at large text sizes.** The title and Back of Today's details started at the screen edge while the fields were in the centred column; they now start at the column, as on Settings. In the log pane beside the board, "Today's log" and the entry count overlapped at large text sizes; the count now steps aside when there is no room. The phone is unchanged. Not yet run on a device.

- **On an iPad the title of a Settings page lines up with its list.** On a wide window the large title and the Back and Done actions of the Settings pages and editors sat at the screen edge while the list was in a centred column; they now start where the list does. The phone is unchanged. Not yet run on an iPad.

- **On an iPad, Edit entry is as tall as its content, and the miniature board in Settings is in a card.** Edit entry was a tall card with empty space under the fields; it now fits what it shows. The board at the top of Settings sits in a card like the other groups. The phone is unchanged. Not yet run on a device.

- **The launch screen shows the Log board.** While Field Test starts, the iPhone now shows the shape of the board (the header card, the note box, the Last bar, the 12 tiles and the tab bar, in plain grey) in light and dark, where it showed a plain yellow screen, so the app appears to open straight onto the board. The picture is drawn from the app's own screen layout, and a check fails the build if the two drift apart. Not yet run on a device: after installing a build, delete the old app first, because iOS keeps the old launch screen.

- **Animations run on the render thread and the phone can draw at 120 Hz.** The page transitions of Settings now fade on the render thread like the other animations, the app's property list allows a ProMotion iPhone to run at 120 Hz, and the durations and curves of sheets, the toast, pages and row swipes are recorded as desktop defaults. Not yet run on a device; the feel and the frame rate are checked on the iPhone.

- **The Interactions board is now called Observations.** The fifth tab, the board, its settings (**Second board: Observations**, **Observation types**), the Edit entry sheet, the person list, the empty board, the in-app help and the user guide use the new word, and the tab has an eye in place of the speech bubble. Nothing you saved changes: your setup file, your logs and the text you share are as they were. Not yet run on a device.

- **A person can be moved to another location.** In Settings, People, tap a name and choose a location under **Location** (or **No location**), then **Save**. Until now the location could only be chosen when the person was added. Entries already logged are not touched; the location only decides who **Everyone** ticks in the picker. Not yet run on a device.

- **The people list is easier to use.** The people you used last are chips at the top of the list (**Recent**), and people saved for the place that is in force today come first in the list. Not yet run on a device.

- **The app is called Field Test.** The home screen, the window title, the App Store and Google Play name, the setup and log file names and the documentation use the new name. The bundle identifier and the Android application id are now `com.iainchesworthlabs.fieldtest`, so the app is a new app record in App Store Connect and in the Play Console.

- **The app now speaks of software and hardware testing, not robots.** Today's details calls the first version "Software version" (it was "Robot software version"), the summary row and the log start with "Software" for it, and "Software" (not "Robot") is the name that another version cannot use. The privacy page no longer has a Children section.
The sample error tiles are now generic, such as Audio Dropout, Incorrect Action, Lost Connection, WiFi Connect Failed, Stopped Responding, Timed Out and Unexpected Stop. The sample observation tiles are generic too: Entered room 1, Began task, Start task and End task.
The music icon is gone. A setup file that names the icon `music` now gets the default speech bubble, as any icon the app does not know does; the sample and the guide use `list` and `speech`.

- **First launch starts with "Set up by hand".** Most people have no setup file, so setting up by hand is now the main button and "Import a setup file" is the second one under it. The Last bar on an empty Log shows "0 today" beside "No entries yet", as the design has it.

- **Adding an error type is the first row in Other.** "Add a new error type" is under the search field, so it is found without scrolling past a full board. When a search finds nothing the row reads "No match? Add “…”" and a tap fills the form with what you typed.

- **Review says how to use it.** The copy button at the top reads "Copy log" beside its icon, and a line under the list says "Tap a line to edit it. Swipe left for Copy and Delete."

- **Unit prefix and totals line are in one place in Settings.** They are rows on the Settings list, under Tester. The Tester page no longer repeats them under "In the shared log", so there is one place to change each setting.

- **The unit prefix and the totals line are on the Settings list.** "Unit prefix" and "Include a totals line" are rows under Tester, so nobody has to guess that they are behind it. They are still on the Tester page too.

- **Plainer wording on four screens.** First launch by hand asks for the "Unit prefix" and shows "For example UNIT" as an example. The note on a template's value prompt shows the line as the template editor does ("Began task [answer]"). The Error types headings are "On your board" and "Not on your board". The Import or export note no longer names AirDrop, so it is true on every platform.

- **The note template editor uses plain words.** A template now has a Name (the chip on the Log screen), a Question to ask (what you are asked when you tap the chip) and a Line in the log (what gets written), with one sentence at the top saying what a template is for. You no longer type curly brackets: an Insert answer button puts the answer in the line at the cursor, shown as [answer], and an Example under the fields shows the log line, such as "In the log: 10:30 (Free Text) Began task 1". A new template starts with its question and line made from its name. The refusal messages and the setup-file import messages use the same words, and the list says "Asks" and "Logs". Setup files are unchanged. Not yet run on a device.

- **The unit and the versions are easier to fill in.** On Today's details the Environment row is now called Number suffix and says where it goes ("Added after the number, like UNIT-27-prod"); Settings calls its list Number suffixes. The software version field is for the main software and shows a fixed v, so you type the numbers only (a typed v is not doubled), and every version prints as v1.2.3 in the log, in the change lines and in the shared text. A new Other versions row holds further versions with a name, such as the Influencer phone app: Add a version, name it, type its numbers, Done. They are printed under the Software Version line, for example "Influencer Version: v4.5.0". Logs from earlier versions open as before, with the software version they had. An older build cannot read a log or a backup that has other versions. Not yet run on a device.

- **A log is unshared again when it changes after you share it.** You can share several times a day, so a share is no longer the end of the day. A log counts as unshared when it has entries that were added, edited, deleted or restored since the last Share (or a corrected header that changes the text), or when it has entries and was never shared. Only Share counts: Copy all and copying a line do not mark a log as shared. The new-day prompt offers to share first only for an unshared log, and says when the log was shared and has changed since; with everything shared it only offers to start a new log. The same goes for the alert on Today's details, and History shows the empty circle and "Changed since sharing" for a log that changed after its share. Logs saved by an earlier build are upgraded on launch, and one that was shared reads as shared. Not yet run on a device.

- **Polish: the shared parts of every screen now match the approved designs.** Text is drawn at its exact size (13.5 and 10.5 pt are no longer rounded), tile labels keep the design's line spacing, and the icons are the designs' own drawings. The tab bar shows the selected tab by colour and weight only, the undo toast has no countdown bar, and the toast, banners, alerts, sheets and cards have the designs' shadows. Buttons under an open sheet or alert are no longer faded, compact titles no longer wrap ("Edit entry" on one line), and alerts, sheets, rows, fields, the time picker and the settings board use the designs' sizes and spacing. Not yet checked on a device.

- **Today's details, the first-launch screens and the import pages match their designs.** The large title collapses to the compact bar once Today's details has scrolled, and the way to the Software version sheet is the check mark of the version field. The Software version sheet is one card of From, To and Time, with the time chosen on a second sheet. The new-log alert offers "Share this log first" when the log has not been shared. First launch offers an import button, a set-up-by-hand button and a line about setup files, and a setup file that cannot be imported is listed as plain sentences with a warning sign each.

- **Polish: the Log screen and the Other picker now match the approved designs.** The template chips have their icons, the failed-tap banner names the error type, and the template prompt shows the line the note will have. The Other picker has the designs' rows, swatches and separators, in both light and dark. Sheets start their bar at the designs' height, and the Other tile reads "Other…". Not yet checked on a device.

- **The wording the approved designs show.** A setup file that cannot be imported is now described in plain sentences, such as "Error type 3 has no name." and "Locations 1 and 2 are both called North Site.", instead of the place in the file. A name that is already used is refused with "There is already an error type called “Stopped Responding”. Names must be different.", a note template pattern without `{value}` with "The pattern needs {value} where the answer goes", and a finished export names the real file ("field-test-setup.ejconfig is ready to send."). A setup file may also give a note template an `icon` ("music", "speech"); a file without one is read as before. The app also now knows the extra text the screens show (the version and entry count of a past log, the day it was shared, the colour name of an error type, the footnote of the edit sheet, a preview of a template, and the failed tap's name), which the screens will use as they are updated. Only run off-screen so far, not yet run on a device.

- **Polish: Review, Edit entry, the time picker and History now match the approved designs.** Review's header block, rows and Share button are drawn as designed (the "T|" and "(Free Text)" prefixes in the secondary colour, a share icon, no divider under the last row), a past log shows "12 entries · Shared 5 Nov at 16:58" on one line, and Edit entry has the type's colour beside its name, the time as a chip and the "Logged at ..." line under the shared line. The time is now chosen on a second, shorter sheet over Edit entry, with a "Now" chip. History rows show the entries and the software version, the date headings and the empty state sit where the designs put them, and the delete alert names the full date and now covers the tab bar. Not yet checked on a device.

- **Polish: Settings and its editors now match the approved designs.** The Settings list has six rows of 44 pt with the thin chevron and no counts, and the board, hint and list are spaced as drawn. The error types, locations and note templates editors have the designs' handle, cross, chevron, 56 and 64 pt rows, full-width separators, fields, buttons and wording ("Settings" and "Note templates" in the back button), and the error type sheet shows the colour's name, a centred Retire button and covers the tab bar. The note template preview shows the whole note line. Import or export has the file and share icons, link-coloured rows and the "Exported to Files" message with the file name. Reordering a location, and an error type, works by the handle (a drag, or the swipe up and down of VoiceOver) as well as by the arrows the design draws on Error types. The unit prefix and the totals switch moved to the Tester page, under "In the shared log". Not yet checked on a device.

### Removed

- **The version line at the foot of Settings is gone.** The “About Field Test” row, the last row of Settings, already opens the page with the version, build, platform and storage, so the same text is no longer repeated under it. Not yet run on a device.

### Fixed

- **A setup file sent by AirDrop opens in the app.** The setup file type no longer claims to be JSON, so the phone no longer shows the file as a page of text first; Field Test opens on the import. A plain `.json` file still offers the app. Not yet run on a device.

- **A template chip no longer wraps its name onto two lines when the name only just fits.** "Began task" could break after "Began", which made the chip, and the note box above the board, taller than they should be.

- **Two quick changes to one entry no longer overwrite each other.** Putting people on an interaction a moment after holding a note on it (or giving it an answer) used to keep only the later change, because each edit started from the entry as it was when it was made. Each edit is now worked out when its turn comes, from the entry as it is then, so both stay.

- **The People row on the Edit entry sheet of a held interaction tile now opens the person picker.** Holding an interaction tile with an empty note box opens Edit entry, but tapping its People row did nothing, so a person could only be chosen from Review. Not yet run on a device.

- **The Interactions board has no built-in tiles.** It showed six interaction types (Paired with, Began task, Entered room 1, Assigned, Start task and End task) that nobody had made. They are gone: the board shows only the interaction types you create in Settings or bring in a setup file, like the error types, and is empty with an **Add interaction type** tile until there is one. Types already saved in your setup stay. Not yet run on a device.

- **A name in Review stays right when the list of people changes.** The rows of Review and of a past log are made again when a person is added, renamed, retired or forgotten, so a name never lags behind the list. The People row of Edit entry opens the same person picker as the board (search, add a new person, Everyone here, up to 20 people).

- **The app fills the whole iPhone screen.** The black bars above and below the app are gone, and the tab bar now sits at the bottom of the screen over the home indicator.

- **The keyboard no longer breaks the layout on the iPhone.** Today's details, the sheets and the editors now end at the top of the keyboard, with the focused field fully in view and the bar and the status bar left where they are. The keyboard, the number pad included, goes away when you leave the page, change tab or close the sheet that you were typing in. Choosing a row such as Environment no longer brings the number pad back to the field you had been typing in.

- **"LAST" lines up with the text beside it.** On the Log screen's last-entry bar, the small "LAST" label now sits on the same baseline as the entry text instead of a few points above it.

- **Add ends dictation and puts the keyboard away.** Tapping Add on a note now ends dictation first and drops any words the recogniser had not yet delivered, so nothing is written into the box or the next note afterwards. Once the note is recorded, the note box also gives up focus, so the keyboard goes down and no longer covers the tiles. A note that is refused keeps its focus. Not yet run on a device.

- **No second bar beside the microphone in the note box.** With focus in the note box, a thin scroll mark was drawn at its right edge even when the note was one line and nothing scrolled, and it showed as a second edge just right of the microphone (for instance after the microphone and speech-recognition prompts closed). The mark now shows only when the note is taller than the box. Not yet run on a device.

- **Tapping the note box no longer pops up the Undo / Paste menu.** The box now just takes the cursor and the keyboard. Press-and-hold selection handles in the note box are off with it. Not yet run on a device.

- **The reading-order check ignores what a list clips.** The accessibility check of the Review screen no longer counts the part of a row that lies past the edge of the list, which made `review.cross_cutting` fail on Windows builds. The app itself is unchanged. Not yet run on a device.

- **Spike controls in the test build.** The board shows a tile that fires the impact haptic on press, a tile that fires the success haptic on tap, and a button that opens the share sheet and shows what it reported.

- **Large text no longer hides controls.** At the largest text size the Commit label sits above its value (the value was cut off), the buttons of an alert stay in view under a scrolling message, the Copy and Delete labels of a tall row stay in sight, and the undo toast is two lines at most instead of covering the board.

- **All twelve tiles stay reachable at every text size.** With the "Not filled in yet" strip, or at a large text size, the board's tiles shrink to fit above the tab bar (no smaller than 88 pt) and the Last bar gives way when even that does not fit. From a text scale of 1.2 up the header card shows two lines, the template chips are one row that scrolls sideways, and a long strip is cut with an ellipsis.

- **The Software version sheet shows its last line whole.** The note under the preview ("Entries before … keep …") is no longer cut off above the Log the change button at the usual text size.

- **The app starts a little faster.** The theme asked Qt for the list of installed fonts once for every text style, though only the monospaced one names a font family. It now asks once, and a style with no family does not look.

- **A long word on a tile now shrinks to fit its label.** The tile set a smaller size on a copy of the label's font that QML ignores, so only the label's own fit shrank the word, and on macOS that left "Disconnection" a little wider than its tile. The smaller font is now made by the type role, and the share is checked against the word as it is measured at that size. Not yet run on a device.

- **A tile label can use a little more of the tile’s width for a long word.** When a word does not fit on a line even at the smallest label size, the label now takes up to 4 pt from each side margin before the word is allowed to break. Not yet run on a device: on an iPhone, check the tile “Disconnection” at the default text size.

- **A long word on a tile no longer breaks.** A label such as “Disconnection” that was only a little too wide for its tile used to wrap its last letter onto a second line. The label is now made just small enough for its longest word to fit on a line, to no less than three quarters of its usual size; labels whose words fit keep their size. A word that cannot fit even then still breaks, and is never cut off. The font on a device can differ from the one the tests use, so the tiles are still to be checked on an iPhone.

- **The version fields show just the example number.** After the fixed “v”, the Software version field read “vFor example 1.4.2”; it now reads “v1.4.2”, and the Other versions field “v4.5.0”. Not yet run on a device.

- **The Software version sheet and its time sheet no longer move their bars to make up for the sheet's offset.** The sheet now places its content where the design's bars start, so the two sheets sit where the design has them without a correction of their own.

- **The last differences from the approved screens are closed.** An alert card hangs from one point and sits higher when it is taller, as the mock-ups have it; the panel of a selected tile on the Settings board is 12 pt above and below, and the board's Other tile reads "Other… (fixed)"; the line under "Start logging" goes once Today's details has scrolled, and the button then sits at the bottom of the safe area; the time picker no longer draws a colon between its columns, which its own design covers with the selection band, and its band sits 3 pt under the middle row as in the design; and the log line of the version sheet is set in a monospaced face (Menlo on the phone), which Qt on a desktop did not read from a list of families in one string. Not yet run on a device.

- **Edit entry and its time sheet no longer move their bars to make up for the sheet's offset.** The sheet now places its content where the design's bars start, so both sheets sit where the design has them without a correction of their own.

