<#
.SYNOPSIS
    Prepares a Windows machine's dependencies for this project's self-hosted GitHub Actions runner.

.DESCRIPTION
    Covers docs/WINDOWS_SELF_HOSTED_RUNNER_SETUP.md steps 3-6, 8: Git (with bash.exe on PATH),
    CMake, Python, and the PowerShell execution policy, all installed machine-wide, then restarts
    the runner service so it picks up the changes.

    Deliberately NOT covered - see the doc for why:
      - Registering the runner itself (needs a per-repo/per-org token from GitHub's UI).
      - Installing Visual Studio / Build Tools' C++ workload. This script only checks for it.

    Run this from an ELEVATED PowerShell prompt. A non-elevated session cannot complete the
    machine-wide installs (winget hangs on a UAC prompt it can never answer) or the execution
    policy change, and will fail loudly rather than silently falling back to a per-user install.

.PARAMETER RunnerServiceName
    Exact Windows service name for the runner, e.g. "actions.runner.myorg-myrepo.my_runner".
    Find it with: Get-Service -Name "actions.runner*"
    If omitted, the script finds it automatically as long as exactly one such service exists.
#>

param(
    [string]$RunnerServiceName
)

$ErrorActionPreference = "Stop"

function Assert-Elevated {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        throw "Not running elevated. Re-launch PowerShell as Administrator and run this script again."
    }
}

function Get-MachinePath {
    [System.Environment]::GetEnvironmentVariable("Path", "Machine")
}

function Add-MachinePathEntry([string]$Entry, [switch]$Prepend) {
    $current = Get-MachinePath
    $parts = $current -split ';'
    if ($parts -contains $Entry) {
        if ($Prepend -and $parts[0] -ne $Entry) {
            # Already present but not first - move it to the front. Needed for
            # Git\bin specifically: C:\Windows\System32 ships its own bash.exe
            # (a WSL launcher stub) and wins any PATH search that finds it
            # first, so a GitHub Actions `shell: bash` step silently resolves
            # to that stub instead of Git Bash and fails with "Windows
            # Subsystem for Linux has no installed distributions."
            $reordered = @($Entry) + ($parts | Where-Object { $_ -and $_ -ne $Entry })
            [System.Environment]::SetEnvironmentVariable("Path", ($reordered -join ';'), "Machine")
            Write-Host "  Already on machine PATH but not first; moved to front: $Entry"
            return
        }
        Write-Host "  Already on machine PATH: $Entry"
        return
    }
    if ($Prepend) {
        [System.Environment]::SetEnvironmentVariable("Path", "$Entry;$current", "Machine")
        Write-Host "  Added to front of machine PATH: $Entry"
    } else {
        [System.Environment]::SetEnvironmentVariable("Path", "$current;$Entry", "Machine")
        Write-Host "  Added to machine PATH: $Entry"
    }
}

Assert-Elevated

Write-Host "=== 1/6: Visual Studio C++ workload (check only) ===" -ForegroundColor Cyan
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$hasCppWorkload = $false
if (Test-Path $vswhere) {
    # -products * is required: -latest alone only searches the full VS IDE products
    # (Community/Professional/Enterprise), silently skipping a Build Tools-only install.
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) { $hasCppWorkload = $true }
}
if ($hasCppWorkload) {
    Write-Host "  Found: $vsPath"
} else {
    Write-Warning "No Visual Studio install with the C++ desktop workload found. Install Visual Studio (or Build Tools) with 'Desktop development with C++' before running CI here - this script cannot install it for you."
}

Write-Host "=== 2/6: Git for Windows (git.exe AND bash.exe) ===" -ForegroundColor Cyan
if (Test-Path "C:\Program Files\Git\bin\git.exe") {
    Write-Host "  Already installed."
} else {
    winget install --id Git.Git -e --scope machine --accept-package-agreements --accept-source-agreements --silent
}
# bash.exe lives in Git\bin, which the installer does not add to PATH by default
# (only Git\cmd, which has git.exe, goes on PATH automatically). It must come
# BEFORE C:\Windows\System32 in PATH - see the comment in Add-MachinePathEntry.
Add-MachinePathEntry "C:\Program Files\Git\bin" -Prepend

Write-Host "=== 3/6: CMake ===" -ForegroundColor Cyan
if (Test-Path "C:\Program Files\CMake\bin\cmake.exe") {
    Write-Host "  Already installed."
} else {
    winget install --id Kitware.CMake -e --scope machine --accept-package-agreements --accept-source-agreements --silent
}

Write-Host "=== 4/6: Python (machine-wide - required for install-qt-action) ===" -ForegroundColor Cyan
if (Test-Path "C:\Program Files\Python312\python.exe") {
    Write-Host "  Already installed."
} else {
    winget install --id Python.Python.3.12 -e --scope machine --accept-package-agreements --accept-source-agreements --silent
}

Write-Host "=== 5/6: PowerShell execution policy ===" -ForegroundColor Cyan
$policy = Get-ExecutionPolicy -Scope LocalMachine
if ($policy -eq "RemoteSigned" -or $policy -eq "Unrestricted" -or $policy -eq "Bypass") {
    Write-Host "  Already permissive enough: $policy"
} else {
    Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope LocalMachine -Force
    Write-Host "  Set to RemoteSigned."
}

Write-Host "=== 6/6: Restart the runner service ===" -ForegroundColor Cyan
if (-not $RunnerServiceName) {
    $candidates = Get-Service -Name "actions.runner*" -ErrorAction SilentlyContinue
    if ($candidates.Count -eq 1) {
        $RunnerServiceName = $candidates[0].Name
    } elseif ($candidates.Count -gt 1) {
        Write-Warning "Multiple runner services found; re-run with -RunnerServiceName specified:"
        $candidates | Select-Object Name | Format-Table
    } else {
        Write-Warning "No runner service found (name starting 'actions.runner'). Register the runner first (see docs/WINDOWS_SELF_HOSTED_RUNNER_SETUP.md step 1), then restart it manually so it picks up today's PATH/policy changes."
    }
}
if ($RunnerServiceName) {
    Restart-Service -Name $RunnerServiceName -Force
    Write-Host "  Restarted: $RunnerServiceName"
}

Write-Host ""
Write-Host "Done. This PowerShell window still has the OLD PATH cached - open a new one to verify:" -ForegroundColor Yellow
Write-Host '  cmake --version; python --version; git --version; where bash'
