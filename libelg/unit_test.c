/*! \file libelg/unit_test.c
 *  \brief unit tests - Skyhook ELG API Version 3.0 (IoT)
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
#include <alloca.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#define SKY_LIBELG 1
#include "config.h"
#include "beacons.h"
#include "workspace.h"
#include "libelg.h"
#include "utilities.h"

/* Example assumes a scan with 100 AP beacons
 */
#define SCAN_LIST_SIZE 100

/*! \brief set mac address. 30% are virtual AP
 *
 *  @param mac pointer to mac address
 *
 *  @returns 0 for success or negative number for error
 */
void set_mac(uint8_t *mac)
{
	if (rand() % 3 == 0) {
		/* Virtual MAC */
		uint8_t ref[] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e };
		memcpy(mac, ref, sizeof(ref));
		mac[rand() % 3 + 3] ^= (0x01 << (rand() % 7));
	} else {
		/* Non Virtual MAC */
		uint8_t ref[] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e };
		memcpy(mac, ref, sizeof(ref));
		mac[rand() % 3] = (rand() % 256);
		mac[rand() % 3 + 3] = (rand() % 256);
	}
}

/*! \brief dump the beacons in the workspace
 *
 *  @param ctx workspace pointer
 *
 *  @returns 0 for success or negative number for error
 */
void dump(sky_ctx_t *ctx)
{
	int i;

	logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
	       "WorkSpace: Expect %d, got %d, AP %d starting at %d",
	       ctx->expect, ctx->len, ctx->ap_len, ctx->ap_low);
	for (i = 0; i < ctx->len; i++) {
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "Beacon % 2d: Type: %d, MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %d",
		       i, ctx->beacon[i].ap.type, ctx->beacon[i].ap.mac[0],
		       ctx->beacon[i].ap.mac[1], ctx->beacon[i].ap.mac[2],
		       ctx->beacon[i].ap.mac[3], ctx->beacon[i].ap.mac[4],
		       ctx->beacon[i].ap.mac[5], ctx->beacon[i].ap.rssi);
	}
#if 0
	uint32_t *p = (void *)ctx;

	for (i = 0; i < sky_sizeof_workspace(MAX_BEACONS) / sizeof(int); i += 8)
		printf("ctx: %08X %08X %08X %08X  %08X %08X %08X %08X\n",
		       p[i + 000], p[i + 001], p[i + 002], p[i + 003],
		       p[i + 004], p[i + 005], p[i + 006], p[i + 007]);
#endif
}

/*! \brief validate fundamental functionality of the ELG IoT library
 *
 *  @param ac arg count
 *  @param av arg vector
 *
 *  @returns 0 for success or negative number for error
 */
int logger(sky_log_level_t level, const char *s, int max)
{
	printf("Skyhook libELG %s: %.*s\n",
	       level == SKY_LOG_LEVEL_CRITICAL ?
		       "CRIT" :
		       level == SKY_LOG_LEVEL_ERROR ?
		       "ERRR" :
		       level == SKY_LOG_LEVEL_WARNING ?
		       "WARN" :
		       level == SKY_LOG_LEVEL_DEBUG ? "DEBG" : "UNKN",
	       max, s);
	return 0;
}

/*! \brief validate fundamental functionality of the ELG IoT library
 *
 *  @param ac arg count
 *  @param av arg vector
 *
 *  @returns 0 for success or negative number for error
 */
int main(int ac, char **av)
{
	int i;
	sky_errno_t sky_errno = -1;
	sky_ctx_t *ctx;
	uint32_t *p;
	uint32_t bufsize;
	uint8_t aes_key[AES_SIZE] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e,
				      0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e,
				      0xd4, 0x85, 0x64, 0xb2 };
	uint8_t mac[MAC_SIZE] = { 0xd4, 0x85, 0x64, 0xb2, 0xf5, 0x7e };
	time_t timestamp = time(NULL);
	uint32_t ch = 65;
	uint8_t *pstate;
	uint8_t *prequest;
	uint32_t request_size;
	uint32_t response_size;

	beacon_t b[25];

	/* Intializes random number generator */
	srand((unsigned)time(NULL));

	for (i = 0; i < 25; i++) {
		b[i].ap.magic = BEACON_MAGIC;
		b[i].ap.type = SKY_BEACON_AP;
		set_mac(b[i].ap.mac);
		b[i].ap.rssi = rand() % 128;
	}

	if (sky_open(&sky_errno, mac /* device_id */, MAC_SIZE, 1, 1, aes_key,
		     NULL, SKY_LOG_LEVEL_DEBUG, &logger) == SKY_ERROR) {
		printf("sky_open returned bad value, Can't continue\n");
		exit(-1);
	}
	/* Test sky_sizeof_workspace */
	bufsize = sky_sizeof_workspace(SCAN_LIST_SIZE);

	/* sky_sizeof_workspace should return a value below 5k and above 0 */
	if (bufsize == 0 || bufsize > 4096) {
		printf("sky_sizeof_workspace returned bad value, Can't continue\n");
		exit(-1);
	}

	/* allocate workspace */
	ctx = (sky_ctx_t *)(p = alloca(bufsize));

	/* initialize the workspace */
	memset(p, 0, bufsize);

	if (sky_new_request(ctx, bufsize, &sky_errno, bufsize) != ctx) {
		printf("sky_new_request() returned bad value\n");
		printf("sky_errno contains '%s'\n", sky_perror(sky_errno));
	}

	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "ctx: magic:%08X size:%08X crc:%08X",
	       ctx->header.magic, ctx->header.size, ctx->header.crc32);

	for (i = 0; i < 25; i++) {
		if (sky_add_ap_beacon(ctx, &sky_errno, b[i].ap.mac, timestamp,
				      b[i].ap.rssi, ch, 1))
			logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
			       "sky_add_ap_beacon sky_errno contains '%s'",
			       sky_perror(sky_errno));
		else
			logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
			       "Added Test Beacon % 2d: Type: %d, MAC %02X:%02X:%02X:%02X:%02X:%02X rssi: %d\n",
			       i, b[i].ap.type, b[i].ap.mac[0], b[i].ap.mac[1],
			       b[i].ap.mac[2], b[i].ap.mac[3], b[i].ap.mac[4],
			       b[i].ap.mac[5], b[i].ap.rssi);
	}

	if (sky_finalize_request(ctx, &sky_errno, &prequest, &request_size,
				 (void *)NULL, NULL, NULL, NULL,
				 &response_size))
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "sky_finalize_request sky_errno contains '%s'",
		       sky_perror(sky_errno));
	if (strcmp((char *)prequest, "SKYHOOK REQUEST MSG"))
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "sky_finalize_request bad request buffer");
	dump(ctx);

	if (sky_close(&sky_errno, &pstate))
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "sky_close sky_errno contains '%s'\n",
		       sky_perror(sky_errno));
}
