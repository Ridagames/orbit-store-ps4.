# Generate Orbit's shaders on PS4

The selected approach is the PS4 Piglet/Shacc compiler. The owner declined the
experimental PC compiler. No experimental compiler output is used.

All eight current PS4 shader binaries were generated on firmware 12.00 on
12 November 2026. Two diagnostic builds compiled, exported, reloaded and linked
them successfully, producing identical files. The verified source/binary pairs
and a capture receipt are in `app/assets/shaders/`. Ordinary retail-Piglet
rendering still needs its separate hardware check.
The separate **Orbit Shader Capture** diagnostic (`ORBT00003`) prepares them
without requiring Orbit's main interface or service to start.

## Compiler prerequisite

A jailbroken retail PS4 can host the capture, but retail firmware alone does not
provide the compiler. Supply a matching compiler-enabled pair from your existing
PS4 development/homebrew setup:

- `libScePigletv2VSH.sprx`
- `libSceShaccVSH.sprx`

The [2 Ship PS4 installation guide](https://github.com/alechurri/2ship2harkinian-ps4/blob/main/docs/INSTALL.md)
describes this pair and local extraction from a compatible RetroArch installation.
The [ioQuake3 capture implementation](https://github.com/Mayo1970/ioQuake3-PS4/blob/main/code/ps4/ps4_shaderbin.c)
documents the binary-export entry point. These are references for the route;
Orbit's run on the target console is still required. A stock Piglet file plus an
unrelated compiler file is not a verified pair.

Neither module is downloaded by Orbit or included in its source/release package.
The matching pair used for this capture is kept in ignored private inputs.
Future captures after shader changes require the pair again.

## Build the capture diagnostic

Follow [BUILDING.md](../BUILDING.md) to create `orbit-app-build-ps4:0.2` and bootstrap
the native toolchain. Then either use the existing pair on the console under
`/data/self/system/common/lib/`:

```sh
docker run --rm --network none -v "$PWD:/work" -w /work/app \
  orbit-app-build-ps4:0.2 make shader-capture
```

Or put both locally supplied files in the ignored directory
`app/.deps/piglet-capture/` and include them only in this private diagnostic:

```sh
docker run --rm --network none -v "$PWD:/work" -w /work/app \
  -e PIGLET_MODULE_DIR=/work/app/.deps/piglet-capture \
  orbit-app-build-ps4:0.2 make shader-capture
```

Output:
`app/build/shader-capture/UP9000-ORBT00003_00-ORBITSHADER00001.pkg`.
The title differs from native Orbit `ORBT00002` and browser `ORBT00001`.
Locally supplied module hashes are saved in `private-module-hashes.json` beside
the diagnostic. Its private package must not be copied into a public release.
The capture-only CRT reserves a 32 MiB internal-memory budget and a 256 MiB
system-library heap allowance. This fixed Shacc's failed `malloc` while compiling
the backdrop fragment. The normal app's CRT is unchanged. The capture builder
checks the pinned CRT source before generating this private variant.

## Run and collect

1. On the console-testing machine, install and open **Orbit Shader Capture**.
2. The tool selects both modules from one location, resolves graphics calls from
   that exact Piglet handle, and checks binary-export availability. The compiler
   flag can incorrectly remain false for the recognized private pair; actual
   compile/reload/link results determine success.
   There is no mandatory import that silently loads retail Piglet first.
3. It compiles `backdrop`, `batch2d`, `mesh2d` and `blur`, each with a vertex and
   fragment stage. The source prefixes and attribute binding order are generated
   from the main renderer. It exports each binary, reloads it into a fresh shader
   and links the pair. Only successful pairs are saved.
4. Keep the result dialog open while copying the output over the console's FTP
   server. The app path is `/download0/orbit-store/shaders`; from a privileged FTP
   server it is normally mounted at
   `/mnt/sandbox/ORBT00003_000/download0/orbit-store/shaders/`. Confirm the mounted
   title directory on that console. Copy all eight `.bin` files, their eight
   `.glsl` files, and `capture.log` to `build/ps4-shader-capture/` on the PC.
5. Require `ALL EIGHT STAGES PASSED` in the log, then verify exact inputs:

   ```sh
   python3 app/tools/shader-assets.py verify build/ps4-shader-capture
   ```

A missing pair, unavailable compiler, failed compilation, rejected export or
failed link is an incomplete capture. The log records the stage and result;
do not import partial output. Each run removes its previous named captures so
stale files cannot make a failed run look complete.

## Build and validate the actual app

Copy the verified `.bin`/`.glsl` pairs into `app/assets/shaders/`, preserving the
exact names. Keep `capture.log` with the hardware evidence. Rebuild the ordinary
native PKG and run `app/tools/verify-package.sh` as described in BUILDING.md.
Normal Orbit packages contain the generated shader assets, not the compiler pair.

Then test the ordinary app against the intended retail Piglet environment. The
capture proves compile/export/reload/link on the capture environment; it cannot
by itself prove rendering, retail-module compatibility, fonts, blending, controller
input, audio or lifecycle behavior. Record the console firmware and module hashes
with those results and commit the eight generated pairs after successful checks.
