// Orbit Store TV app - A stand-in Orbit backend for PC previews.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Answers the API calls the app makes with invented games, two drives and a
// queue in every state, and draws simple cover art as PNG. Nothing here is a
// real product or real artwork. Preview only: never part of the console app.

#pragma once

#include "orbit/http.hpp"
#include "orbit/thread.hpp"

#include <array>
#include <string>
#include <vector>

namespace orbit::host
{

enum class FixtureMode
{
    normal,  // a catalogue, drives, a queue, favourites and a Library
    power_countdown, // completed work with a cancellable shutdown countdown
    debrid,  // normal fixture plus a connected TorBox account
    offline, // Orbit is not running
    setup,   // sources not chosen yet
    updates, // as normal, with newer releases of Orbit and the TV app
    // As normal, with PS4 game updates and DLC: a PS4 game that isn't installed with an
    // update and two DLC (Get everything), and PS4 Library games with updates.
    game_updates,
};

class Fixture final : public http::Transport
{
  public:
    explicit Fixture(FixtureMode mode);
    http::Response send(const http::Request &request) override;
    // Tests: the TV app's look copy the service keeps.
    const std::string &tv_look() const { return tv_look_; }
    void set_tv_look(std::string text) { tv_look_ = std::move(text); }

  private:
    struct Job
    {
        std::string id;
        std::string release;
        std::string game;
        std::string title_id;
        std::string title;
        std::string storage;
        std::string filename;
        std::string format;
        std::string status;
        std::string error;
        double received = 0.0;
        double total = 0.0;
        double speed = 0.0;
        double retry_at = 0.0;
        std::string delivery = "direct";
        std::string update_version, base_game, base_title; // an update: its version and game
    };
    std::string jobs_json() const;
    // Get everything and Update all (/api/v1/bundles), added at once except for a
    // provider-page item, which waits (and everything after it) until the bundle stops.
    struct BundleItem
    {
        std::string release, title, kind, state, job;
    };
    std::string bundle_id_;
    std::string bundle_state_ = "idle";
    std::vector<BundleItem> bundle_items_;
    int bundle_count_ = 0;
    std::string bundle_json() const;
    std::string updates_json() const;
    std::string addons_json() const;
    std::string art(const std::string &game, bool hero) const;

    std::string library_json() const;
    std::string files_json(const std::string &storage, const std::string &path) const;
    std::string favourites_json() const;
    std::string sources_json() const;
    std::string events_json() const; // {downloadFinished: bool, …}
    std::string catalogue_json() const;
    std::string service_json() const;
    std::string tv_app_json() const;

    FixtureMode mode_;
    Mutex mutex_;
    std::vector<Job> jobs_;
    int next_job_ = 100;
    std::vector<std::string> favourites_{"tidewater"};
    // Browse's saved filters, each as its JSON (Saved filters in every look).
    std::vector<std::string> saved_filters_{
        "{\"id\":\"f0000000000000001\",\"name\":\"Small PS5 exFAT\",\"page\":\"games\",\"filters\":{\"query\":\"\","
        "\"source\":[],\"format\":[\"exFAT\"],\"tags\":[],\"region\":\"\",\"platform\":\"ps5\",\"size\":\"small\","
        "\"collection\":\"all\",\"hideInstalled\":true,\"sort\":\"title\",\"addonType\":\"\"}}",
        "{\"id\":\"f0000000000000002\",\"name\":\"Co-op nights\",\"page\":\"games\",\"filters\":{\"query\":\"\","
        "\"source\":[\"archive\"],\"format\":[],\"tags\":[\"local-coop\"],\"region\":\"EUR\",\"platform\":\"\",\"size\":\"\","
        "\"collection\":\"all\",\"hideInstalled\":false,\"sort\":\"release\",\"addonType\":\"\"}}"};
    int next_filter_ = 3;
    std::string library_action_; // the last accepted Library action, as JSON
    std::string file_operation_ = "null"; // the last file operation, as JSON
    std::string browser_state_ = "idle";
    std::string archive_name_; // signed in to Archive.org when set
    std::string ntfy_server_, ntfy_topic_; // ntfy notifications, on when both are set
    std::string browser_id_;
    std::string browser_title_;
    int browser_count_ = 0;

    // Settings and updates.
    std::vector<std::string> enabled_;
    bool acknowledged_ = false;
    bool power_enabled_ = false;
    std::string power_job_;
    std::string history_files_ = "ask";
    std::string preferred_ = "usb0";
    double catalogue_checked_ = 0.0; // Unix seconds
    double catalogue_after_ = 0.0;
    std::string running_ = "0.6.0"; // Orbit
    std::string saved_ = "0.6.0";
    double service_checked_ = 0.0;
    std::string service_phase_ = "idle";
    double stopped_until_ = 0.0; // after Stop Orbit: refused until then (steady seconds)
    std::string tv_installed_ = "1.0.0";
    std::string tv_look_; // the TV app's look copy (/api/v1/tv-app/look)
    double tv_checked_ = 0.0;
    std::string tv_phase_ = "idle";
    bool update_all_ = false;  // the last update was "Update Orbit"
    std::string tv_updated_;   // the TV app version it installed
    bool notifications_ = true; // notifications on the console
    std::array<bool, 5> events_{true, true, true, true, true}; // kNotificationEvents chosen
    bool delete_packages_ = false; // delete a package once it is installed
    bool delete_archives_ = true, extract_archives_ = true; // /api/v1/settings/extraction
    int connections_ = 8;          // connections per download
};

} // namespace orbit::host
