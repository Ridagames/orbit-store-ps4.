// Orbit Store TV app - Console entry point.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the display, controller and sound, connects to the Orbit backend on
// this console's loopback address, then runs the storefront every frame:
// read input, update, play the sounds it asked for, draw, present. Adapted
// from ps5-homebrew-ui's src/main.cpp.

#include "audio/cues.hpp"
#include "audio/mixer.hpp"
#include "core/input.hpp"
#include "core/save_file.hpp"
#include "core/version.hpp"
#include "gfx/renderer.hpp"
#include "gfx/script.hpp"
#include "orbit/art.hpp"
#include "orbit/gl_textures.hpp"
#include "orbit/http.hpp"
#include "orbit/i18n.hpp"
#include "orbit/look.hpp"
#include "orbit/mark.hpp"
#include "orbit/perf_probe.hpp"
#include "orbit/screens.hpp"
#include "orbit/starter.hpp"
#include "orbit/store.hpp"
#include "orbit/ui.hpp"
#include "platform/ps4/audio_out.hpp"
#include "platform/ps4/display_egl.hpp"
#include "platform/ps4/native_capture.hpp"
#include "platform/ps4/pad.hpp"
#include "platform/ps4/provider_dialog.hpp"
#include "platform/ps4/system.hpp"
#include "ui/fonts.hpp"

#include "gfx/gl_headers.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <span>
#include <string>

extern "C" int sceSystemServiceLaunchWebBrowser(const char *uri, void *parameters);
extern "C" int sceSystemServiceParamGetInt(int param_id, int *value);

#ifdef ORBIT_PERF_TRACE
std::int64_t orbit::perf::now_us()
{
    return hui::sys::monotonic_us();
}
#endif

namespace
{

constexpr const char *kAssets = "/app0/assets";
constexpr const char *kBackend = "127.0.0.1";
constexpr std::uint16_t kPort = 34177; // config/network.json, as the backend
constexpr const char *kBrowserUrl = "http://127.0.0.1:34177/";

bool load_font(hui::gfx::Renderer &renderer, const char *name, hui::gfx::Font *font,
               hui::ui::FontRef *ref)
{
    std::string data;
    const std::string path = std::string(kAssets) + "/fonts/" + name;
    if (!hui::save::read_file(path, &data, hui::gfx::kFontFileLimit) || !font->load(data, false))
    {
        hui::sys::log("[ORBIT] font %s failed: %s", name, font->error().c_str());
        return false;
    }
    ref->font = font;
    ref->texture = renderer.batch().create_font_texture(*font);
    font->forget_atlas(); // only the GPU keeps the pixels
    if (ref->texture == 0) {
        ref->font = nullptr;
        hui::sys::log("[ORBIT] font upload failed: %s", name);
        return false;
    }
    return true;
}

std::string today()
{
    // Days since 1970 to a civil date (H. Hinnant's algorithm): the console's
    // C library has no gmtime_r.
    const long long days = static_cast<long long>(std::time(nullptr)) / 86400;
    const long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long day = doy - (153 * mp + 2) / 5 + 1;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    char text[16];
    std::snprintf(text, sizeof(text), "%04lld-%02lld-%02lld", year, month, day);
    return text;
}

} // namespace

int main()
{
    using namespace hui;
#ifdef ORBIT_PS4
    sys::initialize_logging();
#endif
    sys::log("[ORBIT] entry");
#ifdef ORBIT_PS4
    if (!sys::initialize_platform())
    {
        sys::log("[ORBIT] required system module unavailable; inspect native-startup.log");
        sys::park();
    }
#endif

    // 4K when the OpenGL runtime can choose its size, else its fixed profile.
    ps4::Display display;
    const bool modes = ps4::Display::supports_display_modes();
    if (!(modes && display.open(3840, 2160)) && !display.open(1920, 1080))
    {
        sys::startup_failure("display open failed");
    }
    sys::log("[ORBIT] display %dx%d", display.width(), display.height());

    gfx::Renderer renderer;
    gfx::Font light;
    gfx::Font regular;
    gfx::Font medium;
    gfx::Font semibold;
    orbit::ui::Type type;
    sys::log("[ORBIT] initializing renderer and fonts");
    if (!renderer.init() ||
        !load_font(renderer, "inter-display-light.huifont", &light, &type.light) ||
        !load_font(renderer, "inter-regular.huifont", &regular, &type.regular) ||
        !load_font(renderer, "inter-medium.huifont", &medium, &type.medium) ||
        !load_font(renderer, "inter-semibold.huifont", &semibold, &type.semibold))
    {
        sys::startup_failure("renderer or required font initialization failed");
    }
    // The other interfaces' faces. Without one, that interface draws in Inter.
    gfx::Font xlight;
    gfx::Font tight_light;
    gfx::Font tight_medium;
    gfx::Font sora;
    gfx::Font sora_semibold;
    gfx::Font tight_semibold;
    if (!load_font(renderer, "inter-tight-extralight.huifont", &xlight, &type.xlight))
        type.xlight = type.light;
    if (!load_font(renderer, "inter-tight-light.huifont", &tight_light, &type.tight_light))
        type.tight_light = type.light;
    if (!load_font(renderer, "inter-tight-medium.huifont", &tight_medium, &type.tight_medium))
        type.tight_medium = type.medium;
    if (!load_font(renderer, "sora-regular.huifont", &sora, &type.sora))
        type.sora = type.regular;
    if (!load_font(renderer, "sora-semibold.huifont", &sora_semibold, &type.sora_semibold))
        type.sora_semibold = type.semibold;
    if (!load_font(renderer, "inter-tight-semibold.huifont", &tight_semibold, &type.cinema))
        type.cinema = type.semibold;
    // The kit's six slots, for controller glyphs and the on-screen keyboard.
    ui::Fonts fonts;
    fonts.regular = type.regular;
    fonts.semibold = type.semibold;
    fonts.display = type.light;
    fonts.mono = type.medium;
    fonts.pixel = type.regular;
    fonts.hand = type.regular;

    orbit::GlTextures textures(renderer.batch());
    const std::uint32_t mark = textures.create(orbit::mark::render(128));
    const std::uint32_t loader_mark = textures.create(orbit::mark::render(256, 0.32f));

    ps4::Pad pad;
    sys::log("[ORBIT] opening controller");
    pad.open();
    pad.set_light_bar(0x2a, 0x6d, 0xf4);
    InputTracker tracker;
    audio::Mixer mixer;
    mixer.set_bus_gain(audio::Bus::ui, 0.7f);
    ps4::AudioOut audio_out;
    sys::log("[ORBIT] opening audio");
    audio_out.start(mixer);
    audio::SoundBank sounds;
    const auto bank = sounds.load(std::string(kAssets) + "/audio/sfx");
    sys::log("[ORBIT] sounds files=%d rejected=%d", bank.files, bank.rejected);

    orbit::http::SocketTransport transport(kBackend, kPort);
    orbit::http::SocketTransport art_transport(kBackend, kPort, 30000);
    // When Orbit isn't running, the app starts it through the jailbreak's ELF
    // loader: the copy Orbit saved, or the one in this package if newer.
    orbit::starter::LoaderStarter starter({}, orbit::starter::LoaderStarter::kLoaderPort,
                                          [](const std::string &line)
                                          { sys::log("[ORBIT] %s", line.c_str()); });
    // The interface and language chosen in App settings → Appearance, kept on this console.
    orbit::look::load(orbit::look::kPrefsPath);
    sys::log("[ORBIT] appearance=%s path=%s", orbit::look::title(orbit::look::current()), orbit::look::kPrefsPath);
    // The language, before anything shows or reports text: the one chosen, else the
    // console's own. SCE_SYSTEM_SERVICE_PARAM_ID_LANG is 1; English when Orbit has no catalogue.
    int system_language = 1;
    if (sceSystemServiceParamGetInt(1, &system_language) != 0)
        system_language = 1;
    orbit::i18n::set_system(orbit::i18n::from_system(system_language));
    const std::string language(orbit::look::prefs().language.empty() ? orbit::i18n::system()
                                                                      : orbit::look::prefs().language);
    if (!orbit::i18n::load(std::string(kAssets) + "/i18n", language))
        hui::sys::log("[ORBIT] language %s unavailable; using English", language.c_str());
    hui::sys::log("[ORBIT] language=%s system=%d", orbit::i18n::language().data(), system_language);

    // A language beyond Inter's scripts (Chinese, Japanese, Korean, Arabic)
    // brings its own font: one per language, loaded alone and kept only on
    // the GPU, as the CJK ones are 20-41 MB. Arabic also turns the interface
    // right to left.
    gfx::Font language_font;
    std::string language_font_file = "-";
    gfx::Font *const main_fonts[] = {&light,  &regular, &medium,        &semibold,      &xlight,
                                     &tight_light, &tight_medium, &sora, &sora_semibold, &tight_semibold};
    std::uint64_t language_revision = orbit::i18n::revision();
    const auto use_language_fonts = [&]
    {
        gfx::set_right_to_left(orbit::i18n::right_to_left());
        const std::string file(orbit::i18n::fallback_font(orbit::i18n::language()));
        if (file == language_font_file)
            return;
        language_font_file = file;
        for (gfx::Font *font : main_fonts)
            font->set_fallback(nullptr, 0);
        renderer.batch().set_fallback_font(nullptr);
        language_font = gfx::Font();
        if (file.empty())
            return;
        std::uint32_t handle = 0;
        {
            std::string packed;
            std::size_t size = 0;
            gfx::FontBytes bytes(nullptr, std::free);
            if (save::read_file(std::string(kAssets) + "/fonts/" + file, &packed, gfx::kFontFileLimit))
                bytes = gfx::inflate_font(packed, &size);
            std::string().swap(packed);
            if (bytes != nullptr && language_font.load(std::string_view(bytes.get(), size), false))
                handle = renderer.batch().set_fallback_font(&language_font);
            language_font.forget_atlas(); // the pixels go with bytes
        }
        if (handle == 0)
        {
            sys::log("[ORBIT] language font %s failed", file.c_str());
            return;
        }
        for (gfx::Font *font : main_fonts)
            font->set_fallback(&language_font, handle);
        sys::log("[ORBIT] language font %s", file.c_str());
    };
    use_language_fonts();

    ps4::ProviderDialog provider_dialog;
    orbit::Store store(transport, &starter);
    if (provider_dialog.initialize()) store.set_provider_window(&provider_dialog);
    // Three fetches at once: each new connection to Orbit waits about 100 ms
    // before the rest of an image over 16 KB arrives (PS4, 16 November 2026),
    // so one fetch at a time left the decoder idle most of the time.
    orbit::ArtLimits art_limits;
    art_limits.fetch_workers = 3;
    orbit::ArtCache art(art_transport, textures, art_limits);
    store.start();
    art.start();

    // SCE_SYSTEM_SERVICE_PARAM_ID_TIME_ZONE (4, minutes) and _SUMMERTIME (5), for
    // Crossbar's ribbons that follow the time of day.
    int zone = 0;
    int summer = 0;
    if (sceSystemServiceParamGetInt(4, &zone) != 0)
        zone = 0;
    if (sceSystemServiceParamGetInt(5, &summer) != 0)
        summer = 0;
    orbit::look::set_utc_offset(zone + (summer != 0 ? 60 : 0));
    orbit::Context context(type, fonts, store, art);
    context.mark = mark;
    context.loader_mark = loader_mark;
    context.today = today();
    context.app_version = read_content_version("/app0/sce_sys/param.json");
    context.open_browser = []
    { return sceSystemServiceLaunchWebBrowser(kBrowserUrl, nullptr) == 0; };
    context.close_app = [] { hui::sys::quit(); };
    context.open_support = [](const std::string &url)
    { return url.rfind("https://", 0) == 0 && sceSystemServiceLaunchWebBrowser(url.c_str(), nullptr) == 0; };
    context.open_download = [](const orbit::BrowserSelection &selection)
    {
        const std::string url = orbit::browser_handoff_url(selection);
        return !url.empty() && sceSystemServiceLaunchWebBrowser(url.c_str(), nullptr) == 0;
    };
    orbit::App app(context);
    orbit::Frame frame;
    frame.glass_texture = renderer.glass_texture();
    sys::log("[ORBIT] ready version=%s", context.app_version.c_str());

    std::int64_t last = sys::monotonic_us();
    std::uint64_t frames = 0;
    std::int64_t sample_start = last, update_us = 0, draw_us = 0, swap_us = 0;
    unsigned sample_frames = 0, timing_samples = 0;
    PadSample samples[64];
    ui::Feedback feedback;
    for (;;)
    {
        if (frames == 0) sys::log("[ORBIT] first frame update");
        const std::int64_t now = sys::monotonic_us();
        float dt = frames == 0 ? 1.0f / 60.0f : static_cast<float>(now - last) / 1e6f;
        const auto last_frame_time = last;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f; // a hitch must not teleport the animations
#ifdef ORBIT_PERF_TRACE
        const std::int64_t perf_input_start = orbit::perf::now_us();
#endif
        const std::size_t count = pad.read(samples);
        InputFrame input = tracker.update(std::span<const PadSample>(samples, count),
                                                static_cast<std::uint64_t>(now));
#ifdef ORBIT_NATIVE_CAPTURE
        ps4::read_requested_input(input);
#endif
#ifdef ORBIT_PERF_TRACE
        const std::int64_t perf_input_us = orbit::perf::now_us() - perf_input_start;
#endif
#ifdef ORBIT_PERF_TRACE
        // Test-only frame pacing for UI speed checks: one line a second with the
        // screen, plus every frame slower than 50 ms. Never in release packages.
        constexpr unsigned kTimingSamples = 1000000;
        {
            static std::int64_t perf_start = now, perf_sum = 0, perf_max = 0;
            static unsigned perf_frames = 0, perf_late = 0, perf_hitches = 0;
            const std::int64_t frame_us = now - last_frame_time;
            const int perf_look = static_cast<int>(orbit::look::prefs().interface);
            if (frames > 0)
            {
                ++perf_frames;
                perf_sum += frame_us;
                perf_max = frame_us > perf_max ? frame_us : perf_max;
                perf_late += frame_us > 17500 ? 1 : 0;
                perf_hitches += frame_us > 34000 ? 1 : 0;
                if (frame_us > 50000)
                {
                    // Where the slow frame went, in tenths of a millisecond.
                    char parts[384];
                    int used = 0;
                    for (int s = 0; s < orbit::perf::kSlots && used < static_cast<int>(sizeof parts); ++s)
                        used += std::snprintf(parts + used, sizeof parts - static_cast<std::size_t>(used), " %s=%lld",
                                              orbit::perf::kSlotNames[s],
                                              static_cast<long long>(orbit::perf::slot_us[s] / 100));
                    sys::log("[PERF] slow frame_ms=%lld tab=%d game=%d look=%d%s", static_cast<long long>(frame_us / 1000),
                             app.current_tab(), app.game_page_open() ? 1 : 0, perf_look, parts);
                }
            }
            for (std::int64_t &slot : orbit::perf::slot_us)
                slot = 0;
            orbit::perf::slot_us[orbit::perf::input] = perf_input_us;
            if (now - perf_start >= 1000000 && perf_frames)
            {
                sys::log("[PERF] fps=%u mean_ms=%.1f max_ms=%.1f late=%u hitches=%u tab=%d game=%d look=%d tex_mb=%zu",
                         perf_frames, static_cast<double>(perf_sum) / perf_frames / 1000.0,
                         static_cast<double>(perf_max) / 1000.0, perf_late, perf_hitches, app.current_tab(),
                         app.game_page_open() ? 1 : 0, perf_look, textures.live_megabytes());
                perf_start = now;
                perf_sum = perf_max = 0;
                perf_frames = perf_late = perf_hitches = 0;
            }
        }
#else
        constexpr unsigned kTimingSamples = 60;
#endif
        if (input.nav != Direction::none && timing_samples < kTimingSamples)
            sys::log("[ORBIT] navigation=%d frame_ms=%lld", static_cast<int>(input.nav),
                     static_cast<long long>((now - last_frame_time) / 1000));
        feedback.clear();
        // A provider dialog owns the controller until FINISHED, including the
        // frame it closes. Keep draining service updates while it is visible.
        const bool provider_visible = store.provider_dialog_visible();
        {
            ORBIT_PERF_SCOPE(store_tick);
            store.tick();
        }
        {
            ORBIT_PERF_SCOPE(screen_update);
            app.update(provider_visible || store.provider_dialog_visible() ? InputFrame{} : input, dt, feedback);
        }
        // A language chosen in App settings may need another font or direction.
        if (orbit::i18n::revision() != language_revision)
        {
            language_revision = orbit::i18n::revision();
            use_language_fonts();
        }
        {
            ORBIT_PERF_SCOPE(audio);
            for (const audio::CueEvent &event : feedback.cues)
                sounds.play(mixer, audio::SoundSet::glass, event);
            if (feedback.rumble_strength > 0.0f)
                pad.rumble(feedback.rumble_strength, feedback.rumble_seconds);
            pad.tick(dt);
        }

        frame.reset();
        const auto draw_start = sys::monotonic_us();
        update_us += draw_start - now;
        if (frames == 0) sys::log("[ORBIT] first frame draw");
        {
            ORBIT_PERF_SCOPE(screen_draw);
            app.draw(frame);
        }
#ifdef ORBIT_PERF_TRACE
        const std::int64_t perf_render_start = orbit::perf::now_us();
#endif
        renderer.begin();
        renderer.backdrop(frame.backdrop);
        renderer.draw(frame.scene);
        if (frame.glass)
            renderer.glass();
        renderer.draw(frame.overlay);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        renderer.present(0, display.width(), display.height());
#ifdef ORBIT_PERF_TRACE
        orbit::perf::slot_us[orbit::perf::render] += orbit::perf::now_us() - perf_render_start;
#endif
#ifdef ORBIT_NATIVE_CAPTURE
        ps4::capture_requested_frame(display.width(), display.height());
#endif
        if (frames == 0)
        {
            // PS4 Piglet must release the shell's splash before its first swap.
            // Waiting until after the swap can leave the title blocked there.
            sys::log("[ORBIT] releasing splash before first swap");
            if (!sys::hide_splash_screen())
                sys::startup_failure("could not release the system splash screen");
            sys::log("[ORBIT] first frame swap");
        }
        const auto swap_start = sys::monotonic_us();
        draw_us += swap_start - draw_start;
#ifdef ORBIT_PERF_TRACE
        const bool swapped = [&]
        {
            ORBIT_PERF_SCOPE(swap);
            return display.swap();
        }();
        if (!swapped)
#else
        if (!display.swap())
#endif
        {
            sys::log("[ORBIT] fatal: swap failed frame=%llu error=%s",
                     static_cast<unsigned long long>(frames),
                     ps4::egl_error_name(display.last_error()));
            sys::park();
        }
        if (++frames == 1)
        {
            sys::log("[ORBIT] first frame presented");
            sys::log_started();
        }
        swap_us += sys::monotonic_us() - swap_start;
        if (++sample_frames >= 120 && timing_samples < kTimingSamples)
        {
            sys::log("[ORBIT] frames=%u elapsed_ms=%lld update_us=%lld draw_us=%lld swap_us=%lld",
                     sample_frames, static_cast<long long>((sys::monotonic_us() - sample_start) / 1000),
                     static_cast<long long>(update_us / sample_frames),
                     static_cast<long long>(draw_us / sample_frames),
                     static_cast<long long>(swap_us / sample_frames));
            ++timing_samples; sample_frames = 0; sample_start = sys::monotonic_us();
            update_us = draw_us = swap_us = 0;
        }
        // Piglet's interval-1 swap can block on PS4. Cap the interval-0 loop
        // without adding another full-frame delay after expensive rendering.
        constexpr std::int64_t frame_budget_us = 1000000 / 60;
        const std::int64_t elapsed = sys::monotonic_us() - now;
        if (elapsed >= 0 && elapsed < frame_budget_us)
        {
            ORBIT_PERF_SCOPE(idle);
            sys::sleep_us(static_cast<std::uint32_t>(frame_budget_us - elapsed));
        }
    }
}
