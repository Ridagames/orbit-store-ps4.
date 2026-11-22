#ifndef ORBIT_PS4_BGFT_H
#define ORBIT_PS4_BGFT_H
#include <stddef.h>
#include <stdint.h>
/* Public reverse-engineered PS4 ABI. Use explicit 64-bit lengths: some older
 * OpenOrbis headers incorrectly used 32-bit unsigned int for these fields. */
typedef struct { void *heap; size_t heap_size; } Ps4BgftInit;
typedef struct {
    int32_t user_id, entitlement_type;
    const char *id, *content_url, *content_ex_url, *content_name, *icon_path, *sku_id;
    int32_t option;
    const char *playgo_scenario_id, *release_date, *package_type, *package_sub_type;
    uint64_t package_size;
} Ps4BgftDownload;
typedef struct { Ps4BgftDownload params; uint32_t slot; } Ps4BgftDownloadEx;
typedef struct {
    uint32_t bits;
    int32_t error_result;
    uint64_t length, transferred, length_total, transferred_total;
    uint32_t num_index, num_total, rest_sec, rest_sec_total;
    int32_t preparing_percent, local_copy_percent;
} Ps4BgftProgress;
_Static_assert(sizeof(Ps4BgftDownload) == 104, "PS4 BGFT download ABI");
_Static_assert(sizeof(Ps4BgftDownloadEx) == 112, "PS4 BGFT storage ABI");
_Static_assert(offsetof(Ps4BgftDownload, playgo_scenario_id) == 64, "PS4 BGFT options ABI");
_Static_assert(sizeof(Ps4BgftProgress) == 64, "PS4 BGFT progress ABI");
_Static_assert(offsetof(Ps4BgftProgress, length_total) == 24, "PS4 BGFT byte counts ABI");
#endif
