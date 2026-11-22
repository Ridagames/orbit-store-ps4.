# Orbit Store PS4 source and licence bundle

This tree builds service **0.1.0**, native app **0.1.0** (`ORBT00002`) and its
browser shortcut (`ORBT00001`). A release must ship corresponding sources and
notices for `orbit_store.elf` and `orbit_store_ps4.pkg` together.

## Current status

No PS4 release is certified. Native runtime source provenance is resolved through
exact-release matching and pinned rebuilds. The separate RAR review remains
deferred under the owner's direction to follow PS5.
`tools/release-provenance.json` records the source mapping. SDK binary archives
are useful build inputs, but are not substitutes for their source code.

## Prepare a local source audit before console testing

```sh
python3 tools/source_bundle.py --review --fetch --output build/source-review-0.1.0
```

Use a new empty output directory for each snapshot. The command never publishes
or includes application binaries. It can snapshot uncommitted changes without
altering the Git index or creating a commit. `SOURCES.json` marks the snapshot as
review-only, records the base commit and working-tree status, separates source
archives from binary build inputs, and lists unresolved components. It must not
be described as complete while those entries remain.

Existing archives are read from `build/upstream/`. Missing inputs fetched with
`--fetch` are cached under `.state/source-downloads/`, so a host audit also works
when Docker owns the native build cache. The public-facing `release-draft/` is
kept in Git for handoff but excluded from corresponding-source archives.

The application tarball includes current source, tests, build documentation,
original fonts/assets, decoder patches and approved encrypted catalogue inputs.
It excludes ignored caches, local state, agent instructions and obsolete handoffs
according to `.gitattributes`. `SOURCE-SHA256SUMS` covers the individual inputs;
the ZIP contains those inputs and their checksum list.

## Dependencies

`tools/release-sources.json` is the inventory of filenames, immutable source
revisions where applicable, URLs, SHA-256 hashes and input kinds. It includes:

- PS4 payload SDK v0.9 source and its binary build input, and the SDK source at
  `573b4a0` (v0.9 plus firmware 13.52 and 14.00) whose startup code replaces
  v0.9's `crt1.o`.
- The original OpenOrbis v0.5.4 packaging input, actual header/linker sources at
  `6929baf`, recovered LLVM v1.2 binaries and full source at `83e2569`, and musl
  sources at `779f951` used for the native rebuild. Import-library source and
  symbol lists are included in the application tarball.
- Pinned source for create-fself, create-gp4 and readoelf.
- LLVM 15.0.7 source, matching the payload SDK's published C++ runtime recipe;
  all three installed libraries match the pinned SDK release, whose CI invokes
  that recipe.
- libmicrohttpd, curl, OpenSSL, SQLite, 7-Zip, UnRAR, libarchive, XZ/liblzma,
  bzip2, zlib and libwebp source archives used by the build recipes.
- elfldr source at `a481740` (v0.7 plus firmware 13.52 and 14.00), which
  `tools/Dockerfile.sdk.ps4` builds unmodified for the payload and the TV app
  package, and its source at the v0.7 tag (`b3ee497`). The image builds both the
  way upstream CI does and first checks that v0.7 and SDK v0.9's `crt1.o` come
  out byte for byte identical to the official files.
- Exact cJSON and CA certificate inputs used by the local build, under
  `build-inputs/`; application/component notices under `licenses/`.

The legacy browser dependency versions are locked in `package-lock.json`; their
runtime notices are included. This is not an offline mirror of Node packages,
Docker images or all host build tools. Normal bootstrapping can need the network.

## Rebuild or modify

Verify `SOURCE-SHA256SUMS`, unpack the application tarball, then follow
`BUILDING.md` and `app/README.md`. Restore `build-inputs/cjson/` and
`build-inputs/cacert.pem` under `.deps/` before bootstrap. Put the pinned 7-Zip, UnRAR and
SQLite archives in `.deps/` to reuse them without downloading. `tools/bootstrap.py`
copies elfldr out of the SDK image built from `tools/Dockerfile.sdk.ps4`.

`tools/build-deps.sh` rebuilds the statically linked HTTP, TLS and archive
libraries. `tools/build-7zip.sh`, `tools/7zip-orbit.mak` and `tools/patch-7zip.py`
rebuild the 7-Zip decoder; `tools/build-unrar.sh` and `tools/patch-unrar.py`
rebuild UnRAR, which extracts RAR archives. The SDK's `libcxx.sh` is its C++ runtime recipe.
`app/tools/setup-webp-dependencies.sh` rebuilds the native WebP decoder.
`app/tools/setup-orbis-toolchain.sh` pins the packaging inputs and invokes
`app/tools/setup-orbis-runtime.py` to assemble the verified native runtime. See
[the rebuild recipe](docs/openorbis-runtime.md) for changes to these components.
The preparer verifies the active runtime and includes its provenance record under
`build-inputs/openorbis-runtime-provenance.json`. Piglet shader binaries must match their supplied GLSL
sources (both are in `app/assets/shaders/`); a package without them cannot pass release checks.

Original fonts, OFL notices and baking scripts are included. See `BUILDING.md` for
font tooling. Game binaries and external catalogue artwork are not bundled.

Final release packaging requires a clean source commit, resolved provenance and
licence gates, rebuilt binaries and successful native package verification.
`npm run release:package` then stages the binaries, feeds, source bundle and
checksums locally. Console acceptance remains a separate requirement; a source
snapshot or matching checksum cannot satisfy it.
