/*! \file libel/beacons.c
 *  \brief utilities - Skyhook Embedded Library
 *
 * Copyright 2015-present Skyhook Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>
#include "../.submodules/tiny-AES128-C/aes.h"
#define SKY_LIBEL 1
#include "libel.h"

void dump_workspace(Sky_ctx_t *ctx);
void dump_cache(Sky_ctx_t *ctx);

/*! \brief test two MAC addresses for being virtual aps
 *
 *  @param macA pointer to the first MAC
 *  @param macB pointer to the second MAC
 *
 *  @return -1, 0 or 1
 *  return 0 when NOT similar, -1 indicates keep A, 1 keep B
 */
static int similar(uint8_t macA[], uint8_t macB[])
{
    /* Return 1 (true) if OUIs are identical and no more than 1 hex digits
     * differ between the two MACs. Else return 0 (false).
     */
    size_t num_diff = 0; // Num hex digits which differ
    size_t i;

    if (memcmp(macA, macB, 3) != 0)
        return 0;

    for (i = 3; i < MAC_SIZE; i++) {
        if (((macA[i] & 0xF0) != (macB[i] & 0xF0) && ++num_diff > 1) ||
            ((macA[i] & 0x0F) != (macB[i] & 0x0F) && ++num_diff > 1))
            return 0;
    }

    /* MACs are similar, choose one to remove */
    return (memcmp(macA + 3, macB + 3, MAC_SIZE - 3) < 0 ? -1 : 1);
}

/*! \brief shuffle list to remove the beacon at index
 *
 *  @param ctx Skyhook request context
 *  @param index 0 based index of AP to remove
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t remove_beacon(Sky_ctx_t *ctx, int index)
{
    if (index >= ctx->len)
        return SKY_ERROR;

    if (ctx->beacon[index].h.type == SKY_BEACON_AP)
        ctx->ap_len -= 1;

    memmove(&ctx->beacon[index], &ctx->beacon[index + 1],
        sizeof(Beacon_t) * (ctx->len - index - 1));
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "idx:%d", index)
    ctx->len -= 1;
    return SKY_SUCCESS;
}

/*! \brief insert beacon in list based on type and AP rssi
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno pointer to errno
 *  @param b beacon to add
 *  @param index pointer where to save the insert position
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t insert_beacon(
    Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, int *index)
{
    int i;

    /* sanity checks */
    if (!validate_workspace(ctx) || b->h.magic != BEACON_MAGIC ||
        b->h.type >= SKY_BEACON_MAX) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Invalid params. Beacon type %s",
            sky_pbeacon(b))
        return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
    }

    /* find correct position to insert based on type */
    for (i = 0; i < ctx->len; i++)
        if (ctx->beacon[i].h.type >= b->h.type)
            break;

    /* add beacon at the end */
    if (i == ctx->len) {
        ctx->beacon[i] = *b;
        ctx->len++;
    } else {
        /* if AP, add in rssi order */
        if (b->h.type == SKY_BEACON_AP) {
            for (; i < ctx->ap_len; i++)
                if (ctx->beacon[i].h.type != SKY_BEACON_AP ||
                    ctx->beacon[i].ap.rssi > b->ap.rssi)
                    break;
        }
        /* shift beacons to make room for the new one */
        memmove(&ctx->beacon[i + 1], &ctx->beacon[i],
            sizeof(Beacon_t) * (ctx->len - i));
        ctx->beacon[i] = *b;
        ctx->len++;
    }
    /* report back the position beacon was added */
    if (index != NULL)
        *index = i;
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Beacon type %s inserted idx: %d",
        sky_pbeacon(b), i);

    if (b->h.type == SKY_BEACON_AP)
        ctx->ap_len++;
    return SKY_SUCCESS;
}

/*! \brief try to reduce AP by filtering out based on diversity of rssi
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
#define RSSI_SCORE(rssi, band) ((fabs((int)(rssi / band) * band - rssi)))
#define RSSI(idx, beacon) (beacon[idx].ap.rssi - beacon[0].ap.rssi)
static Sky_status_t filter_by_rssi(Sky_ctx_t *ctx)
{
    int i, reject;
    float band_range, worst;
    float rssi_score[MAX_AP_BEACONS + 1];
    uint16_t rssi_band[MAX_AP_BEACONS + 1] = { 0 };
    uint16_t rssi_cluster[MAX_AP_BEACONS + 1] = { 0 };

    /* what share of the range of rssi values does each beacon represent */
    band_range = RSSI(ctx->ap_len - 1, ctx->beacon) / ((float)ctx->ap_len - 1);

    /* if the rssi range is small, throw away middle beacon */
    if (band_range < 0.5) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "Warning: rssi range is small. Discarding one beacon...")
        return remove_beacon(ctx, ctx->ap_len / 2);
    }

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "range: %d band range: %.2f",
        RSSI(ctx->ap_len - 1, ctx->beacon), band_range)

    /* score beacons */
    for (i = 1; i < ctx->ap_len - 1; i++) {
        rssi_band[i] = (int)(RSSI(i, ctx->beacon) / band_range);
        rssi_cluster[rssi_band[i]]++;
        rssi_score[i] = RSSI_SCORE(RSSI(i, ctx->beacon), band_range);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "beacon: %d rssi: %d band: %d(%d) score: %.2f", i,
            ctx->beacon[i].ap.rssi,
            (int)(ctx->beacon[0].ap.rssi + (rssi_band[i] * band_range)),
            rssi_band[i], rssi_score[i]);
    }
    /* update score based on clusters */
    for (i = 1; i < ctx->ap_len - 1; i++) {
        if (rssi_cluster[rssi_band[i]] > 1)
            rssi_score[i] +=
                (int)((rssi_cluster[rssi_band[i]] - 1) * band_range);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "beacon: %d rssi: %d cluster: %d(%d) new score: %.2f", i,
            ctx->beacon[i].ap.rssi, rssi_band[i], rssi_cluster[rssi_band[i]],
            rssi_score[i]);
    }

    /* find AP with poorest fit to ideal rssi */
    /* always keep lowest and highest rssi */
    for (i = 1, reject = ctx->ap_len / 2, worst = 0; i < ctx->ap_len - 1; i++) {
        if (rssi_score[i] > worst) {
            worst = rssi_score[i];
            reject = i;
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "candidate: %d, rssi %d worst %.2f", i, ctx->beacon[i].ap.rssi,
                worst)
        } else {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "         : %d, rssi %d worst %.2f", i, ctx->beacon[i].ap.rssi,
                worst)
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "reject: %d, rssi %d worst %.2f", reject,
        ctx->beacon[reject].ap.rssi, worst)
    return remove_beacon(ctx, reject);
}

/*! \brief try to reduce AP by filtering out virtual AP
 *
 *  @param ctx Skyhook request context
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static Sky_status_t filter_virtual_aps(Sky_ctx_t *ctx)
{
    int i, j;
    int cmp;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "ap_len: %d of %d APs", (int)ctx->ap_len,
        (int)ctx->len)
    dump_workspace(ctx);

    /* look for any AP beacon that is 'similar' to another */
    if (ctx->beacon[0].h.type != SKY_BEACON_AP) {
        LOGFMT(ctx, SKY_LOG_LEVEL_CRITICAL, "beacon type not WiFi")
        return SKY_ERROR;
    }

    for (j = 0; j <= ctx->ap_len; j++) {
        for (i = j + 1; i <= ctx->ap_len; i++) {
            if ((cmp = similar(ctx->beacon[i].ap.mac, ctx->beacon[j].ap.mac)) <
                0) {
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    "remove_beacon: %d similar to %d", j, i)
                dump_workspace(ctx);
                remove_beacon(ctx, j);
                return SKY_SUCCESS;
            } else if (cmp > 0) {
                LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                    "remove_beacon: %d similar to %d", i, j)
                dump_workspace(ctx);
                remove_beacon(ctx, i);
                return SKY_SUCCESS;
            }
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "no match")
    return SKY_ERROR;
}

/*! \brief add beacon to list
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param b pointer to new beacon
 *  @param is_connected This beacon is currently connected
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
Sky_status_t add_beacon(
    Sky_ctx_t *ctx, Sky_errno_t *sky_errno, Beacon_t *b, bool is_connected)
{
    int i = -1;

    /* check if maximum number of non-AP beacons already added */
    if (b->h.type != SKY_BEACON_AP &&
        ctx->len - ctx->ap_len > (TOTAL_BEACONS - MAX_AP_BEACONS)) {
        LOGFMT(ctx, SKY_LOG_LEVEL_WARNING,
            "too many (b->h.type: %s) (ctx->len - ctx->ap_len: %d)",
            sky_pbeacon(b), ctx->len - ctx->ap_len)
        return sky_return(sky_errno, SKY_ERROR_TOO_MANY);
    }

    /* insert the beacon */
    if (insert_beacon(ctx, sky_errno, b, &i) == SKY_ERROR)
        return SKY_ERROR;
    if (is_connected)
        ctx->connected = i;

    /* done if no filtering needed */
    if (b->h.type != SKY_BEACON_AP || ctx->ap_len <= MAX_AP_BEACONS)
        return sky_return(sky_errno, SKY_ERROR_NONE);

    /* beacon is AP and need filter */
    if (filter_virtual_aps(ctx) == SKY_ERROR)
        if (filter_by_rssi(ctx) == SKY_ERROR) {
            LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "failed to filter")
            return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
        }

    return sky_return(sky_errno, SKY_ERROR_NONE);
}

/*! \brief check if a beacon is in a cacheline
 *
 *  @param ctx Skyhook request context
 *  @param b pointer to new beacon
 *  @param cl pointer to cacheline
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
static bool beacon_in_cache(Sky_ctx_t *ctx, Beacon_t *b, Sky_cacheline_t *cl)
{
    int j, ret = 0;

    /* score each cache line wrt beacon match ratio */
    for (j = 0; ret == 0 && j < cl->len; j++)
        if (b->h.type == cl->beacon[j].h.type) {
            switch (b->h.type) {
            case SKY_BEACON_AP:
                if (memcmp(b->ap.mac, cl->beacon[j].ap.mac, MAC_SIZE) == 0)
                    ret = 1;
                break;
            case SKY_BEACON_BLE:
                if ((memcmp(b->ble.mac, cl->beacon[j].ble.mac, MAC_SIZE) ==
                        0) &&
                    (b->ble.major == cl->beacon[j].ble.major) &&
                    (b->ble.minor == cl->beacon[j].ble.minor) &&
                    (memcmp(b->ble.uuid, cl->beacon[j].ble.uuid, 16) == 0))
                    ret = 1;
                break;
            case SKY_BEACON_CDMA:
                if ((b->cdma.sid == cl->beacon[j].cdma.sid) &&
                    (b->cdma.nid == cl->beacon[j].cdma.nid) &&
                    (b->cdma.bsid == cl->beacon[j].cdma.bsid))
                    ret = 1;
                break;
            case SKY_BEACON_GSM:
                if ((b->gsm.ci == cl->beacon[j].gsm.ci) &&
                    (b->gsm.mcc == cl->beacon[j].gsm.mcc) &&
                    (b->gsm.mnc == cl->beacon[j].gsm.mnc) &&
                    (b->gsm.lac == cl->beacon[j].gsm.lac))
                    ret = 1;
                break;
            case SKY_BEACON_LTE:
                if ((b->lte.e_cellid == cl->beacon[j].lte.e_cellid) &&
                    (b->lte.mcc == cl->beacon[j].lte.mcc) &&
                    (b->lte.mnc == cl->beacon[j].lte.mnc))
                    ret = 1;
                break;
            case SKY_BEACON_NBIOT:
                if ((b->nbiot.mcc == cl->beacon[j].nbiot.mcc) &&
                    (b->nbiot.mnc == cl->beacon[j].nbiot.mnc) &&
                    (b->nbiot.e_cellid == cl->beacon[j].nbiot.e_cellid) &&
                    (b->nbiot.tac == cl->beacon[j].nbiot.tac))
                    ret = 1;
                break;
            case SKY_BEACON_UMTS:
                if ((b->umts.ucid == cl->beacon[j].umts.ucid) &&
                    (b->umts.mcc == cl->beacon[j].umts.mcc) &&
                    (b->umts.mnc == cl->beacon[j].umts.mnc) &&
                    (b->umts.lac == cl->beacon[j].umts.lac))
                    ret = 1;
                break;
            default:
                ret = 0;
            }
        }
    return ret;
}

/*! \brief find cache entry with best match
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *  @param put boolean which reflects put or get from cache
 *
 *  @return index of best match or empty cacheline or -1
 */
int find_best_match(Sky_ctx_t *ctx, bool put)
{
    int i, j, start, end;
    float ratio[CACHE_SIZE];
    int score[CACHE_SIZE];
    float bestratio = 0;
    int bestscore = 0;
    int bestc = -1;

    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "%s",
        put ? "for save to cache" : "for get from cache")
    dump_workspace(ctx);
    dump_cache(ctx);

    /* score each cache line wrt beacon match ratio */
    for (i = 0; i < CACHE_SIZE; i++) {
        ratio[i] = score[i] = 0;
        if (put &&
            (ctx->cache->cacheline[i].time == 0 ||
                (uint32_t)(*ctx->gettime)(NULL)-ctx->cache->cacheline[i].time >
                    (CACHE_AGE_THRESHOLD * 60 * 60))) {
            /* looking for match for put, empty cacheline = 1st choice */
            score[i] = TOTAL_BEACONS * 2;
        } else if (!put && ctx->cache->cacheline[i].time == 0) {
            /* looking for match for get, ignore empty cache */
            continue;
        } else {
            /* Non empty cacheline - count matching beacons */
            if (ctx->ap_len) {
                start = 0;
                end = ctx->ap_len;
            } else {
                start = ctx->ap_len - 1;
                end = ctx->len;
            }

            for (j = start; j < end; j++) {
                if (beacon_in_cache(
                        ctx, &ctx->beacon[j], &ctx->cache->cacheline[i])) {
                    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                        "Beacon %d type %s matches cache %d of 0..%d", j,
                        sky_pbeacon(&ctx->beacon[j]), i, CACHE_SIZE - 1)
                    score[i] = score[i] + 1.0;
                }
            }
        }
    }

    for (i = 0; i < CACHE_SIZE; i++) {
        if (score[i] == TOTAL_BEACONS * 2) {
            ratio[i] = 1.0;
        } else if (ctx->ap_len && ctx->cache->cacheline[i].ap_len) {
            // score = intersection(A, B) / union(A, B)
            ratio[i] =
                (float)score[i] /
                (ctx->ap_len + ctx->cache->cacheline[i].ap_len - score[i]);
        } else if (ctx->len - ctx->ap_len &&
                   ctx->cache->cacheline[i].len -
                       ctx->cache->cacheline[i].ap_len) {
            // if all cell beacons match
            ratio[i] = (score[i] == ctx->len - ctx->ap_len) ? 100.0 : 0.0;
        }
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache: %d: score %.2f (%d/%d)", i,
            ratio[i] * 100, score[i],
            ctx->len + ctx->cache->cacheline[i].len - score[i])
        if (ratio[i] > bestratio) {
            bestratio = ratio[i];
            bestscore = score[i];
            bestc = i;
        }
    }

    /* if match is for get, must meet threshold */
    if (!put) {
        if (ctx->len <= CACHE_BEACON_THRESHOLD && bestscore == ctx->len) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Only %d beacons; pick cache %d of 0..%d score %.2f", ctx->len,
                bestc, CACHE_SIZE - 1, ratio[bestc] * 100)
            return bestc;
        } else if (ctx->len > CACHE_BEACON_THRESHOLD &&
                   bestratio * 100 > CACHE_MATCH_THRESHOLD) {
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "location in cache, pick cache %d of 0..%d score %.2f", bestc,
                CACHE_SIZE - 1, score[bestc] * 100)
            return bestc;
        }
    } else if (bestc >= 0) { /* match is for put */
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
            "save location in best cache, %d of 0..%d score %.2f", bestc,
            CACHE_SIZE - 1, score[bestc] * 100);
        return bestc;
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cache match failed");
    return -1;
}

/*! \brief find cache entry with oldest entry
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *
 *  @return index of oldest cache entry, or empty
 */
int find_oldest(Sky_ctx_t *ctx)
{
    int i;
    uint32_t oldestc = 0;
    int oldest = (*ctx->gettime)(NULL);

    for (i = 0; i < CACHE_SIZE; i++) {
        /* Discard old cachelines */
        if ((uint32_t)(*ctx->gettime)(NULL)-ctx->cache->cacheline[i].time >
            (CACHE_AGE_THRESHOLD * 60 * 60)) {
            ctx->cache->cacheline[i].time = 0;
            return i;
        }
        if (ctx->cache->cacheline[i].time == 0)
            return i;
        else if (ctx->cache->cacheline[i].time < oldest) {
            oldest = ctx->cache->cacheline[i].time;
            oldestc = i;
        }
    }
    LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "cacheline %d oldest time %d", oldestc,
        oldest);
    return oldestc;
}

/*! \brief add location to cache
 *
 *  @param ctx Skyhook request context
 *  @param loc pointer to location info
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
Sky_status_t add_cache(Sky_ctx_t *ctx, Sky_location_t *loc)
{
    int i = -1;
    int j;
    uint32_t now = (*ctx->gettime)(NULL);

    /* compare current time to Mar 1st 2019 */
    if (now <= 1551398400) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Don't have good time of day!")
        return SKY_ERROR;
    }

    /* Find best match in cache */
    /*    yes - add entry here */
    /* else find oldest cache entry */
    /*    yes - add entryu here */
    if ((i = find_best_match(ctx, 1)) < 0) {
        i = find_oldest(ctx);
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "find_oldest chose cache %d of 0..%d",
            i, CACHE_SIZE - 1)
    } else {
        if (loc->location_source == SKY_LOCATION_SOURCE_UNKNOWN) {
            ctx->cache->cacheline[i].time = 0; /* clear cacheline */
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "Server undetermined location. find_best_match found cache match %d of 0..%d",
                i, CACHE_SIZE - 1);
        } else
            LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG,
                "find_best_match found cache match %d of 0..%d", i,
                CACHE_SIZE - 1);
    }
    if (loc->location_source == SKY_LOCATION_SOURCE_UNKNOWN) {
        LOGFMT(ctx, SKY_LOG_LEVEL_DEBUG, "Won't add unknown location to cache")
        return SKY_ERROR;
    }
    for (j = 0; j < TOTAL_BEACONS; j++) {
        ctx->cache->cacheline[i].len = ctx->len;
        ctx->cache->cacheline[i].ap_len = ctx->ap_len;
        ctx->cache->cacheline[i].beacon[j] = ctx->beacon[j];
        ctx->cache->cacheline[i].loc = *loc;
        ctx->cache->cacheline[i].time = now;
    }
    return SKY_SUCCESS;
}

/*! \brief get location from cache
 *
 *  @param ctx Skyhook request context
 *
 *  @return cacheline index or -1
 */
int get_cache(Sky_ctx_t *ctx)
{
    uint32_t now = (*ctx->gettime)(NULL);

    /* compare current time to Mar 1st 2019 */
    if (now <= 1551398400) {
        LOGFMT(ctx, SKY_LOG_LEVEL_ERROR, "Don't have good time of day!")
        return SKY_ERROR;
    }
    return find_best_match(ctx, 0);
}
