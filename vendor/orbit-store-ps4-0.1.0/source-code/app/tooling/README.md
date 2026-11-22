# Console packaging toolchain

The PS4 app builds against the source-traced OpenOrbis runtime in
`.deps/orbis/runtime/`. `tools/setup-orbis-toolchain.sh` preserves the original
SDK under `.deps/orbis/toolchain/`, invokes `setup-orbis-runtime.py`, and builds
the Go packaging tools into `.deps/orbis/gotools/bin/`. See
[runtime recovery](../../docs/openorbis-runtime.md) for exact source pins.

- `link.x` + `crt1.o`/`crti.o`/`crtn.o` produce the OELF layout create-fself
  expects (`.data.sce_process_param` and friends).
- musl `libc.a`, `libc++.a`, `libc++abi.a`, `libunwind.a`, `libm.a`,
  `libpthread.a` and `libclang_rt.builtins-x86_64.a` provide the runtime.
- Locally generated `lib/*.so` stubs supply the import surface (kernel, Piglet, pad, audio,
  user service, sysmodule, system service, common/web dialogs).
- `pkgtool/*.dll` are LibOrbisPkg's architecture-neutral assemblies; they run
  under any `dotnet` runtime for the final `pkg_build` step.
- `create-fself`, `create-gp4` and `readoelf` are built from pinned source
  commits (the release ships x86-64 binaries only).

`orbis-runtime-lock.json` pins the recovered C++ archives and runtime source
inputs. `orbis-imports.json` records the original SDK's exported function names.
These are build inputs; keep both files in source snapshots.
