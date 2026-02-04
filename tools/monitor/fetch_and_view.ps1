# Requires: OpenSSH client available in PATH (ssh.exe, scp.exe)
# Purpose : Fetch process activity logs from a child PC over SSH/SCP and summarize them.
param(
    [Parameter(Mandatory = $true)]
    [string]$Host,

    [Parameter(Mandatory = $true)]
    [string]$User,

    [Parameter(Mandatory = $true)]
    [string]$KeyPath,

    [Parameter(Mandatory = $true)]
    [string]$RemotePath,

    [int]$RecentHours = 24
)

$ErrorActionPreference = "Stop"

function Ensure-Tool($name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "$name not found in PATH. Install the OpenSSH client or add it to PATH."
    }
}

Ensure-Tool "ssh"
Ensure-Tool "scp"

$cacheRoot = Join-Path $env:TEMP "ParentalMonitor"
$hostCache = Join-Path $cacheRoot $Host
New-Item -ItemType Directory -Force -Path $hostCache | Out-Null

Write-Host "Fetching logs from $User@$Host ..."

# Copy remote logs to cache folder
$scpCmd = @("scp", "-i", $KeyPath, "-q", "$User@$Host:`"$RemotePath`"", "$hostCache\")
$proc = Start-Process -FilePath $scpCmd[0] -ArgumentList $scpCmd[1..($scpCmd.Length-1)] -NoNewWindow -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    throw "scp failed with exit code $($proc.ExitCode)"
}

# Load and parse logs
$cutoff = (Get-Date).ToUniversalTime().AddHours(-$RecentHours)
$events = @()

Get-ChildItem -Path $hostCache -Filter *.jsonl | Sort-Object LastWriteTime |
    ForEach-Object {
        Get-Content -Path $_.FullName | ForEach-Object {
            if (-not $_) { return }
            try {
                $evt = $_ | ConvertFrom-Json
                if (-not $evt.ts) { return }
                $ts = [datetime]::Parse($_.ts).ToUniversalTime()
                if ($ts -ge $cutoff) {
                    $evt | Add-Member -NotePropertyName tsParsed -NotePropertyValue $ts
                    $events += $evt
                }
            } catch {
                Write-Warning "Skipped malformed line in $($_.FullName)"
            }
        }
    }

if ($events.Count -eq 0) {
    Write-Host "No events in the last $RecentHours hours."
    exit 0
}

# Recent timeline
Write-Host ""
Write-Host "Recent events (last $RecentHours h):"
$events | Sort-Object tsParsed |
    Select-Object @{n="Time"; e={$_.tsParsed.ToString("yyyy-MM-dd HH:mm:ss")}},
                  @{n="Event"; e={$_.event}},
                  @{n="PID"; e={$_.pid}},
                  @{n="PPID"; e={$_.ppid}},
                  @{n="Image"; e={$_.image}},
                  @{n="User"; e={$_.user}} |
    Format-Table -AutoSize

# Top processes by launches
Write-Host ""
Write-Host "Top processes by launches:"
$events | Where-Object { $_.event -eq "create" } |
    Group-Object image | Sort-Object Count -Descending |
    Select-Object -First 10 @{n="Launches"; e={$_.Count}}, @{n="Image"; e={$_.Name}} |
    Format-Table -AutoSize

Write-Host ""
Write-Host "Total events analyzed: $($events.Count)"
