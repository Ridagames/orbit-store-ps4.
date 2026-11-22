// In-process reader only. 7-Zip never receives a destination path, never opens
// volumes, and never writes files itself. Orbit validates every entry first.
#include "sevenzip.h"
#include "Common/MyInitGuid.h"
#include "Common/MyCom.h"
#include "Common/UTFConvert.h"
#include "Windows/PropVariant.h"
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "7zip/ICoder.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <climits>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include "../C/Alloc.h"
#include <string>
#include <time.h>

STDAPI CreateArchiver(const GUID *, const GUID *, void **);
// The service's kernel-log lines (backend/diagnostics.c). Weak: the 7-Zip
// reader test links this file without the service.
extern "C" void orbit_trace(const char *format, ...) __attribute__((weak, format(printf, 1, 2)));
#define TRACE(...) do { if (orbit_trace) orbit_trace(__VA_ARGS__); } while (0)
static double seconds() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
Z7_DEFINE_GUID(CLSID_CArchiveHandler, 0x23170F69, 0x40C1, 0x278A, 0x10,0,0,1,0x10,0,0,0);
using NWindows::NCOM::CPropVariant;
using namespace NArchive::NExtract;
struct Context {
    const SevenCallbacks *cb;
    void *user;
    UString password;
    bool passwordAsked = false;
    int result = 0;
    Int32 failed = NOperationResult::kOK; // why 7-Zip stopped on an entry, for the error
    bool unreadable = false;               // reading the archive file itself failed
    HRESULT poll() {
        if (cb->stopped(user)) { result = 1; return E_ABORT; }
        return result ? E_ABORT : S_OK;
    }
    HRESULT pass(BSTR *out) {
        passwordAsked = true;
        if (password.IsEmpty()) { result = -2; return E_ABORT; }
        return StringToBstr(password, out);
    }
};
class Input final: public IInStream, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_1(IInStream)
    Z7_IFACE_COM7_IMP(ISequentialInStream)
public:
    int fd;
    Context *ctx;
};
Z7_COM7F_IMF(Input::Read(void *data, UInt32 size, UInt32 *count)) {
    if (count) *count = 0;
    RINOK(ctx->poll())
    ssize_t n;
    do n = read(fd, data, size); while (n < 0 && errno == EINTR);
    if (n < 0) { ctx->unreadable = true; return E_FAIL; }
    if (count) *count = (UInt32)n;
    return S_OK;
}
Z7_COM7F_IMF(Input::Seek(Int64 offset, UInt32 origin, UInt64 *pos)) {
    RINOK(ctx->poll())
    if (origin > 2) return E_INVALIDARG;
    off_t n = lseek(fd, (off_t)offset, origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    if (n < 0) return E_FAIL;
    if (pos) *pos = n;
    return S_OK;
}
class Open final: public IArchiveOpenCallback, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_2(IArchiveOpenCallback, ICryptoGetTextPassword)
public:
    Context *ctx;
};
Z7_COM7F_IMF(Open::SetTotal(const UInt64 *, const UInt64 *)) { return ctx->poll(); }
Z7_COM7F_IMF(Open::SetCompleted(const UInt64 *, const UInt64 *)) { return ctx->poll(); }
Z7_COM7F_IMF(Open::CryptoGetTextPassword(BSTR *out)) { return ctx->pass(out); }
struct Entry { std::string path; int64_t size; bool directory; };
class Output final: public ISequentialOutStream, public CMyUnknownImp {
public:
    Z7_IFACES_IMP_UNK_1(ISequentialOutStream)
public:
    Context *ctx;
};
static void reported(UInt64 done);  // 7-Zip's progress: unpacked bytes so far
static void written(UInt32 size);    // bytes handed to Orbit since then
Z7_COM7F_IMF(Output::Write(const void *data, UInt32 size, UInt32 *count)) {
    if (count) *count = 0;
    RINOK(ctx->poll())
    if ((ctx->result = ctx->cb->write(ctx->user, data, size))) return E_ABORT;
    if (count) *count = size;
    written(size);
    return S_OK;
}
class Extract final: public IArchiveExtractCallback, public ICryptoGetTextPassword, public CMyUnknownImp {
    Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback, ICryptoGetTextPassword)
    Z7_IFACE_COM7_IMP(IProgress)
public:
    Context *ctx;
    const std::vector<Entry> *entries;
    bool extracting = false;
};
Z7_COM7F_IMF(Extract::SetTotal(UInt64)) { return ctx->poll(); }
Z7_COM7F_IMF(Extract::SetCompleted(const UInt64 *done)) {
    if (done) reported(*done);
    return ctx->poll();
}
Z7_COM7F_IMF(Extract::CryptoGetTextPassword(BSTR *out)) { return ctx->pass(out); }
Z7_COM7F_IMF(Extract::PrepareOperation(Int32)) { return ctx->poll(); }
Z7_COM7F_IMF(Extract::GetStream(UInt32 index, ISequentialOutStream **out, Int32 mode)) {
    *out = nullptr;
    RINOK(ctx->poll())
    extracting = mode == NAskMode::kExtract;
    if (!extracting) return S_OK; // Solid blocks may decode skipped neighbours.
    if (index >= entries->size()) return E_FAIL;
    const auto &e = (*entries)[index];
    if ((ctx->result = ctx->cb->begin(ctx->user, e.path.c_str(), e.size, e.directory))) return E_ABORT;
    if (!e.directory) {
        auto *stream = new(std::nothrow) Output;
        if (!stream) { ctx->result = -1; return E_OUTOFMEMORY; }
        stream->ctx = ctx;
        static_cast<ISequentialOutStream *>(stream)->AddRef();
        *out = stream;
    }
    return S_OK;
}
Z7_COM7F_IMF(Extract::SetOperationResult(Int32 result)) {
    if (result != NOperationResult::kOK) {
        ctx->failed = result;
        ctx->result = result == NOperationResult::kWrongPassword ||
            (ctx->passwordAsked && (result == NOperationResult::kCRCError || result == NOperationResult::kDataError)) ? -2 : -1;
        return E_ABORT;
    }
    if (extracting && (ctx->result = ctx->cb->end(ctx->user))) return E_ABORT;
    return ctx->poll();
}
static bool flag(IInArchive *arc, UInt32 index, PROPID key) {
    CPropVariant p;
    if (arc->GetProperty(index, key, &p) != S_OK) throw 1;
    return (p.vt == VT_BOOL && p.boolVal) || (p.vt == VT_BSTR && p.bstrVal && *p.bstrVal);
}
// RAR5 windows, LZMA/LZMA2 dictionaries and PPMd models (7-Zip calls
// Orbit7zip_WindowAlloc and Orbit7zip_ModelAlloc, patched in by
// tools/patch-7zip.py). Up to 64 MB stays in memory, as much as
// libarchive itself uses. A larger one lives in an unlinked file in the
// extraction folder, mapped into memory.
//
// Mapping a file is not enough on its own: on a PS4 every page written to the
// mapping counts as the service's memory until it is dropped (13 Nov 2026: a
// 128 MB RAR window took the service from 140 to 269 MB, and a 64 MB mapping
// in a probe counted 64 MB, the same as plain memory; writing pages back
// without dropping them doesn't change that). So every 64 MB of unpacked data
// a window over 128 MB is written back to its file and dropped from memory
// (trim_windows); the decoder reads back from the file what it still needs.
// Pages it reads back count too until the next drop, so between drops the
// service grew about twice what was unpacked: a 1 GB-window RAR (331 MB
// unpacked) peaked 156 MB above the service's usual 177 MB, where keeping the
// window took 330 MB, and would take 1 GB for a bigger game. A window of any
// size then costs about as much as a 128 MB one kept whole, so a window's
// size never decides which archives Orbit can extract. The price is time:
// each drop writes the new part of the window to the drive (about 2.5 s), so
// that RAR took 38 s instead of about 12.
//
// A PPMd model is never dropped: the decoder reads all of it all the time, so
// a dropped model comes straight back (13 Nov 2026, host test: dropped at
// 771 MB, back to the whole model within the next 1 MB unpacked), and on a
// PS4 every page of it from the drive. It costs its full size: on the console
// a 1 GB model took the service from 174 to about 1,080 MB for 7 minutes and
// the PS4 moved about 400 MB of other programs' memory to its swap; 256 MB
// models (beyond 7-Zip's strongest setting, 192 MB) took it to 430 MB.
//
// The file starts empty (sparse): writing zeros to claim its drive space
// counted as the service's memory too (316 MB of zeros: +250 MB) and made each
// later page a read from the drive before the decoder could write it (14
// instead of 36 MB/s). So the free space is checked instead.
static const size_t kMemoryWindow = (size_t)65 << 20; // a 64 MB window plus its margin
static const size_t kTrimAbove = (size_t)129 << 20;   // 128 MB plus margin: kept, like a 64 MB one
static const uint64_t kTrimEvery = (uint64_t)64 << 20; // unpacked bytes between drops
static const uint64_t kSpareSpace = (uint64_t)16 << 20; // as extract.c leaves per file
static thread_local int window_directory = -1;
static thread_local int64_t window_reserve = 0;
static thread_local bool window_short = false; // a window didn't fit on the drive
static thread_local uint64_t trim_mark = 0;
// How far the decoder has got (see reach): 7-Zip's last report plus what it
// has written since.
static thread_local uint64_t last_report = 0, since_report = 0;
static std::mutex window_lock;
struct Window { void *at; size_t size; bool whole; pthread_t owner; };
static Window mapped_windows[16]; // a 7z can run several decoders at once
extern "C" void seven_window_place(int directory, int64_t reserve) {
    window_directory = directory;
    window_reserve = reserve > 0 ? reserve : 0;
}
static std::atomic<unsigned> window_count{0};
// Free bytes on the extraction drive: backend/extract.c's extract_room, which
// tests can set; weak, like orbit_trace, for the reader test.
extern "C" int64_t extract_room(int directory) __attribute__((weak));
static uint64_t drive_room(int directory) {
    if (extract_room) {
        int64_t room = extract_room(directory);
        return room > 0 ? (uint64_t)room : 0;
    }
    struct statvfs drive;
    if (fstatvfs(directory, &drive)) return 0;
    return (uint64_t)drive.f_bavail * (drive.f_frsize ? drive.f_frsize : drive.f_bsize);
}
static void *mapped_window(size_t size, bool whole) {
    if (window_directory < 0) return nullptr;
    // Room on the drive for the part of the file the archive can fill (a
    // window or dictionary at most the unpacked size; a PPMd model all of
    // itself), besides the files still to be unpacked.
    uint64_t claim = whole ? size : std::min<uint64_t>(size, (uint64_t)window_reserve + ((uint64_t)1 << 20));
    uint64_t done = last_report + since_report, left = (uint64_t)window_reserve > done ? (uint64_t)window_reserve - done : 0;
    if (drive_room(window_directory) < claim + left + kSpareSpace) {
        window_short = true;
        TRACE("extract: no room for a %zu MB %s on the drive", size >> 20, whole ? "model" : "window");
        return nullptr;
    }
    char name[64];
    snprintf(name, sizeof name, ".orbit-window-%ld-%u", (long)getpid(), window_count++);
    int fd = openat(window_directory, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return nullptr;
    unlinkat(window_directory, name, 0); // gone with the mapping, even after a crash
    void *at = nullptr;
    if (!ftruncate(fd, (off_t)size)) {
        int flags = MAP_SHARED;
#ifdef MAP_NOSYNC
        flags |= MAP_NOSYNC; // written out when dropped (trim_windows), not on a timer
#endif
        at = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, fd, 0);
        if (at == MAP_FAILED) at = nullptr;
        TRACE("extract: %zu MB %s on the drive%s", size >> 20, whole ? "model" : "window", at ? "" : ", not mapped");
    }
    close(fd);
    return at;
}
static void *window_alloc(size_t size, bool whole) {
    if (size <= kMemoryWindow) return BigAlloc(size);
    void *at = mapped_window(size, whole);
    if (!at) return nullptr;
    std::lock_guard<std::mutex> hold(window_lock);
    for (auto &w : mapped_windows)
        if (!w.at) { w = {at, size, whole, pthread_self()}; return at; }
    munmap(at, size);
    return nullptr;
}
// Write this extraction's big windows back to their files and drop them from
// memory. 7-Zip decodes on the extracting thread (Z7_ST), which is also the
// only thread that frees its windows, so no mapping changes under it; another
// extraction's windows are left alone.
static void trim_windows() {
    Window mine[16];
    size_t count = 0;
    {
        std::lock_guard<std::mutex> hold(window_lock);
        for (const auto &w : mapped_windows)
            if (w.at && !w.whole && w.size >= kTrimAbove && pthread_equal(w.owner, pthread_self()))
                mine[count++] = w;
    }
    for (size_t i = 0; i < count; ++i) {
        double start = seconds();
        int rc = msync(mine[i].at, mine[i].size, MS_SYNC | MS_INVALIDATE);
#ifdef __linux__
        // Linux keeps invalidated shared pages mapped; this unmaps them (the
        // data stays in the file), as MS_INVALIDATE does on the PS4.
        madvise(mine[i].at, mine[i].size, MADV_DONTNEED);
#endif
        TRACE("extract: %zu MB window dropped from memory in %.1f s%s", mine[i].size >> 20,
              seconds() - start, rc ? " (failed)" : "");
    }
}
// How far the decoder has got: 7-Zip's last report (unpacked bytes, files
// skipped in a solid block included, as they pass through the window too)
// plus what it has written since. Reports alone are not enough: RAR5 reports
// only when it reads a new table, which can be rare.
static void reach(uint64_t done) {
    if (done < trim_mark) trim_mark = done; // a new count started
    if (done - trim_mark < kTrimEvery) return;
    trim_mark = done;
    trim_windows();
}
static void reported(UInt64 done) {
    last_report = done;
    since_report = 0;
    reach(done);
}
static void written(UInt32 size) {
    since_report += size;
    reach(last_report + since_report);
}
extern "C" void *Orbit7zip_WindowAlloc(size_t size) { return window_alloc(size, false); }
extern "C" void *Orbit7zip_ModelAlloc(size_t size) { return window_alloc(size, true); }
extern "C" void Orbit7zip_WindowFree(void *at, size_t size) {
    if (!at) return;
    {
        std::lock_guard<std::mutex> hold(window_lock);
        for (auto &w : mapped_windows)
            if (w.at == at) { size = w.size; w = {}; munmap(at, size); return; }
    }
    BigFree(at);
}
static int decode(int fd, const char *kind, const char *password, const SevenCallbacks *cb,
                  void *user, char *error, size_t cap, bool inspect) {
    Context ctx{cb, user, UString(), false, 0};
    if (!ConvertUTF8ToUnicode(AString(password ? password : ""), ctx.password)) return -1;
    // The selected handler must match the catalogue; never auto-detect executables or volumes.
    unsigned id = !strcmp(kind, "zip") ? 1 : !strcmp(kind, "7z") ? 7 : !strcmp(kind, "rar") ? 3 : 0;
    if (!id) return -1;
    unsigned char signature[8] = {};
    if (pread(fd, signature, sizeof signature, 0) < 0) return -1;
    if (id == 3 && !memcmp(signature, "Rar!\x1a\x07\x01\x00", 8)) id = 0xcc;
    GUID cls{0x23170F69, 0x40C1, 0x278A, {0x10,0,0,1,0x10,(Byte)id,0,0}};
    CMyComPtr<IInArchive> arc;
    if (CreateArchiver(&cls, &IID_IInArchive, (void **)&arc) != S_OK) return -1;
    auto *input = new Input; input->fd = fd; input->ctx = &ctx;
    CMyComPtr<IInStream> in = input;
    auto *opener = new Open; opener->ctx = &ctx;
    CMyComPtr<IArchiveOpenCallback> open = opener;
    lseek(fd, 0, SEEK_SET);
    const UInt64 searchLimit = 0;
    HRESULT hr = arc->Open(in, &searchLimit, open);
    if (hr != S_OK) {
        if (ctx.result == 1) return 1;
        if (ctx.unreadable) snprintf(error, cap, "Could not read the archive from the drive. Check the drive, then retry.");
        else if (ctx.passwordAsked) return -2;
        else snprintf(error, cap, "This download is not a readable %s archive.", kind);
        return -1;
    }
    UInt32 count = 0;
    if (arc->GetNumberOfItems(&count) != S_OK || count > 100000) return -1;
    // Reject multi-volume archives even when the first file happens to decode.
    CPropVariant volume;
    if (arc->GetArchiveProperty(kpidIsVolume, &volume) != S_OK ||
        (volume.vt == VT_BOOL && volume.boolVal)) {
        snprintf(error, cap, "Multi-part archives aren't supported."); return -1;
    }
    std::vector<Entry> entries;
    std::vector<UInt32> included;
    entries.reserve(count);
    for (UInt32 i = 0; i < count; ++i) {
        if (ctx.poll() != S_OK) return ctx.result;
        CPropVariant p, size, mode, attr;
        if (arc->GetProperty(i, kpidPath, &p) != S_OK || p.vt != VT_BSTR || !p.bstrVal ||
            arc->GetProperty(i, kpidSize, &size) != S_OK ||
            arc->GetProperty(i, kpidPosixAttrib, &mode) != S_OK ||
            arc->GetProperty(i, kpidAttrib, &attr) != S_OK) return -1;
        AString path;
        UString wide(p.bstrVal);
        if (wide.Len() != SysStringLen(p.bstrVal)) return -1; // Embedded NUL.
        ConvertUnicodeToUTF8(wide, path);
        bool directory = flag(arc, i, kpidIsDir);
        int64_t bytes = size.vt == VT_UI8 && size.uhVal.QuadPart <= INT64_MAX ? (int64_t)size.uhVal.QuadPart : -1;
        if (directory) bytes = 0;
        if (bytes < 0) return -1;
        unsigned unixMode = mode.vt == VT_UI4 ? mode.ulVal :
            attr.vt == VT_UI4 && (attr.ulVal & 0x8000) ? attr.ulVal >> 16 : 0;
        unsigned type = unixMode & S_IFMT;
        bool unsafe = (type && type != S_IFREG && type != S_IFDIR) ||
            flag(arc, i, kpidSymLink) || flag(arc, i, kpidHardLink) ||
            flag(arc, i, kpidIsAltStream) || flag(arc, i, kpidIsAnti);
        entries.push_back({std::string(path.Ptr(), path.Len()), bytes, directory});
        int check = cb->check(user, path.Ptr(), bytes, directory, unsafe);
        if (!check) included.push_back(i);
        else if (check != 2) return check;
    }
    if (inspect) { arc->Close(); return 0; }
    trim_mark = last_report = since_report = 0;
    window_short = false;
    auto *extractor = new Extract; extractor->ctx = &ctx; extractor->entries = &entries;
    CMyComPtr<IArchiveExtractCallback> extract = extractor;
    hr = included.empty() ? S_OK : arc->Extract(included.data(), (UInt32)included.size(), 0, extract);
    arc->Close();
    int result = ctx.result ? ctx.result : hr == S_OK ? 0 : -1;
    // A failed drive read is not evidence that the supplied password was wrong.
    if (ctx.unreadable && result != 1) result = -1;
    // Say why 7-Zip stopped, rather than one sentence for every failure.
    if (result == -1 && !*error) {
        const char *why =
            ctx.unreadable ? "Could not read the archive from the drive. Check the drive, then retry." :
            window_short ? "Not enough free space to extract this archive." :
            hr == E_OUTOFMEMORY ? "Not enough memory or free space on the drive to extract this archive." :
            ctx.failed == NOperationResult::kUnsupportedMethod ? "The archive uses a compression method Orbit can't extract." :
            ctx.failed == NOperationResult::kDataError ? "The archive is damaged: some of its data can't be decoded." :
            ctx.failed == NOperationResult::kCRCError ? "The archive is damaged: an extracted file failed its checksum." :
            ctx.failed == NOperationResult::kUnavailable || ctx.failed == NOperationResult::kUnexpectedEnd
                ? "The archive is incomplete: it ends before all of its files." :
            ctx.failed == NOperationResult::kDataAfterEnd ? "The archive is damaged: it has unexpected data after its end." :
            ctx.failed == NOperationResult::kHeadersError ? "The archive is damaged: its file list can't be read." : NULL;
        if (why) snprintf(error, cap, "%s", why);
    }
    return result;
}
static int invoke(int fd, const char *kind, const char *password,
                  const SevenCallbacks *cb, void *user, char *error, size_t cap, bool inspect) {
    int result;
    try { result = decode(fd, kind, password, cb, user, error, cap, inspect); }
    catch (const std::bad_alloc &) {
        snprintf(error, cap, "Not enough memory to extract this archive on the PS4. Use Orbit Zero to extract it on a computer.");
        return -1;
    } catch (...) { result = -1; }
    if (result == -2) snprintf(error, cap, "The archive password is missing or incorrect. The downloaded archive was kept.");
    else if (result == -1 && !*error) snprintf(error, cap, "The archive is damaged or uses an unsupported compression method.");
    return result;
}

extern "C" int seven_inspect(int fd, const char *kind, const char *password,
                            const SevenCallbacks *cb, void *user, char *error, size_t cap) {
    return invoke(fd, kind, password, cb, user, error, cap, true);
}
extern "C" int seven_extract(int fd, const char *kind, const char *password,
                            const SevenCallbacks *cb, void *user, char *error, size_t cap) {
    return invoke(fd, kind, password, cb, user, error, cap, false);
}
