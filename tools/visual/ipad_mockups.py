#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write the iPad mock-ups of Stage 8 (docs/design/screens/*.ipad-portrait*.dc.html and *.ipad-landscape*.dc.html).

    python tools/visual/ipad_mockups.py [--check]

The phone mock-ups are written by hand. The iPad ones are many (each screen in two orientations and two schemes) and
share one set of layout rules, so they are generated from this file, with every colour taken from docs/design/tokens.json
and every size from the layout rules of docs/plan/stage-8.md ("Layout rules"). Change a rule here and in the plan page
together, then run the script again. With --check it writes nothing and fails when a file differs from what it would write
(so a hand edit of a generated file is caught).

The design frame is an iPad Air 11-inch: 820 x 1180 pt in portrait, 1180 x 820 pt in landscape, with a 24 pt status bar and
a 20 pt home indicator. The pages are plain static HTML (no bindings), so tools/visual/render_mockups.py renders them as they
are, at the size in their `$preview`.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCREENS = ROOT / "docs" / "design" / "screens"
TOKENS = json.loads((ROOT / "docs" / "design" / "tokens.json").read_text(encoding="utf-8"))

SAFE_TOP = 24
HOME = 20
TAB_CONTENT = 49
TAB_BAR = TAB_CONTENT + HOME
MARGIN = 24
GAP = 12
TILE_GAP = 12
LOG_COLUMN = 680
FORM_COLUMN = 640
READING_COLUMN = 720
SHEET_WIDTH = 560
PANE_GAP = 20
BOARD_SHARE = 0.58
TILE_LABEL = 17
COMPOSER = 110
HEADER_CARD = 64

TYPES = [
    ("Wrong Response", 2),
    ("Sequence Out of Order", 0),
    ("Wrong Track Played", 1),
    ("Track Not Found", 0),
    ("Track Skipped", 0),
    ("Audio Drop Out", 0),
    ("Volume Jump", 0),
    ("Mute Delay", 1),
    ("Speech Delay", 1),
    ("Track Delay", 0),
    ("Navigation Failure", 0),
]

ENTRIES = [
    ("09:42", "T|", "Wrong Response"),
    ("10:05", "T|", "Wrong Response"),
    ("10:20", "T|", "Mute Delay"),
    ("10:30", "(Free Text)", "Played Morning Medley"),
    ("11:02", "T|", "Wrong Track Played – asked for Evening Song"),
    ("11:40", "T|", "Speech Delay"),
    ("12:10", "(Free Text)", "Played Evening Song"),
]

ICON = {
    "chevron": '<path d="M9 6l6 6-6 6"></path>',
    "back": '<path d="M15 6l-6 6 6 6"></path>',
    "song": '<path d="M9 18V5l12-2v13"></path><circle cx="6" cy="18" r="3"></circle><circle cx="18" cy="16" r="3"></circle>',
    "note": '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"></path>',
    "search": '<circle cx="11" cy="11" r="7"></circle><path d="M21 21l-4.3-4.3"></path>',
    "tick": '<path d="M5 12l5 5L20 7"></path>',
    "copy": '<rect x="9" y="9" width="12" height="12" rx="2"></rect><path d="M5 15V5a2 2 0 0 1 2-2h10"></path>',
    "share": '<path d="M12 3v13M7 8l5-5 5 5"></path><path d="M5 13v6a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-6"></path>',
    "log": '<rect x="3" y="3" width="7" height="7" rx="1.5"></rect><rect x="14" y="3" width="7" height="7" rx="1.5"></rect>'
    '<rect x="3" y="14" width="7" height="7" rx="1.5"></rect><rect x="14" y="14" width="7" height="7" rx="1.5"></rect>',
    "review": '<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"></path>',
    "history": '<rect x="3" y="4" width="18" height="17" rx="2"></rect><path d="M16 2v4M8 2v4M3 10h18"></path>',
    "settings": '<circle cx="12" cy="12" r="3"></circle><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3'
    'M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"></path>',
    "calendar": '<rect x="3" y="4" width="18" height="17" rx="2"></rect><path d="M16 2v4M8 2v4M3 10h18"></path>',
    "file": '<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"></path><path d="M14 3v5h5"></path>',
    "pencil": '<path d="M4 20h4L19 9l-4-4L4 16z"></path>',
    "plus": '<path d="M12 5v14M5 12h14"></path>',
}


def icon(name: str, colour: str, size: int = 24, width: float = 2) -> str:
    return (
        f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="{colour}" stroke-width="{width}" '
        f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">{ICON[name]}</svg>'
    )


@dataclass(frozen=True)
class Frame:
    name: str
    width: int
    height: int

    @property
    def landscape(self) -> bool:
        return self.width > self.height


PORTRAIT = Frame("ipad-portrait", 820, 1180)
LANDSCAPE = Frame("ipad-landscape", 1180, 820)


class Page:
    """One mock-up: a frame, a scheme, and the colours of that scheme."""

    def __init__(self, frame: Frame, dark: bool) -> None:
        self.frame = frame
        self.dark = dark
        self.c = TOKENS["colour"]["dark" if dark else "light"]
        self.tiles = [t["darkHex" if dark else "hex"] for t in TOKENS["tilePalette"][:11]]
        other = TOKENS["tilePalette"][11]
        self.other_fill = other["darkHex"] if dark else other["hex"]
        self.other_border = other["darkBorder"] if dark else other["border"]

    # ---- geometry ----

    def column(self, limit: int) -> tuple[int, int]:
        """The x and the width of a centred column no wider than `limit`."""
        width = min(limit, self.frame.width - 2 * MARGIN)
        return (self.frame.width - width) // 2, width

    def shadow(self) -> str:
        return "none" if self.dark else "0 1px 2px rgba(20,33,43,0.06)"

    # ---- building blocks ----

    def header_card(self, missing: bool = False) -> str:
        c = self.c
        strip = ""
        if missing:
            strip = (
                f'<div style="margin-top: 10px; padding-top: 10px; border-top: 1px solid rgba(255,255,255,0.25); font-size: 13px; '
                f'color: {c["headerCardTextSecondary"]}">Not filled in yet: UNIT number, Location, Version</div>'
            )
        return (
            f'<a href="Header.{self.frame.name}{self.suffix()}.dc.html" style="display: block; background: {c["headerCard"]}; '
            f"border-radius: 20px; padding: 12px 16px; min-height: {HEADER_CARD}px; box-sizing: border-box; text-decoration: none; "
            f'color: {c["textOnHeaderCard"]}">'
            f'<div style="display: flex; align-items: center; gap: 12px">'
            f'<div style="display: flex; flex-direction: column; gap: 2px; flex-grow: 1">'
            f'<div style="font-size: 20px; font-weight: 700; letter-spacing: -0.2px">UNIT-27</div>'
            f'<div style="font-size: 13px; color: {c["headerCardTextSecondary"]}">North Site · v1.4.2</div></div>'
            f'<div style="display: flex; flex-direction: column; align-items: flex-end; gap: 2px; font-size: 12px; '
            f'color: {c["headerCardTextSecondary"]}"><div style="font-weight: 600; color: {c["textOnHeaderCard"]}">Sat 7 Nov</div>'
            f"<div>Sam Example</div></div>"
            f'{icon("chevron", c["headerCardTextSecondary"], 16)}</div>{strip}</a>'
        )

    def composer(self) -> str:
        c = self.c

        def chip(name: str, label: str) -> str:
            return (
                f'<button style="height: 32px; padding: 0 12px; border-radius: 16px; border: 1px solid {c["border"]}; '
                f'background: {c["fieldBackground"]}; font-family: inherit; font-size: 13px; font-weight: 500; color: {c["textPrimary"]}; '
                f'display: flex; align-items: center; gap: 6px">{icon(name, c["primaryText"], 14)}{label}</button>'
            )

        return (
            f'<div style="background: {c["surface"]}; border-radius: 16px; padding: 12px; height: {COMPOSER}px; box-sizing: border-box; '
            f'display: flex; flex-direction: column; gap: 10px; box-shadow: {self.shadow()}">'
            f'<div style="display: flex; gap: 8px">{chip("song", "Played song")}{chip("note", "Visitor interaction")}</div>'
            f'<div style="display: flex; gap: 8px; align-items: center">'
            f'<div style="flex-grow: 1; height: 44px; box-sizing: border-box; border-radius: 12px; border: 1px solid {c["border"]}; '
            f'background: {c["fieldBackground"]}; padding: 0 12px; display: flex; align-items: center; font-size: 15px; '
            f'color: {c["textPlaceholder"]}">Add a note to today’s log…</div>'
            f'<button style="height: 44px; min-width: 64px; border-radius: 12px; border: 0; background: {c["primary"]}; '
            f'color: {c["textOnPrimary"]}; font-family: inherit; font-size: 15px; font-weight: 600">Add</button></div></div>'
        )

    def last_bar(self) -> str:
        c = self.c
        return (
            f'<a href="Review.{self.frame.name}{self.suffix()}.dc.html" style="display: flex; align-items: center; gap: 10px; height: 44px; '
            f'padding: 0 14px; background: {c["surface"]}; border-radius: 14px; text-decoration: none; color: {c["textPrimary"]}; '
            f'box-shadow: {self.shadow()}">'
            f'<span style="font-size: 12px; font-weight: 600; color: {c["textSecondary"]}; text-transform: uppercase; letter-spacing: 0.5px">Last</span>'
            f'<span style="font-size: 14px; flex-grow: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis">10:20  T| Mute Delay</span>'
            f'<span style="font-size: 13px; color: {c["textSecondary"]}; white-space: nowrap">7 today</span>'
            f'{icon("chevron", c["textSecondary"], 14)}</a>'
        )

    def board(self, width: int, tile_height: int) -> str:
        c = self.c
        tiles = []
        for (name, count), fill in zip(TYPES, self.tiles):
            badge = ""
            if count:
                badge = (
                    f'<span style="position: absolute; top: 10px; right: 10px; min-width: 28px; height: 28px; padding: 0 8px; '
                    f'box-sizing: border-box; border-radius: 14px; background: {c["badgeBackground"]}; color: {c["badgeText"]}; '
                    f'font-size: 15px; font-weight: 700; display: flex; align-items: center; justify-content: center">{count}</span>'
                )
            aria = name + (f", {count} today" if count else "")
            tiles.append(
                f'<button class="tile" aria-label="{aria}" style="position: relative; height: {tile_height}px; border-radius: 16px; '
                f"border: 0; background: {fill}; padding: 14px; display: flex; align-items: flex-end; text-align: left; "
                f"font-family: inherit; font-size: {TILE_LABEL}px; font-weight: 600; line-height: 1.2; color: {c['textPrimary']}; "
                f'box-shadow: {"none" if self.dark else "0 1px 2px rgba(20,33,43,0.08)"}"><span>{name}</span>{badge}</button>'
            )
        tiles.append(
            f'<a href="Other.{self.frame.name}{self.suffix()}.dc.html" aria-label="Other error type" style="height: {tile_height}px; '
            f"box-sizing: border-box; border-radius: 16px; border: 1.5px dashed {self.other_border}; background: {self.other_fill}; "
            f"padding: 14px; display: flex; flex-direction: column; justify-content: space-between; text-decoration: none; "
            f'color: {c["textPrimary"]}">{icon("search", c["primaryText"], 28)}'
            f'<span style="font-size: {TILE_LABEL}px; font-weight: 600">Other…</span></a>'
        )
        return (
            f'<div style="width: {width}px; display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: {TILE_GAP}px">'
            + "".join(tiles)
            + "</div>"
        )

    def toast(self, left: int, width: int, bottom: int) -> str:
        c = self.c
        return (
            f'<div role="status" style="position: absolute; left: {left}px; width: {width}px; bottom: {bottom}px; height: 52px; '
            f'border-radius: 14px; background: {c["toastBackground"]}; color: {c["toastText"]}; display: flex; align-items: center; '
            f'gap: 10px; padding: 0 8px 0 14px; box-sizing: border-box; box-shadow: 0 8px 24px rgba(20,33,43,0.25)">'
            f'{icon("tick", c["toastAccent"], 20, 2.5)}<span style="flex-grow: 1; font-size: 14px">Mute Delay logged at 10:20</span>'
            f'<button style="height: 40px; padding: 0 14px; border-radius: 10px; border: 0; background: {c["toastButton"]}; '
            f'color: {c["toastAccent"]}; font-family: inherit; font-size: 15px; font-weight: 600">Undo</button></div>'
        )

    def tab_bar(self, selected: int) -> str:
        c = self.c
        names = [("log", "Log", "Main"), ("review", "Review", "Review"), ("history", "History", "History"), ("settings", "Settings", "BoardSettings")]
        items = []
        for i, (glyph, label, target) in enumerate(names):
            colour = c["primaryText"] if i == selected else c["textSecondary"]
            weight = 600 if i == selected else 500
            items.append(
                f'<a href="{target}.{self.frame.name}{self.suffix()}.dc.html" style="display: flex; flex-direction: column; align-items: center; '
                f'gap: 3px; text-decoration: none; color: {colour}; font-size: 10.5px; font-weight: {weight}">{icon(glyph, colour)}{label}</a>'
            )
        _, width = self.column(LOG_COLUMN)
        return (
            f'<nav style="position: absolute; left: 0; right: 0; bottom: 0; height: {TAB_BAR}px; box-sizing: border-box; padding-top: 8px; '
            f'background: {c["surface"]}; border-top: 1px solid {c["tabBarBorder"]}; display: flex; justify-content: center">'
            f'<div style="width: {width}px; display: grid; grid-template-columns: repeat(4, minmax(0, 1fr))">{"".join(items)}</div></nav>'
        )

    def log_rows(self, font: int = 15) -> str:
        c = self.c
        rows = []
        for i, (time, marker, text) in enumerate(ENTRIES):
            border = "" if i == len(ENTRIES) - 1 else f'border-bottom: 1px solid {c["reviewDivider"]}; '
            rows.append(
                f'<a href="EditEntry.{self.frame.name}{self.suffix()}.dc.html" style="display: flex; gap: 12px; padding: 13px 14px; {border}'
                f'text-decoration: none; color: {c["reviewText"]}; font-size: {font}px; line-height: 1.35">'
                f'<span style="font-variant-numeric: tabular-nums; color: {c["reviewTextSecondary"]}; min-width: 44px">{time}</span>'
                f'<span style="flex-grow: 1"><span style="color: {c["reviewTextSecondary"]}">{marker}</span> {text}</span></a>'
            )
        return "".join(rows)

    def review_card(self) -> str:
        c = self.c
        border = f"border: 1px solid {c['reviewDivider']}; " if self.dark else ""
        return f'<div style="background: {c["reviewSurface"]}; {border}border-radius: 14px; overflow: hidden">{self.log_rows()}</div>'

    def header_block(self) -> str:
        c = self.c
        border = f"border: 1px solid {c['reviewDivider']}; " if self.dark else ""
        lines = ["Facilitator: Sam Example", "Date: 7 Nov 2026", "UNIT #: UNIT-27", "Location: North Site", "Software Version: 1.4.2 (a1b2c3d)"]
        return (
            f'<a href="Header.{self.frame.name}{self.suffix()}.dc.html" style="display: block; background: {c["reviewSurface"]}; {border}'
            f'border-radius: 14px; padding: 12px 14px; text-decoration: none; color: {c["reviewText"]}; font-size: 14px; line-height: 1.55">'
            + "".join(f"<div>{line}</div>" for line in lines)
            + "</a>"
        )

    def primary_button(self, label: str, glyph: str | None = None) -> str:
        c = self.c
        mark = icon(glyph, c["textOnPrimary"], 20) if glyph else ""
        return (
            f'<button style="height: 54px; width: 100%; border-radius: 14px; border: 0; background: {c["primary"]}; color: {c["textOnPrimary"]}; '
            f'font-family: inherit; font-size: 17px; font-weight: 600; display: flex; align-items: center; justify-content: center; gap: 8px">'
            f"{mark}{label}</button>"
        )

    def secondary_button(self, label: str) -> str:
        c = self.c
        return (
            f'<button style="height: 54px; width: 100%; border-radius: 14px; border: 1px solid {c["border"]}; background: {c["primaryTint"]}; '
            f'color: {c["primaryText"]}; font-family: inherit; font-size: 17px; font-weight: 600">{label}</button>'
        )

    def section_label(self, text: str) -> str:
        c = self.c
        return (
            f'<div style="font-size: 12px; font-weight: 600; color: {c["textSecondary"]}; text-transform: uppercase; letter-spacing: 0.5px; '
            f'margin: 4px 0 -6px 4px">{text}</div>'
        )

    def grouped(self, rows: list[tuple[str, str, bool]], height: int = 48) -> str:
        """A grouped list: (label, value, chevron) per row."""
        c = self.c
        out = []
        for i, (label, value, chevron) in enumerate(rows):
            border = "" if i == len(rows) - 1 else f"border-bottom: 1px solid {c['divider']}; "
            tail = icon("chevron", c["textSecondary"], 14) if chevron else ""
            val = f'<span style="color: {c["textSecondary"]}">{value}</span>' if value else ""
            out.append(
                f'<div style="display: flex; align-items: center; gap: 10px; height: {height}px; padding: 0 14px; {border}font-size: 16px">'
                f'<span style="flex-grow: 1">{label}</span>{val}{tail}</div>'
            )
        return f'<div style="background: {c["surface"]}; border-radius: 14px; overflow: hidden">{"".join(out)}</div>'

    def nav_bar(self, back: str, title: str, back_target: str) -> str:
        c = self.c
        x, width = self.column(FORM_COLUMN)
        return (
            f'<div style="position: absolute; left: 0; right: 0; top: {SAFE_TOP}px; height: 44px; display: flex; align-items: center">'
            f'<a href="{back_target}.{self.frame.name}{self.suffix()}.dc.html" style="position: absolute; left: {MARGIN - 8}px; height: 44px; '
            f'display: flex; align-items: center; gap: 2px; text-decoration: none; color: {c["primaryText"]}; font-size: 17px">'
            f'{icon("back", c["primaryText"], 22)}{back}</a>'
            f'<div style="position: absolute; left: {x}px; width: {width}px; text-align: center; font-size: 17px; font-weight: 600">{title}</div></div>'
        )

    def suffix(self) -> str:
        return ".dark" if self.dark else ""

    def document(self, title: str, body: str, background: str | None = None) -> str:
        c = self.c
        bg = background or c["background"]
        hover = c["primaryPressed"] if not self.dark else c["primaryText"]
        props = json.dumps({"$preview": {"width": self.frame.width, "height": self.frame.height}}, separators=(",", ":"))
        return (
            "<!doctype html>\n"
            '<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            f"<title>{title} – iPad {'landscape' if self.frame.landscape else 'portrait'}{' (dark)' if self.dark else ''}</title>\n"
            '<script src="./support.js"></script>\n</head>\n<body>\n<x-dc>\n<helmet>\n'
            '<link rel="preconnect" href="https://fonts.googleapis.com">\n'
            '<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&amp;display=swap" rel="stylesheet">\n'
            f"<style>\nbody{{margin:0}}\na{{color:{c['primaryText']}}}a:hover{{color:{hover}}}\n"
            ".tile:active{transform:scale(0.96)}\n.tile{transition:transform 120ms ease, box-shadow 120ms ease}\n</style>\n</helmet>\n"
            "<!-- Generated by tools/visual/ipad_mockups.py from docs/design/tokens.json. Edit the script, not this file. -->\n"
            f'<div style="width: {self.frame.width}px; height: {self.frame.height}px; position: relative; overflow: hidden; background: {bg}; '
            f'font-family: Inter, -apple-system, system-ui, sans-serif; color: {c["textPrimary"]}">\n'
            f"{body}\n</div>\n</x-dc>\n"
            f"<script type=\"text/x-dc\" data-dc-script data-props='{props}'>\n"
            "class Component extends DCLogic {}\n</script>\n</body>\n</html>\n"
        )


# ---- screens ----


def log_portrait_metrics(p: Page) -> tuple[int, int, int]:
    x, width = p.column(LOG_COLUMN)
    tile_width = (width - 2 * TILE_GAP) / 3
    tile_height = int(max(104, min(152, round(tile_width * 0.7))))
    return x, width, tile_height


def log_landscape_metrics(p: Page) -> dict[str, int]:
    inner = p.frame.width - 2 * MARGIN
    board_width = round((inner - PANE_GAP) * BOARD_SHARE)
    log_width = inner - PANE_GAP - board_width
    top = SAFE_TOP + 16
    bottom = p.frame.height - TAB_BAR - 16
    board_top = top + COMPOSER + GAP
    tile_height = int((bottom - board_top - 3 * TILE_GAP) // 4)
    return {
        "board_x": MARGIN,
        "board_width": board_width,
        "log_x": MARGIN + board_width + PANE_GAP,
        "log_width": log_width,
        "top": top,
        "bottom": bottom,
        "tile_height": tile_height,
    }


def log_body(p: Page, toast: bool = True, missing: bool = False) -> str:
    c = p.c
    if not p.frame.landscape:
        x, width, tile_height = log_portrait_metrics(p)
        body = (
            f'<div style="position: absolute; left: {x}px; width: {width}px; top: {SAFE_TOP + 16}px; display: flex; flex-direction: column; gap: {GAP}px">'
            f"{p.header_card(missing)}{p.composer()}{p.last_bar()}{p.board(width, tile_height)}</div>"
        )
        if toast:
            tw = min(width, 520)
            body += p.toast((p.frame.width - tw) // 2, tw, TAB_BAR + 16)
        return body + p.tab_bar(0)

    m = log_landscape_metrics(p)
    border = f"border: 1px solid {c['reviewDivider']}; " if p.dark else ""
    pane = (
        f'<div style="position: absolute; left: {m["log_x"]}px; width: {m["log_width"]}px; top: {m["top"]}px; '
        f'bottom: {p.frame.height - m["bottom"]}px; display: flex; flex-direction: column; gap: {GAP}px">'
        f"{p.header_card(missing)}"
        f'<section aria-label="Today’s log" style="flex-grow: 1; min-height: 0; background: {c["reviewSurface"]}; {border}border-radius: 16px; '
        f'display: flex; flex-direction: column; overflow: hidden; position: relative">'
        f'<div style="display: flex; align-items: center; gap: 8px; height: 52px; padding: 0 6px 0 14px; border-bottom: 1px solid {c["reviewDivider"]}">'
        f'<span style="font-size: 17px; font-weight: 700; color: {c["reviewText"]}; flex-grow: 1">Today’s log</span>'
        f'<span style="font-size: 14px; color: {c["reviewTextSecondary"]}">7 entries</span>'
        f'<button aria-label="Copy whole log" style="width: 44px; height: 44px; border: 0; background: transparent; display: flex; '
        f'align-items: center; justify-content: center">{icon("copy", c["primaryText"], 20)}</button></div>'
        f'<div style="flex-grow: 1; overflow: hidden; padding-bottom: 76px">{p.log_rows()}</div>'
        f'<div style="padding: 0 14px 14px">{p.primary_button("Share log", "share")}</div>'
        "</section></div>"
    )
    body = (
        f'<div style="position: absolute; left: {m["board_x"]}px; width: {m["board_width"]}px; top: {m["top"]}px; display: flex; '
        f'flex-direction: column; gap: {GAP}px">{p.composer()}{p.board(m["board_width"], m["tile_height"])}</div>' + pane
    )
    if toast:
        # The toast sits at the foot of the log, over the list's bottom padding and above Share log, so it covers no tile and
        # no entry.
        body += p.toast(m["log_x"] + 14, m["log_width"] - 28, p.frame.height - m["bottom"] + 14 + 54 + 12)
    return body + p.tab_bar(0)


def main_screen(p: Page) -> str:
    return p.document("Log – main board", log_body(p))


def dimmed(content: str) -> str:
    """The screen behind a sheet or an alert, as the phone mock-ups draw it: faded over the scrim colour of the page."""
    return f'<div aria-hidden="true" style="position: absolute; inset: 0; opacity: 0.35">{content}</div>'


def form_sheet(p: Page, inner: str) -> str:
    """A sheet on an iPad: a card of the sheet width in the middle of the screen, as tall as its content and no taller than the
    screen less 64 pt above and below (it scrolls inside beyond that), with all four corners rounded."""
    c = p.c
    width = SHEET_WIDTH
    limit = p.frame.height - 2 * (SAFE_TOP + 40)
    return (
        f'<div role="dialog" style="position: absolute; left: {(p.frame.width - width) // 2}px; width: {width}px; top: 50%; '
        f'transform: translateY(-50%); max-height: {limit}px; padding-bottom: 16px; background: {c["sheetBackground"]}; border-radius: 16px; '
        f'overflow: hidden; display: flex; flex-direction: column; box-shadow: 0 20px 48px rgba(0,0,0,0.3)">{inner}</div>'
    )


def sheet_nav(p: Page, left: str, title: str, right: str = "") -> str:
    c = p.c
    action = (
        f'<span style="min-width: 64px; text-align: right; font-size: 17px; font-weight: 600; color: {c["primaryText"]}">{right}</span>'
        if right
        else '<span style="min-width: 64px"></span>'
    )
    return (
        f'<div style="display: flex; align-items: center; padding: 12px 16px 12px; height: 44px">'
        f'<span style="min-width: 64px; font-size: 17px; color: {c["primaryText"]}">{left}</span>'
        f'<h1 style="flex-grow: 1; margin: 0; text-align: center; font-size: 17px; font-weight: 600">{title}</h1>{action}</div>'
    )


def other_screen(p: Page) -> str:
    c = p.c
    swatch = f"width: 14px; height: 14px; border-radius: 4px; border: 1px solid {c['border']}"

    def row(label: str, fill: str, count: str = "", last: bool = False) -> str:
        border = "" if last else f"border-bottom: 1px solid {c['divider']}; "
        tail = f'<span style="font-size: 14px; color: {c["textSecondary"]}">{count}</span>' if count else ""
        return (
            f'<div style="height: 52px; display: flex; align-items: center; gap: 12px; padding: 0 14px; {border}font-size: 16px">'
            f'<span style="{swatch}; background: {fill}"></span><span style="flex-grow: 1">{label}</span>{tail}</div>'
        )

    off = row("[Your other error type]", c["swatch"]) + row("[Another error type]", c["swatch"], last=True)
    on = "".join(
        [
            row("Navigation Failure", p.tiles[10]),
            row("Audio Drop Out", p.tiles[5]),
            row("Track Not Found", p.tiles[3]),
            row("Wrong Response", p.tiles[0], "2 today"),
            row("Mute Delay", p.tiles[7], "1 today"),
            row("Wrong Track Played", p.tiles[2], "1 today", last=True),
        ]
    )
    inner = (
        sheet_nav(p, "Cancel", "Log another error")
        + f'<div style="padding: 0 16px 12px"><div style="display: flex; align-items: center; gap: 8px; height: 40px; border-radius: 10px; '
        f'background: {c["searchBackground"]}; padding: 0 10px; font-size: 16px; color: {c["textPlaceholder"]}">'
        f'{icon("search", c["textSecondary"], 18)}Search error types</div></div>'
        f'<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 12px">'
        f'<div style="height: 52px; display: flex; align-items: center; gap: 10px; padding: 0 14px; background: {c["surfaceRaised"]}; '
        f'border-radius: 14px; color: {c["primaryText"]}; font-size: 16px; font-weight: 600">{icon("plus", c["primaryText"], 20)}Add a new error type</div>'
        f'{p.section_label("Not on your board")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">{off}</div>'
        f'{p.section_label("On your board")}<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">{on}</div>'
        f'</div>'
    )
    body = dimmed(log_body(p, toast=False)) + form_sheet(p, inner)
    return p.document("Other error picker", body, background=c["sheetScrim"])


def review_body(p: Page) -> str:
    c = p.c
    x, width = p.column(READING_COLUMN)
    return (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {SAFE_TOP}px; display: flex; flex-direction: column; gap: {GAP}px">'
        f'<div style="display: flex; align-items: center; justify-content: space-between; height: 44px">'
        f'<span style="font-size: 15px; color: {c["textSecondary"]}">Sat 7 Nov · 7 entries</span>'
        f'<span aria-label="Copy whole log" style="height: 44px; display: flex; align-items: center; justify-content: center; gap: 6px; '
        f'font-size: 15px; font-weight: 600; color: {c["primaryText"]}">{icon("copy", c["primaryText"], 22)}Copy log</span></div>'
        f'<h1 style="margin: -4px 0 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">Today’s log</h1>'
        f"{p.header_block()}{p.review_card()}"
        f'<p style="margin: 0; font-size: 13px; line-height: 1.4; color: {c["textSecondary"]}">Tap a line to edit it. Swipe left for Copy and Delete.</p>'
        f'<div style="margin-top: 4px">{p.primary_button("Share log", "share")}</div></div>' + p.tab_bar(1)
    )


def review_screen(p: Page) -> str:
    return p.document("Today’s log", review_body(p), background=p.c["reviewBackground"])


def edit_entry_screen(p: Page) -> str:
    c = p.c

    def field(label: str, value: str) -> str:
        return (
            f'<div style="display: flex; align-items: center; height: 52px; padding: 0 14px; border-bottom: 1px solid {c["divider"]}; font-size: 16px">'
            f'<span style="flex-grow: 1">{label}</span><span style="color: {c["textSecondary"]}">{value}</span>'
            f'{icon("chevron", c["textSecondary"], 14)}</div>'
        )

    inner = (
        sheet_nav(p, "Cancel", "Edit entry", "Save")
        + f'<div style="padding: 0 16px; display: flex; flex-direction: column; gap: 12px">'
        f'<div style="background: {c["surfaceRaised"]}; border-radius: 14px; overflow: hidden">'
        f'{field("Error", "Wrong Track Played")}'
        f'<div style="display: flex; align-items: center; height: 52px; padding: 0 14px; font-size: 16px"><span style="flex-grow: 1">Time</span>'
        f'<span style="padding: 6px 10px; border-radius: 8px; background: {c["timeChipBackground"]}; font-size: 16px; font-weight: 500; '
        f'font-variant-numeric: tabular-nums">11:02</span></div></div>'
        f'{p.section_label("Note")}'
        f'<div style="background: {c["surfaceRaised"]}; border-radius: 14px; padding: 14px; font-size: 17px; min-height: 72px; box-sizing: border-box">'
        f"asked for Evening Song</div>"
        f'{p.section_label("In the shared log")}'
        f'<div style="background: {c["surfaceRaised"]}; border-radius: 14px; padding: 12px 14px; font-size: 14px; line-height: 1.45">'
        f'11:02 T| Wrong Track Played – asked for Evening Song<div style="margin-top: 6px; font-size: 13px; color: {c["textSecondary"]}">'
        f"Logged at 11:02 on UNIT-27, version 1.4.2 (a1b2c3d).</div></div>"
        f'<div style="height: 52px; display: flex; align-items: center; justify-content: center; background: {c["surfaceRaised"]}; '
        f'border-radius: 14px; color: {c["destructive"]}; font-size: 16px; font-weight: 600">Delete entry</div></div>'
    )
    behind = log_body(p, toast=False) if p.frame.landscape else review_body(p)
    body = dimmed(behind) + form_sheet(p, inner)
    return p.document("Edit entry", body, background=c["sheetScrim"])


def new_day_screen(p: Page) -> str:
    c = p.c
    width = 340
    inner = (
        f'<div style="display: flex; justify-content: center">{icon("calendar", c["primaryText"], 26)}</div>'
        f'<h2 style="margin: 0; text-align: center; font-size: 20px; font-weight: 700">It’s Sunday 8 November</h2>'
        f'<p style="margin: 0; text-align: center; font-size: 15px; line-height: 1.45; color: {c["textBody"]}">Saturday’s log has 7 entries '
        f"and hasn’t been shared yet. Starting a new log keeps it in History.</p>"
        f'<div style="display: flex; flex-direction: column; gap: 8px; margin-top: 8px">'
        f'<button style="height: 50px; border-radius: 14px; border: 0; background: {c["primary"]}; color: {c["textOnPrimary"]}; '
        f'font-family: inherit; font-size: 16px; font-weight: 600">Start a new log</button>'
        f'<button style="height: 50px; border-radius: 14px; border: 1px solid {c["border"]}; background: {c["primaryTint"]}; '
        f'color: {c["primaryText"]}; font-family: inherit; font-size: 16px; font-weight: 600">Share Saturday’s log first</button>'
        f'<button style="height: 46px; border: 0; background: transparent; color: {c["primaryText"]}; font-family: inherit; '
        f'font-size: 16px">Keep Saturday’s log open</button></div>'
    )
    card = (
        f'<div role="alertdialog" style="position: absolute; left: {(p.frame.width - width) // 2}px; width: {width}px; '
        f'top: {p.frame.height * 3 // 8 - 120}px; background: {c["surfaceRaised"]}; border-radius: 22px; padding: 24px 20px 16px; '
        f'box-sizing: border-box; display: flex; flex-direction: column; gap: 10px; box-shadow: 0 20px 48px rgba(0,0,0,0.3)">{inner}</div>'
    )
    body = dimmed(log_body(p, toast=False)) + card
    return p.document("New day prompt", body, background=c["alertScrim"])


def header_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(FORM_COLUMN)

    def form_row(label: str, value: str, hint: str = "", muted: bool = False) -> str:
        colour = c["textSecondary"] if muted else c["textPrimary"]
        note = f'<div style="font-size: 13px; color: {c["textSecondary"]}; margin: 6px 4px 0">{hint}</div>' if hint else ""
        return (
            f'<div><div style="font-size: 13px; font-weight: 600; color: {c["textSecondary"]}; margin: 0 4px 6px">{label}</div>'
            f'<div style="height: 52px; display: flex; align-items: center; padding: 0 14px; border-radius: 12px; background: {c["surface"]}; '
            f'border: 1px solid {c["border"]}; font-size: 18px; font-weight: 600; color: {colour}">{value}</div>{note}</div>'
        )

    locations = (
        f'<div><div style="font-size: 13px; font-weight: 600; color: {c["textSecondary"]}; margin: 0 4px 6px">Location</div>'
        f'<div style="background: {c["surface"]}; border-radius: 14px; overflow: hidden">'
        f'<div style="height: 48px; display: flex; align-items: center; padding: 0 14px; border-bottom: 1px solid {c["divider"]}; font-size: 16px">'
        f'<span style="flex-grow: 1">North Site</span>{icon("tick", c["primaryText"], 20)}</div>'
        f'<div style="height: 48px; display: flex; align-items: center; padding: 0 14px; border-bottom: 1px solid {c["divider"]}; font-size: 16px">South Site</div>'
        f'<div style="height: 48px; display: flex; align-items: center; padding: 0 14px; font-size: 16px">Other…</div></div></div>'
    )
    # One column, as on the phone: the same fields in the same order (and the same VoiceOver order), no wider than the form
    # column. In landscape the page scrolls, as it does on the phone.
    body = (
        p.nav_bar("Log", "", "Main")
        + f'<div style="position: absolute; left: {x}px; width: {width}px; top: {SAFE_TOP + 52}px; display: flex; flex-direction: column; gap: 16px">'
        f'<div><h1 style="margin: 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">Today’s details</h1>'
        f'<p style="margin: 6px 0 0; font-size: 15px; color: {c["textSecondary"]}">Set these once at the start of the day. They head your log.</p></div>'
        f'{form_row("Facilitator", "Sam Example", muted=True)}{form_row("Date", "Sat 7 Nov 2026", muted=True)}'
        f'{form_row("UNIT number", "UNIT-27", "Type the number only.")}{form_row("Environment", "None")}'
        f"{locations}"
        f'{form_row("Software version", "1.4.2", "Format 1.4.2 – numbers only.")}'
        f'{form_row("Commit", "a1b2c3d", "The short code (7 to 12 letters and numbers) that identifies the exact build. Leave it empty if you can’t get it.")}'
        f'<div style="margin-top: 8px">{p.primary_button("Start logging")}</div>'
        f'<p style="margin: 0; text-align: center; font-size: 13px; color: {c["textSecondary"]}">You can log errors before every field is filled.</p></div>'
    )
    return p.document("Today’s details", body)


def mini_board(p: Page, width: int) -> str:
    c = p.c
    gap = 8
    tile_height = 64
    cells = []
    for (name, _), fill in zip(TYPES, p.tiles):
        cells.append(
            f'<div style="height: {tile_height}px; border-radius: 12px; background: {fill}; padding: 8px 10px; box-sizing: border-box; '
            f'display: flex; align-items: flex-end; font-size: 13px; font-weight: 600; line-height: 1.2">{name}</div>'
        )
    cells.append(
        f'<div style="height: {tile_height}px; border-radius: 12px; border: 1.5px dashed {p.other_border}; background: {p.other_fill}; '
        f'padding: 8px 10px; box-sizing: border-box; display: flex; align-items: flex-end; font-size: 13px; font-weight: 600; '
        f'color: {c["textSecondary"]}">Other… (fixed)</div>'
    )
    return (
        f'<div style="background: {c["surface"]}; border-radius: 16px; padding: 12px; box-shadow: {p.shadow()}">'
        f'<div style="display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: {gap}px">{"".join(cells)}</div></div>'
    )


def settings_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(FORM_COLUMN)
    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {SAFE_TOP + 16}px; display: flex; flex-direction: column; gap: 14px">'
        f'<h1 style="margin: 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">Settings</h1>'
        f'{p.section_label("Your board")}{mini_board(p, width)}'
        f'<p style="margin: -4px 4px 0; font-size: 13px; line-height: 1.4; color: {c["textSecondary"]}">Hold and drag a tile to move it. '
        f"Tap a tile to change its error type, or + to add one. Each type keeps its colour.</p>"
        + p.grouped([("Error types", "", True), ("Note templates", "", True), ("Locations", "", True)])
        + p.grouped([("Facilitator", "Sam Example", True), ("Unit prefix", "UNIT", True), ("Include a totals line", "Off", False), ("Time format", "24-hour", True), ("Error prefix", "T|", True)])
        + p.grouped([("Import or export setup", "", True)])
        + "</div>"
        + p.tab_bar(3)
    )
    return p.document("Settings", body)


def history_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(READING_COLUMN)

    def item(title: str, detail: str, shared: str, last: bool = False) -> str:
        border = "" if last else f"border-bottom: 1px solid {c['divider']}; "
        mark = icon("tick", c["primaryText"], 18) if shared.startswith("Shared") else (
            f'<span style="width: 14px; height: 14px; border-radius: 7px; border: 1.5px solid {c["textSecondary"]}"></span>'
        )
        return (
            f'<div style="display: flex; align-items: center; gap: 12px; padding: 12px 14px; {border}">'
            f'<div style="flex-grow: 1"><div style="font-size: 16px; font-weight: 600">{title}</div>'
            f'<div style="font-size: 14px; color: {c["textSecondary"]}">{detail}</div></div>'
            f'<div style="display: flex; align-items: center; gap: 6px; font-size: 14px; color: {c["textSecondary"]}">{mark}{shared}</div>'
            f'{icon("chevron", c["textSecondary"], 14)}</div>'
        )

    def group(date: str, rows: str) -> str:
        return f'{p.section_label(date)}<div style="background: {c["surface"]}; border-radius: 14px; overflow: hidden">{rows}</div>'

    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {SAFE_TOP + 16}px; display: flex; flex-direction: column; gap: 14px">'
        f'<h1 style="margin: 0; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">History</h1>'
        f'<p style="margin: -6px 0 0; font-size: 15px; color: {c["textSecondary"]}">Past logs, newest first. Today’s log is on the Review tab.</p>'
        + group("6 Nov 2026", item("UNIT-27 · South Site", "4 entries · v1.4.2", "Shared 17:42") + item("UNIT-27 · North Site", "9 entries · v1.4.1", "Shared 12:15", True))
        + group("5 Nov 2026", item("UNIT-27 · North Site", "12 entries · v1.4.1", "Shared 16:58", True))
        + group("2 Nov 2026", item("UNIT-26 · North Site", "5 entries · v1.4.0", "Not shared", True))
        + "</div>"
        + p.tab_bar(2)
    )
    return p.document("History", body)


def first_launch_screen(p: Page) -> str:
    c = p.c
    x, width = p.column(480)

    def choice(glyph: str, title: str) -> str:
        return (
            f'<div style="display: flex; align-items: center; gap: 14px; padding: 16px; background: {c["surface"]}; border-radius: 16px; '
            f'border: 1px solid {c["border"]}">{icon(glyph, c["primaryText"], 26)}'
            f'<div style="flex-grow: 1; font-size: 17px; font-weight: 600">{title}</div>{icon("chevron", c["textSecondary"], 16)}</div>'
        )

    top = (p.frame.height - 470) // 2
    body = (
        f'<div style="position: absolute; left: {x}px; width: {width}px; top: {top}px; display: flex; flex-direction: column; gap: 16px">'
        f'<div style="width: 72px; height: 72px; border-radius: 18px; background: {c["headerCard"]}; display: flex; align-items: center; '
        f'justify-content: center; margin: 0 auto">{icon("log", c["textOnHeaderCard"], 40)}</div>'
        f'<h1 style="margin: 0; text-align: center; font-size: 30px; font-weight: 700; letter-spacing: -0.4px">CS Bingo Board</h1>'
        f'<p style="margin: 0; text-align: center; font-size: 16px; line-height: 1.45; color: {c["textBody"]}">Log what goes wrong during a session, '
        f"as it happens, and share the log at the end of the day.</p>"
        f'{p.section_label("How do you want to start?")}'
        f'{choice("pencil", "Set up by hand")}'
        f'{choice("file", "Import a setup file")}'
        f'<p style="margin: 0; font-size: 13px; line-height: 1.4; color: {c["textSecondary"]}">A setup file holds your error types, locations and '
        f"note templates. You can import one later in Settings.</p></div>"
    )
    return p.document("First launch", body)


SCREENS_TO_WRITE = {
    "Main": main_screen,
    "Review": review_screen,
    "Header": header_screen,
    "BoardSettings": settings_screen,
    "History": history_screen,
    "Other": other_screen,
    "EditEntry": edit_entry_screen,
    "NewDay": new_day_screen,
    "FirstLaunch": first_launch_screen,
}


def outputs() -> dict[Path, str]:
    files = {}
    for name, make in SCREENS_TO_WRITE.items():
        for frame in (PORTRAIT, LANDSCAPE):
            for dark in (False, True):
                page = Page(frame, dark)
                files[SCREENS / f"{name}.{frame.name}{page.suffix()}.dc.html"] = make(page)
    return files


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="write nothing; fail when a file is not what would be written")
    args = parser.parse_args(argv)
    stale = []
    for path, text in outputs().items():
        current = path.read_text(encoding="utf-8") if path.is_file() else None
        if current == text:
            continue
        if args.check:
            stale.append(path.name)
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path.relative_to(ROOT)}")
    if stale:
        print("not current (run tools/visual/ipad_mockups.py): " + ", ".join(stale), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
