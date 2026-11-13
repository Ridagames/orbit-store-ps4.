# When something needs attention

**For Orbit Store 0.0.1 for PS4.** Settings paths below use the default Store layout. In Crossbar, Cinema and Panorama, the main menu is called **Settings**; see [Find settings after switching](appearance.md#find-settings-after-switching).

## Opening and updating Orbit

### I see two Orbit icons

That’s normal if you use both versions. **Orbit Store** on the Games row is the TV app from `orbit_store_ps4.pkg`. **Orbit Store Browser** is added when `orbit_store.elf` runs and opens Orbit in the PS4’s Internet Browser. Both use the same downloads, favourites and service settings. Appearance is saved separately for each.

### The Orbit Store Browser icon doesn’t open

The icon opens a running Orbit; it can’t start a stopped one. Start Orbit from the TV app, LaunchPad or your ELF loader on port 9021, then open the icon again. If the icon was deleted, starting Orbit adds it back.

From another device, use `http://<ps4-ip>:34177/` on the same local network. Keep that port private to your home network.

### The TV app says Orbit didn’t start

- **Turn on GoldHEN’s payload loader:** **GoldHEN → Servers Settings → Enable PayLoader Server**. The app needs it to start its ELF loader.
- **Close other apps first,** such as ezRemote. Programs started this way share memory with open apps, and with another app open the PS4 may not have enough left to start Orbit.
- **Right after the PS4 turns on,** Orbit sometimes needs a second try. The app tries again by itself, so the first start can take up to about 40 seconds longer. If it still shows **Orbit didn’t start**, choose **Try again**.

The app never stops another running payload to make room.

### An update says Orbit is already running

After an in-app update, select **Close and reopen** when offered, then open Orbit from the Games row. The updated app saves the queue, stops the older service and starts its new one.

If you replaced files yourself, open **App settings → Updates & startup** and compare **Running** with **Saved for next start**. Use **Restart Orbit**, or choose **Stop Orbit to restart** and start `/data/orbit-store/orbit_store.elf` again through the TV app, LaunchPad or your ELF loader. Pairing and the queue stay saved.

If a TV app update still opens the previous version, close the app with **PS button → Close Application** and open it again. If it still shows the old version, restart the console when convenient and run GoldHEN again.

### Automatic shutdown hasn’t started

Check **App settings → Downloads → Automatic PS4 shutdown**. **Wait for** doesn’t ignore other unfinished work: paused or failed downloads, pending installation checks, and active file operations or updates hold off shutdown. A failed installation cancels the plan. When all work is finished, the countdown lasts 60 seconds. See [Automatic shutdown](shutdown.md).

### The selected drive is too small

Check free space and queued work in the download options. Archives may need room for the archive and the unpacked files together, and installing needs extra console storage. Choose another drive before starting, or free up space and resume.

## Finding and downloading games

### A game or image is missing

Check the enabled sources and clear Browse’s filters. Then try **App settings → Game catalogue → Refresh catalogue**.

Artwork comes from other websites. Orbit tries a fallback image when the first one fails. Network or DNS limits, or an image host that’s down, can still leave artwork missing. Some dates and other details are still missing; corrections arrive with catalogue updates. Games without a date come after dated games when sorting by release date.

### Catalogue refresh reports a network error or HTTP 0

Orbit didn’t get a reply from the catalogue host. It keeps the catalogue it already has, so you can keep browsing; new entries appear after a refresh succeeds. Your queue and files are kept. Try **Refresh catalogue** again later, after any cooldown shown. Downloads from reachable providers carry on.

If it keeps happening, check the PS4’s internet access and save a diagnostic report. A GitHub webpage that loads doesn’t prove the catalogue file host is reachable.

### A download fails or is slow

- **Provider returned a page:** retry from Downloads. For Vikingfile and AkiraBox, repeat the provider step. Orbit never saves an error or verification page as your file.
- **Provider throttling:** let the retry wait finish. Orbit follows the provider’s requested wait.
- **Drive disconnected:** reconnect the same drive. Orbit recognises it after it’s mounted again, using its saved marker or the download’s own partial file. Keep those files and select **Resume**. Orbit never switches to a different drive by itself.
- **Not enough space:** free space on the chosen drive, counting unfinished downloads, then retry.
- **Source changed:** keep the partial until you decide whether to remove it and start again. Orbit won’t add a different file onto it.
- **Partial file deleted or empty:** choose **Retry**. Orbit starts that download again from the beginning on the same drive.

More connections help only when the host allows them. Provider speed limits, your network and the drive still apply.

### TorBox stops or can’t reach its download server

TorBox uses at most eight connections per game and Orbit respects its cooldowns. If the message says the original source no longer has the file, choose another download option; a fresh link can’t bring back a removed file. If failures continue, save a diagnostic report and include the Orbit version, source and where it stopped. Don’t share your TorBox key or download links.

### Vikingfile doesn’t enter Downloads

Turn on Vikingfile under **Sources & accounts** and [sign in](vikingfile.md) in the version you use; the TV app and the browser keep separate sessions. Select the option and drive, then **Download to PS4**. If the Vikingfile page opens, complete the verification, move the pointer to **Download** with the left stick and press ○. The page appears on the PS4 even if you started on your phone.

If the page times out or changes, go back to Orbit and try that option again. Cancel an open provider page before starting another. A file that doesn’t match the selected option isn’t queued.

### AkiraBox doesn’t enter Downloads

Open the download page from Orbit, complete any verification, then select **Download**. AkiraBox may show an ad instead; close it and try again, or choose another source for the game. If the page times out, go back to Orbit and open it again. For a paused or failed download, use **Refresh download link**.

### Archive.org reports HTTP 401, 403 or 404

Some files need an Archive.org account. Follow [Archive.org sign-in](downloads.md#archiveorg-sign-in), then retry. If the page says the item was removed or is unavailable, signing in won’t bring it back; choose another source. An HTTP code alone doesn’t show which case applies.

### Archive needs a password

Orbit tries the [known source passwords](downloads.md#zip-rar-and-7z-downloads) by itself. If the archive needs another one, choose **Enter archive password**, type the source’s password exactly, then **Retry extraction**. Keep the downloaded archive and enough free space for its unpacked files. A damaged or unsupported archive can still fail with the right password; other failures show the extraction error instead.

### A USB drive is missing

Reconnect the drive and reopen **Library → Files**; drive information refreshes by itself. A drive appears even if its `homebrew` folder doesn’t exist yet. Orbit creates the folder when needed and checks it can write there when a download starts or resumes. If it can’t, the download stops and the partial file is kept.

If the drive still doesn’t appear, include your firmware, Orbit version and a fresh diagnostic report in your bug report.

## Library, installing and DLC

### A finished download isn’t in Library

Library shows installed games. A finished download is a saved file until you install it: open the download and choose **Install on PS4**. See [installing games](install-games.md).

To find the file, check **Downloads → Finished → Location**, then look at that path in **Library → Files**. Unpacked downloads may be a folder with several files. The recorded path stays in history even if the file is later moved or deleted.

### A downloaded PKG or FPKG game doesn’t appear

Open the finished download’s **Downloaded files**, select the package and choose **Install on PS4**. Check installation status separately from download progress. If **Check installation** appears after a restart or lost response, use it and check the PS4 home screen before trying again. Orbit keeps the downloaded file if installation fails. See [installation help](install-games.md#if-installation-needs-attention).

Installing through Orbit doesn’t change compatibility: the game still needs a suitable firmware and GoldHEN.

If you report a problem, include the title ID (starting with `CUSA`), source, format, drive, Orbit version, any installer error, and whether it failed while downloading or installing. Don’t share download links.

### Add-ons are missing or don’t work

If the Add-ons page is empty, turn on the relevant sources, clear its filters and refresh the catalogue. A page opened from a game’s details shows only that game’s extras.

If a downloaded DLC doesn’t appear in the game, check that its package was installed, then compare the required base-game edition, title ID, region and version. A matching title ID alone doesn’t confirm compatibility. Activation packages need their content already in the game; bundles need each package installed. See [the DLC guide](add-ons.md).

## Interface and settings

### I can’t find a setting after changing interface

Crossbar, Cinema and Panorama call the main menu **Settings**, with the same sections inside. See the [settings map](appearance.md#find-settings-after-switching).

### My interface choice switches back

Select **Keep** before the 15-second countdown ends. **Switch back**, or not answering in time, restores the previous interface. Each app or browser saves its own choice. Clearing browser data, a private window or a different IP address or hostname may reset it. See [where your choice is saved](appearance.md#where-your-choice-is-saved).

## Files

- **A Files action says storage is busy:** wait for the current download step, installation or file operation to finish. Opening folders still works.
- **A copied source changed:** keep the original and retry after whatever was changing it has stopped. Orbit didn’t keep the incomplete copy.
- **Circle leaves a folder:** it goes to the parent folder, then all drives, then Library.
- **Vikingfile says sign-in couldn’t be confirmed:** use **Check sign-in** or open its account page. A network error or provider check doesn’t mean you’re signed out.

## Reporting a problem

### Share a diagnostic report

Open **App settings → Diagnostics & about → View diagnostics**. In the browser, choose **Copy diagnostic report**; if copying isn’t available, Orbit shows the report text to select. Reports leave out pairing codes, access tokens, download links, game names and paths. Nothing is sent by itself.

You can also save a report as a file with **Save diagnostic report** in the TV app or the browser, then choose a PS4 drive. Attach the file yourself when reporting a problem.

![Diagnostics & about in the PS4 TV app with View diagnostics and Save diagnostic report](../assets/0.0.1/native-diagnostics.jpg)

*You choose whether to save and share a report.*

When reporting a problem in [Issues](https://github.com/saawant12/orbit-store-ps4/issues), include the Orbit version, PS4 firmware, GoldHEN version, what you did and the error shown. For a display or navigation problem, include the interface (Store, Crossbar, Cinema or Panorama) and whether you used a controller, keyboard, mouse or touch. Remove private details before posting.

[Getting started](getting-started.md) · [Downloads](downloads.md) · [Library](library.md) · [Appearance](appearance.md)
