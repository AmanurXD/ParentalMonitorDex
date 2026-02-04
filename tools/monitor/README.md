ParentalMonitor Viewer (terminal)
=================================

Goal
----
Lightweight terminal tools you run on the **parent** machine to view per‑process activity from each child PC without using email/SMTP. Two pieces:
- **monitor_app.py** — Textual TUI (exe-friendly) with device list → Process Viewer. Currently uses mock data; ready to wire to real SSH/driver feeds.
- **fetch_and_view.ps1** — Simple PowerShell pull+summary over SSH/SCP (kept for quick checks).

Assumptions
-----------
- Each child PC runs your ParentalMonitor driver + service, writing newline‑delimited JSON logs to a known path (example below).
- OpenSSH client is available on the parent PC (built into recent Windows 10/11). You have SSH key‑based access to the child PCs.
- You know: host/IP, ssh username, private key path, and the remote log directory.

Expected log format (JSONL)
---------------------------
One JSON object per line, UTC timestamps:
```json
{"ts":"2026-02-03T09:12:01.123Z","pid":4120,"ppid":756,"event":"create","image":"C:\\Windows\\System32\\notepad.exe","user":"DESKTOP\\child1"}
{"ts":"2026-02-03T09:35:44.002Z","pid":4120,"ppid":756,"event":"exit","image":"C:\\Windows\\System32\\notepad.exe","user":"DESKTOP\\child1","exitCode":0}
```

Apps / scripts
--------------
- `monitor_app.py`
  - Rich terminal UI (Textual). Keyboard/mouse friendly.
  - Left: devices; Right: actions + process table.
  - Today: mock data only (no driver needed). Next: wire to your real data source.
  - Run:  
    ```powershell
    cd <repo_root>\tools\monitor
    py -3 -m pip install textual      # first time
    py -3 monitor_app.py
    ```
  - Keys: Up/Down select device, Enter open actions, R refresh mock data, Q/Esc quit.

- `fetch_and_view.ps1`
  - Fetches log files via scp, parses JSONL, prints recent events + top processes.
  - Use when you have logs written by the driver/service and SSH access is set up.

Usage
-----
PowerShell viewer (log-based):
```powershell
cd <repo_root>\tools\monitor
.\fetch_and_view.ps1 -Host 192.168.1.50 -User parent -KeyPath C:\Keys\child1_ed25519 `
    -RemotePath "/var/log/parentalmonitor/*.jsonl" -RecentHours 24
```

Notes
-----
- The script keeps a local cache under `%TEMP%\ParentalMonitor\<host>\`.
- It does not delete remote logs.
- If your child PCs run Windows, set `-RemotePath "C:/ProgramData/ParentalMonitor/logs/*.jsonl"` (OpenSSH on Windows accepts forward slashes).
- To add TLS/SSH pinning or SFTP instead of scp, we can extend the script later.
