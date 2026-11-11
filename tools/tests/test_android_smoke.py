# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for android_smoke.py (label: unit), against a fake device: no emulator is needed.

Run with: python3 -m unittest discover -s tools/tests -v
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import android_smoke as smoke  # noqa: E402

ID = smoke.APPLICATION_ID


class Clock:
    """Time that moves only when the code under test sleeps, so a timeout costs nothing."""

    def __init__(self) -> None:
        self.now = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class FakeDevice:
    """Answers the adb commands Smoke uses, as an emulator with this application would."""

    def __init__(
        self,
        *,
        installs: bool = True,
        starts: bool = True,
        reports_home: bool = True,
        survives_home: bool = True,
        crash_line: str = "",
        view_filter: bool = True,
        send_filter: bool = True,
        offered_for_images: bool = False,
        backup: bool = True,
    ) -> None:
        self.installs = installs
        self.starts = starts
        self.reports_home = reports_home
        self.survives_home = survives_home
        self.crash_line = crash_line
        self.view_filter = view_filter
        self.send_filter = send_filter
        self.offered_for_images = offered_for_images
        self.backup = backup
        self.running = False
        self.pid = 4242
        self.log: list[str] = []
        self.commands: list[list[str]] = []

    def __call__(self, args: list[str]) -> str:
        self.commands.append(args)
        if args[0] == "install":
            return "Performing Streamed Install\nSuccess\n" if self.installs else "Failure [INSTALL_FAILED_OLDER_SDK]\n"
        if args[:3] == ["shell", "pm", "query-activities"]:
            text = " ".join(args)
            offered = (
                ("VIEW" in text and self.view_filter)
                or ("application/json" in text and self.send_filter)
                or ("image/png" in text and self.offered_for_images)
            )
            return f"1 activities found:\n  name={ID}.EjActivity\n  packageName={ID}\n" if offered else "No activities found\n"
        if args[:2] == ["logcat", "-c"]:
            self.log = []
            return ""
        if args[0] == "shell" and args[1] == "am" and args[2] == "start":
            if self.starts:
                if not self.running or not self.survives_home:
                    self.pid += 1 if self.running else 0
                self.running = True
                self.log.append("11-10 13:17:45.822 4242 4260 I ej      : lifecycle: active")
            return "Status: ok\n"
        if args[:4] == ["shell", "input", "keyevent", "KEYCODE_HOME"]:
            if self.reports_home:
                self.log.append("11-10 13:18:09.149 4242 4260 I ej      : lifecycle: inactive")
            if not self.survives_home:
                self.pid += 1  # the system ended the process while it was in the background
            return ""
        if args[:2] == ["shell", "pidof"]:
            return f"{self.pid}\n" if self.running else ""
        if args[:4] == ["logcat", "-d", "-s", "ej:I"]:
            return "--------- beginning of main\n" + "\n".join(self.log) + "\n"
        if args[:2] == ["logcat", "-d"] and args[2].startswith("--pid="):
            return (self.crash_line + "\n") if self.crash_line else "11-10 13:17:46 4242 4242 D nativeloader: ok\n"
        if args[:3] == ["shell", "dumpsys", "package"]:
            return "flags=[ HAS_CODE ALLOW_CLEAR_USER_DATA ALLOW_BACKUP ]\n" if self.backup else "flags=[ HAS_CODE ]\n"
        raise AssertionError(f"an adb command the fake does not know: {args}")


def run(device: FakeDevice, timeout: float = 30.0) -> list[str]:
    clock = Clock()
    return smoke.Smoke(device, sleep=clock.sleep, clock=clock, timeout=timeout).run("app.apk", ID)


class SmokeTest(unittest.TestCase):
    def test_a_healthy_application_passes(self) -> None:
        self.assertEqual(run(FakeDevice()), [])

    def test_the_package_is_installed_first_and_a_failure_stops_the_run(self) -> None:
        device = FakeDevice(installs=False)
        problems = run(device)
        self.assertEqual(len(problems), 1)
        self.assertIn("did not install", problems[0])
        self.assertIn("INSTALL_FAILED_OLDER_SDK", problems[0])
        self.assertEqual([c for c in device.commands if c[0] != "install"], [])

    def test_an_application_that_never_reaches_active_is_reported_after_the_timeout(self) -> None:
        problems = run(FakeDevice(starts=False), timeout=20.0)
        self.assertEqual(len(problems), 1)
        self.assertIn("did not reach 'lifecycle: active' within 20 s", problems[0])

    def test_missing_open_with_and_share_to_filters_are_reported(self) -> None:
        problems = run(FakeDevice(view_filter=False, send_filter=False))
        self.assertEqual(len(problems), 2)
        self.assertTrue(any("Open with" in p and "not offered" in p for p in problems))
        self.assertTrue(any("Share to of JSON" in p for p in problems))

    def test_being_offered_for_an_image_is_reported(self) -> None:
        problems = run(FakeDevice(offered_for_images=True))
        self.assertEqual(len(problems), 1)
        self.assertIn("image", problems[0])
        self.assertIn("must not be", problems[0])

    def test_going_home_without_an_inactive_line_is_reported(self) -> None:
        problems = run(FakeDevice(reports_home=False), timeout=10.0)
        self.assertTrue(any("going Home did not give 'lifecycle: inactive'" in p for p in problems))

    def test_a_process_ended_in_the_background_is_reported(self) -> None:
        problems = run(FakeDevice(survives_home=False))
        self.assertTrue(any("process changed" in p for p in problems), problems)

    def test_a_crash_in_the_applications_log_is_reported(self) -> None:
        for line in (
            "E AndroidRuntime: FATAL EXCEPTION: main",
            "E QtLoader: java.lang.UnsatisfiedLinkError: dlopen failed: library \"libc++_shared.so\" not found",
            "E x: java.lang.NoSuchMethodError: com.iainchesworthlabs.errorjournaling.EjShare.present",
            "F libc: Fatal signal 11 (SIGSEGV), code 1",
        ):
            problems = run(FakeDevice(crash_line=line))
            self.assertEqual(len(problems), 1, line)
            self.assertIn("has a fault", problems[0])

    def test_a_missing_backup_flag_is_reported(self) -> None:
        problems = run(FakeDevice(backup=False))
        self.assertEqual(problems, ["the package does not have the backup flag"])

    def test_the_component_started_is_the_launcher_activity(self) -> None:
        device = FakeDevice()
        run(device)
        starts = [c for c in device.commands if c[:3] == ["shell", "am", "start"]]
        self.assertEqual(len(starts), 2)  # the first start and the return from Home
        for command in starts:
            self.assertIn(f"{ID}/{smoke.ACTIVITY_CLASS}", command)

    def test_the_log_is_cleared_before_the_application_starts(self) -> None:
        device = FakeDevice()
        run(device)
        names = [" ".join(c[:2]) for c in device.commands]
        self.assertLess(names.index("logcat -c"), names.index("shell am"))

    def test_a_different_application_id_is_used_everywhere(self) -> None:
        device = FakeDevice()
        clock = Clock()
        # The fake answers for ID, so with another id the filters are "not offered": the id reaches the queries.
        problems = smoke.Smoke(device, sleep=clock.sleep, clock=clock, timeout=5.0).run("app.apk", "com.example.new")
        self.assertTrue(any("not offered" in p for p in problems))
        self.assertTrue(any("com.example.new" in " ".join(c) for c in device.commands))

    def test_this_repositorys_activity_matches_the_package_check(self) -> None:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))
        import check_android_package as package

        # The constant is ACTIVITY_CLASS where the application ID has been separated from the Java package (PR 179), and
        # ACTIVITY before: either way, the same class.
        activity = getattr(package, "ACTIVITY_CLASS", None) or getattr(package, "ACTIVITY")
        self.assertEqual(smoke.ACTIVITY_CLASS, activity)
        self.assertEqual(smoke.APPLICATION_ID, package.APPLICATION_ID)


if __name__ == "__main__":
    unittest.main()
