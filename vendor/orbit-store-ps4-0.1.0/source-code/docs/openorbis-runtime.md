# Native runtime source recovery

The runtime used by both `ORBT00002` and `ORBT00001` is now assembled by
`app/tools/setup-orbis-runtime.py`. Its immutable inputs are recorded in
`tools/release-sources.json` and `app/tooling/orbis-runtime-lock.json`.

## Recovered sources

The SDK archive distributed as OpenOrbis v0.5.4, SHA-256
`3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526`,
contains older constituent builds. Its release label does not identify them.

| Component | Identified source | Evidence |
|---|---|---|
| OpenOrbis headers and `link.x` | `6929bafac0b565692ed61e66032a79185ee9cf65` (v0.5.3) | All 691 matching files from the source archive equal the installed SDK bytes |
| libc++, libc++abi, libc++experimental, libunwind, compiler builtins | `OpenOrbis/llvm-project` `83e2569f0c062d980ae142d8dc583f6b8733be35` (v1.2) | All five archives and 180 installed headers equal the official v1.2 binary release |

The [LLVM v1.2 release](https://github.com/OpenOrbis/llvm-project/releases/tag/v1.2)
identifies that source commit. Its full source archive includes component notices
and `.github/workflows/release-stable.yml`, the upstream build recipe. The original
historical build environment is not claimed to be bit-reproducible. The recovered
library hashes are enforced before they can enter the active runtime.

## Rebuilt components

The precise musl and stub-generator revisions of the old SDK build could not be
proved from its floating checkouts. They are replaced as follows:

- **musl and CRT:** unmodified `OpenOrbis/musl` commit
  `779f95174b44d39e6a8a788b36289cf4768944a9`. Its 218 installed headers match the
  previous SDK. Configure selects `x86_64-scei-ps4`; Clang explicitly targets
  `x86_64-pc-freebsd12-elf`, including on ARM hosts. Shared libraries and compiler
  wrappers are disabled. Flags are `-fPIC -DPS4 -D_LIBUNWIND_IS_BAREMETAL=1` plus
  upstream configure's flags. No source patch or disabled diagnostic is needed.
- **Ten import libraries:** the SDK's complete exported function-name sets are
  recorded in `app/tooling/orbis-imports.json`. Orbit generates assembly and links
  the libraries with Clang/LLD. Every exported symbol is compared with the old
  SDK during the recovery audit. These are link-only symbol declarations;
  create-fself records imports for the console to resolve. Their generated bodies
  are not included in either application. Sony firmware implementations are not
  distributed.

The active runtime has 1,088 headers plus the linker script, all byte-identical
to the previous inputs. Unused SDL binaries/headers from the SDK are excluded.
WebP is rebuilt from the pinned 1.4.0 source archive against these verified headers.

## Build and modification

Normal shortcut/native builds invoke the setup automatically. Initial setup needs
network access for uncached archives and Go tools. With the dependencies cached,
the build works in a container with `--network none`.

```sh
docker run --rm -v "$PWD:/work" -w /work orbit-app-build:0.2 \
  bash app/tools/setup-orbis-toolchain.sh
```

`.deps/orbis/toolchain` under `app/` preserves the original SDK. The active target
is `app/.deps/orbis/runtime`; the build log is `app/.deps/orbis/runtime-build.log`.
Its `provenance.json` records input hashes, recipes, compiler versions, configure
flags and every installed header/library hash. Changed recipes, compiler versions
or files invalidate the cache. Builds use an isolated staging directory and only
activate a successfully completed runtime.

To modify musl, update its pinned source archive entry and runtime lock, then
rebuild the shortcut, payload and native app in that order. To modify a C++
runtime, use the included upstream LLVM build recipe, record the new source and
result hashes, and update the lock. Do not silently replace cached libraries.
For a full rebuild, remove generated `app/.deps/orbis/runtime`, `app/.deps/webp`,
`app/build/obj`, and the generated shortcut/payload outputs, then follow
[BUILDING.md](../BUILDING.md). Source directories and console data are unaffected.

The source-bundle preparer checks the active runtime's files, recipe hashes and
source pins, and includes its provenance record. Source provenance does not certify
PS4 runtime behavior, settle the separate RAR review, or waive the shader gate.
