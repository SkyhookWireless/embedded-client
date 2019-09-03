/*! \file libel/workspace.h
 *  \brief Skyhook Embedded Library
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
#ifndef SKY_WORKSPACE_H
#define SKY_WORKSPACE_H

#define SKY_MAGIC 0xD1967805
typedef struct sky_header {
    uint32_t magic; /* SKY_MAGIC */
    uint32_t size; /* total number of bytes in structure */
    uint32_t time; /* timestamp when structure was allocated */
    uint32_t crc32; /* crc32 over header */
} Sky_header_t;

typedef struct sky_cacheline {
    int16_t len; /* number of beacons */
    int16_t ap_len; /* number of AP beacons in list (0 == none) */
    uint32_t time;
    Beacon_t beacon[TOTAL_BEACONS]; /* beacons */
    Sky_location_t loc; /* Skyhook location */
} Sky_cacheline_t;

/* Access the cache config parameters */
#define CONFIG(cache, param) (cache->config.param)

typedef struct sky_config_pad {
    uint32_t last_config_time; /* time when the last new config was received */
    uint32_t total_beacons;
    uint32_t max_ap_beacons;
    uint32_t cache_match_threshold;
    uint32_t cache_age_threshold;
    uint32_t cache_beacon_threshold;
    uint32_t cache_neg_rssi_threshold;
    /* add more configuration params here */
} Sky_config_t;

typedef struct sky_cache {
    Sky_header_t header; /* magic, size, timestamp, crc32 */
    uint32_t sky_id_len; /* device ID len */
    uint8_t sky_device_id[MAC_SIZE]; /* device ID */
    uint32_t sky_partner_id; /* partner ID */
    uint32_t sky_aes_key_id; /* aes key ID */
    uint8_t sky_aes_key[16]; /* aes key */
    int len; /* number of cache lines */
    Sky_cacheline_t cacheline[CACHE_SIZE]; /* beacons */
    int newest;
    Sky_config_t config; /* dynamic config parameters */
} Sky_cache_t;

typedef struct sky_ctx {
    Sky_header_t header; /* magic, size, timestamp, crc32 */
    Sky_loggerfn_t logf;
    Sky_randfn_t rand_bytes;
    Sky_log_level_t min_level;
    Sky_timefn_t gettime;
    int16_t len; /* number of beacons in list (0 == none) */
    Beacon_t beacon[TOTAL_BEACONS + 1]; /* beacon data */
    bool in_cache[TOTAL_BEACONS]; /* beacon in cache */
    int16_t ap_len; /* number of AP beacons in list (0 == none) */
    int16_t connected; /* which beacon is conneted (-1 == none) */
    Gps_t gps; /* GNSS info */
    /* Assume worst case is that beacons and gps info takes twice the bare structure size */
    Sky_cache_t *cache;
} Sky_ctx_t;
#endif
