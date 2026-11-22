# Library

Library shows the games registered on your console and the game sources reported on its drives. Search by name, title ID or path; narrow the results by status, location or format. Open a card to see its details; with a compatible provider, mount or unmount a source or copy or move it to another drive. On a stock PS4 Orbit supplies the inventory itself and provider actions stay unavailable. Library keeps itself up to date: there is nothing to refresh, and game sizes are measured on their own (see [Automatic updates](#automatic-updates-and-artwork)). **Files** browses and manages what is on your drives: see [Files](#files).

Files and automatic Library updates were tested on a PS4 on firmware 12.00 in November 2026.

## Requirements

On PS4 there is no game-image manager: Orbit reads the installed-package inventory itself. A title folder containing an `app.pkg` in `/user/app`, or in the matching folders on extended storage, counts as installed. Orbit takes each name from the game's `param.sfo` or `param.json`, then the catalogue, then the title ID. These games are shown as installed PKGs with the package's size as their source size, and drive capacity comes from Orbit's own storage checks.

Orbit also keeps a compatible-provider path for the documented [v1 games API](https://github.com/drakmor/ShadowMountPlus/blob/1.7/docs/api.md): if a provider answers on its configured local port, its inventory, mounts and storage actions are used instead. On a stock PS4 nothing answers, so Library shows the console's own inventory. The port is read from `/data/shadowmount/config.ini` (default 10101) and `api_enabled=0` is respected; Orbit does not change that configuration. Pair your phone or computer with Orbit to view the Library. Download-source selection is unnecessary.

## Status labels

| Label | Meaning |
|---|---|
| Installed | The title is registered on the console — an `app.pkg` under `/user/app` or extended storage, or reported by the provider. |
| Not installed | A source is known, but the title is not reported as installed. |
| On drive | The provider reports the source as available. |
| Mounted | The provider reports the source currently mounted. |
| Source missing | The known source is unavailable. Its drive may be disconnected, or the source may have moved or been removed. |

On a stock PS4 only installed packages appear; provider-only states do not.

A title can be installed while its source is missing, or be on a drive without being installed. Counts therefore overlap. These labels describe inventory state, not whether the game will launch. PS4 and PS5 titles are identified separately when the provider knows the platform.

The API reports one entry per title ID. Orbit reflects that inventory; it does not enumerate every duplicate copy on every drive. Folder, PKG, FFPFSC, FFPFS, FFPKG and exFAT labels follow the reported source/image type. Unknown image types use the generic Image label. Console-reported size is omitted when unknown and is not a recursive measurement of source files.

## Automatic updates and artwork

Paired clients share one background inventory subscription across Discover, Browse, Downloads and Library, with actual provider reads limited to one attempt per 30 seconds across clients. Orbit also works on its own, so Library has no Refresh or Measure buttons:

- **Scans.** When Files copies or moves a game image or game folder into a homebrew folder, unpacks an archive there, or a drive is connected, Orbit asks the provider to scan, at most once a minute; on a stock PS4 it re-reads the installed-package folders itself. A second reason within that minute waits for it. While a provider registers what it found, Orbit reads the inventory every ten seconds for a minute and a half, and Library shows **Looking for new games…**. The same happens after **Scan for games** and the scans downloads ask for. Downloads and Files installs already ask for their own scan (see [installing downloaded games](package-installation.md)).
- **Drives.** Orbit looks at its drives every ten seconds; connecting or removing one reads the games and drives again.
- **Game sizes.** Orbit measures game sizes the first time it finds games on a drive without one, then again only when new ones appear, at most once a minute. A size shows **Measuring…** meanwhile. A game the provider can't measure is not retried until another new game appears.

**Try again** appears only when the inventory isn't answering. It reads the inventory sooner, subject to a shared five-second cooldown, and never asks a provider to rescan, mount, install or delete games. Automatic scans don't change the result of an action you started.

If the provider stops responding, the last successful result stays visible with a saved-snapshot warning. A successful empty response is shown as an empty Library. The cache is held in memory and ends when Orbit stops. Leaving Library does not stop an active storage operation. Completed operation messages can be dismissed from Library without removing their files. Orbit tracks the current provider storage job in the background; opening Orbit after a restart reads its status without resubmitting the operation.

Artwork is optional. Orbit matches a title ID to an existing catalogue artwork URL, independently of enabled download sources. Other titles use the Orbit mark. No game artwork is bundled, and the browser does not use arbitrary image URLs supplied by the inventory provider.

## Installed version and updates

Library shows the installed version of each PS4 game: the higher of the APP_VER in the game's own param.sfo and in an installed update package. When the catalogue has a newer update this console's firmware runs, the game gets an **Update** badge with that version, and the **Updates** view (shown only while there is one) lists every game with an update. A game's details offer **Update · size**, and the Updates view offers **Update all**. Each update downloads and installs on its own; see [Get everything and updates](package-installation.md#get-everything-and-updates). Selecting **Update** again after a failed or cancelled download picks that download up again. If the PS4 couldn't add an update, the game shows **Not added** with the reason until it is added. Orbit Zero shows the same badge and Updates view.

Nothing is offered when Orbit can't read the installed version, when the catalogue has no update for that title ID, or when every newer update needs newer firmware. Library offers updates only for title IDs the catalogue lists, so another region's copy gets none.

## Catalogue and download status

Catalogue cards show active downloads and known Library copies. Each download option is matched against its saved release ID, title ID, format, filename and size. A catalogue correction cannot silently turn a saved download into a different file.

An exact Library match requires the title ID, format and filename to agree, with matching measured size when available. The provider does not expose a reliable release version; versioned or otherwise ambiguous copies are labelled **Related copy in Library**, and Download stays available. A completed transfer is labelled **Downloaded**, which is history rather than proof the file still exists. Completed downloads show their saved file or extraction-folder path under **Location** in both the browser and native app. Showing a location does not prove registration or installation. Orbit 1.5.0 automatically requests scans after new image/folder downloads or transfers and provides a separate, explicit **Install on PS4** action for PKG/FPKG games. Downloaded packages stay on their drive unless the optional deletion-after-installation setting is enabled. See [installing downloaded games](package-installation.md).

## Library actions

**Scan for games** stays as a manual fallback for games Orbit didn't add itself, for example ones copied to a drive from a computer. It asks the provider to scan its configured locations; on a stock PS4 it re-reads the installed-package folders itself. It requires confirmation because a provider scan may register games or mount sources; a queued scan is not a completed scan. The provider may defer it while a game is active.

**Mount game** and **Unmount game** appear only when the running provider advertises those capabilities and manages the selected source. Close a running game before confirming unmount. Orbit re-reads the source before sending an action. Library game actions do not launch or uninstall games. The separate Files page can delete unprotected files after confirmation.

## Storage management

Game sizes are measured automatically (see [Automatic updates](#automatic-updates-and-artwork)); unavailable sizes are not shown as zero. Measurements remain cached until the source identity changes. Shared files can represent multiple titles, so summing rows is not a reliable measure of total drive usage.

Copy, move and mount actions need a compatible provider; they stay unavailable on a stock PS4, where Library shows drive capacity from Orbit's own checks.

Open a game and select **Copy to drive** or **Move to drive**. Destinations come from the provider's current writable locations. Orbit checks the selected source, destination and free space again before starting. The game must be unmounted. Downloads keep running during a copy or move: the copy leaves room for what unfinished downloads still need, and a download that starts meanwhile leaves room for what the copy still writes and never takes its name. Copies and moves take turns with Files actions. Existing files are not overwritten by the provider.

Copy retains the source. Move requires confirmation: Orbit copies through ShadowMount, reads back and compares the files, registers the new location, then asks ShadowMount to remove the original. If verification or registration fails, the original stays. A compatible ShadowMount API with copy, exact-source deletion and manual-source registration is required. Progress remains available after leaving the details screen, with cancellation while the provider reports it is safe. The final stage of a move can be non-cancellable. An uncertain response never automatically repeats a copy/move request; Orbit checks the job status instead. ShadowMount owns each copy or deletion job and may continue it if Orbit stops. Orbit does not automatically restart an interrupted multi-stage move; check both locations before trying again. Only the provider's current or most recent job is available, not a permanent operation history. Completed, cancelled and failed operations appear as compact results, with byte counts and paths under **Details**. Active operations keep their progress bar and cancellation controls. Drive-capacity warnings are separate from the operation result; a capacity refresh failure does not change a completed operation into a failed one.

Requests use the documented local API, never ShadowMount's private socket or a browser-provided URL. Orbit exposes only paired, same-origin actions with fixed operation names. The server resolves opaque source/destination identifiers against freshly read provider data. Older providers remain usable for viewing inventory; unsupported actions are unavailable.

## Files

Open **Library → Files** (the **Files** button in the header; a **Files** row in Crossbar; Panorama's **Files** tab, or a drive's tile on the TV app's Library page). Files is only a file manager: your games stay in Library's own list. It shows Orbit's drives (internal storage, USB drives and extended storage) with their free space. Select a drive, then open folders. Sort by name, size or date modified, and search the open folder. Up to 2,000 items of a folder are shown.

Names starting with a dot, Orbit's own working files and the drive's system folders (`$RECYCLE.BIN`, `System Volume Information`, and the console's `user` and `ps4` folders on extended storage) are hidden. Links are never followed and nothing outside a drive can be reached.

Select a folder to open it directly. In Store and Cinema, its row’s **…** button opens folder actions. Crossbar uses the row’s action menu; Panorama shows actions for the selected entry. Select a file to view its actions. **Circle** or **Escape** closes an open dialog or menu first, then goes up one folder; from the drive list it returns to Library.

- **Open** also appears among folder actions.
- **Unpack here** extracts a ZIP, 7z or RAR archive next to it, as downloads do: into a folder named after the archive when it has several top-level entries. Orbit first tries the passwords it knows, then asks for the archive's password. The archive is kept.
- **Install on PS4** sends a PKG file to the console's installer. With a compatible provider it can also register a game image (`.exfat`, `.ffpkg`, `.ffpfsc`) or a game folder (one with `eboot.bin` or `sce_sys/param.json`): games in a `homebrew` folder are found by a scan, and newer providers add games from any other folder by path. On a stock PS4 only PKG files install. An installation can't be stopped from Orbit once it starts.
- **Save to this device** (phone or computer, not the PS4's browser) downloads the file through a link that works for five minutes.
- **Measure size** totals a folder.
- **Copy** and **Move** wait for a destination: open the folder to copy or move to, then select **Paste here**.
- **Rename** and **Delete** (delete asks first and can't be undone).
- **New folder** creates a folder in the open folder.

Copying, moving, deleting, unpacking, installing and measuring run one at a time and show their progress in a dialog. Copies and moves can be paused; all but installs can be cancelled. **Hide** keeps the operation running and shows a one-line status (the heading in Store and Cinema, the side panel in Crossbar, a tile in Panorama) that opens the dialog again. Copies are written in a hidden staging folder and appear only when complete, so a cancelled or failed copy leaves nothing behind. Nothing is ever replaced: copying into a folder that already has the name adds `(2)`, `(3)` and so on; moving refuses an existing name. Moving within a drive is instant; moving to another drive copies first, then removes the originals (that last step can't be cancelled). Orbit checks free space before copying or unpacking.

What something else is using is locked, with the reason shown: Orbit's own folder, unfinished downloads, files the PS4 is installing, installed games and games a provider has mounted, and the running operation. Locked entries can't be moved, renamed or deleted; copying is also refused for Orbit’s private files and sources being written. Files actions run while downloads continue, taking turns with Library copies. A download that starts during an action leaves room for what the action still writes and can't take a name it is writing or removing. A paused download’s owned file remains locked.

## Local validation

`make test-files` runs the real C backend on a disposable folder: listing, hidden names and links, new folders and renames, copies, moves and deletes with pause and cancel, free-space and link refusals, measuring, locks for Orbit's folder and provider-managed games, unpacking (and an archive password, when 7-Zip is installed), installs through a ShadowMount fixture with and without adding by path, the automatic scan after a game lands in homebrew (once a minute), and saving a file with ranges. `make test-library` runs the real C backend against disposable loopback fixtures. It covers authentication, source independence, mixed PS4/PS5 inventory, installed PKGs found in a fixture app folder when the provider does not list them, status/format/location mapping, empty results, malformed and oversized responses, duplicates, unsupported API versions, redirects, shared refresh limits, stale-result recovery, nonblocking requests and shutdown, desktop isolation, explicit scan/mount/unmount, automatic size measuring (once, then for new games), source and destination revalidation, copy/move progress, cancellation, download serialization, capability checks and recovery of an existing provider job. `tests/library_verified_move.py` also covers readback, nested files, corrupt copies, registration/deletion failures and cancellation before removal. These tests do not contact a console.

On a stock PS4, Files actions, installed-game detection and Library updates passed console checks on firmware 12.00 in November 2026. A ShadowMount-compatible provider on PS4 has not been tested.

Add-ons are a separate catalogue. Finding the base game in Library does not confirm its edition, version or add-on compatibility, and does not mark DLC as installed. See [Add-ons](addons.md) for downloading and installing them.
