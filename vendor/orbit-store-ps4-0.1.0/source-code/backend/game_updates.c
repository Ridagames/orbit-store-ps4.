/* Game updates from the catalogue: which one this console can use, and Library's
 * "Update available" facts. Nothing here starts a download or reads the drive. */
#include "game_updates.h"
#include "firmware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int game_version_value(const char *v) {
    if (!v) return -1;
    if (*v == 'v' || *v == 'V') v++;
    size_t major = strspn(v, "0123456789");
    if (!major || major > 3 || v[major] != '.') return -1;
    const char *minor = v + major + 1;
    if (strspn(minor, "0123456789") != 2 || minor[2]) return -1;
    return atoi(v) * 100 + atoi(minor);
}
void game_version_text(const char *version, char *out, size_t cap) {
    int value = game_version_value(version);
    if (!cap) return;
    if (value < 0) *out = 0;
    else snprintf(out, cap, "%d.%02d", value / 100, value % 100);
}
bool game_firmware_runs(uint32_t raw, const char *required) {
    int need = game_version_value(required);
    if (need < 0) return false;
    if (!firmware_known(raw)) return true;
    char current[16];
    firmware_text(raw, current, sizeof current);
    return game_version_value(current) >= need;
}
const Release *game_update_best_locked(const char *title_id, uint32_t raw) {
    const Release *best = NULL;
    int best_version = -1, best_firmware = -1;
    for (size_t i = 0; i < orbit.release_count; i++) {
        const Release *r = &orbit.releases[i];
        if (!r->update_version[0] || strcmp(r->title_id, title_id) ||
            !release_visible_on_console(r) || !source_enabled_locked(r) ||
            !game_firmware_runs(raw, r->required_firmware)) continue;
        int version = game_version_value(r->update_version), firmware = game_version_value(r->required_firmware);
        if (version > best_version || (version == best_version && firmware > best_firmware)) {
            best = r; best_version = version; best_firmware = firmware;
        }
    }
    return best;
}
cJSON *game_updates_json(void) {
    cJSON *out = cJSON_CreateArray(), *x;
    pthread_mutex_lock(&orbit.mutex);
    cJSON_ArrayForEach(x, orbit.game_updates) {
        const Release *release = release_find(json_text(x, "id"));
        if (!release || !release_visible_on_console(release) || !source_enabled_locked(release)) continue;
        cJSON *entry = cJSON_Duplicate(x, true);
        cJSON_DeleteItemFromObjectCaseSensitive(entry, "url");
        cJSON_DeleteItemFromObjectCaseSensitive(entry, "browserUrl");
        cJSON_AddBoolToObject(entry, "browserAvailable", provider_browser_supported(release));
        cJSON_AddBoolToObject(entry, "directAvailable", provider_url_supported(release));
        if (!game_firmware_runs(orbit.firmware, release->required_firmware))
            cJSON_AddStringToObject(entry, "firmwareIssue", "newer-firmware");
        char current[16];
        firmware_text(orbit.firmware, current, sizeof current);
        if (*current) cJSON_AddStringToObject(entry, "consoleFirmware", current);
        cJSON_AddItemToArray(out, entry);
    }
    pthread_mutex_unlock(&orbit.mutex);
    return out;
}
void game_updates_annotate(cJSON *snapshot) {
    cJSON *games = cJSON_GetObjectItemCaseSensitive(snapshot, "games"), *game;
    int count = 0;
    pthread_mutex_lock(&orbit.mutex);
    cJSON_ArrayForEach(game, games) {
        int installed = game_version_value(json_text(game, "installedVersion"));
        if (installed < 0 || strcmp(json_text(game, "platform"), "ps4")) continue;
        const Release *update = game_update_best_locked(json_text(game, "titleId"), orbit.firmware);
        if (!update || game_version_value(update->update_version) <= installed) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "gameId", update->game_id);
        cJSON_AddStringToObject(o, "version", update->update_version);
        cJSON_AddStringToObject(o, "baseGameId", update->base_game_id);
        cJSON_AddStringToObject(o, "requiredFirmware", update->required_firmware);
        cJSON_DeleteItemFromObjectCaseSensitive(game, "update");
        cJSON_AddItemToObject(game, "update", o);
        count++;
    }
    pthread_mutex_unlock(&orbit.mutex);
    cJSON_DeleteItemFromObjectCaseSensitive(snapshot, "updateCount");
    cJSON_AddNumberToObject(snapshot, "updateCount", count);
}
