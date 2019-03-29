/*! \file libelg/workspace.h
 *  \brief Skyhook ELG API workspace structures
 *
 * Copyright 2019 Skyhook Inc.
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
struct sky_header {
	uint32_t magic;
	uint32_t size;
	time_t time;
	uint32_t crc32;
};

typedef struct sky_ctx {
	struct sky_header header; /* magic, size, timestamp, crc32 */
	int16_t expect; /* number of beacons to be added */
	int16_t len; /* number of beacons in list (0 == none) */
	beacon_t beacon[MAX_BEACONS + 1]; /* beacon data */
	int16_t ap_len; /* number of AP beacons in list (0 == none) */
	int16_t ap_low; /* first of AP beacons in list (0 based index) */
	int16_t connected; /* which beacon is conneted (-1 == none) */
	gps_t gps; /* GPS info */
	uint8_t request[sizeof(struct sky_header) + sizeof(int32_t) +
			(sizeof(beacon_t) * MAX_BEACONS) + sizeof(int32_t) +
			sizeof(gps_t)];
} sky_ctx_t;

#endif
