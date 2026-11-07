# Stage 0 spikes: share sheet and haptic

Work packages 0.4 and 0.5 of the [Stage 0 plan](stage-0.md). Both run on the empty application behind the
`ej_ios_bridge` interface.

!!! note "First device run, 7 Nov 2026"
    The owner ran TestFlight 0.1.0 (3) on an iPhone 17: both haptics and the share sheet work. The rest of the
    checklist below is still open; see [Device results](#device-results).

## What was built

- `error_journaling/ios_bridge.hpp`: an abstract `platform_services` with `present_share_sheet(text, done)`,
  `prepare_haptics()` and `fire_haptic(kind)`, plus `make_platform_services()`. The interface has no Qt in it.
  `src/platform/ios/ios_bridge.mm` (compiled with `-fobjc-arc`) implements it; `src/platform/desktop/ios_bridge.cpp`
  is the stub (share reports `unavailable`, haptics do nothing). CMake picks the directory; there is no `#if`.
- `error_journaling/qml_bridge.hpp` (`ej_qml_bridge`): a QObject over that interface. QML gets it as the required
  property `bridge` of the root window and calls `shareSpikeText()`, `prepareHaptics()`, `hapticImpact()` and
  `hapticSuccess()`, and listens to `shareFinished(outcome, activityType)` with outcome `shared`, `cancelled`,
  `unavailable` or `failed`.
- `Main.qml`: placeholder tiles in the board area. The first fires `hapticImpact()` from `MouseArea.onPressed`
  (touch-down) and calls `prepareHaptics()` when it is created; the second fires `hapticSuccess()` on a tap (the note
  path); the third shares the fixed text "Sam Example logged a Wrong Response on UNIT at North Site."

## What works (verified)

- The code compiles for the desktop and passes its unit tests (stub, outcome mapping, haptic routing, a completion that
  arrives after the bridge is destroyed) and the offscreen app smoke test.
- The iOS code compiles for a device in `ios-build.yml` (see the pull request for the run). A compile says the API
  calls exist and ARC is accepted; it says nothing about behaviour.

## What was awkward

- **The root view controller.** Qt owns the `UIWindow`; there is no public Qt API for its view controller, and the
  bridge has no `QWindow`. The spike walks `UIApplication.connectedScenes`, takes the foreground-active
  `UIWindowScene`'s key window, then follows `presentedViewController` to the top. If Qt ever moves to a different
  scene setup, or a second window appears, this is the line to revisit. The alternative, `QWindow::winId()` cast to a
  `UIView*` and `view.window.rootViewController`, is exact for a given window but needs the `QWindow` passed in; Stage 5
  should pass it if more than one window ever exists.
- **Threading back to Qt.** `UIActivityViewController.completionWithItemsHandler` and all UIKit calls are main-thread
  only, and on iOS the Qt event loop runs on the main thread, so the handler can emit the Qt signal directly. The
  `QPointer` guard covers a handler that fires after the bridge is gone. If the Qt loop is ever moved off the main
  thread, the handler must post with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- **The callback contract.** The system handler never runs when nothing was presented, so the failure path
  (no view controller) posts its callback to the main queue; the promise "after the call returns" holds on iOS. The
  desktop stub calls back immediately, so callers must not rely on either order.
- **Cancelled versus failed.** `completed == NO` with no error is "cancelled", including choosing an activity and then
  backing out. An activity type is only reported when one was chosen. Whether Slack reports as `shared` depends on
  Slack's extension completing, which is out of our hands.
- **iPad.** The popover anchor is set so an iPad does not raise; the product is iPhone only.
- **Haptic generators.** A generator that is not prepared costs a start-up delay, and the engine goes idle again after
  a few seconds. The spike prepares at creation and again after every fire; Stage 5 must also prepare when the
  board becomes visible and on `Qt::ApplicationActive`. The success haptic uses `UINotificationFeedbackGenerator`.
- **Qt touch handling.** `MouseArea.onPressed` is delivered from Qt's synthesised mouse events for a touch; whether
  that adds latency over a native `touchesBegan` is the open question for measurement.

## Recommended shape for Stage 5

Keep the interface as it is: a Qt-free `platform_services`, created once in `main.cpp`, owned by a thin `qml_bridge`.
Add to it only what a feature needs:

- `share(text) -> signal(outcome, activityType)`, as built. Stage 5 passes the formatted entry text instead of the
  fixed string and, if the real flow needs it, a file URL as a second item.
- `haptic(kind)` with `impact` for touch-down on a tile and `success` for a saved note; add `warning` only if a
  design needs it.
- Fire from `onPressed`, never `onClicked`, and prepare when the board appears and when the app becomes active.
- Tests stay against a fake `platform_services`; the iOS file stays thin so there is little to test there.

## Only a device can confirm

Not verified until someone has run these on an iPhone (the iPhone 17 for TestFlight, or any iPhone with a build of this
branch):

1. **The sheet appears** above the Qt window, with the text, and is dismissible by swipe and by tapping outside.
2. **Shared** is reported (`shared`, with a plausible activity type) after choosing Copy; **cancelled** after dismissing.
3. **Posting to Slack** works from the sheet with the Slack app installed, and the outcome and activity type the app
   shows for it. Use a test channel and the fictional text only.
4. **The share survives rotation and backgrounding** (portrait only, but check the app switcher and a phone call).
5. **Haptic fires on touch-down**, not on release: hold a finger on the tile and feel it at the moment of contact.
6. **Haptic latency under 100 ms** from touch to haptic, measured in Instruments (for example signposts around
   the touch handler and the call, read in Instruments on a connected device): the interval between the touch event and the
   `impactOccurred` call, and, if available, the actuator. Repeat cold (first press after launch) and after 10 s idle.
7. **The success haptic** feels different from the impact, and fires once per tap.
8. **No haptic on the simulator** is expected; the simulator proves only that nothing crashes.

Record the results (device, iOS version, numbers) in this file when they exist.

## Device results

TestFlight 0.1.0 (3), built from `develop` at `8b02a96`, on an iPhone 17, run by the owner on 7 Nov 2026.

| Check | Result |
|---|---|
| 1. The sheet appears and is dismissible | Works |
| 2. `shared` and `cancelled` reported | Not recorded |
| 3. Posting to Slack | Not tried |
| 4. Share survives rotation and backgrounding | Not tried |
| 5. Haptic fires on touch-down | Works; the owner is happy with it |
| 6. Haptic latency under 100 ms | Open: measured with the in-app timing line of Stage 5 (5.3), not Instruments (owner decision, 7 Nov 2026) |
| 7. Success haptic differs and fires once per tap | Works; the owner is happy with it |
| 8. Simulator | Not applicable |

The approach stands: the Stage 5 bridge keeps the root-view-controller sheet and the prepared impact generator fired
on the press. Rows 2 to 4 are part of the Stage 5 device checklist.
