# PS4 port scope

The target is Orbit Store's PS5 feature set: catalogue, download providers,
add-ons, extraction, package installation, Files, Library, pairing, updates,
four interface layouts and 26 languages. Implementation is under review;
[release status](README.md) describes the remaining dependencies and validation.

**Orbit Zero is excluded.** The desktop companion, transfer receiver, Zero APIs
and companion UI are outside this port's agreed scope.

PS5-specific platform and build components are replaced by PS4 equivalents:
OpenOrbis/GLES2 and PS4 PKGs replace the PS5 graphics/toolchain/FFPKG stack;
BGFT handles PS4 package tasks; installed PS4 titles are scanned from the console's
application metadata. PS5 ShadowMount game-image mounting is not available on PS4.

The approved catalogue retains PS4 and PS5 entries. A platform label or successful
download does not mean a PS5 game can run or install on PS4. No new catalogue feed
generation was introduced for this port.
