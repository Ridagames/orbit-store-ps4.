# TorBox downloads

TorBox is an optional delivery service for supported catalogue links. Your existing source choices still apply. Orbit sends the selected link to your connected TorBox account, waits for its file to be ready, then saves that file to the PS5 drive you chose.

## Connect an account

Open the browser version of Orbit, or pair a phone, and go to **App settings → Sources & accounts → TorBox**. Enter the API key from your [TorBox settings](https://torbox.app/settings) and choose **Connect TorBox**. Once connected, both the native app and browser version offer **Download using → TorBox** on a game's download page.

TorBox's current host availability, plan restrictions, quotas and file-size limits apply. An account connection does not guarantee that every catalogue option is supported. Choose the original source to keep using Orbit's existing direct or PS5 browser flow. Orbit never switches a TorBox job back to the original source automatically.

Since 0.0.3, Orbit remembers your last **Direct** or **TorBox** choice. **Always use TorBox**, beneath the account controls, overrides that remembered choice for new downloads. It applies only to options TorBox can take now: options it can't, such as AkiraBox or a host TorBox has paused, use their original source. It still requires a connected account. Turning it on does not change jobs already in the queue. Orbit Zero keeps its own preference with its own account settings.

Orbit checks TorBox availability for Archive.org, Vikingfile and DataNodes. For a supported DataNodes option, choosing TorBox prepares the file from its permanent page without opening the provider browser. 1fichier is deferred from 1.3.5. AkiraBox continues to use its browser download flow.

Orbit does not reject a file solely because it exceeds TorBox's advertised per-file size. It checks host availability and remaining account allowances, then lets TorBox accept or reject the request; any returned size or account error appears in Downloads. A larger torrent allowance does not imply a larger web-download allowance. Choose **Refresh TorBox** in **Sources & accounts** after an account or provider change.

The key is saved privately by Orbit on the PS5. It is not returned in API status, saved in browser storage, or included in catalogue data or diagnostic reports. Paired devices can manage the connection. Disconnecting removes Orbit's saved key and pauses unfinished TorBox jobs; it keeps local partial files and does not remove anything from the remote TorBox account.

## Orbit Zero on your computer

Open **Settings → Sources & accounts → TorBox** in any Zero interface. Enter your TorBox API key on the computer and connect the account. Choose **Download using → TorBox** on the game's download page. This explicit choice downloads on the computer and transfers to the selected PS5 drive, even if your usual preference is to download directly on the console.

The computer and PS4 use separate TorBox connections. Zero encrypts its saved API key with the operating system credential store; it never sends that key to the PS4 or a download provider. Only the desktop session can change these account settings. Since **1.9.2**, Zero keeps TorBox's host availability up to date by itself and checks it again when a TorBox download starts, so you don't need to choose **Refresh TorBox** first.

Pausing, resuming and switching accounts preserve computer partials. Resume renews the temporary file link; it does not start another TorBox item. An existing job remains tied to its original TorBox account. Completing or cancelling a local job does not delete remote TorBox files.

## Preparation and recovery

Downloads shows preparation before file transfer begins. If TorBox makes no progress on the file for five minutes, Orbit stops and says the source may be offline or the file removed; choose another download option. A file TorBox has queued or paused in your account keeps waiting. Orbit accepts only one file with the selected filename and exact size; it rejects unexpected files, ZIP collections and files marked infected. Normal transfer validation and catalogue checksums, when supplied, still apply.

Pause, resume, retry and cancel use the regular download queue. Resume asks TorBox for a fresh temporary download address while retaining the original account, remote item and file identity. Temporary addresses are never written to the queue's saved state. A different account cannot resume an existing job.

Orbit respects provider cooldowns and spaces new submissions at least 61 seconds apart, including across restarts. If a submission's response is lost, Orbit checks the account before retrying. It does not blindly create a second remote download. A confirmed rejection may be retried after its cooldown. A lost submission that TorBox's list (read past its cache) still doesn't show two minutes after it was sent never arrived, so Retry sends it again; until then the job waits on "Preparing with TorBox". The send time is saved with the job (`debridSubmittedAt`); a job saved without one, by an older version, is sent again on its next retry.

Orbit Zero can request a fresh temporary TorBox link after a connection failure or an expired link. It preserves the partial file and verifies its identity before resuming. A fresh link does not guarantee a different CDN server. If the TorBox server remains unreachable, Downloads explains how to retry or change the CDN in TorBox settings. It does not replace a raw-file partial with a ZIP download.

## Compatibility

Raw-file TorBox jobs use state schema 5. Archive extraction jobs use schema 6. Direct-only raw-file queues use schema 4. Existing schema-3 TorBox queues are backed up and migrated while retaining their two-range checkpoints. Direct PS5 TorBox downloads use at most eight connections per game, within the shared 64-connection budget, and buffered disk writes. Zero 1.8.4 limits computer-side TorBox downloads to four connections, uses 32 MiB parallel ranges, and uses a continuous request when one connection is selected. Older Orbit builds refuse unsupported state instead of trying to resume TorBox bytes from the original provider. Use a build that supports the jobs remaining in history.

This implementation currently supports TorBox web downloads. It does not add torrents, purchase plans, manage remote files, or connect other Debrid services.

## Development checks

`make test-debrid` checks the production download-address rules against Hyperdrive hostnames from TorBox's public speed-test API, including rejection of unrelated domains, credentials and non-HTTPS URLs. It also runs isolated mock-provider tests, including exact-file checks, eight-connection transfer integrity, durable queue migration, pause/restart, temporary-link renewal, account isolation, cooldowns, uncertain submissions and credential redaction. DataNodes and 1fichier cases check permanent-page submission, reuse of existing account items, host limits and restart recovery. Zero tests also cover encrypted credential storage, desktop-only account setup, signed-URL redaction and computer download/LAN transfer resume integrity. Native controller tests cover the explicit delivery choice, DataNodes and 1fichier without browser verification, and account disconnection. These local fixtures do not establish live TorBox host availability or replace a console/account acceptance test.

Provider contract: [TorBox API documentation](https://api-docs.torbox.app/) and [OpenAPI schema](https://api.torbox.app/openapi.json).
