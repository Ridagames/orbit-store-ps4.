// Orbit Store TV app - Starting Orbit when it isn't running.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/starter.hpp"
#include "orbit/i18n.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

namespace orbit::starter
{

namespace
{

constexpr long long kMaxPayload = 64ll << 20;

// major.minor.patch, then 1 for a release or 0 for a beta, then the beta number.
bool version_parts(std::string_view text, std::array<unsigned, 5> *parts)
{
    if (!text.empty() && text.front() == 'v')
        text.remove_prefix(1);
    *parts = {0, 0, 0, 1, 0};
    for (int i = 0; i < 3; ++i)
    {
        unsigned value = 0;
        int digits = 0;
        while (!text.empty() && text.front() >= '0' && text.front() <= '9')
        {
            if (++digits > 6)
                return false;
            value = value * 10 + static_cast<unsigned>(text.front() - '0');
            text.remove_prefix(1);
        }
        if (digits == 0)
            return false;
        (*parts)[i] = value;
        if (i < 2)
        {
            if (text.empty() || text.front() != '.')
                return false;
            text.remove_prefix(1);
        }
    }
    if (text.empty())
        return true;
    constexpr std::string_view kBeta = "-beta.";
    if (text.substr(0, kBeta.size()) != kBeta)
        return false;
    text.remove_prefix(kBeta.size());
    (*parts)[3] = 0;
    unsigned value = 0;
    int digits = 0;
    while (!text.empty() && text.front() >= '0' && text.front() <= '9')
    {
        if (++digits > 6)
            return false;
        value = value * 10 + static_cast<unsigned>(text.front() - '0');
        text.remove_prefix(1);
    }
    (*parts)[4] = value;
    return digits > 0 && text.empty();
}

std::string read_small(const std::string &path)
{
    std::string text;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return text;
    char buffer[64];
    const ssize_t n = read(fd, buffer, sizeof(buffer));
    close(fd);
    if (n > 0)
        text.assign(buffer, static_cast<std::size_t>(n));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

// A regular file that starts like Orbit: ELF64, little-endian, x86-64.
bool readable_elf(const std::string &path, long long *bytes)
{
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat info
    {
    };
    unsigned char head[20] = {};
    const bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 64 &&
                    info.st_size <= kMaxPayload &&
                    read(fd, head, sizeof(head)) == static_cast<ssize_t>(sizeof(head)) &&
                    std::memcmp(head, "\177ELF\2\1", 6) == 0 && head[18] == 62 && head[19] == 0;
    close(fd);
    if (ok)
        *bytes = static_cast<long long>(info.st_size);
    return ok;
}

 // elfldr.bin is a raw GoldHEN BinLoader bootstrap, not an ELF file.
bool readable_bin_payload(const std::string &path, long long *bytes)
{
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    struct stat info
    {
    };
    const bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) &&
                    info.st_size >= 64 && info.st_size <= (16ll << 20);
    close(fd);
    if (ok)
        *bytes = static_cast<long long>(info.st_size);
    return ok;
}

Payload candidate(const std::string &elf, const std::string &version, const char *source)
{
    Payload payload;
    if (!readable_elf(elf, &payload.bytes))
        return payload;
    payload.path = elf;
    payload.version = read_small(version);
    payload.source = source;
    return payload;
}

const char *outcome_name(Outcome outcome)
{
    switch (outcome)
    {
    case Outcome::sent:
        return "sent";
    case Outcome::no_loader:
        return "no loader";
    case Outcome::no_payload:
        return "no payload";
    case Outcome::failed:
        break;
    }
    return "failed";
}

} // namespace

int compare_versions(std::string_view a, std::string_view b)
{
    std::array<unsigned, 5> x{};
    std::array<unsigned, 5> y{};
    const bool valid_a = version_parts(a, &x);
    const bool valid_b = version_parts(b, &y);
    if (!valid_a || !valid_b)
        return valid_a == valid_b ? 0 : valid_a ? 1 : -1;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (x[i] != y[i])
            return x[i] > y[i] ? 1 : -1;
    }
    return 0;
}

Payload choose(const Locations &where)
{
    Payload saved = candidate(where.saved_elf, where.saved_version, "saved");
    Payload bundled = candidate(where.bundled_elf, where.bundled_version, "bundled");
    if (saved.path.empty())
        return bundled;
    if (bundled.path.empty())
        return saved;
    // The saved copy is what payload managers start and what Orbit's updater
    // refreshes, so it wins unless this package carries a newer release.
    return compare_versions(bundled.version, saved.version) > 0 ? bundled : saved;
}

Outcome send(const std::string &path, std::uint16_t port, int timeout_ms, std::string *error)
{
    const int file = open(path.c_str(), O_RDONLY);
    if (file < 0)
    {
        *error = tr("Could not read {path}.", {{"path", path}});
        return Outcome::no_payload;
    }
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        close(file);
        *error = tr("Could not open a connection on this console.");
        return Outcome::failed;
    }
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    const int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(port);
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof(peer)) != 0)
    {
        const bool refused = errno == ECONNREFUSED;
        close(fd);
        close(file);
        *error = refused ? tr("No ELF loader is listening on port {port}.",
                              {{"port", std::to_string(port)}})
                         : tr("Could not reach the ELF loader on port {port}.",
                              {{"port", std::to_string(port)}});
        return refused ? Outcome::no_loader : Outcome::failed;
    }

    std::vector<char> buffer(64u << 10);
    Outcome outcome = Outcome::sent;
    for (;;)
    {
        const ssize_t got = read(file, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR)
            continue;
        if (got < 0)
        {
            *error = tr("Could not read {path}.", {{"path", path}});
            outcome = Outcome::failed;
            break;
        }
        if (got == 0)
            break;
        std::size_t sent = 0;
        while (sent < static_cast<std::size_t>(got))
        {
#ifdef MSG_NOSIGNAL
            const ssize_t n = ::send(fd, buffer.data() + sent, got - sent, MSG_NOSIGNAL);
#else
            const ssize_t n = ::send(fd, buffer.data() + sent, got - sent, 0);
#endif
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
            {
                *error = tr("The ELF loader stopped taking Orbit part way.");
                outcome = Outcome::failed;
                break;
            }
            sent += static_cast<std::size_t>(n);
        }
        if (outcome != Outcome::sent)
            break;
    }
    close(file);
    if (outcome == Outcome::sent)
    {
        // The loader starts the payload once it reads the end of the file.
        // Wait for it to close its side, so nothing is cut short.
        shutdown(fd, SHUT_WR);
        char ignored[256];
        while (recv(fd, ignored, sizeof(ignored), 0) > 0)
        {
        }
    }
    close(fd);
    return outcome;
}

LoaderStarter::LoaderStarter(Locations where, std::uint16_t port,
                             std::function<void(const std::string &)> log,
                             std::uint16_t goldhen_port, int elfldr_wait_ms)
    : where_(std::move(where)), port_(port), log_(std::move(log)), goldhen_port_(goldhen_port),
      elfldr_wait_ms_(elfldr_wait_ms)
{
}

std::string LoaderStarter::bundled_version() const
{
    return candidate(where_.bundled_elf, where_.bundled_version, "bundled").version;
}

Result LoaderStarter::start()
{
    Result result;
    bool started_elfldr = false;
    const Payload payload = choose(where_);
    if (payload.path.empty())
    {
        result.outcome = Outcome::no_payload;
        result.detail = tr("No copy of Orbit was found to start.");
    }
    else
    {
        result.version = payload.version;
        std::string error;
        result.outcome = send(payload.path, port_, 10000, &error);
        // A refused connection sent no bytes, so starting elfldr can't run Orbit twice.
        // GoldHEN's PayLoader gets elfldr only (never Orbit, too big for it), and only
        // when a copy can be read: an empty connection would wedge it.
        if (result.outcome == Outcome::no_loader && goldhen_port_ != 0)
        {
            long long elfldr_bytes = 0;
            const std::string elfldr = readable_bin_payload(where_.saved_elfldr_bin, &elfldr_bytes)
                                            ? where_.saved_elfldr_bin
                                        : readable_bin_payload(where_.bundled_elfldr_bin, &elfldr_bytes)
                                            ? where_.bundled_elfldr_bin
                                            : std::string();
            std::string loader_error;
            if (!elfldr.empty() && send(elfldr, goldhen_port_, 10000, &loader_error) == Outcome::sent)
            {
                started_elfldr = true;
                if (log_)
                    log_("elfldr.bin: full bootstrap transfer accepted by GoldHEN BinLoader 9090; execution is not yet confirmed; waiting for listener 9021");
                // Send Orbit once elfldr listens; until then its port refuses, so no
                // bytes go anywhere.
                result.outcome = Outcome::no_loader;
                int waited = 0;
                for (; result.outcome == Outcome::no_loader && waited < elfldr_wait_ms_; waited += 250)
                {
                    usleep(250000);
                    result.outcome = send(payload.path, port_, 10000, &error);
                }
                if (log_)
                {
                    char phase[192];
                    if (result.outcome == Outcome::sent)
                        std::snprintf(phase, sizeof(phase), "elfldr: port %u accepted Orbit after approximately %d ms",
                                      port_, waited + 250);
                    else
                        std::snprintf(phase, sizeof(phase), "elfldr: port %u did not accept Orbit within %d ms (outcome=%s)",
                                      port_, elfldr_wait_ms_, outcome_name(result.outcome));
                    log_(phase);
                }
                if (result.outcome == Outcome::no_loader)
                    error = tr("GoldHEN's payload loader didn't start Orbit's elfldr. Check that "
                               "GoldHEN's PayLoader Server is on, or start an ELF loader.");
            }
        }
        result.detail = result.outcome == Outcome::sent
                            ? tr("Sent Orbit {version} to the ELF loader.",
                                 {{"version", payload.version.empty()
                                                  ? std::string(tr("(version unknown)"))
                                                  : payload.version}})
                            : error;
    }
    if (log_)
    {
        char line[320];
        std::snprintf(line, sizeof(line), "start: %s copy=%s version=%s bytes=%lld elfldr=%s: %s",
                      outcome_name(result.outcome), payload.source,
                      payload.version.empty() ? "?" : payload.version.c_str(), payload.bytes,
                      started_elfldr ? "started" : "no", result.detail.c_str());
        log_(line);
    }
    return result;
}

} // namespace orbit::starter
