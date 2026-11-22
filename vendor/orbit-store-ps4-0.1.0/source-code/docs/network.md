# HTTP port selection

Orbit uses **TCP 34177** for its web interface and API. The payload-loader port is separate: the send helper still defaults to TCP 9021.

Checked on 2026-10-03. Port 6971 was rejected after finding an existing PS4 payload using it. The survey used project documentation:

| Project/service | Documented ports | Source |
|---|---|---|
| APR Emu Updater web UI | 6971 | [Project README](https://github.com/tsuramatsu1/apr-emu-updater) |
| websrv / ELF loader | 8080 / 9021 | [Project README](https://github.com/ps5-payload-dev/websrv/blob/master/README.md) |
| ftpsrv | 2121 | [Project README](https://github.com/ps5-payload-dev/ftpsrv/blob/master/README.md) |
| etaHEN services | FTP 1337, RPC 8000, log 9081, loader 9021, IPC 9028, DPI 9090 and 12800 | [Project README](https://github.com/etaHEN/etaHEN/blob/main/README.md) |
| PS4 Web File Manager | 8888, with higher fallback ports | [Project README](https://github.com/owendswang/ps5-web-file-manager/blob/main/README.md) |
| PS4 File Explorer | 5905 | [Project README](https://github.com/juma-sayeh/PS5-File-Explorer) |
| Payload Manager X | 8084 default, 8184 alternative | [Maintainer's integration documentation](https://github.com/bsk193/ps5-unified-autoloader-x) |
| singleDPI | 9090, 12800, 9091 | [Project README](https://github.com/MaxMilu/ps5-direct-package-installer/blob/main/README_EN.md) |

The official [IANA CSV registry](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.csv) lists 33891–34248 as unassigned, which includes 34177. Searches found no documented PS4 homebrew use of 34177. This is a surveyed choice, not an exclusive allocation or a guarantee against future/custom software using it.

## Consistency and conflict handling

`config/network.json` is the shared setting. The build derives the C port constant and launcher URL; the development proxy reads it directly. Console startup reports a failed listener with the exact port before making launcher changes. It does not terminate another service or silently move to a port that would leave the installed icon pointing elsewhere.

For a development build with another port, edit the shared setting and regenerate/rebuild. Existing deployed icons require a tested metadata migration before switching their port; the current installer rejects mismatches. Desktop test processes can use `--port` without installing an icon.

## Listener recovery

Orbit owns the HTTP polling thread and uses libmicrohttpd in external polling mode, with a 250 ms maximum socket wait. Every two monotonic seconds it checks the listening socket and makes a nonblocking loopback TCP handshake. Three consecutive refused handshakes, an invalid/non-listening socket or a fatal polling error cause that thread to dispose of the old HTTP daemon and bind the same configured port again. A full backlog, handshake timeout or resource exhaustion alone does not trigger recovery.

Rebinding waits 500 ms initially, doubling to an eight-second maximum while unavailable. Stop interrupts that wait. Initial startup still fails on a port conflict; recovery never stops another service or changes ports. Listener recovery keeps the process, instance lock, pairing, queue, catalogue and download workers. Clients must reconnect; requests whose responses were lost must not be automatically replayed.

Diagnostics record `listener-recovery` with `started` and `ok` stages. This handles a surviving process losing its listener; it is not a promise of downloads while the console sleeps or recovery from a terminated/frozen process. Local fault tests cover lost listeners, polling failure, rebind backoff and preservation of transfers. Physical PS4 rest/wake and active-download behavior remain unverified; console checks are deferred.
