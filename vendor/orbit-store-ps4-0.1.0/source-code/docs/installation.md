# Orbit setup, startup and updates

Orbit Store 0.0.1 for PS4, tested on a PS4 on firmware 12.00 with GoldHEN.

## Choose how to start Orbit

- **Native app:** `orbit_store_ps4.pkg` contains native app **0.0.1**, service **0.0.1** and **elfldr** v0.7, the ELF loader Orbit runs through. Install it and open **Orbit Store**; nothing has to run first. When an ELF loader answers on port 9021, the app sends Orbit there. When nothing answers, it gives its elfldr to GoldHEN's payload loader on port 9090, waits for elfldr to answer on 9021, then sends Orbit. GoldHEN's loader never gets Orbit itself: it can't start a program that big. This needs GoldHEN's payload loader on (**GoldHEN → Servers Settings → Enable PayLoader Server**).
- **Separate service:** send `orbit_store.elf` once to an ELF loader on port 9021 (for example elfldr, which GoldHEN's LaunchPad can start). GoldHEN's LaunchPad and its loader on 9090 can't start `orbit_store.elf` itself; it is too big for them. Later starts go through Orbit's small starter (see [Payload managers](#payload-managers)), the native app or the same ELF loader.

Close other apps such as ezRemote before starting Orbit. Programs started through elfldr share memory with open apps: on a test PS4 with ezRemote open, `orbit_store.elf` exited before it started (with ezRemote closed it started in 14 seconds). If Orbit doesn't answer, the native app and the starter send it once more by themselves, then say to close other apps.

Stop an older running service before starting its replacement. Replacing a package or uploading an ELF only changes saved files; it does not replace the active process. The current native and service versions are intentionally different.

<a id="user-flow"></a>

## Browser first run

1. Send `orbit_store.elf` once to an ELF loader on port 9021. It saves the runtime to `/data/orbit-store/orbit_store.elf` and elfldr to `/data/orbit-store/elfldr.elf`, adds the **Orbit Store Browser** icon, then starts the saved runtime through the same loader and exits.
2. Optionally open **App settings → Updates & startup → Payload managers** and choose **Add Orbit** for your manager. This allows Orbit to add and update its copy in LaunchPad's payload list or the Homebrew Launcher folder.
3. Open the **Orbit Store Browser** icon. It opens `http://127.0.0.1:34177/` in the console browser while Orbit is running.
4. After a reboot, run the jailbreak as usual. On GoldHEN v2.4b18.10 or newer, you can separately enable **Start automatically** after allowing the LaunchPad copy. AutoRun then runs Orbit's starter when GoldHEN loads. Otherwise, open the native app, run Orbit from LaunchPad (after **Add Orbit**) or send the saved copy to your ELF loader. The icon persists.

The icon is installed from an embedded PS4 PKG using BGFT. Its executable opens the local URL in the Internet Browser.

The icon is a browser shortcut and does not start a stopped backend. If Orbit has stopped, start it from the native app, LaunchPad or your ELF loader and reopen the icon.

On first opening the storefront, choose your download sources and acknowledge the download-rights and risk notice. Sources start off, and the choice is saved on the console for all paired devices. The catalogue lists PS4 games across Archive.org, Vikingfile and AkiraBox. For an AkiraBox browser option, choose storage, select **Open download page on PS4**, complete any provider verification, then select **Download**. Orbit captures the link, closes the page and returns to its browser interface or the native app. AkiraBox may show an ad instead; press **Close** (✕) and try again. If the ad keeps coming back, choose another source for the game. Orbit validates the captured file before queueing it. The provider browser runs on PS4; paired devices control the same session. **Sources** lets you change this later; disabling a source pauses unfinished downloads and preserves their files. Re-enabling it requires resuming those downloads manually. There is no library import in this version.

## Vikingfile account sign-in

In the browser, open **App settings → Sources & accounts → Vikingfile → Open Vikingfile account**.
In the native TV app, open **App settings → Sources & accounts**, scroll to
**Vikingfile** and select **Open Vikingfile account**. On
Vikingfile's official page, enter your account code (called an **account hash**),
complete any verification and select **Login**. Return to Orbit afterward.
Use the same account control to manage your account or sign out on Vikingfile.
Select **Check sign-in** to verify the session. Orbit shows **Signed in** only
after Vikingfile confirms it; this does not indicate whether the account is Premium.
If verification is required or the session has expired, sign in again on the official page.

For a Vikingfile game, choose a drive and select **Download to PS4**. Orbit first
uses the active PS4 user’s saved sign-in from the same browser used to sign in.
If Vikingfile returns a ready link, Orbit validates it and starts the download
without opening a provider window. If the provider needs verification, its page
opens instead, full screen: complete verification, move the pointer to
**Download** with the left stick and press ○. ✕ does nothing on this page, so a
stray press can't close it and lose the verification. To give up, press the PS
button, return to Orbit and cancel the download page there.
The browser interface and native app keep separate browser sessions.

Checked on a PS4 (firmware 12.00) on 13 November 2026: signing in through
Orbit Store Browser, Orbit showing **Signed in**, and signed-in downloads that
start without opening the page. Premium accounts were not tested.

## Install downloaded games in 1.5.0

Orbit provides **Downloads → Downloaded files → Install on PS4** for completed PKG/FPKG downloads and transfers. Select each package explicitly, including packages from bundles. The PS4 uses its configured installation storage and keeps the downloaded file by default. Optional deletion after a confirmed successful install is under **App settings → Storage & cleanup → Installed packages**. New compatible images and game folders automatically request a scan when ShadowMount’s local API is running. This is separate from installing or updating Orbit itself. See [installing downloaded games](package-installation.md) for steps, requirements and status recovery.

## Payload managers

Orbit saves its own runtime at `/data/orbit-store/orbit_store.elf`, and elfldr beside it, on startup. It does not add copies to LaunchPad or Homebrew Launcher until you choose **Add Orbit** in **App settings → Updates & startup → Payload managers**. Each choice is remembered independently. An existing copy from an older version is preserved but does not count as permission: choose **Allow updates to this copy** to let Orbit manage it.

**Stop syncing** leaves the existing copy in place but stops updating that manager's copy.

| Payload manager | After you opt in |
|---|---|
| GoldHEN LaunchPad (`/data/GoldHEN`, `/data/payloads`) | Copies Orbit's starter into `/data/payloads` as `orbit_store.elf` and keeps it current. Open it from LaunchPad's payload list. |
| Homebrew Launcher (`/data/homebrew`) | Adds and refreshes `OrbitStore/eboot.elf` (the same starter) and its icon. |

The starter is a small program, not the service: GoldHEN can't start a program of the service's size. It sends the saved service to elfldr on port 9021. When nothing answers there, it first gives the saved elfldr to GoldHEN's payload loader (9090) and waits up to 30 seconds for elfldr. If Orbit hasn't answered 40 seconds after it was sent, the starter sends it once more. Its messages are console notifications (in English): Orbit already running; Orbit's saved copy missing (run `orbit_store.elf` again); no loader reached (turn on GoldHEN's PayLoader Server under **GoldHEN → Servers Settings**, or start an ELF loader); Orbit didn't start (close other apps such as ezRemote).

Missing or invalid manager preferences never authorize a copy. If a setup fails after the choice is saved, **Retry setup** finishes it. Orbit does not create an absent manager's top-level folder.

GoldHEN AutoRun uses `/data/GoldHEN/payloads.ini`, section `[AutoRun]`, with Orbit’s `/user/data/payloads/orbit_store.elf` entry (the starter). Enabling it requires permission to keep the LaunchPad copy current. Disabling it removes only Orbit’s AutoRun entries; other payloads, sections and global GoldHEN settings are preserved. **Stop syncing** is separate from disabling AutoRun. Orbit never writes `autoload.txt` on PS4. AutoRun at boot still needs a console check after a restart.

## Updates and reinstall

**Update Orbit** is under native or browser **App settings → Updates & startup** (other layouts use **Settings**). It updates the detected native installation and the download service.

The app downloads only the package when the feed confirms it contains the latest service. Select **Close and reopen** afterward. When the updated app opens, it saves the queue, stops the older service and starts its bundled service through the compatible ELF loader on port 9021, resuming downloads interrupted by that restart. If an app was updated manually rather than through this flow, it offers **Update download service** when its bundled service is newer.

For browser/service-only updates, **Restart Orbit** sends the saved copy to the ELF loader on port 9021. When nothing answers there, it first gives the saved elfldr to GoldHEN's payload loader (9090). If neither works, Orbit keeps running and says so; then stop it and start it from the native app, LaunchPad or your ELF loader. Verify the running versions afterward; a saved update alone is not an active update.

The individual service controls below also cover older versions and manual updates.

From beta.3 onward, open **App settings → Updates & startup → Check for updates**. Orbit reads its fixed public release feed. Choose **Install update**, or **Reinstall release** when the latest version is already running. The console fetches the ELF, checks its SHA-256 and ELF header, and atomically replaces its saved copy and refreshes only the manager copies you opted to sync. A failed copy refresh is shown as an error and can be retried. No app-icon removal or second icon is required.

The panel distinguishes **Running**, **Saved for next start**, and **Latest release**. Saving a release does not replace the running process. Choose **Stop Orbit to restart**, confirm the pause, then start Orbit again: open the native app, run Orbit's LaunchPad or Homebrew Launcher entry, or send `/data/orbit-store/orbit_store.elf` to your ELF loader. Then reopen the Orbit icon. Downloads, paired-device tokens and source preferences are preserved; review and resume the paused queue. The stop action affects this Orbit process only. If you placed a copy manually and have not allowed Orbit to sync it, replace it yourself or use the saved path above; running an old copy starts that older version.

Don't put the downloaded `orbit_store.elf` into `/data/payloads` or the Homebrew Launcher folder yourself: the copy there is Orbit's starter, and LaunchPad can't run the full file. Send the full file to an ELF loader once; Orbit then keeps the starter, the saved service and elfldr current itself. Do not delete the home-screen icon or Orbit's data directory. Running another ELF while the old server is still active only saves the replacement for the next start.

Only an authenticated device can check, reinstall or stop Orbit. Production desktop previews cannot write console files. Update downloads use the fixed Orbit GitHub feed and release paths, an HTTPS host allowlist, bounded responses, SHA-256 and ELF validation. Repeated checks and downloads are throttled, and HTTP `Retry-After` is honoured. Opening the app in 0.4.2 or later schedules a background release check, with a shared six-hour cooldown while Orbit runs. A newer release shows a dismissible View update banner; failed checks do not interrupt browsing. No automatic installation, arbitrary URL uploads, downgrades or game downloads are part of this updater.

## Implementation

`build/orbit_runtime.elf` is the server plus first-run icon setup, stripped of debug information. `build/orbit_store.elf` is the same program carrying that runtime, the starter and elfldr. On start it saves them, sends the saved runtime to the ELF loader and exits, so the larger file never stays running (started from it, the service held about 180 MB instead of about 140 MB). When no loader answers it runs as the service itself. `build/orbit_starter.elf` (`launcher/starter.c`) is the starter payload managers run; it holds no service code. The pinned elfldr (a481740: v0.7 plus firmware 13.52 and 14.00) comes from `tools/bootstrap.py` (`.deps/elfldr/elfldr.elf`). `orbit_core.elf` excludes icon setup and start-up refresh for isolated diagnostics.

Startup acquires the instance lock and HTTP listener before changing app metadata. If Orbit is already running, a repeated load leaves the running server and its queue alone. A newer `orbit_store.elf` still replaces the saved runtime, so the new version starts next time; the notification explicitly asks the user to stop Orbit and run the saved payload. A failed bind reports the exact port and changes nothing.

The icon installer writes only `/user/app/ORBT00001/sce_sys/param.json` and `icon0.png`, plus its own installation records. It registers the title through AppInstUtil (`sceAppInstUtilAppInstallTitleDir`, falling back to `sceAppInstUtilAppInstallAll`): no system-volume remount and no `eboot.bin`. Later starts verify the installation without rewriting files. If the Orbit icon was manually deleted, the next Orbit start recreates its missing app directory and registers it again. The old ready record is cleared first so interrupted recovery can retry. Unknown title folders, changed files and symlinks are rejected. An interrupted owned setup can fill missing files and retry registration. Icon failure is reported while the server stays available over the LAN.

All manager copies and settings files are written as a temporary file followed by a rename, and never through a symlink. A lock file (`/data/orbit-store/autoboot.lock`) serialises the running server and a newly loaded payload.

Changing the icon's manifest or port in a future release requires an explicit, tested migration. The current installer preserves mismatched existing files rather than overwriting them.

## Console testing

Tested on a PS4 on firmware 12.00 with GoldHEN on 12–13 November 2026, including
GoldHEN AutoRun at boot, automatic shutdown, a USB drive and package installation.

Known issue: right after the PS4 turns on, the first program a freshly started
elfldr is given sometimes fails; later ones work. The TV app and the starter send
Orbit once more when it doesn't answer, so the first start can take up to about
40 seconds longer. Historical PS5 runs are not PS4 test evidence.
