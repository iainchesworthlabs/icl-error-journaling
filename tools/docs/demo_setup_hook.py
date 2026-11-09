# SPDX-License-Identifier: GPL-3.0-or-later
"""MkDocs hook: publish the demo setup file at <site>/demo/demo-setup.ejconfig (docs/plan/stage-11.md, 11.3).

App Review and new users open this address in Safari, and the phone offers to open the file in CS Bingo Board. The
.ejconfig extension is what makes that work: Pages serves it as a download rather than showing JSON text, and the app
declares the type as its own. The file is config/sample-config.json byte for byte (stand-in names only, PRD NFR-10).
It is copied at build time and never committed under a second name: check_config_files.py allows only the sample.
"""

from __future__ import annotations

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "config" / "sample-config.json"
TARGET = Path("demo") / "demo-setup.ejconfig"


def publish(site_dir: Path, source: Path = SOURCE) -> Path:
    """Copy the sample setup into the built site and return where it went."""
    if not source.is_file():
        raise FileNotFoundError(f"the demo setup file's source is missing: {source}")
    target = site_dir / TARGET
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    return target


def on_post_build(config, **kwargs) -> None:  # noqa: ANN001 (MkDocs passes its own config type)
    publish(Path(config["site_dir"]))
