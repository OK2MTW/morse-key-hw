# Firmware update check in the application (on every login)

Document version: 1 (2026-11-12). Audience: the application project (server on the Raspberry Pi + web front end).
Goal: **on every user login the RPi checks whether a newer CW Key firmware exists, downloads it, and the application
offers the update to the user.** File formats, flashing and safety rules: [firmware-flashing.md](firmware-flashing.md).

## 1. Source of truth

| What | URL | Notes |
|---|---|---|
| Newest release (manifest) | `https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/manifest.json` | `version`, file names, sizes, SHA-256; ETag; GitHub caches it **≤ 5 min** (`Cache-Control: max-age=300`) |
| Files of the newest release | same folder `…/firmware/latest/<file>` | stable names: `cwkey-f411.uf2`, `cwkey-f401.uf2`, `cwkey-f411-full.bin`, `cwkey-f401-full.bin` |
| A specific version | `…/firmware/dist/cwkey-<version>/manifest.json` | e.g. to roll back |

Do **not** use the GitHub REST API (`releases/latest`): unauthenticated it is limited to 60 requests/hour per IP, and
release downloads have no CORS. Do **not** hard-code a version folder (`dist/cwkey-0.3.0/` keeps existing and always
answers 0.3.0 – a typical cause of "the app still offers 0.3.0").

## 2. On every login (RPi server)

```
login
  └─ check_firmware()                       # asynchronous, must not delay the login
       1. GET latest/manifest.json
            headers: If-None-Match: <stored ETag>, Cache-Control: no-cache
            timeout 5 s; on any error -> keep the stored state, log it, done
       2. 304 Not Modified          -> nothing new, done
          200 -> parse JSON, validate (name == "CwKey", semver "version", targets with full/uf2 entries)
       3. if version <= stored_version -> store ETag, done
       4. for each target file (full.bin and .uf2 of both chips):
            download from latest/<file> into  firmware-cache/<version>.tmp/
            check size and SHA-256 against the manifest; on mismatch delete the .tmp folder, done (retry next login)
       5. write the manifest into the folder, rename  <version>.tmp -> <version>  (atomic)
       6. stored_version = version, stored ETag = new ETag, keep the previous version folder (rollback), delete older ones
```

- Compare versions **numerically** (`0.10.0 > 0.9.0`), not as strings.
- Download **all** files of the release before announcing it (never offer a half-downloaded version).
- The RPi serves the files from its own cache to the front end (same origin, works offline, no CORS concerns).
- Optional: also check once a day in the background, not only at login.

Suggested server API for the front end:

| Endpoint | Returns |
|---|---|
| `GET /api/firmware/latest` | `{ "version": "0.4.0", "manifest": {...}, "checkedAt": "...", "error": null }` |
| `GET /api/firmware/<version>/<file>` | the cached file (`application/octet-stream`) |

## 3. Which version is in the connected key?

| Firmware in the key | How the application learns its version |
|---|---|
| **0.4.0 and newer** | USB MIDI `GET_INFO` → major.minor.patch and chip ([midi-settings-spec.md](midi-settings-spec.md)). Chrome/Edge (PC, Android), Firefox. |
| older, or a browser without Web MIDI | unknown while the key runs. In the bootloader drive: `INFO_UF2.TXT` gives the chip; `CURRENT.UF2` contains the running firmware – search its payload for the ASCII text `CwKey x.y.z` (present since 0.1.1). **Verify on the prototype.** |
| nothing connected | offer the update anyway as "available" (the user may update later) |

## 4. Offering the update (front end, after login)

1. Ask the server for `/api/firmware/latest`.
2. If the key's version is known and **lower** → show "Firmware X.Y.Z is available (you have A.B.C)" with the release notes
   link `https://github.com/OK2MTW/morse-key-hw/releases/tag/v<version>` and an **Update** button.
   If the version is unknown → show "Firmware X.Y.Z is available" with the same button and a hint how to check.
3. **Update** (details and mandatory checks: [firmware-flashing.md](firmware-flashing.md) section 5):
   - 0.4.0+ with Web MIDI: send `ENTER_BOOTLOADER` (confirmation `55 46 32`); otherwise ask the user to press NRST twice.
   - Wait for the drive (`STMF411BOOT` / `BlackPill`), read `INFO_UF2.TXT`, pick `cwkey-f411.uf2` or `cwkey-f401.uf2`.
   - Check SHA-256 and that every UF2 block address is ≥ `0x08010000`, write the file to the drive.
   - Success = the drive disappears and the key returns (with MIDI: `GET_INFO` shows the new version).
4. Never flash without the user's click; never retry automatically more than once.

## 5. Checklist when the application keeps offering an old version

1. On the RPi: `curl -s https://raw.githubusercontent.com/OK2MTW/morse-key-hw/main/firmware/latest/manifest.json | grep '"version"'`
   – must show the newest version (allow 5 min after a release).
2. Does the application read `latest/manifest.json` (not `dist/cwkey-0.3.0/…`, not a copy bundled with the app)?
3. Is the check really run at login (log line with the version found)? Did the download or SHA-256 check fail?
4. Front end: service worker / HTTP cache of `/api/firmware/latest` – send `Cache-Control: no-cache`.
