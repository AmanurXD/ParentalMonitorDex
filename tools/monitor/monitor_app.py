#!/usr/bin/env python3
"""
ParentalMonitor Control UI (terminal, Textual)

Current state: UI skeleton with mock data.
Planned: wire to SSH/driver-backed data sources.

Usage:
  python monitor_app.py           # run with mock data
Keys:
  Up/Down: select device
  Enter / Click: open actions (Process Viewer)
  R: refresh mock data
  Q / Esc: quit
"""

from __future__ import annotations

import random
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta
from typing import Dict, List

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical
from textual.reactive import reactive
from textual.widgets import (
    Header,
    Footer,
    ListView,
    ListItem,
    Label,
    Static,
    Button,
    DataTable,
)


@dataclass
class Device:
    name: str
    host: str
    status: str
    last_seen: datetime
    notes: str = ""


@dataclass
class Proc:
    name: str
    pid: int
    user: str
    started: datetime
    cpu: float
    mem_mb: float


def _mock_devices() -> List[Device]:
    now = datetime.utcnow()
    return [
        Device("Ethan-PC", "192.168.1.50", "online", now - timedelta(seconds=45), "Grade 6"),
        Device("Liam-Laptop", "192.168.1.51", "online", now - timedelta(minutes=3), "Grade 4"),
        Device("Spare-VM", "192.168.1.60", "offline", now - timedelta(hours=3), "Lab VM"),
    ]


def _mock_processes() -> Dict[str, List[Proc]]:
    now = datetime.utcnow()

    def sample_processes(base_pid: int) -> List[Proc]:
        procs = [
            ("chrome.exe", "child", 12.5, 180),
            ("code.exe", "child", 8.2, 260),
            ("py.exe", "child", 4.1, 95),
            ("discord.exe", "child", 6.7, 210),
            ("notepad.exe", "child", 0.1, 20),
            ("powershell.exe", "child", 1.2, 75),
        ]
        out: List[Proc] = []
        for i, (name, user, cpu, mem) in enumerate(procs):
            start = now - timedelta(minutes=random.randint(1, 120))
            out.append(
                Proc(
                    name=name,
                    pid=base_pid + i,
                    user=f"DESKTOP\\{user}",
                    started=start,
                    cpu=round(cpu + random.random() * 2, 1),
                    mem_mb=round(mem + random.random() * 30, 1),
                )
            )
        return out

    return {
        "Ethan-PC": sample_processes(4000),
        "Liam-Laptop": sample_processes(5000),
        "Spare-VM": sample_processes(6000),
    }


class MonitorApp(App):
    CSS = """
    Screen {
        background: #0b1020;
    }

    #layout {
        height: 1fr;
        padding: 1 2;
    }

    #devices {
        width: 32%;
        border: heavy #2b3a67;
        background: #0e1630;
        padding: 1;
    }

    #content {
        width: 68%;
        border: heavy #2b3a67;
        background: #0e1630;
        padding: 1;
    }

    #toprow {
        height: auto;
        margin-bottom: 1;
    }

    #device_details, #actions {
        border: round #26335c;
        background: #0b1020;
        padding: 1;
        width: 1fr;
    }

    #actions Button {
        margin-right: 1;
    }

    #proc_table {
        height: 1fr;
        margin-top: 1;
    }

    ListView {
        height: 1fr;
        border: round #26335c;
        background: #0b1020;
    }

    .device-item {
        padding: 0 1;
    }

    .status-online {
        color: #7ef7c4;
    }

    .status-offline {
        color: #f5a9a9;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("escape", "quit", "Quit"),
        ("r", "refresh", "Refresh mock data"),
        ("enter", "open_actions", "Open actions"),
    ]

    selected_device = reactive("Ethan-PC")

    def __init__(self) -> None:
        super().__init__()
        self.devices: List[Device] = []
        self.processes: Dict[str, List[Proc]] = {}

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)

        with Horizontal(id="layout"):
            with Vertical(id="devices"):
                yield Label("Devices", id="devices_label")
                yield ListView(id="device_list")

            with Vertical(id="content"):
                with Horizontal(id="toprow"):
                    yield Static(id="device_details")
                    with Static(id="actions"):
                        yield Label("Actions")
                        yield Button("Process Viewer", id="action_process")

                yield DataTable(id="proc_table")

        yield Footer()

    def on_mount(self) -> None:
        self.load_mock()
        self.populate_devices()
        self.populate_processes()
        self.query_one("#device_list", ListView).index = 0

    def load_mock(self) -> None:
        self.devices = _mock_devices()
        self.processes = _mock_processes()

    def populate_devices(self) -> None:
        lv = self.query_one("#device_list", ListView)
        lv.clear()
        for dev in self.devices:
            # Define the color directly for the markup
            color = "#7ef7c4" if dev.status == "online" else "#f5a9a9"
            
            item = ListItem(
                Label(
                    f"[b]{dev.name}[/b] - {dev.host} - "
                    f"[{color}]{dev.status}[/]" # Use the color code directly
                ),
                name=dev.name,
            )
            lv.append(item)

    def populate_processes(self) -> None:
        table = self.query_one("#proc_table", DataTable)
        if not table.columns:
            table.add_columns("Name", "PID", "User", "Started (local time)", "CPU %", "Mem MB")

        table.clear()
        procs = self.processes.get(self.selected_device, [])
        for p in sorted(procs, key=lambda x: x.started, reverse=True):
            table.add_row(
                p.name,
                str(p.pid),
                p.user,
                p.started.strftime("%Y-%m-%d %H:%M:%S"),
                f"{p.cpu:.1f}",
                f"{p.mem_mb:.1f}",
            )
        table.cursor_coordinate = (0, 0) if procs else None
        self.render_device_details()

    def render_device_details(self) -> None:
        dev = next((d for d in self.devices if d.name == self.selected_device), None)
        pane = self.query_one("#device_details", Static)
        if not dev:
            pane.update("[b]Device[/b]\n\n[dim]None selected[/dim]")
            return
        pane.update(
            f"[b]Device:[/b] {dev.name}\n"
            f"[b]Host/IP:[/b] {dev.host}\n"
            f"[b]Status:[/b] {dev.status}\n"
            f"[b]Last seen:[/b] {dev.last_seen.strftime('%Y-%m-%d %H:%M:%S UTC')}\n"
            f"[b]Notes:[/b] {dev.notes or '—'}"
        )

    # Events
    def on_list_view_highlighted(self, event: ListView.Highlighted) -> None:
        item = event.item
        if item and item.name:
            self.selected_device = item.name
            self.populate_processes()

    def on_list_view_selected(self, event: ListView.Selected) -> None:
        item = event.item
        if item and item.name:
            self.selected_device = item.name
            self.populate_processes()

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "action_process":
            self.populate_processes()

    # Actions
    def action_refresh(self) -> None:
        self.load_mock()
        self.populate_devices()
        self.populate_processes()

    def action_open_actions(self) -> None:
        self.populate_processes()


def main() -> None:
    app = MonitorApp()
    app.run()


if __name__ == "__main__":
    sys.exit(main())
