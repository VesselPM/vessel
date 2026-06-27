# Vessel
*almost-sandboxed package runtime*

Vessel is a tool for installing and running GNU/Linux applications in a lightweight container. It uses mount namespaces, overlayfs, and some basic device isolation to give each app its own view of the filesystem.

The whole thing is a single binary with no daemon, no dbus, no polkit. It does not use Docker images or OCI formats. Packages are plain `.tar.gz` archives with a `manifest.toml` inside.

---

## Quickstart

```sh
# Install a package from a repo
vessel install org.obsidian.editor

# Run it (requires root for namespaces)
sudo vessel run org.obsidian.editor

# Run user-installed package with root
sudo VESSEL_HOME=$HOME/.local/share/vessel vessel run org.obsidian.editor

# Or install from a local file
vessel install some-package-1.0.0.ves
```

---

## How it works

Packages end up in `~/.local/share/vessel/packages/<id>/`. When you run one, Vessel:

1. Creates a new mount + UTS + (optionally) network namespace
2. Sets up an overlay filesystem with:
   - the package directory on top
   - dependency directories in the middle
   - the real root filesystem at the bottom
3. Mounts a tmpfs at `/dev`, `/tmp`, `/proc`, `/sys`, and `/run`
4. Hides host paths like `/home`, `/root`, `/mnt`, `/media`
5. Mounts a read-only bind of your real home into the container (if the
   manifest requests it)
6. Calls `chroot` into the overlay and runs the app's entrypoint

The app sees its own files and the system files it needs, but cannot touch other installed packages or most of the host.

---

## Package format (`.ves`)

A `.ves` file is a gzip-compressed tar archive. It must contain a file called `manifest.toml` at the root. The rest of the archive is whatever the app needs.

### manifest.toml

```toml
vessel = 1

[name]
id = "org.obsidian.editor"
version = "0.1.0"
entry = "/usr/bin/obsidian"
arch = ["x86_64"]

[deps]
vessels = ["org.example.libfoo"]
system = ["/usr/lib/libssl.so.3"]

[expose]
bin = ["obsidian"]
desktop = ["share/applications/obsidian.desktop"]

[permissions]
network = false
gpu = true
audio = false
```

| Section | Key | Description |
|---|---|---|
| (root) | `vessel` | manifest format version (int) |
| `[name]` | `id` | unique package identifier |
| | `version` | semver-ish version string |
| | `entry` | binary to run inside the container |
| | `arch` | supported architectures |
| `[deps]` | `vessels` | vessel package dependencies (installed + mounted at runtime) |
| | `system` | absolute paths to bind-mount from the host |
| `[expose]` | `bin` | binary names to shim into `/usr/bin` |
| | `desktop` | `.desktop` files to symlink into `/usr/share/applications` |
| `[permissions]` | `network` | if false, creates a new network namespace (no internet) |
| | `gpu` | bind-mounts `/dev/dri` |
| | `audio` | bind-mounts `/dev/snd` |
| | `devices` | explicit device nodes to expose |
| | `ipc` | granular host paths to mount (supports globs and `$ENV_VAR`) |
| `[fs]` | `home` | map a host path into the container (`{from: "...", to: "/home", mode: "ro"}`) |
| | `var` | map a tmpfs to `/var` |
| | `custom` | arbitrary bind mounts |

Dependencies listed in `[deps.vessels]` are:

1. Downloaded from repos and installed automatically when the package is installed
2. Mounted into the overlay at runtime (so the running app can see their files)

This is recursive by the way. If `foo` depends on `bar` and `bar` depends on `baz`, they
all end up in the overlay.

---

## Repos

A repo is just an HTTP server hosting an `index.toml` and `.ves` files. By default, a repo called `main` is created pointing at [the official index](https://github.com/vesselpm/repo).

```
vessel repo list
vessel repo add my-repo https://example.com/packages
vessel repo remove my-repo
```

Repos are stored in `~/.local/share/vessel/repos.toml`.

### index.toml format

```toml
[packages.com.example.hello]
version = "1.0.0"
url = "https://example.com/hello-1.0.0.ves"
description = "prints a greeting"
vessel_deps = ["libfoo"]

[packages.org.example.libfoo]
version = "2.1.0"
url = "https://example.com/libfoo-2.1.0.ves"
description = "core utility library"
```

If `url` is relative, it is resolved against the repo URL (well, actually it is treated as-is and prepended to the repo URL, so keep it absolute or relative to the repo root).

---

## Commands

| Command | Description |
|---|---|
| `vessel install <file.ves | id>` | Install from file or repo |
| `vessel remove <id>` | Uninstall |
| `vessel list` | List installed packages |
| `vessel info <id>` | Show package details |
| `vessel run <id> [args...]` | Run in sandbox (needs root) |
| `vessel shim <id> [name]` | Create `/usr/bin/<name>` → `vessel-run <id>` |
| `vessel search <query>` | Search across all repo indexes |
| `vessel repo list` | Show configured repos |
| `vessel repo add <name> <url>` | Add a repo |
| `vessel repo remove <name>` | Remove a repo |
| `vessel update` | Refresh indexes and check for updates |
| `vessel upgrade [pkg...]` | Upgrade packages (all or by name) |
| `vessel tui` | TUI for the operations lol |

`vessel-run` is a symlink to `vessel` that behaves like `vessel run`. Just like a multi-call binary.

---

## Paths

By default everything lives under `$HOME/.local/share/vessel/`:

```
~/.local/share/vessel/
├── packages/      # extracted package files
├── run/           # overlay upperdirs/workdirs (recreated on each run)
├── shims/         # shim scripts
├── cache/         # cached repo indexes and downloaded .ves files
├── repos.toml     # repo list
└── db.toml        # installed package database
```

Override with `VESSEL_HOME`:

```sh
VESSEL_HOME=/opt/vessel sudo vessel run org.obsidian.editor
```

When running with `sudo`, the default home changes to `/root/.local/share/vessel`. Either set `VESSEL_HOME` or install packages as root too.

---

## Building

Dependencies: `meson`, `ninja`, `libarchive`, `libcurl`, `ncurses`.

```sh
meson setup build
ninja -C build
sudo ninja -C build install
```

---

## Why not just use...

- **Docker / Podman**: those are daemons, this is not. Vessel is a single
  binary that does `unshare` + `mount` + `chroot`. No registry, no layers
  (well, overlayfs is layers, but you know what I mean), no image pull daemon.
- **Flatpak**: Flatpak has a runtime system, portals, and a whole desktop
  stack. Vessel is ~4000 lines of C with no runtime dependencies beyond libc,
  libarchive, libcurl, and ncurses.
- **`bwrap`**: Bubblewrap is a tool for building sandboxes, not a package
  manager. Vessel does both. It installs packages and runs them.
- **AppImage**: each AppImage is a self-contained file. Vessel shares
  dependencies between packages.

---

## License

Vessel itself is licensed under the [GNU Lesser General Public License v2.1 or later](LICENSE) (LGPL-2.1 for short).
