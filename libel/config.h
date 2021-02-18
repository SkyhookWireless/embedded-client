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
#define SKY_DEBUG true
#define SKY_LOG_LENGTH 200

/*! \brief The maximum number of beacons passed to the server in a request
 */
#define TOTAL_BEACONS 20
/*! \brief The maximum number of AP beacons passed to the server in a request
 */
#define MAX_AP_BEACONS 15
/*! \brief The maximum number of child APs in a Virtual Group. No more than 16 allowed
 */
#define MAX_VAP_PER_AP 4
/*! \brief The maximum number of child APs total in a request
 */
#define MAX_VAP_PER_RQ 12
/*! \brief The percentage of beacons that must match in a cached scan/location
 */
#define CACHE_MATCH_THRESHOLD_USED 50 // Score needed when matching just Used APs
#define CACHE_MATCH_THRESHOLD_ALL 65 // Score needed when matching Used and Unused APs

/*! \brief The maximum age (in hr) that a cached value is concidered useful
 */
#define CACHE_AGE_THRESHOLD 24

/*! \brief If there are CACHE_BEACON_THRESHOLD or more beacons in workspace
 *   after filtering, then the cache match score is compared to
 *   CACHE_MATCH_THRESHOLD, otherwise 100% match is required to return the cached
 *   location (i.e. all beacons must match when only few beacons are in workspace).
 */
#define CACHE_BEACON_THRESHOLD 3

/*! \brief The minimum rssi value preferred for cache matching
 */
#define CACHE_RSSI_THRESHOLD 90

/*! \brief The number of entries in the scan/response cache
 */
#define CACHE_SIZE 1

/*! \brief The maximum space the dynamic configuration parameters may take up in bytes
 */
#define MAX_CLIENTCONFIG_SIZE 100

/*! \brief TBR Authentication
 */
#define SKY_TBR_DEVICE_ID true // Include device_id in location requests (typically omitted)

/*! \brief Application Data
 */
#define SKY_MAX_DL_APP_DATA 100 // Max space reserved for downlink app data
#define SKY_MAX_UL_APP_DATA 100 // Max space reserved for uplink app data

#endif
