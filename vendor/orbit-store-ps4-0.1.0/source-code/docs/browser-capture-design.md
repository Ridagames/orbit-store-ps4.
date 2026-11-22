# Provider pages on PS4

Vikingfile and AkiraBox downloads need the provider's own page: Cloudflare's
check, sometimes a "Verify you are human" tick, then the provider's Download
button. Orbit never reads another process's memory to find the link. The old
memory scan crashed the PS4 shell and has been removed.

## Flow

1. The TV app asks the service to start a session for the selected option. The
   service reports `nativeDialogAvailable: false` on PS4, so the TV app does not
   open a browser window of its own. Sony's window refuses mini apps, and the TV
   app must be one for its graphics.
2. The service starts **Orbit Store Browser** (ORBT00001, a normal application)
   with `sceLncUtilLaunchApp`, and exposes to it, until it reports the window
   open, the option's public page (`helperUrl`), the text to watch for
   (`helperWatch`) and which of Sony's windows to use (`helperWindow`:
   `custom` or `normal`).
3. The helper gets the console's local session from `GET /api/v1/session`, reads
   `GET /api/v1/browser`, opens the page in Sony's web browser window with a
   callback (type 2: any address containing the text), and posts
   `{"action":"helper-opened","id":…}`.
4. The person completes Cloudflare's check if asked and selects Download (when
   signed in to Vikingfile, its page can go to the file by itself). The
   window closes itself on the first address containing the text and returns
   it. The helper posts `{"action":"captured","id":…,"url":…}`. Closing the
   window without a match posts `cancel`.
5. The service accepts the address only if it is the selected file's own route
   (`provider_capture_url`), checks the response as for any captured link and
   adds it to Downloads, then starts the TV app again to bring it to the front.

Watched text: the longest run of letters, digits and `. _ -` in the option's
file name. Those characters are never encoded, so the run appears as-is in
every link seen on 13 November 2026: Vikingfile's `vikingfile.com/d/<id>/<name>`,
its file servers' `/download/<id>/<name>` routes (`ia.`, `ty.` and others) and
its signed OVH storage links (the name is inside `response-content-disposition`),
and AkiraBox's download routes on either `akirabox.com` or `akirabox.to` (its page
moves between them). A run shorter than 6 characters, or one inside the page's
own address, falls back to `vikingfile.com/download/` or `<page host>/download/`.
Other sources have no PS4 page flow.

A signed-in Vikingfile account usually skips the page: the service fetches the
file page with the account's cookies and takes the link from its redirect.

Vikingfile sign-in uses the same window without a watch; Orbit then reads the
window's cookie jar at `/user/home/<user>/webkit/webbrowser_dialog/NKCookie.jar.db`.

Started from its home-screen tile, Orbit Store Browser opens Orbit in the
Internet Browser as before. The service replaces an installed helper older than
the one it carries (version 01.04). While the window is open the helper asks
the service once a second whether its session is still running; when it was
cancelled in Orbit or timed out, the helper closes the window.

Vikingfile pages open in Sony's custom window (mode 2): Cross does nothing
there, so a stray press can't close the page and lose Cloudflare's wait. The
left stick moves the pointer, Circle follows the link under it, and there is no
Close or Back; to give up, press PS and cancel in Orbit, which closes the window
within a second. AkiraBox pages and the Vikingfile sign-in page use the normal
window (mode 1), where the D-pad jumps between links and Cross closes the page:
AkiraBox's Download can open a blank ad page that only Close can leave, and the
sign-in page is closed with Cross when done. The owner chose this split on
13 November 2026. An earlier finding that the custom window "never fires for
downloads" was wrong: Orbit was watching for `/download/` while Vikingfile's
button had moved to `/d/`.
Covered on the host by `tests/launcher_browser.py` and `tests/providers_driver.c`.

See also the [API contract](api.md).
