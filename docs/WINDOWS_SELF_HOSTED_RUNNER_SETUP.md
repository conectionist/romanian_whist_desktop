# Setting up a Windows self-hosted runner

**Why this document exists:** GitHub-hosted runner jobs are blocked while an account payment issue
is unresolved. `ci2.yml` routes the Windows build to a self-hosted runner in the meantime. This is
the checklist for preparing a new Windows machine to be one, written from the machine that was
actually gotten working (`win_vm_runner`).

## The one fact that shapes everything below

**The runner runs as a Windows service, and that service does not run as you.** By default it runs
as `NT AUTHORITY\NETWORK SERVICE` — a different account than whoever is logged in and clicking
through the setup. Check it:

```powershell
Get-WmiObject Win32_Service -Filter "Name LIKE 'actions.runner%'" | Select-Object Name, StartName
```

Two consequences follow from this, and both bit us:

1. **Every dependency must be installed machine-wide, not per-user.** A per-user install (which is
   what most installers default to when not run elevated) lands under
   `C:\Users\<you>\AppData\...`, and that folder's NTFS permissions restrict it to you, `SYSTEM`,
   and `Administrators` — `NETWORK SERVICE` cannot read it, regardless of `PATH`.
2. **A machine `PATH` (or other machine environment variable) change made while the runner service
   is already running is invisible to it until it restarts.** Windows services snapshot their
   environment at start time. Restart the service as the last step after any such change:

   ```powershell
   Restart-Service -Name "<exact service name from the query above>"
   ```

Everything from here on assumes an **elevated** PowerShell prompt (Run as Administrator) — the
machine-wide installs and the execution policy change both need it, and a non-elevated `winget
--scope machine` will hang waiting for a UAC prompt that a non-interactive session can never answer.

## 1. Register the runner

Standard GitHub flow: repo → Settings → Actions → Runners → New self-hosted runner → follow the
generated `config.cmd`. Install it as a service (`config.cmd` offers this, or run `svc.sh`/the
service install step) rather than leaving it running in a console — a service survives reboots and
logouts.

## 2. C++ toolchain: Visual Studio (or Build Tools) with the Desktop C++ workload

Needed for MSVC and the Windows SDK. Verify an existing install actually has the workload the
project needs — Visual Studio can be installed without it:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

If this prints nothing, install Visual Studio (Community is fine) or the standalone Build Tools
with the "Desktop development with C++" workload, which pulls in both MSVC and a Windows SDK.

## 3. Git for Windows — for `git.exe` *and* `bash.exe`

Several of this project's CI steps use `shell: bash`. Git for Windows' own installer only adds
`Git\cmd` (which has `git.exe`) to `PATH` by default — `bash.exe` lives in the separate `Git\bin`
folder, which needs adding explicitly:

```powershell
winget install --id Git.Git -e --scope machine --accept-package-agreements --accept-source-agreements
[System.Environment]::SetEnvironmentVariable("Path",
    [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";C:\Program Files\Git\bin",
    "Machine")
```

## 4. CMake

```powershell
winget install --id Kitware.CMake -e --scope machine --accept-package-agreements --accept-source-agreements
```

## 5. Python — machine-wide

`jurplel/install-qt-action` (the action that fetches Qt in CI) shells out to Python's `aqtinstall`
package. Install it machine-wide, for the `NETWORK SERVICE` reason above:

```powershell
winget install --id Python.Python.3.12 -e --scope machine --accept-package-agreements --accept-source-agreements
```

## 6. PowerShell execution policy

The runner executes a number of generated PowerShell scripts internally — its own job setup, and
scripts that actions like `install-qt-action` write to a temp folder at run time. The default
`Restricted` policy blocks all of them with `UnauthorizedAccess`.

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope LocalMachine -Force
```

`RemoteSigned`, not `Bypass`: it still requires a valid signature for anything downloaded from the
internet (flagged with Windows' Mark-of-the-Web) and only allows locally-generated scripts — like
the runner's own temp scripts — to run unsigned.

## 7. Qt needs no manual install

`jurplel/install-qt-action` fetches and caches the pinned Qt version automatically at job time,
the same way it does on GitHub-hosted runners. It does need one non-default option on a
self-hosted Windows runner — see the workflow snippet below.

## 8. Restart the runner service

Last step, and also the last step after any future machine-wide `PATH`/policy change:

```powershell
Restart-Service -Name "<exact service name>"
```

## 9. Workflow configuration

Two things the workflow YAML needs, beyond what a GitHub-hosted job needs:

```yaml
  build-windows:
    runs-on: [self-hosted, Windows]
    steps:
      ...
      - uses: jurplel/install-qt-action@v4
        with:
          version: ${{ env.QT_VERSION }}
          cache: true
          setup-python: false   # see below
```

`setup-python: false` matters: by default `install-qt-action` runs its own
`actions/setup-python`, which downloads a hosted-tool-cache Python via a script built for
GitHub-hosted runner images. That script does not work on a self-hosted Windows runner. Setting
`setup-python: false` tells the action to use the Python already on `PATH` (step 5) instead of
installing its own.

## 10. Verify locally before trusting a CI run

A local dry run surfaces MSVC-specific problems (this codebase's Windows build had never actually
compiled under MSVC before this) much faster than iterating through CI:

```powershell
$env:CMAKE_PREFIX_PATH = "<path to a Qt install>\msvc2022_64"   # any local Qt 6.10.3+ works for this check
cmake --preset release
cmake --build --preset release

# Test executables need Qt's DLLs to launch - install-qt-action puts Qt's bin
# on PATH automatically in CI; replicate that here:
$env:Path = "<path to a Qt install>\msvc2022_64\bin;" + $env:Path
ctest --preset release --output-on-failure --no-tests=error
```

## Checklist

- [ ] Runner registered, running as a Windows service
- [ ] Noted the service account (`Get-WmiObject Win32_Service ...`)
- [ ] Visual Studio/Build Tools with the C++ desktop workload (`vswhere` confirms it)
- [ ] Git for Windows installed; `Git\bin` (not just `Git\cmd`) on the machine `PATH`
- [ ] CMake installed machine-wide
- [ ] Python installed machine-wide
- [ ] Execution policy `RemoteSigned` at `LocalMachine` scope
- [ ] Runner service restarted after all of the above
- [ ] Workflow: `runs-on: [self-hosted, Windows]` and `setup-python: false` on `install-qt-action`
- [ ] Local `cmake --preset release` → build → `ctest` passes before relying on a CI run

## Can this be a script instead of a manual checklist?

Mostly, yes — see `tools/setup-windows-runner.ps1`. It runs steps 2-6, 8 and 9's prerequisites in
one pass. Two things it deliberately does NOT try to automate:

- **Registering the runner itself** (step 1) — that involves a per-repo/per-org registration token
  from GitHub's UI that expires quickly and shouldn't be scripted into a file that might be reused
  or committed.
- **Visual Studio's install** — the workload install is slow, wants its own progress UI, and is
  worth watching rather than backgrounding; the script only *checks* for the workload with
  `vswhere` and tells you to install it if missing.

Run it from an elevated PowerShell prompt, and **watch it run** rather than firing it via a
scheduled task or unattended session — every one of the pitfalls this document exists to record
(a per-user install invisible to the service account, a stale `PATH` in an already-running
service) is exactly the kind of thing that's obvious when you watch the output and easy to miss
if you don't. The script is for consistency and speed on a machine you're sitting in front of, not
for walking away from.
