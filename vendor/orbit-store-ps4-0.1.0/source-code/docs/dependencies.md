# PS4 dependency and release audit

The maintained inventory is [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
Pinned archives are in `tools/release-sources.json`; binary-to-source mappings
are in `tools/release-provenance.json`. [SOURCE-BUNDLE.md](../SOURCE-BUNDLE.md)
describes the source-audit command, which can run before hardware testing.

Native runtime provenance is resolved: exact C++ release matching, identified
OpenOrbis headers/linker script, pinned musl/CRT rebuild, and generated import
libraries with matching symbol sets. See [runtime recovery](openorbis-runtime.md).
The payload C++ mapping remains verified against SDK v0.9 and LLVM 15.0.7.

The RAR review remains deferred under the owner's explicit PS4 instruction to
follow PS5: retain RAR4/RAR5 support, matching sources, original terms and recipes.
As on PS5 1.9.2, RAR goes through RARLAB's UnRAR 7.23 (`unrarsrc-7.2.7.tar.gz`,
`licenses/unrar.txt`); 7-Zip's RAR decoder stays for windows over 128 MB.
No licence clearance is asserted. `licenseCleared` and source completeness remain
separate fields; hardware gates remain deferred. Passing tests do not establish
console acceptance or permission to distribute.
