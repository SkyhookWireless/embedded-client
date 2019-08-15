/*! \file libel/libel.h
 *  \brief Top level header file for Skyhook Embedded Library
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
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#ifndef SKY_LIBEL_H
#define SKY_LIBEL_H

#define SKY_MAX_BEACONS 255
#define SKY_MAX_CACHELINES 128
#define MAX_CLIENTCONFIG_SIZE 100

#define MAC_SIZE 6
#define AES_SIZE 16

#define MAX_DEVICE_ID 16

/* March 1st 2019 */
#define TIMESTAMP_2019_03_01 1551398400

/*! \brief API return value
 */
typedef enum {
    SKY_SUCCESS = 0,
    SKY_ERROR = -1,
} Sky_status_t;

/*! \brief sky_finalize_request return value
 */
typedef enum {
    SKY_FINALIZE_ERROR = -1,
    SKY_FINALIZE_LOCATION = 0,
    SKY_FINALIZE_REQUEST = 1,
} Sky_finalize_t;

/*! \brief sky_loc_source location source
 */
typedef enum {
    SKY_LOCATION_SOURCE_UNKNOWN = 0,
    SKY_LOCATION_SOURCE_HYBRID,
    SKY_LOCATION_SOURCE_CELL,
    SKY_LOCATION_SOURCE_WIFI,
    SKY_LOCATION_SOURCE_GNSS,
    SKY_LOCATION_SOURCE_MAX,
} Sky_loc_source_t;

/*! \brief sky_loc_status location status
 */
typedef enum {
    SKY_LOCATION_STATUS_SUCCESS = 0,
    SKY_LOCATION_STATUS_UNSPECIFIED_ERROR,
    SKY_LOCATION_STATUS_BAD_PARTNER_ID_ERROR,
    SKY_LOCATION_STATUS_DECODE_ERROR,
    SKY_LOCATION_STATUS_API_SERVER_ERROR,
} Sky_loc_status_t;

/*! \brief Skyhook location information
 */
typedef struct sky_location {
    float lat, lon; /* GNSS info */
    uint16_t hpe;
    uint32_t time;
    Sky_loc_source_t location_source;
    Sky_loc_status_t location_status;
} Sky_location_t;

/*! \brief sky_errno Error Codes
 */
typedef enum {
    SKY_ERROR_NONE = 0,
    SKY_ERROR_NEVER_OPEN,
    SKY_ERROR_ALREADY_OPEN,
    SKY_ERROR_BAD_PARAMETERS,
    SKY_ERROR_TOO_MANY,
    SKY_ERROR_BAD_WORKSPACE,
    SKY_ERROR_BAD_STATE,
    SKY_ERROR_DECODE_ERROR,
    SKY_ERROR_ENCODE_ERROR,
    SKY_ERROR_RESOURCE_UNAVAILABLE,
    SKY_ERROR_CLOSE,
    SKY_ERROR_BAD_KEY,
    SKY_ERROR_NO_BEACONS,
    SKY_ERROR_ADD_CACHE,
    SKY_ERROR_GET_CACHE,
    SKY_ERROR_LOCATION_UNKNOWN,
    SKY_ERROR_SERVER_ERROR,
    SKY_ERROR_NEW_CONFIG,
    SKY_ERROR_MAX
} Sky_errno_t;

/*! \brief sky_log_level logging levels
 */
typedef enum {
    SKY_LOG_LEVEL_CRITICAL = 1,
    SKY_LOG_LEVEL_ERROR,
    SKY_LOG_LEVEL_WARNING,
    SKY_LOG_LEVEL_DEBUG,
    SKY_LOG_LEVEL_ALL = SKY_LOG_LEVEL_DEBUG,
} Sky_log_level_t;

/*! \brief pointer to logger callback function
 */
typedef int (*Sky_loggerfn_t)(Sky_log_level_t level, char *s);

/*! \brief pointer to random bytes callback function
 */
typedef int (*Sky_randfn_t)(uint8_t *rand_buf, uint32_t bufsize);

/*! \brief pointer to gettime callback function
 */
typedef time_t (*Sky_timefn_t)(time_t *t);

/*! \brief pointer to gettime callback function
 */
typedef void *(*Sky_mallocfn_t)(size_t size);

#ifndef SKY_LIBEL
typedef void Sky_ctx_t;
#else
#include "config.h"
#include "beacons.h"
#include "crc32.h"
#include "workspace.h"
#include "utilities.h"
#endif

Sky_status_t sky_open(Sky_errno_t *sky_errno, uint8_t *device_id, uint32_t id_len,
    uint32_t partner_id, uint32_t aes_key_id, uint8_t aes_key[16], void *state_buf,
    Sky_log_level_t min_level, Sky_loggerfn_t logf, Sky_randfn_t rand_bytes, Sky_timefn_t gettime,
    Sky_mallocfn_t malloc);

int32_t sky_sizeof_state(void *sky_state);

int32_t sky_sizeof_workspace(void);

Sky_ctx_t *sky_new_request(void *workspace_buf, uint32_t bufsize, Sky_errno_t *sky_errno);

Sky_status_t sky_add_ap_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint8_t mac[6],
    time_t timestamp, int16_t rssi, int32_t freq, bool is_connected);

Sky_status_t sky_add_cell_lte_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t tac,
    uint32_t e_cellid, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rsrp,
    bool is_connected);

Sky_status_t sky_add_cell_gsm_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t lac,
    uint32_t ui, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rssi, bool is_connected);

Sky_status_t sky_add_cell_umts_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t lac,
    uint32_t ui, uint16_t mcc, uint16_t mnc, time_t timestamp, int16_t rscp, bool is_connected);

Sky_status_t sky_add_cell_cdma_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint32_t sid,
    uint16_t nid, uint16_t bsid, time_t timestamp, int16_t rssi, bool is_connected);

Sky_status_t sky_add_cell_nb_iot_beacon(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, uint16_t mcc,
    uint16_t mnc, uint32_t e_cellid, uint32_t tac, time_t timestamp, int16_t nrsrp,
    bool is_connected);

Sky_status_t sky_add_gnss(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, float lat, float lon,
    uint16_t hpe, float altitude, uint16_t vpe, float speed, float bearing, uint16_t nsat,
    time_t timestamp);

Sky_status_t sky_sizeof_request_buf(Sky_ctx_t *ctx, uint32_t *size, Sky_errno_t *sky_errno);

Sky_finalize_t sky_finalize_request(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, void *request_buf,
    uint32_t bufsize, Sky_location_t *loc, uint32_t *response_size);

Sky_status_t sky_decode_response(Sky_ctx_t *ctx, Sky_errno_t *sky_errno, void *response_buf,
    uint32_t bufsize, Sky_location_t *loc);

char *sky_perror(Sky_errno_t sky_errno);

char *sky_pserver_status(Sky_loc_status_t status);
#ifdef SKY_LIBEL
char *sky_pbeacon(Beacon_t *b);
#endif

Sky_status_t sky_close(Sky_errno_t *sky_errno, void **sky_state);

#endif
