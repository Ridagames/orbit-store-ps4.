// Orbit Store TV app - Get everything and Update available: the choices shared with the
// browser version (src/bundle.ts) and Orbit Zero.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "orbit/model.hpp"
#include "orbit/json.hpp"

#include <gtest/gtest.h>

namespace orbit
{
namespace
{

std::vector<Release> releases(const char *text)
{
    json::Value value;
    EXPECT_TRUE(json::parse(text, &value));
    return parse_catalog(value);
}

// Two builds of update 1.27 (native for 9.00, backport for 6.72), 1.31 that needs 11.50 and
// an older 1.20, all for CUSA90020; one DLC for it and one for another game.
const char *kUpdates = R"([
 {"id":"u127-vk","gameId":"update-cusa90020-1-27-900","titleId":"CUSA90020","platform":"ps4","title":"Skyward",
  "sourceId":"vikingfile","provider":"Vikingfile","format":"PKG","sizeBytes":12000000000,"updateVersion":"1.27",
  "requiredFirmware":"9.00","browserAvailable":true,"baseGameId":"skyward-ps4","updateId":"update-cusa90020-1-27-900"},
 {"id":"u127-ar","gameId":"update-cusa90020-1-27-672","titleId":"CUSA90020","platform":"ps4","title":"Skyward",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":11000000000,"updateVersion":"1.27",
  "requiredFirmware":"6.72","backport":true,"baseGameId":"skyward-ps4","updateId":"update-cusa90020-1-27-672"},
 {"id":"u131","gameId":"update-cusa90020-1-31-1150","titleId":"CUSA90020","platform":"ps4","title":"Skyward",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":13000000000,"updateVersion":"1.31",
  "requiredFirmware":"11.50","firmwareIssue":"newer-firmware","baseGameId":"skyward-ps4","updateId":"update-cusa90020-1-31-1150"},
 {"id":"u120","gameId":"update-cusa90020-1-20-505","titleId":"CUSA90020","platform":"ps4","title":"Skyward",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":9000000000,"updateVersion":"1.20",
  "requiredFirmware":"5.05","baseGameId":"skyward-ps4","updateId":"update-cusa90020-1-20-505"}
])";
const char *kAddons = R"([
 {"id":"dlc-royal","gameId":"royal-pack","addonId":"royal-pack","titleId":"CUSA90020","platform":"ps4","title":"Royal Pack",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":6000000000,"baseGameId":"skyward-ps4"},
 {"id":"dlc-other","gameId":"other-pack","addonId":"other-pack","titleId":"CUSA90099","platform":"ps4","title":"Other Pack",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":1000000000,"baseGameId":"other-ps4"}
])";
const char *kGame = R"([
 {"id":"skyward-ar","gameId":"skyward-ps4","titleId":"CUSA90020","platform":"ps4","title":"Skyward",
  "sourceId":"archive","provider":"Archive.org","format":"PKG","sizeBytes":46000000000}
])";

TEST(GameUpdates, VersionsCompareAsNumbers)
{
    EXPECT_LT(compare_game_versions("1.05", "1.27"), 0);
    EXPECT_GT(compare_game_versions("11.00", "9.00"), 0);
    EXPECT_EQ(compare_game_versions("1.5", "1.50"), 0);
    EXPECT_LT(compare_game_versions("", "1.00"), 0);
}

TEST(GameUpdates, TheNewestUpdateThisFirmwareRunsWinsAndANativeBuildBeatsABackport)
{
    const std::vector<Game> updates = group_games(releases(kUpdates));
    const Game *on_900 = newest_update(updates, "CUSA90020", "9.00");
    ASSERT_NE(on_900, nullptr);
    EXPECT_EQ(on_900->id, "update-cusa90020-1-27-900"); // 1.31 needs 11.50
    const Game *on_672 = newest_update(updates, "CUSA90020", "6.72");
    ASSERT_NE(on_672, nullptr);
    EXPECT_EQ(on_672->id, "update-cusa90020-1-27-672"); // only the backport runs
    const Game *on_505 = newest_update(updates, "CUSA90020", "5.05");
    ASSERT_NE(on_505, nullptr);
    EXPECT_EQ(on_505->id, "update-cusa90020-1-20-505");
    EXPECT_EQ(newest_update(updates, "CUSA90099", "9.00"), nullptr);
}

TEST(GameUpdates, EverythingListsTheGameItsUpdateAndItsOwnDlcInOrder)
{
    const std::vector<Game> games = group_games(releases(kGame));
    const std::vector<Game> updates = group_games(releases(kUpdates));
    const std::vector<Game> addons = group_games(releases(kAddons));
    ASSERT_EQ(games.size(), 1u);
    const Release &option = games[0].releases[0];
    std::vector<EverythingRow> rows = everything_rows(games[0], option, updates, addons, {}, "9.00", "", "");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].kind, EverythingRow::Kind::game);
    EXPECT_FALSE(rows[0].installed);
    EXPECT_EQ(rows[1].kind, EverythingRow::Kind::update);
    EXPECT_EQ(rows[1].release->update_version, "1.27");
    EXPECT_EQ(rows[2].kind, EverythingRow::Kind::dlc);
    EXPECT_EQ(rows[2].release->id, "dlc-royal");
    EXPECT_TRUE(everything_offered(rows));
    EXPECT_EQ(everything_summary(rows), "Game, update 1.27 and 1 DLC");

    // Installed at 1.27 already: the game row is marked and no update is offered.
    LibraryGame copy;
    copy.title_id = "CUSA90020";
    copy.installed = true;
    copy.installed_version = "1.27";
    rows = everything_rows(games[0], option, updates, addons, {copy}, "9.00", "", "");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_TRUE(rows[0].installed);
    EXPECT_EQ(rows[1].kind, EverythingRow::Kind::dlc);
    EXPECT_EQ(everything_summary(rows), "1 DLC"); // the installed game isn't counted

    // Installed at 1.05: the update and the DLC, without the game.
    copy.installed_version = "1.05";
    rows = everything_rows(games[0], option, updates, addons, {copy}, "9.00", "", "");
    EXPECT_EQ(everything_summary(rows), "Update 1.27 and 1 DLC");
}

TEST(GameUpdates, LibraryOffersOneUpdatePerGameAsTheServiceNamedIt)
{
    const std::vector<Game> updates = group_games(releases(kUpdates));
    LibraryGame a, b, none;
    a.title = "Skyward";
    a.title_id = "CUSA90020";
    a.update_id = "update-cusa90020-1-27-900";
    a.update_version = "1.27";
    b = a; // the same game on a second drive
    b.source_key = "other";
    none.title = "Nothing";
    none.title_id = "CUSA90030";
    const std::vector<LibraryUpdate> list = library_updates({a, b, none}, updates, "", "");
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].release->id, "u127-vk");
    // An update the catalogue no longer lists is left out.
    a.update_id = "update-gone";
    EXPECT_EQ(library_update(a, updates, "", "").release, nullptr);
}

TEST(GameUpdates, BundleStatusReadsItsItems)
{
    json::Value value;
    ASSERT_TRUE(json::parse(R"({"id":"b1","state":"page","items":[
        {"releaseId":"g","kind":"game","state":"added","jobId":"j1"},
        {"releaseId":"u","kind":"update","state":"page"},
        {"releaseId":"d","kind":"dlc","state":"pending"}]})", &value));
    const Bundle bundle = parse_bundle(value);
    EXPECT_TRUE(bundle.running());
    EXPECT_EQ(bundle.added(), 1);
    ASSERT_NE(bundle.item("u"), nullptr);
    EXPECT_EQ(std::string(bundle_state_label(bundle.item("u")->state)), "Open its page on your PS4");
    EXPECT_EQ(std::string(bundle_state_label("skipped")), "Not added");
}

} // namespace
} // namespace orbit
