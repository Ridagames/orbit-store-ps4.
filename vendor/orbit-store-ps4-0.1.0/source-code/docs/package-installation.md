# Install downloaded games

The shared installation UI was introduced in 1.5.0. The PS4 adapter now uses BGFT storage tasks with 64-bit progress, separately from installed-title queries. The equivalent real-console PKG installation passed on the PS5 build (firmware 13.60), with its source retained; PS4-console verification is pending. Local integration tests also use a simulated installer for error and recovery cases; neither establishes compatibility for every game or DLC.

## Install a downloaded game

Finish downloading the files to your PS4. In **Downloads**, open **Downloaded files** or **Install on PS4**. If the download contains several packages, choose the one to install, then select **Install on PS4**. The browser interface and native app expose the same backend action in all four layouts.

The PS4 chooses its configured installation storage; the download destination does not select it. Allow space for the installation as well as the downloaded file. The source PKG remains unless **Delete packages after installing** is enabled in Orbit’s Storage settings (off by default). Only a confirmed successful installation with unchanged source-file identity can trigger that cleanup. Installation progress and errors are separate from download completion. Archives and computer caches have separate cleanup settings. Choosing an update or DLC is explicit, and compatibility with the installed base game remains the console's responsibility.

Installation does not make an incompatible game playable. PS4 packages still require suitable firmware, licences and jailbreak support. Orbit shows installer errors without claiming success merely because a request was accepted.

A replacement conflict preserves the existing installation and downloaded file and reports an error. Orbit does not silently uninstall an existing title. Automatic full-package replacement, including TV-app updates, still needs console verification.

After a restart or a lost status response, use **Check installation** when Orbit has a task ID. Orbit never automatically submits an interrupted installation twice. If the console provides no installation identifier, check its home screen; Orbit leaves the result unconfirmed and retains the file.

Use matching ELF and client builds; a catalogue refresh alone does not add these controls. **Install on PS4** uses a file already on the console. Installation always runs on the PS4.

**Library → Files** can install an existing package — and, with a compatible provider, a game image or game folder — after checking its path and file identity. See [Files](library.md#files).

## Get everything and updates

On a PS4 game's page, **Get everything** lists the game, the newest update this console's firmware runs and the game's DLC, each ticked except an installed game. Orbit can't tell which DLC is installed, so DLC is always ticked; untick what you have. **Download all** adds them to Downloads one at a time; anything that needs its provider page opens that page on the PS4 first, as a single Download does. Library's **Update** and **Update all** add only updates, the same way.

Everything added this way installs on its own once its download is checked; there is no separate **Install on PS4** step. Get everything installs in order: each download waits for the one before it, so the update installs after the game and DLC after the update. Library updates are separate games, so they install as each finishes. A waiting download shows "Installs after {title}.". If an earlier download fails, is cancelled or is not installed, the ones after it stay downloaded and show "Install {title} first, then select Install."; nothing installs out of order. An ordered list also stops adding the rest when a provider page is closed or fails. The order is saved with each download, so it survives a restart.

An update's own version and firmware come from its catalogue record. Library compares that version with the installed game's (the higher of the game's and an installed update's APP_VER), and offers only updates the console's firmware runs. Whether a given update suits a particular dump or region remains the console's check, as with any package.

## Automatic library scans

A newly completed exFAT, FFPFSC, FFPKG image or extracted game folder automatically requests a scan through a running, compatible provider API. On a stock PS4 there is no image manager to register those formats: downloaded packages install through **Install on PS4** and then appear in Library from `/user/app`; nothing else registers a folder or image.

When a scan is requested, Orbit checks the provider's inventory — or, without one, its own installed-package list, matching the title id in the download path. A scan request is not proof that a game has been registered: until confirmed, the download shows a waiting or unconfirmed state. The file path remains available regardless of registration. Existing completed downloads are checked for packages at startup without automatically rescanning every old game.

## Protocol and persistence

- `/api/v1/system` advertises `packageInstallProtocol: 1`.
- Authenticated `GET /api/v1/installations` returns completion receipts, package states and library-scan status.
- Authenticated `POST /api/v1/installations` accepts `install`, `check`, `inspect` and `scan` with server-issued receipt/package IDs. The internal `review` action can inspect an existing completed receipt; the Orbit Zero receiver is not included in this port.
- Download actions expose the same operations as `install-<package-id>`, `check-<package-id>`, `check-packages` and `scan-library`.
- `installations.json` records the original drive and each package's file identity, original `contentId` and separate BGFT `installTask`. Progress is written atomically before submitting an installation. No credentials or source download URLs are recorded there.
- Package headers, original drive identity and file identity are checked again before submission. Relative traversal, symbolic links and substituted files are rejected. Filesystem and installer calls run in a background worker.
- A malformed saved installation history disables new installation actions without discarding that history or the downloaded files.

## Validation

`tests/packages_integration.py` exercises the actual receiver and HTTP API against disposable files and an explicit test-only OS service. It covers installation, retained source files, bad packages, changed files, symlink substitution, restart handling, lost status, missing identifiers and ShadowMount scan/registration separation. Folder-transfer and Library regressions cover the existing completion and storage paths.

Orbit Zero's `tests/package-installations.test.mjs` checks original-console identity, changed connections, server-issued package selection, legacy transfer review and archive receipt mapping. Native model tests cover independent download and installation states. Browser/desktop dialog checks exercise package choice, explicit submission, progress, keyboard focus and closing.

On the PS5 console, the 1.5.0 pass installed SteamWorld Dig (PS4 PKG) successfully with its source retained, and verified ShadowMount scans for existing exFAT, FFPFSC and folder sources. The final Mac app transferred a saved DLC file to USB storage and exposed its requirements and separate Install action. Actual DLC installation, FPKG game launch and a fresh image-download automatic scan remain untested. Those PS5 results are historical and do not satisfy PS4 acceptance.
