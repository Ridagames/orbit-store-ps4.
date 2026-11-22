# Third-party notices

Orbit Store is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See [LICENSE](LICENSE). It is distributed without any warranty.

A PS4 release must include a complete source and licence bundle for both `orbit_store.elf` and `orbit_store_ps4.pkg`. It must contain the application source from the exact build commit, the corresponding sources of the copyleft components below, other linked library sources, and rebuild instructions. See [SOURCE-BUNDLE.md](SOURCE-BUNDLE.md) and the generated `SOURCES.json` for the archive inventory and pinned hashes.

## Components in orbit_store.elf

Archive extraction also links 7-Zip 26.03 for ZIP and 7z archives, and for RAR archives whose window is too big for UnRAR to keep in a PS4's memory. Its sources are pinned in `tools/release-sources.json`; its licence is in [licenses/7zip.txt](licenses/7zip.txt). Most files are LGPL-2.1-or-later, with BSD and public-domain components. The RAR decoder also carries the unRAR restriction: this code may not be used to develop a RAR (WinRAR) compatible archiver.

RAR archives are extracted with RARLAB's UnRAR 7.23 (`unrarsrc-7.2.7.tar.gz`), pinned in `tools/release-sources.json`; its licence is in [licenses/unrar.txt](licenses/unrar.txt). `tools/patch-unrar.py` makes Orbit's changes: UnRAR reads the archive Orbit has already opened, unpacks with the number of threads Orbit chooses, gives its threads a 1 MB stack, and uses the PS4's fixed list of CPU features. As the UnRAR licence requires: UnRAR source code may be used in any software to handle RAR archives without limitations free of charge, but cannot be used to develop RAR (WinRAR) compatible archiver and to re-create RAR compression algorithm, which is proprietary. Distribution of modified UnRAR source code in separate form or as a part of other software is permitted, provided that full text of this paragraph, starting from "UnRAR source code" words, is included in license, or in documentation if license is not available, and in source code comments of resulting package.

| Component | Version | Licence | Licence text | Source |
|---|---|---|---|---|
| PS4 payload SDK startup, compatibility code and imports | v0.9, `1a85cb5a8ea12dc2dcfbcc878700d0d097245de7`; startup code (`crt1.o`) rebuilt from `573b4a0d0d5fa8695cfeee5f5e2f0447c550035d` for firmware 13.52 and 14.00 | GPL-3.0-or-later and per-file BSD terms | [GPL](licenses/GPL-3.0.txt); per-file notices in the source archive | [ps4-payload-dev/sdk](https://github.com/ps4-payload-dev/sdk/tree/573b4a0d0d5fa8695cfeee5f5e2f0447c550035d); both source archives in the source bundle |
| GNU libmicrohttpd | 1.0.1 | LGPL-2.1-or-later | [licenses/LGPL-2.1.txt](licenses/LGPL-2.1.txt) | `libmicrohttpd-1.0.1.tar.gz` in each release; [gnu.org](https://www.gnu.org/software/libmicrohttpd/) |
| curl (libcurl) | 8.18.0 | curl licence | [licenses/curl.txt](licenses/curl.txt) | `curl-8.18.0.tar.xz` in the source bundle; [curl.se](https://curl.se/download/curl-8.18.0.tar.xz) |
| OpenSSL | 3.5.2 | Apache-2.0 | [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) | `openssl-3.5.2.tar.gz` in the source bundle; [openssl.org](https://github.com/openssl/openssl/releases/tag/openssl-3.5.2) |
| SQLite | 3.54.1 | Public domain | [licenses/SQLite.txt](licenses/SQLite.txt) | `sqlite-amalgamation-3540100.zip` in the source bundle; [sqlite.org](https://www.sqlite.org/) |
| cJSON | 1.7.19 | MIT | [licenses/cJSON-MIT.txt](licenses/cJSON-MIT.txt) | Exact files in `build-inputs/cjson/`; [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON/tree/v1.7.19) |
| libarchive (ZIP reader) | 3.8.1 | BSD-2-Clause | [licenses/libarchive-BSD-2-Clause.txt](licenses/libarchive-BSD-2-Clause.txt) | `libarchive-3.8.1.tar.xz` in the source bundle; [libarchive.org](https://www.libarchive.org/) |
| liblzma (XZ Utils) | 5.8.1 | 0BSD | [licenses/liblzma-0BSD.txt](licenses/liblzma-0BSD.txt) | `xz-5.8.1.tar.gz` in the source bundle; [tukaani.org](https://tukaani.org/xz/) |
| bzip2 (libbz2) | 1.0.8 | bzip2 licence | [licenses/bzip2.txt](licenses/bzip2.txt) | `bzip2-1.0.8.tar.gz` in the source bundle; [sourceware.org](https://sourceware.org/bzip2/) |
| zlib | 1.3.2 | zlib licence | [licenses/zlib.txt](licenses/zlib.txt) | `zlib-1.3.2.tar.gz` in the source bundle; [zlib.net](https://zlib.net/) |
| React and React DOM | 19.3.0 | MIT | [licenses/React-MIT.txt](licenses/React-MIT.txt) | [facebook/react](https://github.com/facebook/react) |
| scheduler | 0.28.0 | MIT | [licenses/scheduler-MIT.txt](licenses/scheduler-MIT.txt) | [facebook/react](https://github.com/facebook/react) |
| Vite runtime helper (module preload) | 6.4.6 | MIT | [licenses/Vite-MIT.txt](licenses/Vite-MIT.txt) | [vitejs/vite](https://github.com/vitejs/vite) |
| Inter Tight font (Cinema and Panorama interfaces), variable WOFF2 subsets from Fontsource | 5.3.0 | SIL Open Font License 1.1 | [licenses/InterTight-OFL.txt](licenses/InterTight-OFL.txt) | [rsms/inter-tight](https://github.com/rsms/inter-tight) |
| Sora font (Crossbar interface), variable WOFF2 subsets from Fontsource | 5.3.0 | SIL Open Font License 1.1 | [licenses/Sora-OFL.txt](licenses/Sora-OFL.txt) | [sora-xor/sora-font](https://github.com/sora-xor/sora-font) |
| Mozilla CA certificate bundle, via curl's CA Extract | bundle fetched by `tools/bootstrap.py` | MPL-2.0 | [licenses/MPL-2.0.txt](licenses/MPL-2.0.txt) | Embedded unmodified; [curl.se/docs/caextract.html](https://curl.se/docs/caextract.html) |
| elfldr ELF loader, built unmodified from upstream source; Orbit gives it to GoldHEN's payload loader when no ELF loader runs | `a4817404f5cb2d5c177b05cd5e4548b74e6c2725` (v0.7 plus firmware 13.52 and 14.00) | GPL-3.0-or-later (Copyright John Törnblom) | [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) | Source `elfldr-source-a481740.tar.gz` in the source bundle, built by `tools/Dockerfile.sdk.ps4`; [ps4-payload-dev/elfldr](https://github.com/ps4-payload-dev/elfldr/tree/a4817404f5cb2d5c177b05cd5e4548b74e6c2725) |

libmicrohttpd is linked statically. Its complete source and Orbit's complete source are both provided, so you can rebuild `orbit_store.elf` with a modified libmicrohttpd using the steps in [BUILDING.md](BUILDING.md).

## Components in the TV app (`app/`, ORBT00002)

The native TV app is a separate program with its own build (`app/README.md`). It is GPL-3.0-or-later.

| Component | Version | Licence | Licence text | Source |
|---|---|---|---|---|
| ps5-homebrew-ui kit (shared renderer, widgets, input and sound; PS4 adaptations in this repository), stored in `app/` | commit `4bd942579dd981b3df9c740438489ca6a614ddc0` | GPL-3.0-or-later | [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) | In this repository; [blackbearreloaded/ps5-homebrew-ui](https://github.com/blackbearreloaded/ps5-homebrew-ui) |
| libwebp (WebP decoding) | 1.4.0 | BSD-3-Clause and patent grant | [licence](licenses/libwebp-BSD-3-Clause.txt), [patents](licenses/libwebp-PATENTS.txt) | `libwebp-1.4.0.tar.gz`; build recipe in `app/tools/setup-webp-dependencies.sh` |
| stb_image, stb_vorbis | stb commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | Public domain or MIT | `app/src/third_party/stb/LICENSE` | [nothings/stb](https://github.com/nothings/stb) |
| Inter typeface | 4.1 | SIL Open Font License 1.1 | `app/third_party/fonts/Inter-LICENSE.txt` | [rsms/inter](https://github.com/rsms/inter/releases/tag/v4.1) |
| Inter Tight and Sora typefaces (Crossbar, Cinema and Panorama), static instances baked from google/fonts | google/fonts 6692879 | SIL Open Font License 1.1 | `app/third_party/fonts/InterTight-LICENSE.txt`, `app/third_party/fonts/Sora-LICENSE.txt` | [google/fonts](https://github.com/google/fonts) |
| Noto Sans CJK JP, SC, TC and KR (Japanese, Chinese and Korean) | 2.004 | SIL Open Font License 1.1 | [licenses/NotoSansCJK-OFL.txt](licenses/NotoSansCJK-OFL.txt) | Original fonts in `app/third_party/fonts/`; [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) |
| Noto Sans Arabic | 2.013 | SIL Open Font License 1.1 | [licenses/NotoSansArabic-OFL.txt](licenses/NotoSansArabic-OFL.txt) | Original font in `app/third_party/fonts/`; [notofonts/arabic](https://github.com/notofonts/arabic) |
| Interface sound effects (glass set) | from the kit above | GPL-3.0-or-later (BlackBearReloaded) | [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) | In this repository |
| elfldr ELF loader, packaged as `orbit/elfldr.elf`; the app gives it to GoldHEN's payload loader when no ELF loader runs | `a4817404f5cb2d5c177b05cd5e4548b74e6c2725` (v0.7 plus firmware 13.52 and 14.00) | GPL-3.0-or-later (Copyright John Törnblom) | [licenses/GPL-3.0.txt](licenses/GPL-3.0.txt) | As in the payload table above |

The app's package also carries Orbit's payload, `orbit_store.elf`, so it includes every component listed above for the payload.

The browser shortcut (`ORBT00001`) is built with the same OpenOrbis runtime as
native Orbit (`ORBT00002`) and is embedded in the payload. Sony system modules
such as Piglet are imported from the console; proprietary firmware modules are
not included in the source bundle.

## PS4 runtime sources

The payload links libc++, libc++abi and libunwind from its SDK build image.
SDK v0.9's `libcxx.sh` specifies **LLVM 15.0.7**. All three libraries match the
hash-pinned SDK ZIP; its CI invokes that recipe. The LLVM sources are included.

The native app and browser shortcut use these recovered or rebuilt components:

| Component | Exact source | Notices |
|---|---|---|
| OpenOrbis headers and linker script | `6929bafac0b565692ed61e66032a79185ee9cf65` | [GPL-3.0](licenses/OpenOrbis-GPL-3.0.txt), retained per-file terms in source |
| Native libc++, libc++abi, libunwind and compiler builtins | LLVM fork `83e2569f0c062d980ae142d8dc583f6b8733be35`, official release v1.2 | [libc++](licenses/openorbis-libcxx-LICENSE.txt), [libc++abi](licenses/openorbis-libcxxabi-LICENSE.txt), [libunwind](licenses/openorbis-libunwind-LICENSE.txt), [compiler-rt](licenses/openorbis-compiler-rt-LICENSE.txt): Apache-2.0 with LLVM exceptions and retained legacy terms |
| Native musl and CRT | `779f95174b44d39e6a8a788b36289cf4768944a9`, rebuilt without source patches | [COPYRIGHT](licenses/openorbis-musl-COPYRIGHT.txt): MIT and per-file terms |
| Native import libraries | Generated by Orbit's recorded recipe from `app/tooling/orbis-imports.json` | Orbit GPL-3.0-or-later; symbol names describe console interfaces, with no firmware implementation |

All five recovered LLVM archives match the old SDK bytes. The old musl and stub
revisions could not be proved; the active build replaces them with the pinned
rebuilds above. Complete source archives, original notices and build recipes are
included. See [runtime recovery](docs/openorbis-runtime.md) and
`tools/release-provenance.json`. This resolves runtime source identification;
it does not settle the separate RAR distribution assessment.

## Browser compatibility runtime

The legacy bundle also includes these MIT-licensed components from the lockfile:

| Component | Version | Notice |
|---|---|---|
| core-js | 3.51.0 | [MIT](licenses/core-js-MIT.txt) |
| regenerator-runtime | 0.14.1 | [MIT](licenses/regenerator-runtime-MIT.txt) |
| whatwg-fetch | 3.6.20 | [MIT](licenses/whatwg-fetch-MIT.txt) |
| abortcontroller-polyfill | 1.7.8 | [MIT](licenses/abortcontroller-polyfill-MIT.txt) |
| SystemJS | 6.15.1 | [MIT](licenses/systemjs-MIT.txt) |

## Distribution status

The 7-Zip RAR decoder remains enabled. Its additional unRAR restriction and
compatibility with the combined GPL program remain unresolved. Providing source
and notices does not by itself settle that question. As in Orbit for PS5, the
decoder is kept and its original terms, matching sources and build recipes are
provided. The assessment remains deferred; no clearance is claimed.

The source preparer verifies every dependency hash and distinguishes source
archives from binary build inputs. A review bundle records source completeness separately from release approval
and includes the active native runtime provenance record. Final packaging
also requires the extracted native PKG and its shader assets to pass verification.

## Not covered by these licences

Game names, cover art and screenshots shown by Orbit belong to their respective owners. Artwork is loaded from external catalogue URLs at runtime; it is not part of Orbit's licensed source and is not licensed under the GPL.

## QR code generator

The Support Orbit panel uses Nayuki’s QR Code generator v1.8.0 under the MIT licence. Pinned sources are included in `backend/qrcodegen.c` and `backend/qrcodegen.h`. See [licence](licenses/QR-Code-generator-MIT.txt) and [upstream](https://github.com/nayuki/QR-Code-generator/tree/v1.8.0). QR codes are generated locally.

## Review scores and playtimes

Optional catalogue metadata includes adapted records from independently published datasets by Zaire Ali (CC BY-SA 4.0) and Sina Ghafouri (CC0). Source attribution, licence links and the changes made are in [licenses/game-metadata.txt](licenses/game-metadata.txt). The metadata licences apply separately from the application code.
