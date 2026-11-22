"""Reproducible PS4 compatibility patch for the pinned decoder source."""
from pathlib import Path
root = Path(__file__).resolve().parents[1] / '.deps/7zip'


def apply(path, marker, patch, old=None):
    source = path.read_text()
    if patch in source:  # already applied (some patches keep the old line inside)
        return
    if old and old in source:
        path.chmod(path.stat().st_mode | 0o200)
        path.write_text(source.replace(old, patch, 1))
    elif patch not in source:
        assert marker in source
        path.chmod(path.stat().st_mode | 0o200)
        path.write_text(source.replace(marker, patch + marker, 1))


# PS4's Jaguar CPU has no AES-NI, VAES or SHA-NI; use upstream's software stubs
# for those optional acceleration paths (not for AES or SHA-256 themselves).
apply(root / 'C/AesOpt.c', '#ifdef USE_INTEL_AES\n',
      '''/* PS4's Jaguar CPU has no AES-NI or VAES; use upstream's software stubs. */
#ifdef ORBIT7ZIP_PS4
#undef USE_INTEL_AES
#define Z7_USE_AES_HW_STUB
#undef USE_INTEL_VAES
#define Z7_USE_VAES_HW_STUB
#endif

''',
      old='''/* PS5's Zen 2 CPU has AES-NI but no VAES; its toolchain omits those intrinsics. */
#ifdef ORBIT7ZIP_PS5
#undef USE_INTEL_VAES
#define Z7_USE_VAES_HW_STUB
#endif

''')

# Jaguar also lacks the SHA-NI extension. Use upstream's software stub for that
# optional acceleration path (not for AES or SHA-256).
for name in ('Sha1Opt.c', 'Sha256Opt.c', 'Sha512Opt.c'):
    apply(root / 'C' / name, '#ifdef USE_HW_SHA\n',
          '''#ifdef ORBIT7ZIP_PS4
#undef USE_HW_SHA
#define Z7_USE_HW_SHA_STUB
#endif

''',
          old='''#ifdef ORBIT7ZIP_PS5
#undef USE_HW_SHA
#define Z7_USE_HW_SHA_STUB
#endif

''')

# Orbis has no utimensat; utimes covers file timestamps and symlink timestamp
# restore is skipped where AT_SYMLINK_NOFOLLOW cannot be expressed.
apply(root / 'CPP/Windows/FileDir.cpp',
      '  return utimensat(AT_FDCWD, path, times, flags) == 0;\n',
      '''#ifdef ORBIT7ZIP_PS4
  if (flags & AT_SYMLINK_NOFOLLOW)
    return true;
  struct timeval tv[2];
  tv[0].tv_sec = times[0].tv_sec;  tv[0].tv_usec = (int)(times[0].tv_nsec / 1000);
  tv[1].tv_sec = times[1].tv_sec;  tv[1].tv_usec = (int)(times[1].tv_nsec / 1000);
  return utimes(path, tv) == 0;
#else
  return utimensat(AT_FDCWD, path, times, flags) == 0;
#endif
''',
      old='  return utimensat(AT_FDCWD, path, times, flags) == 0;\n')

# Same for the descriptor variant: futimens -> futimes on Orbis.
apply(root / 'CPP/Windows/FileIO.cpp',
      '        if (futimens(_handle, times) == 0)\n',
      '''#ifdef ORBIT7ZIP_PS4
        struct timeval ftv[2];
        ftv[0].tv_sec = times[0].tv_sec;  ftv[0].tv_usec = (int)(times[0].tv_nsec / 1000);
        ftv[1].tv_sec = times[1].tv_sec;  ftv[1].tv_usec = (int)(times[1].tv_nsec / 1000);
        if (futimes(_handle, ftv) == 0)
#else
        if (futimens(_handle, times) == 0)
#endif
''',
      old='        if (futimens(_handle, times) == 0)\n')

# RAR5 windows ("dictionaries") reach gigabytes. Orbit supplies the window
# storage: RAM for ordinary sizes, a mapped file on the destination drive for
# larger ones (backend/sevenzip.cpp), so any archive can be extracted on a PS4.
rar5 = root / 'CPP/7zip/Compress/Rar5Decoder.cpp'
apply(rar5, 'namespace NCompress {\nnamespace NRar5 {\n',
      '''extern "C" void *Orbit7zip_WindowAlloc(size_t size);
extern "C" void Orbit7zip_WindowFree(void *window, size_t size);

''')
apply(rar5, '#define Z7_RAR_FREE_WINDOW ::BigFree(_window);\n',
      '#define Z7_RAR_FREE_WINDOW Orbit7zip_WindowFree(_window, _winSize_Allocated);\n',
      old='#define Z7_RAR_FREE_WINDOW ::BigFree(_window);\n')
apply(rar5, '      Byte *win = (Byte *)::BigAlloc(allocSize);\n',
      '      Byte *win = (Byte *)Orbit7zip_WindowAlloc(allocSize);\n',
      old='      Byte *win = (Byte *)::BigAlloc(allocSize);\n')

# LZMA and LZMA2 dictionaries (7z) can be as large; the same storage applies.
lzma = root / 'C/LzmaDec.c'
apply(lzma, '// #define kNumTopBits 24\n',
      '''void *Orbit7zip_WindowAlloc(size_t size);
void Orbit7zip_WindowFree(void *window, size_t size);

''')
apply(lzma, '  ISzAlloc_Free(alloc, p->dic);\n  p->dic = NULL;\n',
      '  (void)alloc;\n  Orbit7zip_WindowFree(p->dic, p->dicBufSize);\n  p->dic = NULL;\n',
      old='  ISzAlloc_Free(alloc, p->dic);\n  p->dic = NULL;\n')
apply(lzma, '    p->dic = (Byte *)Orbit7zip_WindowAlloc(dicBufSize);\n',
      '    p->dic = (Byte *)Orbit7zip_WindowAlloc(dicBufSize);\n',
      old='    p->dic = (Byte *)ISzAlloc_Alloc(alloc, dicBufSize);\n')

# PPMd (7z's PPMd method, RAR 3 and 4's text mode, ZIP's PPMd) keeps a model of
# up to 4 GB; 7-Zip's strongest PPMd setting asks 192 MB. Same storage again,
# with drive space claimed for the whole model (it can outgrow the output).
for name, kind in (('Ppmd7.c', 'Ppmd7'), ('Ppmd8.c', 'Ppmd8')):
    ppmd = root / 'C' / name
    apply(ppmd, f'void {kind}_Free(C{kind} *p, ISzAllocPtr alloc)\n',
          '''void *Orbit7zip_ModelAlloc(size_t size);
void Orbit7zip_WindowFree(void *window, size_t size);

''')
    apply(ppmd, '  (void)alloc;\n  Orbit7zip_WindowFree(p->Base, p->AlignOffset + p->Size);\n',
          '  (void)alloc;\n  Orbit7zip_WindowFree(p->Base, p->AlignOffset + p->Size);\n',
          old='  ISzAlloc_Free(alloc, p->Base);\n')
    apply(ppmd, '(Byte *)Orbit7zip_ModelAlloc(p->AlignOffset + size)',
          '(Byte *)Orbit7zip_ModelAlloc(p->AlignOffset + size)',
          old='(Byte *)ISzAlloc_Alloc(alloc, p->AlignOffset + size)')
