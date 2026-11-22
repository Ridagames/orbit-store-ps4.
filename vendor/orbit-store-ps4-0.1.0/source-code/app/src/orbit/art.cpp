// Orbit Store TV app - Game artwork: fetched through Orbit, decoded off the frame.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/art.hpp"
#include "orbit/perf_probe.hpp"

#include <algorithm>
#include <utility>
#include <vector>
#ifdef ORBIT_PERF_TRACE
#include "platform/ps4/system.hpp" // test-only art timing
#endif

namespace orbit
{

namespace
{

// The backend answers 202 while it fetches from the CDN; ask again shortly,
// for up to about half a minute.
constexpr double kPendingDelay = 0.6;
constexpr int kPendingAttempts = 50;
// More queued requests than this means the player scrolled past them.
constexpr std::size_t kMaxQueued = 48;

// Served before `b`: drawn more recently, or drawn as recently and asked for earlier.
template <typename Request> bool sooner(const Request &a, const Request &b)
{
    return a.frame != b.frame ? a.frame > b.frame : a.order < b.order;
}

std::string make_key(const std::string &game_id, api::ArtKind kind)
{
    return game_id + (kind == api::ArtKind::hero ? "#hero" : kind == api::ArtKind::thumbnail ? "#thumbnail" : "#cover");
}

api::ArtKind key_kind(const std::string &key)
{
    if (key.ends_with("#hero"))
        return api::ArtKind::hero;
    return key.ends_with("#thumbnail") ? api::ArtKind::thumbnail : api::ArtKind::cover;
}

} // namespace

ArtCache::ArtCache(http::Transport &transport, TextureSink &sink, ArtLimits limits)
    : transport_(transport), sink_(sink), limits_(limits)
{
}

ArtCache::~ArtCache()
{
    stop();
}

void ArtCache::start()
{
    const int workers = std::clamp(limits_.fetch_workers, 1, kMaxWorkers);
    for (int i = 0; i < workers; ++i)
        threads_[i].start([this] { worker(); });
}

void ArtCache::stop()
{
    {
        Lock lock(mutex_);
        stopping_ = true;
    }
    signal_.notify();
    for (Thread &thread : threads_)
        thread.join();
}

void ArtCache::set_token(const std::string &token)
{
    has_token_ = !token.empty();
    Lock lock(mutex_);
    token_ = token;
}

const Artwork *ArtCache::get(const std::string &game_id, api::ArtKind kind)
{
    const std::string key = make_key(game_id, kind);
    const double now = now_seconds();
    // Artwork is served to the console's session only: wait until it is open.
    if (!has_token_)
        return nullptr;
    auto found = entries_.find(key);
    if (found != entries_.end())
    {
        Entry &entry = found->second;
        entry.last_used = now;
        entry.drawn_frame = frame_;
        if (entry.state == State::ready)
            return &entry.art;
        if (entry.state == State::loading || now < entry.retry_at)
            return nullptr;
    }
    Entry &entry = entries_[key];
    entry.state = State::loading;
    entry.last_used = now;
    entry.drawn_frame = frame_;
    {
        Lock lock(mutex_);
        Request request;
        request.key = key;
        request.game_id = game_id;
        request.kind = kind;
        request.frame = frame_;
        request.order = next_order_++;
        requests_.push_back(std::move(request));
        // Past the limit, the one drawn longest ago is likely gone from the
        // screen. It is dropped and asked again if it is drawn again.
        while (requests_.size() > kMaxQueued)
        {
            auto last = requests_.begin();
            for (auto it = requests_.begin(); it != requests_.end(); ++it)
                if (sooner(*last, *it))
                    last = it;
            const std::string dropped = last->key;
            requests_.erase(last);
            inbox_.post(
                [this, dropped]
                {
                    auto it = entries_.find(dropped);
                    if (it != entries_.end() && it->second.state == State::loading)
                        entries_.erase(it);
                });
        }
    }
    signal_.notify();
    return nullptr;
}

void ArtCache::tick()
{
    inbox_.drain();
    {
        // Waiting requests learn which of them were drawn this frame.
        Lock lock(mutex_);
        for (Request &request : requests_)
        {
            auto found = entries_.find(request.key);
            if (found != entries_.end())
                request.frame = found->second.drawn_frame;
        }
    }
    ++frame_;
    const double now = now_seconds();
    for (int n = 0; n < limits_.uploads_per_frame && !uploads_.empty(); ++n)
    {
        Decoded decoded = std::move(uploads_.front());
        uploads_.pop_front();
        auto found = entries_.find(decoded.key);
        if (found == entries_.end())
            continue; // dropped while it was decoding
        Entry &entry = found->second;
        if (!decoded.ok)
        {
            entry.state = State::failed;
            entry.retry_at = now + limits_.retry_failed;
            continue;
        }
        ORBIT_PERF_SCOPE(art_upload);
        entry.art.texture = sink_.create(decoded.pixels);
        entry.art.width = decoded.pixels.width;
        entry.art.height = decoded.pixels.height;
        entry.art.ambient = decoded.ambient.empty() ? 0 : sink_.create(decoded.ambient);
        if (!decoded.ambient.empty())
        {
            // The cover's main colour, for Crossbar's background.
            std::uint64_t sum[3] = {};
            const std::vector<std::uint8_t> &px = decoded.ambient.rgba;
            const std::size_t count = px.size() / 4;
            for (std::size_t i = 0; i < count; ++i)
                for (int c = 0; c < 3; ++c)
                    sum[c] += px[i * 4 + static_cast<std::size_t>(c)];
            if (count > 0)
                entry.art.ambient_rgb = static_cast<std::uint32_t>(sum[0] / count) << 16 |
                                        static_cast<std::uint32_t>(sum[1] / count) << 8 |
                                        static_cast<std::uint32_t>(sum[2] / count);
        }
        entry.art.ready_at = now;
        entry.state = entry.art.texture != 0 ? State::ready : State::failed;
        if (entry.state == State::failed)
            entry.retry_at = now + limits_.retry_failed;
    }
    evict(api::ArtKind::cover, limits_.max_covers);
    evict(api::ArtKind::hero, limits_.max_heroes);
    evict(api::ArtKind::thumbnail, limits_.max_thumbnails);
}

void ArtCache::evict(api::ArtKind kind, int keep)
{
    std::vector<std::pair<double, std::string>> ready;
    for (const auto &[key, entry] : entries_)
    {
        if (entry.state == State::ready && key_kind(key) == kind)
            ready.emplace_back(entry.last_used, key);
    }
    if (static_cast<int>(ready.size()) <= keep)
        return;
    std::sort(ready.begin(), ready.end());
    const double now = now_seconds();
    const std::size_t extra = ready.size() - static_cast<std::size_t>(keep);
    for (std::size_t i = 0; i < extra; ++i)
    {
        // Never take away something drawn in the last frame or so.
        if (now - ready[i].first < 0.5)
            break;
        auto it = entries_.find(ready[i].second);
        sink_.destroy(it->second.art.texture);
        if (it->second.art.ambient != 0)
            sink_.destroy(it->second.art.ambient);
        entries_.erase(it);
    }
}

bool ArtCache::idle()
{
    inbox_.drain();
    Lock lock(mutex_);
    return requests_.empty() && in_flight_ == 0 && uploads_.empty();
}

void ArtCache::clear()
{
    for (auto &[key, entry] : entries_)
    {
        if (entry.state != State::ready)
            continue;
        sink_.destroy(entry.art.texture);
        if (entry.art.ambient != 0)
            sink_.destroy(entry.art.ambient);
    }
    entries_.clear();
    uploads_.clear();
}

bool ArtCache::work_once()
{
    Request request;
    std::string token;
    {
        Lock lock(mutex_);
        const double now = now_seconds();
        // What is on screen, in the order it was asked for (see art.hpp).
        auto pick = requests_.end();
        for (auto it = requests_.begin(); it != requests_.end(); ++it)
            if (it->not_before <= now && (pick == requests_.end() || sooner(*it, *pick)))
                pick = it;
        if (pick == requests_.end())
            return false;
        request = std::move(*pick);
        requests_.erase(pick);
        token = token_;
        ++in_flight_;
    }

    api::Client client(transport_);
    client.set_token(token);
#ifdef ORBIT_PERF_TRACE
    const std::int64_t perf_fetch = hui::sys::monotonic_us();
#endif
    api::Result<api::Art> art = client.art(request.game_id, request.cover_fallback ? api::ArtKind::cover : request.kind);
#ifdef ORBIT_PERF_TRACE
    const std::int64_t perf_decode = hui::sys::monotonic_us();
#endif
    Decoded decoded;
    decoded.key = request.key;
    const bool thumbnail = request.kind == api::ArtKind::thumbnail;
    const bool hero = request.kind == api::ArtKind::hero;
    const int width = thumbnail ? limits_.thumbnail_width : hero ? limits_.hero_width : limits_.cover_size;
    const int height = thumbnail ? limits_.thumbnail_height : hero ? limits_.hero_height : limits_.cover_size;
    const auto pending = [&] { return (art.ok() && art.value.pending) || art.status == 401 || art.status == 503; };
    if (art.ok() && !art.value.pending)
    {
        Lock decoding(decode_mutex_);
        decoded.ok = image::decode(art.value.bytes, width, height, &decoded.pixels);
    }
#ifdef ORBIT_PERF_TRACE
    std::size_t perf_queued = 0;
    {
        Lock lock(mutex_);
        perf_queued = requests_.size();
    }
    if (art.ok() && !art.value.pending)
        hui::sys::log("[PERF] art kind=%d fetch_ms=%lld decode_ms=%lld kb=%zu queued=%zu ok=%d", static_cast<int>(request.kind),
                      static_cast<long long>((perf_decode - perf_fetch) / 1000),
                      static_cast<long long>((hui::sys::monotonic_us() - perf_decode) / 1000), art.value.bytes.size() / 1024,
                      perf_queued, decoded.ok ? 1 : 0);
#endif
    if (thumbnail && !request.cover_fallback && !decoded.ok && !pending())
    {
        request.cover_fallback = true;
        art = client.art(request.game_id, api::ArtKind::cover);
        if (art.ok() && !art.value.pending)
        {
            Lock decoding(decode_mutex_);
            decoded.ok = image::decode(art.value.bytes, width, height, &decoded.pixels);
        }
    }
    // Still being fetched by the backend, the session is not open yet (a
    // restarted backend), or the artwork workers are busy or still starting
    // (503, as when the app has just started Orbit): ask again shortly
    // rather than giving up.
    const bool again = pending();
    if (again && request.attempts < kPendingAttempts)
    {
        Lock lock(mutex_);
        request.not_before = now_seconds() + kPendingDelay;
        ++request.attempts;
        requests_.push_back(std::move(request)); // keeps its place: picked by frame and order
        --in_flight_;
        return true;
    }
    if (decoded.ok && request.kind == api::ArtKind::cover)
    {
        Lock decoding(decode_mutex_);
        decoded.ambient = image::ambient(decoded.pixels);
    }
    inbox_.post([this, decoded = std::move(decoded)]() mutable
                { uploads_.push_back(std::move(decoded)); });
    {
        Lock lock(mutex_);
        --in_flight_;
    }
    return true;
}

void ArtCache::worker()
{
    for (;;)
    {
        {
            Lock lock(mutex_);
            if (stopping_)
                return;
        }
        if (work_once())
            continue;
        Lock lock(mutex_);
        if (stopping_)
            return;
        // Sleep until a request arrives or a pending one becomes due.
        double wait = 1.0;
        const double now = now_seconds();
        for (const Request &request : requests_)
            wait = std::min(wait, std::max(0.01, request.not_before - now));
        signal_.wait(mutex_, wait);
    }
}

} // namespace orbit
