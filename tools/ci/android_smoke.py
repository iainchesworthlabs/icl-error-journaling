#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Starts the built Android application on a running emulator and checks that it comes up and survives being backgrounded.

    python3 tools/ci/android_smoke.py --apk app.apk --application-id com.iainchesworthlabs.errorjournaling \\
        [--adb adb] [--artifacts DIR] [--timeout 90]

The emulator is started by the workflow (.github/workflows/android-emulator.yml). A Qt release build takes no start-up
arguments ("not allowed in non-debug mode"), so there is no `--smoke-test` here: the application is driven from outside,
and what it says is read from logcat, where the bridge's PerfMarks write the lifecycle lines under the tag `ej`.

It checks, in order:
1. the package installs;
2. Package Manager offers the application for "Open with" of a `content://` octet-stream file and for "Share to" of JSON,
   and not for an image (the intent filters take the setup file's types only);
3. the application starts and reaches `lifecycle: active`, which is the portable lifecycle coordinator running on the
   platform services: the whole of start-up (the native libraries, the SQLite store, the bridge) has worked;
4. Home gives `lifecycle: inactive`, and starting it again gives `lifecycle: active` in the same process;
5. nothing in that process's log is a crash, a missing native library or a missing Java method;
6. the package has the backup flag (docs/plan/stage-9.md, decision 5).

Every decision is in `Smoke`, which is given a function that runs adb, so tools/tests/test_android_smoke.py runs it
without a device. Only main() runs adb.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Callable

# The Java class of the launcher activity (see check_android_package.py): the application ID will change, this will not.
ACTIVITY_CLASS = "com.iainchesworthlabs.errorjournaling.EjActivity"
APPLICATION_ID = "com.iainchesworthlabs.errorjournaling"

ACTIVE = "lifecycle: active"
INACTIVE = "lifecycle: inactive"

# What a crashed or half-linked application leaves in its own log.
FAULTS = re.compile(
    r"FATAL EXCEPTION|UnsatisfiedLinkError|dlopen failed|NoSuchMethodError|ClassNotFoundException|Fatal signal|"
    r"AndroidRuntime: Shutting down VM"
)

Adb = Callable[[list[str]], str]

# The "Open with" and "Share to" intents Package Manager is asked about, and whether the application must be offered.
INTENTS = (
    ("Open with of an octet-stream content file", ["-a", "android.intent.action.VIEW", "-t", "application/octet-stream", "-d", "content://x/y.ejconfig"], True),
    ("Share to of JSON", ["-a", "android.intent.action.SEND", "-t", "application/json"], True),
    ("Share to of an image", ["-a", "android.intent.action.SEND", "-t", "image/png"], False),
)


class Smoke:
    def __init__(
        self,
        adb: Adb,
        *,
        sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic,
        timeout: float = 90.0,
    ) -> None:
        self.adb = adb
        self.sleep = sleep
        self.clock = clock
        self.timeout = timeout

    def lines_of(self, text: str) -> int:
        """How many times the application has logged `text` since logcat was cleared."""
        return sum(1 for line in self.adb(["logcat", "-d", "-s", "ej:I"]).splitlines() if text in line)

    def wait_for(self, text: str, count: int) -> bool:
        """True once the application has logged `text` at least `count` times, False after the timeout."""
        deadline = self.clock() + self.timeout
        while True:
            if self.lines_of(text) >= count:
                return True
            if self.clock() >= deadline:
                return False
            self.sleep(2.0)

    def process_id(self, application_id: str) -> str:
        return self.adb(["shell", "pidof", application_id]).strip().split(" ")[0]

    def run(self, apk: str, application_id: str = APPLICATION_ID) -> list[str]:
        """The problems found, in the order they were found; empty when the application is healthy."""
        component = f"{application_id}/{ACTIVITY_CLASS}"

        installed = self.adb(["install", "-r", apk])
        if "Success" not in installed:
            return [f"the package did not install: {installed.strip()[:200]}"]

        problems: list[str] = []
        for name, arguments, expected in INTENTS:
            offered = application_id in self.adb(["shell", "pm", "query-activities", *arguments])
            if offered != expected:
                problems.append(
                    f"{name}: the application is {'not offered' if expected else 'offered, and must not be'}"
                )

        self.adb(["logcat", "-c"])
        self.adb(["shell", "am", "start", "-W", "-n", component])
        if not self.wait_for(ACTIVE, 1):
            problems.append(f"the application did not reach '{ACTIVE}' within {self.timeout:.0f} s")
            return problems
        pid = self.process_id(application_id)
        if not pid:
            problems.append("the application is not running after it started")
            return problems

        self.adb(["shell", "input", "keyevent", "KEYCODE_HOME"])
        if not self.wait_for(INACTIVE, 1):
            problems.append(f"going Home did not give '{INACTIVE}'")
        self.adb(["shell", "am", "start", "-n", component])
        if not self.wait_for(ACTIVE, 2):
            problems.append(f"coming back did not give a second '{ACTIVE}'")
        if self.process_id(application_id) != pid:
            problems.append("the application was restarted by going Home and coming back (the process changed)")

        faults = [line for line in self.adb(["logcat", "-d", f"--pid={pid}"]).splitlines() if FAULTS.search(line)]
        if faults:
            problems.append(f"the application's log has a fault: {faults[0].strip()[:200]}")

        if "ALLOW_BACKUP" not in self.adb(["shell", "dumpsys", "package", application_id]):
            problems.append("the package does not have the backup flag")
        return problems


def save_artifacts(adb: str, directory: Path) -> None:
    """The whole log and a picture of the screen, for the workflow to upload. Never fails the run."""
    directory.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run([adb, "wait-for-device"], capture_output=True, timeout=120, check=False)
        log = subprocess.run([adb, "logcat", "-d"], capture_output=True, text=True, timeout=120, check=False)
        (directory / "logcat.txt").write_text(log.stdout, encoding="utf-8")
        # Not `exec-out screencap -p`, which returns nothing on some hosts: the picture is made on the device and pulled.
        remote = "/sdcard/ej-smoke.png"
        for attempt in (1, 2):  # a device that is busy can go offline for a moment
            subprocess.run([adb, "wait-for-device"], capture_output=True, timeout=120, check=False)
            made = subprocess.run([adb, "shell", "screencap", "-p", remote], capture_output=True, timeout=120, check=False)
            pulled = subprocess.run([adb, "pull", remote, str(directory / "screen.png")], capture_output=True, timeout=120, check=False)
            if made.returncode == 0 and pulled.returncode == 0:
                break
            if attempt == 2:
                print("warning: the screenshot could not be saved", file=sys.stderr)
            else:
                time.sleep(3.0)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"warning: no artifacts: {error}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--apk", required=True)
    parser.add_argument("--application-id", default=APPLICATION_ID)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--artifacts", type=Path, default=None)
    parser.add_argument("--timeout", type=float, default=90.0)
    args = parser.parse_args(argv)

    def adb(arguments: list[str]) -> str:
        result = subprocess.run([args.adb, *arguments], capture_output=True, text=True, timeout=180, check=False)
        return result.stdout + result.stderr

    # The emulator action returns when the emulator has booted; a device that is reconnecting is waited for, not failed.
    subprocess.run([args.adb, "wait-for-device"], capture_output=True, timeout=180, check=False)
    problems = Smoke(adb, timeout=args.timeout).run(args.apk, args.application_id)
    if args.artifacts is not None:
        save_artifacts(args.adb, args.artifacts)
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    if not problems:
        print(f"{args.application_id}: started, went Home and came back, and logged no fault")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
