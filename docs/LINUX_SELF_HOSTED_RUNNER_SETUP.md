# Setting up a Linux self-hosted runner

**Why this document exists:** GitHub-hosted runner jobs are blocked while an account payment issue
is unresolved. `ci2.yml` routes the Linux build to a self-hosted runner in the meantime. This is
the checklist for preparing a new Linux machine to be one, written from the machine that was
actually gotten working (`linux_old_laptop`, a bare Debian install).

See `docs/WINDOWS_SELF_HOSTED_RUNNER_SETUP.md` for the Windows side. The two share one root cause
worth internalizing up front: **a fresh install has none of the toolchain a GitHub-hosted image
ships with, and `install-qt-action` makes assumptions about the host that a bare machine does not
meet.** Every step below is either apt packages a GitHub-hosted `ubuntu-latest` image already has,
or a workaround for one of those assumptions.

## 1. Register the runner

Standard GitHub flow: repo → Settings → Actions → Runners → New self-hosted runner → follow the
generated `config.sh`. Install it as a service (`svc.sh install && svc.sh start`) rather than
leaving it running in a console — a service survives reboots and logouts.

## 2. Passwordless sudo for the runner's user

The runner has no terminal attached when a job runs, so any `sudo` call - both `install-qt-action`'s
own internal `apt-get install` for Qt's runtime dependencies, and this project's own steps - fails
outright with `sudo: a password is required` unless the account can `sudo` without one.

```bash
echo "<runner-user> ALL=(ALL) NOPASSWD: /usr/bin/apt-get, /usr/bin/apt" | sudo tee /etc/sudoers.d/github-actions-runner
sudo chmod 0440 /etc/sudoers.d/github-actions-runner
sudo visudo -c
```

Scoped to `apt`/`apt-get` rather than a blanket `NOPASSWD:ALL` - least-privilege, and it's the only
command any step here actually needs elevated. `visudo -c` validates the syntax so a typo can't
lock out `sudo` entirely. Takes effect immediately; no service restart needed - `sudo` reads
`sudoers.d` fresh on every invocation, unlike the Windows runner's PATH-caching quirk.

## 3. cmake and a compiler toolchain

A bare install has neither. `ubuntu-latest`'s image ships both; this machine needs them installed
explicitly:

```bash
sudo apt-get update && sudo apt-get install -y cmake build-essential
```

`build-essential` brings `gcc`/`g++`/`make`.

## 4. Skip install-qt-action's own Python setup

`install-qt-action` calls `actions/setup-python` internally unless told not to, and that only ships
prebuilt binaries for a fixed list of distros - it has none for Debian ("The version
'>=3.9.0 <=3.13.3' ... was not found for Debian 13"). Two things follow:

**Install a real Python first**, since `setup-python: false` (below) makes the action fall back to
whatever `python3` is already on the machine:

```bash
sudo apt-get install -y python3 python3-pip python3-venv
```

**Then set `setup-python: false`** on the workflow's `install-qt-action` step:

```yaml
      - uses: jurplel/install-qt-action@v4
        with:
          version: ${{ env.QT_VERSION }}
          cache: true
          setup-python: false
```

## 5. Let pip write into Debian's system Python

Even with a real `python3` present, `install-qt-action`'s internal `pip install` of aqtinstall's
own dependencies fails with `error: externally-managed-environment` - Debian 12+ marks its
apt-owned Python as off-limits to direct `pip install` (PEP 668). Rather than deleting the
system Python's `EXTERNALLY-MANAGED` marker file (which disables the guard machine-wide), scope
the override to just this step:

```yaml
      - uses: jurplel/install-qt-action@v4
        with:
          version: ${{ env.QT_VERSION }}
          cache: true
          setup-python: false
        env:
          PIP_BREAK_SYSTEM_PACKAGES: "1"
```

## 6. patchelf

Needed later, for `cmake --install`'s deploy step: `qt_deploy_runtime_dependencies()` uses it to
rewrite RPATHs so the archived binary finds the Qt libraries shipped beside it rather than
whatever the host happens to have.

```bash
sudo apt-get install -y patchelf
```

## All of it in one command

Steps 2-6's apt packages, combined (run the sudoers step from §2 separately first - it needs
interactive `sudo` once, from an account that already has it):

```bash
sudo apt-get update && sudo apt-get install -y patchelf python3 python3-pip python3-venv cmake build-essential
```

## 7. Qt needs no manual install

Same as Windows: `jurplel/install-qt-action` fetches and caches the pinned Qt version
automatically at job time, including Qt's own runtime dependencies (X11/xcb libraries etc.) via
its own `apt-get install` - which is exactly why passwordless sudo (§2) has to be in place before
this step runs, not just before this project's own steps.

## 8. Workflow configuration

```yaml
  build-linux:
    name: Build (Linux)
    runs-on: [self-hosted, Linux]
    steps:
      - uses: actions/checkout@v7
        with:
          submodules: recursive

      - name: Install build dependencies
        run: sudo apt-get update && sudo apt-get install -y patchelf python3 python3-pip python3-venv cmake build-essential

      - uses: jurplel/install-qt-action@v4
        with:
          version: ${{ env.QT_VERSION }}
          cache: true
          setup-python: false
        env:
          PIP_BREAK_SYSTEM_PACKAGES: "1"

      # ... configure / build / test / deploy as in ci.yml
```

`runs-on: [self-hosted, Linux]` is generic - it targets whichever machine is registered with those
labels, not this one by name. Bringing up a replacement Linux runner needs no workflow change,
just registering it (§1) and running through §2-§6 on it.

## Checklist

- [ ] Runner registered and running as a systemd service (`svc.sh install && svc.sh start`)
- [ ] Passwordless `sudo` for the runner's user, scoped to `apt`/`apt-get` (`visudo -c` passes)
- [ ] `cmake` and `build-essential` installed
- [ ] `python3`, `python3-pip`, `python3-venv` installed
- [ ] `patchelf` installed
- [ ] Workflow: `setup-python: false` and `PIP_BREAK_SYSTEM_PACKAGES: "1"` on the
      `install-qt-action` step

## What this sidesteps that WSL did not

The first attempt at a Linux runner used WSL Ubuntu on a Windows machine (`whist_wsl_ubuntu_runner`),
stopped rather than fixed after it hit the same sudo and Python issues as here, plus one more:
a broken Python stdlib (`AttributeError: module 'types' has no attribute 'ModuleType'`, traced to
something on `PYTHONPATH` or `sys.path` shadowing the real `types` module - never root-caused). A
native Linux install has one fewer layer between the runner and the actual OS, and this list is
shorter for it.
