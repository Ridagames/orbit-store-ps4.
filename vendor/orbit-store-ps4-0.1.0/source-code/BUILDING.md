# Building Orbit Store 0.1.0

This source tree builds the Orbit Store service and native TV app for PS4. Orbit Zero, the companion app for Mac, Windows and Linux that works with it, is maintained and packaged separately. See [release notes](docs/0.1.0-release-notes.md) for the user-facing changes.

This source builds the Orbit **0.1.0** service/browser payload and the native TV app **0.1.0** (`ORBT00002`, content version `00.001.000`) carrying it. The TV app includes the payload built from this tree. Game metadata and external artwork URLs come from the included catalogue snapshot; builds do not fetch game artwork.

Requirements: Node.js 22+, Python 3.11+, a host C compiler, OpenSSL development files and Docker. On macOS use the Command Line Tools and `brew install openssl@3`; the build container already supplies Clang and OpenSSL. Run host Python tools through a project virtual environment. Catalogue tools compile a small host helper against OpenSSL; no extra Python packages are required.

The source archive includes `catalog/games.enc`, `catalog/releases.enc`, `catalog/addons.enc`, `catalog/updates.enc`, `catalog/catalog.enc` and `catalog/embedded.enc` (the same feed compressed, which the service carries), together with all readers, build tools and required decoding material. `catalog:check` and `build` read these inputs directly. After editing catalogue inputs, run `npm run catalog:build` and commit the updated snapshots. Ordinary rebuilds reuse unchanged snapshot bytes.

The shared service supplies catalogue data to both interfaces. Existing catalogue caches are migrated automatically. Archive jobs use queue schema 6, which older builds cannot read; pairing state retains its existing format. Game update downloads name their game under keys 0.0.3 ignores, so 0.0.3 still starts with them in the queue and lists them as plain downloads. TorBox job compatibility is described in [TorBox downloads](docs/debrid.md). Orbit for PS5 1.1.0 uses the v5 catalogue feed for PS4 and PS5 games. Orbit for PS5 1.5.0 and later, and this PS4 release, use v8 with separate game and add-on collections. v7 remains a compatible game-only feed for earlier clients. The v6 feed is pinned to its last catalogue below the older 4,096-option limit. 1fichier remains deferred. The v6 compatibility feed retains ZIP, 7z, RAR, AkiraBox and DataNodes options; v5 excludes archives and those providers. The v4 and v3 feeds also retain PS5-only compatibility. Catalogue updates cannot add these capabilities to an older app.

## Payload and browser interface

```sh
python3 -m venv .venv
docker build -f tools/Dockerfile.sdk.ps4 -t orbit-sdk-ps4:0.9-fw1400 .
.venv/bin/python tools/bootstrap.py
npm ci
npm run build
docker build -t orbit-build-ps4:0.2 .
docker build -t orbit-app-build:0.2 app
python3 tools/embed-launcher.py
docker run --rm -v "$PWD:/work" -w /work orbit-app-build:0.2 bash app/tools/setup-orbis-toolchain.sh
docker run --rm --network none -v "$PWD:/work" -w /work orbit-app-build:0.2 bash launcher/tools/build.sh
docker run --rm --cpus 2 --memory 6g -v "$PWD:/work" orbit-build-ps4:0.2 payload
```

The encrypted-archive implementation compiles 7-Zip 26.03 from pinned source through `tools/build-7zip.sh` and `tools/7zip-orbit.mak`. Its C++ bridge only reads archives; file validation and writes stay in Orbit. To build without network access, copy the source bundle’s `7z2603-src.7z` into `.deps/` first. RAR archives use RARLAB's UnRAR 7.23, unpacking on several threads; 7-Zip's RAR reader takes only archives whose window is over 128 MB, which it keeps in a file on the drive. UnRAR is compiled from pinned source through `tools/build-unrar.sh` with the changes in `tools/patch-unrar.py`; copy the source bundle's `unrarsrc-7.2.7.tar.gz` into `.deps/` to build it without network access. Distribution clearance is pending for the RAR component; see `THIRD-PARTY-NOTICES.md`.

The shortcut build produces `build/orbit-browser.pkg`, a real PS4 browser application embedded in the payload. It requires the app toolchain; the payload image alone cannot build it.

The result is `build/orbit_store.elf`. It carries `build/orbit_runtime.elf` (the runtime saved on the console), `build/orbit_starter.elf` (the small starter that payload managers run, from `launcher/starter.c`) and elfldr. Run `npm run build` before rebuilding the payload whenever the browser interface or catalogue changes.

The SDK image (`tools/Dockerfile.sdk.ps4`) adds firmware 13.52 and 14.00 support, which upstream added after the last releases. It takes the SDK v0.9 release, rebuilds its startup code (`crt1.o`) from SDK commit `573b4a0`, and builds elfldr (GPL-3.0-or-later, [ps4-payload-dev/elfldr](https://github.com/ps4-payload-dev/elfldr)) from commit `a481740`, both in Ubuntu 24.04 with clang 18.1.3 as upstream CI does. Before that it rebuilds SDK v0.9's `crt1.o` and elfldr v0.7 from their release commits and stops unless they match the official files byte for byte. `tools/bootstrap.py` copies elfldr into `.deps/elfldr/elfldr.elf` and checks its SHA-256. The payload and the TV app package carry it unchanged; Orbit gives it to GoldHEN's payload loader when no ELF loader runs.

The source bundle includes the exact cJSON and CA files used for the published build. To reuse them, copy its `build-inputs/cjson/` and `build-inputs/cacert.pem` into `.deps/` before bootstrap.

To rebuild with modified libmicrohttpd, curl, OpenSSL, libarchive, liblzma, bzip2 or zlib, unpack the included source archive, adapt `tools/build-deps.sh` to use it, rebuild `orbit-build-ps4:0.1`, and rebuild the payload. Rebuild the image after pulling these archive-extraction changes too; host tests link the same libarchive through `tools/build-host-archive.sh`. The same source permits relinking the statically linked libmicrohttpd.

## Native TV app

Build the payload first. Retail Piglet has no runtime GLSL compiler, so the app uses eight precompiled Piglet shader stages, kept with their exact GLSL sources in `app/assets/shaders/`. They were captured on a PS4 with the standalone tool in [PS4 shader capture](docs/ps4-shader-capture.md); after changing a shader, export the inputs with `python3 app/tools/shader-assets.py export app/build/shader-inputs` and capture again. A package whose shaders don't match their sources cannot pass package verification.

Then:

```sh
docker build -t orbit-app-build:0.2 app
docker run --rm --cpus 2 --memory 6g -v "$PWD:/work" -w /work/app -e BUILD_JOBS=2 orbit-app-build:0.2 make pkg
docker run --rm --cpus 2 --memory 6g -v "$PWD:/work" -w /work/app orbit-app-build:0.2 bash tools/verify-package.sh
```

Outputs are `app/dist/UP9000-ORBT00002_00-ORBITSTORETV0001.pkg`, the unpacked `ORBT00002` app folder and its ZIP. The verification command extracts the actual PKG and compares its bundled ELF, version, executable and metadata with the build inputs, and requires all eight exact-source Piglet shader stages. A failed verification removes old success evidence. In an unpacked source archive without Git metadata, verification uses the commit in the adjacent source bundle's `SOURCES.json` (or pass `ORBIT_SOURCE_COMMIT` explicitly).

The PS4 package uses SFO APP_VER `01.04` and VERSION `01.00`; the application content version is `00.001.000`, shown in the app as 0.1.0 (0.0.1 used APP_VER `01.01`, 0.0.2 `01.02`, 0.0.3 `01.03`). Future PS4 releases must increase the package version as well as the app version. These are separate fields.

The app uses source-traced OpenOrbis headers, recovered LLVM v1.2 libraries,
musl/CRT rebuilt from a pinned source commit, and locally generated import stubs.
WebP is rebuilt against the same runtime headers. See
[the runtime recovery record](docs/openorbis-runtime.md),
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
[SOURCE-BUNDLE.md](SOURCE-BUNDLE.md) for pins, notices and rebuild instructions.

## Interface languages

The browser interface and the TV app share the catalogues in `i18n/`. English text is the key, so a string without a translation shows in English. After adding or changing interface text (`t()`, `tn()` or `rich()` in `src/`, `tr()` or `trn()` in `app/src/orbit/`, or a fixed message in `backend/`), refresh the list and see what each language is missing:

```sh
node tools/i18n.mjs extract   # rewrites i18n/en.json
node tools/i18n.mjs check     # lists missing, unused and mismatched translations
```

`npm test` fails until every language has every string with the same `{placeholders}`. The TV app follows the PS4's system language; the browser follows the browser's language unless one is chosen in App settings.

For a new language, register its code and native name in `src/i18n.ts`, include it in `tools/i18n.mjs`, and add its system-language mapping and number/date formats in `app/src/orbit/i18n.cpp`. The native build packages every non-English JSON catalogue automatically. Check the font atlas for every character the translation uses.

### Native language fonts

The package includes pre-baked Noto Arabic and CJK atlases; normal builds do not need FontTools. To rebuild fonts after changing translations or character sets:

```sh
python3 -m venv .font-tools
.font-tools/bin/pip install fonttools==4.66.1
PYTHON="$PWD/.font-tools/bin/python" bash app/tools/bake-fonts.sh
```

The baker converts Noto CJK’s CFF curves to quadratic outlines before generating SDFs. `charsets.py` includes the language’s standard character set plus translated UI characters. A small set of language-picker glyphs is added to each primary font; only the selected language’s full fallback atlas is loaded at runtime. The original fonts, conversion scripts and OFL licences are included in the source bundle. Arabic shaping and right-to-left layout are handled by the native renderer.

The Hungarian catalogue was contributed by [isty1974 in issue #13](https://github.com/saawant12/orbit-store-ps5/issues/13#issuecomment-6065940183), with newer interface messages filled in for this version.

## Checks and release packaging

```sh
npm test
npm run test:catalog
docker run --rm --cpus 2 --memory 6g --network none -v "$PWD:/work" orbit-build-ps4:0.2 test-art test-tv-app test-updates test-starter test-https
docker run --rm --cpus 2 --memory 6g --network none -v "$PWD:/work" -w /work/app orbit-app-build:0.2 make test
```

The source snapshot includes the root and native tests. None of these commands contact a console.

From a clean release commit, rebuild both binaries, run the package verification, then `npm run release:package`. The command checks release evidence, version agreement, artifact freshness, package identities and dependency source hashes. It stages both binaries, their checksums, exact source and licence bundle, release notes and both official feeds under `build/release-0.1.0/`. It never uploads or installs anything. Publish feed changes only after their binary URLs are live and their hashes verified.

For source preparation before console testing, run:

```sh
python3 tools/source_bundle.py --review --fetch --output build/source-review-0.1.0
```

This accepts current working-tree changes and records missing provenance explicitly.
It does not package binaries, waive licence requirements or claim console acceptance.
`release:package -- --review` still requires a clean commit and native package
verification, including matching Piglet shaders.
