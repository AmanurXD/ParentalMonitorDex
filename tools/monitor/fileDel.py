#!/usr/bin/env python3
"""
Largest Files Browser (Windows-friendly)

Features:
- Scans all fixed drives (or a specified root) to find top-N largest files.
- Beautiful terminal UI (Textual): searchable list, details pane, status bar.
- Enter: open File Explorer with file selected
- D: delete (to recycle bin)
- R: rescan
- Q / Esc: quit

Notes:
- On very large disks, scanning can take a while. The UI shows progress.
- Requires: pip install textual send2trash
"""

from __future__ import annotations

import os
import sys
import heapq
import time
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional, List, Tuple

# Third-party
from send2trash import send2trash

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.widgets import Header, Footer, ListView, ListItem, Label, Input, Static
from textual.reactive import reactive
from textual.message import Message


@dataclass(frozen=True)
class FileEntry:
    size: int
    path: str

    @property
    def name(self) -> str:
        return os.path.basename(self.path)

    @property
    def parent(self) -> str:
        return os.path.dirname(self.path)


def human_size(num_bytes: int) -> str:
    # binary units
    units = ["B", "KiB", "MiB", "GiB", "TiB", "PiB"]
    n = float(num_bytes)
    for u in units:
        if n < 1024.0 or u == units[-1]:
            return f"{n:,.2f} {u}"
        n /= 1024.0
    return f"{num_bytes} B"


def is_windows() -> bool:
    return os.name == "nt"


def list_fixed_drives_windows() -> List[str]:
    """
    Returns fixed drive roots like ['C:\\', 'D:\\'] on Windows.
    """
    import ctypes

    drives = []
    bitmask = ctypes.windll.kernel32.GetLogicalDrives()
    DRIVE_FIXED = 3

    for letter_i in range(26):
        if bitmask & (1 << letter_i):
            letter = chr(ord("A") + letter_i)
            root = f"{letter}:\\"
            dtype = ctypes.windll.kernel32.GetDriveTypeW(root)
            if dtype == DRIVE_FIXED:
                drives.append(root)
    return drives


def open_in_file_explorer_select(path: str) -> None:
    """
    Open file location and select the file.
    """
    if is_windows():
        # explorer /select,"C:\path\file.ext"
        subprocess.run(["explorer", "/select,", os.path.normpath(path)], check=False)
    elif sys.platform == "darwin":
        subprocess.run(["open", "-R", path], check=False)
    else:
        # Linux: best-effort with common file managers; fallback to opening the folder
        folder = os.path.dirname(path)
        for cmd in (("xdg-open", folder), ("gio", "open", folder), ("nautilus", folder)):
            try:
                subprocess.run(list(cmd), check=False)
                break
            except Exception:
                continue


def iter_files(root: str) -> Iterable[str]:
    """
    Yields file paths under root. Uses os.walk with error handling.
    """
    for dirpath, dirnames, filenames in os.walk(root, topdown=True, followlinks=False):
        # Optionally skip some noisy/system dirs (helps on Windows)
        # Feel free to customize:
        if is_windows():
            lowered = dirpath.lower()
            skip_markers = [
                "\\windows\\winsxs",
                "\\windows\\softwaredistribution",
                "\\system volume information",
                "\\$recycle.bin",
                "\\program files\\windowsapps",
            ]
            if any(m in lowered for m in skip_markers):
                dirnames[:] = []
                continue

        # Avoid permission blowups:
        # Remove dirs we can't enter (os.walk will call onerror, but pruning helps).
        pruned = []
        for d in dirnames:
            full = os.path.join(dirpath, d)
            try:
                # cheap permission check
                os.listdir(full)
                pruned.append(d)
            except Exception:
                continue
        dirnames[:] = pruned

        for fn in filenames:
            yield os.path.join(dirpath, fn)


def top_n_largest_files(roots: List[str], n: int, progress_cb=None) -> List[FileEntry]:
    """
    Keep a min-heap of size n for largest files.
    progress_cb: callable(scanned_files:int, last_path:str, heap_min:int|None)
    """
    heap: List[Tuple[int, str]] = []
    scanned = 0
    last_tick = 0.0

    for root in roots:
        for p in iter_files(root):
            scanned += 1
            try:
                st = os.stat(p, follow_symlinks=False)
                size = int(st.st_size)
            except Exception:
                continue

            if len(heap) < n:
                heapq.heappush(heap, (size, p))
            else:
                if size > heap[0][0]:
                    heapq.heapreplace(heap, (size, p))

            # Throttle progress updates
            if progress_cb:
                now = time.time()
                if now - last_tick > 0.05:
                    last_tick = now
                    progress_cb(scanned, p, heap[0][0] if heap else None)

    # Largest first
    heap.sort(key=lambda t: t[0], reverse=True)
    return [FileEntry(size=s, path=p) for s, p in heap]


class ScanProgress(Message):
    def __init__(self, scanned: int, last_path: str, current_cutoff: Optional[int]) -> None:
        super().__init__()
        self.scanned = scanned
        self.last_path = last_path
        self.current_cutoff = current_cutoff


class ScanDone(Message):
    def __init__(self, entries: List[FileEntry], elapsed: float) -> None:
        super().__init__()
        self.entries = entries
        self.elapsed = elapsed


class LargestFilesApp(App):
    CSS = """
    Screen {
        background: #0b1020;
    }

    #main {
        height: 1fr;
        padding: 1 2;
    }

    #left {
        width: 60%;
        border: heavy #2b3a67;
        background: #0e1630;
        padding: 1;
    }

    #right {
        width: 40%;
        border: heavy #2b3a67;
        background: #0e1630;
        padding: 1;
    }

    #search {
        border: round #4c6fff;
        background: #0b1020;
        padding: 0 1;
        margin-bottom: 1;
    }

    ListView {
        border: round #26335c;
        height: 1fr;
        background: #0b1020;
    }

    .item {
        padding: 0 1;
    }

    .title {
        color: #e7ecff;
        text-style: bold;
    }

    .muted {
        color: #9aa6d6;
    }

    #details {
        border: round #26335c;
        background: #0b1020;
        padding: 1;
        height: 1fr;
    }

    #status {
        border: round #26335c;
        background: #0b1020;
        padding: 1;
        height: auto;
        margin-top: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("escape", "quit", "Quit"),
        ("enter", "open_selected", "Open in Explorer"),
        ("d", "delete_selected", "Delete (Recycle Bin)"),
        ("r", "rescan", "Rescan"),
        ("up", "cursor_up", "Up"),
        ("down", "cursor_down", "Down"),
        ("pageup", "page_up", "Page Up"),
        ("pagedown", "page_down", "Page Down"),
    ]

    # reactive state
    scanned_files = reactive(0)
    last_path = reactive("")
    cutoff_size = reactive(0)
    scanning = reactive(False)
    filter_text = reactive("")
    selected_entry: Optional[FileEntry] = None

    def __init__(self, roots: List[str], top_n: int = 200) -> None:
        super().__init__()
        self.roots = roots
        self.top_n = top_n
        self.all_entries: List[FileEntry] = []
        self.filtered_entries: List[FileEntry] = []

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)

        with Horizontal(id="main"):
            with Vertical(id="left"):
                yield Input(placeholder="Type to filter by filename or path…", id="search")
                yield ListView(id="list")

            with Vertical(id="right"):
                yield Static(id="details")
                yield Static(id="status")

        yield Footer()

    def on_mount(self) -> None:
        self.set_focus(self.query_one("#search", Input))
        self.action_rescan()

    def _post_progress(self, scanned: int, last_path: str, cutoff: Optional[int]) -> None:
        self.post_message(ScanProgress(scanned, last_path, cutoff))

    def _do_scan(self) -> None:
        start = time.time()
        entries = top_n_largest_files(self.roots, self.top_n, progress_cb=self._post_progress)
        elapsed = time.time() - start
        self.post_message(ScanDone(entries, elapsed))

    def on_scan_progress(self, msg: ScanProgress) -> None:
        self.scanned_files = msg.scanned
        self.last_path = msg.last_path
        self.cutoff_size = msg.current_cutoff or 0
        self._render_status()

    def on_scan_done(self, msg: ScanDone) -> None:
        self.scanning = False
        self.all_entries = msg.entries
        self.apply_filter(self.filter_text)
        self._render_status(extra=f"Scan complete in {msg.elapsed:,.1f}s. Showing top {len(self.filtered_entries)}.")
        if self.filtered_entries:
            self.query_one("#list", ListView).index = 0
            self._update_details()

    def action_rescan(self) -> None:
        if self.scanning:
            return
        self.scanning = True
        self.scanned_files = 0
        self.last_path = ""
        self.cutoff_size = 0
        self._render_status(extra="Scanning…")
        self.all_entries = []
        self.filtered_entries = []
        self._populate_list([])
        self._render_details(None)

        # Run scan in background thread to keep UI responsive
        import threading
        t = threading.Thread(target=self._do_scan, daemon=True)
        t.start()

    def apply_filter(self, text: str) -> None:
        self.filter_text = text.strip()
        if not self.filter_text:
            self.filtered_entries = list(self.all_entries)
        else:
            q = self.filter_text.lower()
            self.filtered_entries = [
                e for e in self.all_entries
                if q in e.path.lower() or q in e.name.lower()
            ]
        self._populate_list(self.filtered_entries)

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            self.apply_filter(event.value)

    def _populate_list(self, entries: List[FileEntry]) -> None:
        lv = self.query_one("#list", ListView)
        lv.clear()
        for e in entries:
            lv.append(
                ListItem(
                    Label(f"[b]{human_size(e.size)}[/b]  {e.path}", classes="item"),
                )
            )

    

    def _render_details(self, entry: Optional[FileEntry]) -> None:
        details = self.query_one("#details", Static)
        if not entry:
            details.update(
                "[b]Details[/b]\n\n"
                "[dim]No file selected.[/dim]\n"
            )
            return

        try:
            st = os.stat(entry.path, follow_symlinks=False)
            mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_mtime))
            ctime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(st.st_ctime))
        except Exception:
            mtime = "Unknown"
            ctime = "Unknown"

        details.update(
            f"[b]Details[/b]\n\n"
            f"[b]Name:[/b] {entry.name}\n"
            f"[b]Size:[/b] {human_size(entry.size)}\n"
            f"[b]Path:[/b] {entry.path}\n\n"
            f"[b]Modified:[/b] {mtime}\n"
            f"[b]Created:[/b] {ctime}\n\n"
            "[dim]Keys:[/dim]\n"
            "• [b]Enter[/b] open location & select\n"
            "• [b]D[/b] delete to Recycle Bin\n"
            "• [b]R[/b] rescan\n"
            "• [b]Q/Esc[/b] quit\n"
        )

    def _render_status(self, extra: str = "") -> None:
        status = self.query_one("#status", Static)
        roots = ", ".join(self.roots)
        scanning_line = "[b]Scanning…[/b]" if self.scanning else "[b]Idle[/b]"
        cutoff = human_size(self.cutoff_size) if self.cutoff_size else "—"

        # Keep last path from exploding the layout:
        last = self.last_path.replace("[", "\\[")  # escape Rich markup start
        if len(last) > 200:
            last = last[:200] + "…"

        status.update(
            f"{scanning_line}\n"
            f"[dim]Roots:[/dim] {roots}\n"
            f"[dim]Scanned files:[/dim] {self.scanned_files:,}\n"
            f"[dim]Current top-{self.top_n} cutoff:[/dim] {cutoff}\n"
            f"[dim]Last:[/dim] {last}\n"
            f"{extra}"
        )



    def _update_details(self) -> None:
        lv = self.query_one("#list", ListView)
        idx = lv.index
        if idx is None or idx < 0 or idx >= len(self.filtered_entries):
            self.selected_entry = None
            self._render_details(None)
            return
        self.selected_entry = self.filtered_entries[idx]
        self._render_details(self.selected_entry)

    def on_list_view_highlighted(self, _: ListView.Highlighted) -> None:
        self._update_details()

    def action_cursor_up(self) -> None:
        lv = self.query_one("#list", ListView)
        lv.index = max(0, (lv.index or 0) - 1)
        self._update_details()

    def action_cursor_down(self) -> None:
        lv = self.query_one("#list", ListView)
        lv.index = min(max(0, len(self.filtered_entries) - 1), (lv.index or 0) + 1)
        self._update_details()

    def action_page_up(self) -> None:
        lv = self.query_one("#list", ListView)
        lv.index = max(0, (lv.index or 0) - 10)
        self._update_details()

    def action_page_down(self) -> None:
        lv = self.query_one("#list", ListView)
        lv.index = min(max(0, len(self.filtered_entries) - 1), (lv.index or 0) + 10)
        self._update_details()

    def action_open_selected(self) -> None:
        if not self.selected_entry:
            return
        open_in_file_explorer_select(self.selected_entry.path)

    def action_delete_selected(self) -> None:
        if not self.selected_entry:
            return
        target = self.selected_entry.path
        try:
            send2trash(target)
        except Exception as e:
            self._render_status(extra=f"[b]Delete failed:[/b] {e}")
            return

        # Remove from lists
        self.all_entries = [e for e in self.all_entries if e.path != target]
        self.filtered_entries = [e for e in self.filtered_entries if e.path != target]
        self._populate_list(self.filtered_entries)
        self._render_status(extra=f"Deleted to Recycle Bin: {target}")
        self._update_details()

    def on_list_view_selected(self, _: ListView.Selected) -> None:
        # Clicking/enter in list triggers selection; we treat Enter as open.
        pass


def parse_args(argv: List[str]) -> Tuple[List[str], int]:
    """
    Usage:
      python largest_files_browser.py               -> scan fixed drives (Windows) or / (unix)
      python largest_files_browser.py <root>        -> scan specific root
      python largest_files_browser.py <root> <N>    -> scan root, keep top N
    """
    top_n = 200
    roots: List[str] = []

    if len(argv) >= 2:
        roots = [argv[1]]
    if len(argv) >= 3:
        try:
            top_n = int(argv[2])
            top_n = max(10, min(top_n, 5000))
        except Exception:
            pass

    if not roots:
        if is_windows():
            roots = list_fixed_drives_windows() or ["C:\\"]
        else:
            roots = ["/"]

    return roots, top_n


def main() -> None:
    roots, top_n = parse_args(sys.argv)
    app = LargestFilesApp(roots=roots, top_n=top_n)
    app.run()


if __name__ == "__main__":
    main()
