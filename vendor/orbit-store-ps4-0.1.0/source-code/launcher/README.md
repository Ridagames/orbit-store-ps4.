# PS4 Internet Browser shortcut

The payload embeds `build/orbit-browser.pkg`, a real PS4 package for project title
`ORBT00001`. Its eboot opens the Internet Browser at Orbit's configured loopback
address. It opens the running service; it does not load the service itself.

`launcher/tools/build.sh` builds the eboot and SFO, packages them with OpenOrbis,
and validates the result. `config/network.json` supplies the shared port.

The installer stages `browser-shortcut.pkg` under `/data/orbit-store/`. It saves
submission intent and the BGFT task in `browser-shortcut-task-v2` before following
progress. Interrupted or uncertain submissions are retained and not replayed.
It queries installed-title status separately. It does not scan or register
arbitrary application directories or write a PS5 `param.json` tile.

`GET /api/v1/system` exposes shortcut status, the diagnostic error and registration
method (`bgft-storage`). The transaction and platform-adapter host tests cover
submission, interruption, existing titles, failures and duplicate prevention.
The package structure is locally validated. Actual installation and browser
launch on PS4 remain deferred; PS5 registration tests do not validate this path.
