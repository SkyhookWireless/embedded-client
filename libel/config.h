/*! \file libel/config.h
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
#ifndef SKY_CONFIG_H
#define SKY_CONFIG_H

/* Change to false to remove all calls to logging */
#define SKY_DEBUG true
#define SKY_LOG_LENGTH 80

/*! \brief The maximum number of beacons passed to the server in a request
 */
#define TOTAL_BEACONS 11
/*! \brief The maximum number of AP beacons passed to the server in a request
 */
#define MAX_AP_BEACONS 10

/*! \brief The percentage of beacons that must match in a cached scan/location
 */
#define CACHE_MATCH_THRESHOLD 50

/*! \brief The maximum age (in hr) that a cached value is concidered useful
 */
#define CACHE_AGE_THRESHOLD 24

/*! \brief The minimum beacons that use CACHE_MATCH_THRESHOLD, otherwise 100% match required
 */
#define CACHE_BEACON_THRESHOLD 3

/*! \brief The minimum rssi value preferred for cache matching
 */
#define CACHE_RSSI_THRESHOLD -90

/*! \brief The number of entries in the scan/response cache
 */
#define CACHE_SIZE 1

/*! \brief The maximum space the dynamic configuration parameters may take up in bytes
 */
#define MAX_CLIENTCONFIG_SIZE 100

#endif
