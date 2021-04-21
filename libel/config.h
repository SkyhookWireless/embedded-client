/*! \file libel/config.h
 *  \brief Skyhook Embedded Library
 *
 * Copyright (c) 2020 Skyhook, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */
#ifndef SKY_CONFIG_H
#define SKY_CONFIG_H

/* Change to false to remove all calls to logging */
#ifndef SKY_DEBUG
#define SKY_DEBUG true
#endif

#ifndef SKY_LOG_LENGTH
#define SKY_LOG_LENGTH 120
#endif

/*! \brief The maximum number of beacons passed to the server in a request
 */
#ifndef TOTAL_BEACONS
#define TOTAL_BEACONS 28
#endif

/*! \brief The maximum number of AP beacons passed to the server in a request
 */
#ifndef MAX_AP_BEACONS
#define MAX_AP_BEACONS 20
#endif

/*! \brief The maximum number of child APs in a Virtual Group. No more than 16 allowed
 */
#ifndef MAX_VAP_PER_AP
#define MAX_VAP_PER_AP 4
#endif

/*! \brief The maximum number of child APs total in a request
 */
#ifndef MAX_VAP_PER_RQ
#define MAX_VAP_PER_RQ 12
#endif

/*! \brief The percentage of beacons that must match in a cached scan/location
 */
#ifndef CACHE_MATCH_THRESHOLD_USED
#define CACHE_MATCH_THRESHOLD_USED 50 // Score needed when matching just Used APs
#endif
#ifndef CACHE_MATCH_THRESHOLD_ALL
#define CACHE_MATCH_THRESHOLD_ALL 65 // Score needed when matching Used and Unused APs
#endif

/*! \brief The maximum age (in hr) that a cached value is concidered useful
 */
#ifndef CACHE_AGE_THRESHOLD
#define CACHE_AGE_THRESHOLD 24
#endif

/*! \brief If there are CACHE_BEACON_THRESHOLD or more beacons in workspace
 *   after filtering, then the cache match score is compared to
 *   CACHE_MATCH_THRESHOLD, otherwise 100% match is required to return the cached
 *   location (i.e. all beacons must match when only few beacons are in workspace).
 */
#ifndef CACHE_BEACON_THRESHOLD
#define CACHE_BEACON_THRESHOLD 3
#endif

/*! \brief The minimum rssi value preferred for cache matching
 */
#ifndef CACHE_RSSI_THRESHOLD
#define CACHE_RSSI_THRESHOLD 90
#endif

/*! \brief The number of entries in the scan/response cache
 */
#ifndef CACHE_SIZE
#define CACHE_SIZE 1
#endif

/*! \brief The maximum space the dynamic configuration parameters may take up in bytes
 */
#ifndef MAX_CLIENTCONFIG_SIZE
#define MAX_CLIENTCONFIG_SIZE 100
#endif

/*! \brief TBR Authentication
 */
#ifndef SKY_TBR_DEVICE_ID
#define SKY_TBR_DEVICE_ID true // Include device_id in location requests (typically omitted)
#endif

/*! \brief Application Data
 */
#ifndef SKY_MAX_DL_APP_DATA
#define SKY_MAX_DL_APP_DATA 100 // Max space reserved for downlink app data
#endif
#ifndef SKY_MAX_UL_APP_DATA
#define SKY_MAX_UL_APP_DATA 100 // Max space reserved for uplink app data
#endif

#endif
