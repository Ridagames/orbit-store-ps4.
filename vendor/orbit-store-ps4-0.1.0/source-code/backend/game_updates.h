#ifndef ORBIT_GAME_UPDATES_H
#define ORBIT_GAME_UPDATES_H
#include "orbit.h"
#include <stddef.h>
#include <stdint.h>
/* "01.16", "1.16" or "v1.16" as 116 (major * 100 + minor); -1 when it isn't one. */
int game_version_value(const char *version);
/* "01.16" as "1.16"; empty when it isn't a version. */
void game_version_text(const char *version, char *out, size_t cap);
/* Whether a console on firmware raw runs a file needing "9.00"; true when raw is unknown. */
bool game_firmware_runs(uint32_t raw, const char *required);
/* The newest update for this title ID that the console's firmware runs, from an enabled
 * source; for one version, the build for the newest firmware it runs. NULL when none.
 * Hold orbit.mutex. */
const Release *game_update_best_locked(const char *title_id, uint32_t raw);
/* GET /api/v1/game-updates. Takes orbit.mutex. */
cJSON *game_updates_json(void);
/* Library: an "update" object on each PS4 game whose installedVersion is older than its
 * newest usable update, and "updateCount" on the snapshot. Takes orbit.mutex. */
void game_updates_annotate(cJSON *snapshot);
#endif
