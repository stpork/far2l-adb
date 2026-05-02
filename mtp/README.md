# MTP Plugin for far2l

Browse and manage files on Android (and other MTP-class) devices over USB
from far2l. Backed by [libmtp](https://libmtp.sourceforge.io/).

## Features

- Auto-detect connected MTP devices; enumerate manufacturer, product, serial
- Two-level navigation: device → storage → folders/files
- F3/F5/F6/F7/F8 — view, copy, move, mkdir, delete, with progress and abort
- Single + multi-select copy/move with shared progress dialog (byte +
  total bars; debounced repaints)
- Overwrite dialog (Skip/Overwrite/All/Cancel + remember-choice) with
  atomic-aside-rename so a failed op never destroys data
- Shift+F5 / Shift+F6 / cross-panel F5 / F6 share a path-edit prompt:
  type `..`, `./sub/`, `Internal/Temp/foo`, or a renamed basename — all
  resolve consistently against the destination panel
- In-device Copy/Move via libmtp's `Copy_Object` / `Move_Object` when the
  responder supports them; transparent fallback to host-mediated
  download → upload when it doesn't
- Capability-driven (no vendor/product tables): plugin queries
  `Check_Capability` + `Get_Supported_Filetypes` per session and routes
  uploads to a filetype the device accepts (Sony Xperia rejects
  `Undefined`; plugin picks a neutral advertised type instead)
- Galaxy "blank-filename for new objects" quirk patched via a
  session-wide `{handle: name}` cache

## Build

Part of the far2l tree:

```bash
# from the far2l build directory
cmake --build . --target mtp
```

Output: `install/Plugins/mtp/plug/mtp.far-plug-wide`.

Disable with `-DMTP=NO` at CMake-configure time.

## Install

Copy `install/Plugins/mtp/` into far2l's Plugins folder:

- macOS: `/Applications/far2l.app/Contents/MacOS/Plugins/`
- Linux (system): `/usr/lib/far2l/Plugins/`
- Linux (user): `~/.local/lib/far2l/Plugins/`

Restart far2l. The plugin appears in the drives menu **Alt+F1 / Alt+F2 → MTP**.

## Prerequisites

### Linux

```bash
# Debian/Ubuntu
sudo apt install libmtp-dev libusb-1.0-0-dev

# Fedora
sudo dnf install libmtp-devel libusb1-devel

# Arch
sudo pacman -S libmtp libusb
```

User must be in the `plugdev` group (or equivalent) for udev to grant
non-root USB access — the libmtp packages typically install the
necessary udev rules.

### macOS

```bash
brew install libmtp libusb
```

`libmtp` is also listed in the project `Brewfile`.

**macOS-specific caveat:** the system `icdd` (Image Capture daemon) and
`ptpcamerad` claim MTP-class devices on plug-in with
`kIOUSBExclusiveAccess`, blocking libmtp's open. The plugin works around
this automatically: it writes the per-device "Connecting this device
opens: No application" preference, then SIGKILLs `icdd` /
`mscamerad-xpc` / `ptpcamerad` so launchd's respawn reads the new
preference and skips the claim. No GUI step or `sudo` required.

Set `MTP_NO_ICDD_BYPASS=1` to disable the bypass (useful for diagnosing
whether icdd is the cause of an open failure).

### Device

Connect the device via USB, unlock it, and select **File transfer (MTP)**
mode in the USB-connection notification. Some devices show an "Allow
file transfer" prompt the first time — accept it. Until accepted,
listings may be empty and uploads may fail with `0x2002`; the plugin's
`Get_Storage` retry loop covers the few seconds before the user taps.

## Usage

1. Open from Alt+F1 / Alt+F2 → MTP
2. Pick a device, press Enter — opens the storage list (single-storage
   devices skip this and jump straight to the file view)
3. Navigate folders and copy/move/delete with standard keys
4. Shift+F5 in-place copy: prompts for new name with full storage path
   prefilled; multi-select auto-suffixes `.copy`
5. Shift+F6 rename: same prompt; type `../` or `./sub/` to also move
6. Cross-panel F5/F6 between two MTP panels on the same device uses
   `Copy_Object` / `Move_Object` directly — no host roundtrip

## Key bindings

| Key      | Action                                              |
|----------|-----------------------------------------------------|
| Enter    | Enter directory / connect to device                 |
| F3       | View file (downloaded to a temp location)           |
| F5 / F6  | Copy / Move (host↔device or same-device)            |
| Shift+F5 | In-place copy with prompt (rename or relocate)      |
| Shift+F6 | Rename / move with prompt                           |
| F7       | Make directory                                      |
| F8       | Delete                                              |
| F10      | Close plugin                                        |

## Diagnostics

Set `MTP_LIBMTP_DEBUG=usb,ptp` (or `all`) before launching far2l to
capture libmtp's wire traces in `mtp_libmtp.log` next to the plugin
binary. Plugin-side logs land in `mtp.log` in the same directory when
the plugin is built with `-DCMAKE_BUILD_TYPE=Debug`.
