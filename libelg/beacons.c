/*! \file libelg/beacons.c
 *  \brief utilities - Skyhook ELG API Version 3.0 (IoT)
 *
 * Copyright 2015-2019 Skyhook Inc.
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
#define SKY_LIBELG 1
#include "config.h"
#include "response.h"
#include "beacons.h"
#include "workspace.h"
#include "crc32.h"
#include "libelg.h"
#include "utilities.h"

void dump(sky_ctx_t *ctx);

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
	if (memcmp(macA, macB, 3) != 0)
		return 0;

	size_t num_diff = 0; // Num hex digits which differ
	size_t i;

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
 *  @param int index 0 based index of AP to remove
 *
 *  @return sky_status_t SKY_SUCCESS (if code is SKY_ERROR_NONE) or SKY_ERROR
 */
static sky_status_t remove_beacon(sky_ctx_t *ctx, int index)
{
	if (index >= ctx->len)
		return SKY_ERROR;

	if (ctx->beacon[index].h.type == SKY_BEACON_AP)
		ctx->ap_len -= 1;

	memmove(&ctx->beacon[index], &ctx->beacon[index + 1],
		sizeof(beacon_t) * (ctx->len - index - 1));
	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "remove_beacon: %d", index);
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
static sky_status_t insert_beacon(sky_ctx_t *ctx, sky_errno_t *sky_errno,
				  beacon_t *b, int *index)
{
	int i;

	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "Insert_beacon: type %d", b->h.type);
	/* sanity checks */
	if (!validate_workspace(ctx) || b->h.magic != BEACON_MAGIC ||
	    b->h.type >= SKY_BEACON_MAX)
		return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "Insert_beacon: Done type %d",
	       b->h.type);

	/* find correct position to insert based on type */
	for (i = 0; i < ctx->len; i++)
		if (ctx->beacon[i].h.type >= b->h.type)
			break;
	if (b->h.type == SKY_BEACON_AP)
		/* note first AP */
		ctx->ap_low = i;

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
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "shift beacons to make room for the new one (%d)", i);
		memmove(&ctx->beacon[i + 1], &ctx->beacon[i],
			sizeof(beacon_t) * (ctx->len - i));
		ctx->beacon[i] = *b;
		ctx->len++;
	}
	/* report back the position beacon was added */
	if (index != NULL)
		*index = i;

	if (b->h.type == SKY_BEACON_AP)
		ctx->ap_len++;
	return SKY_SUCCESS;
}

/*! \brief try to reduce AP by filtering out based on diversity of rssi
 *
 *  @param ctx Skyhook request context
 *  @param int index 0 based index of last AP added
 *
 *  @return true if AP removed, or false
 */
static sky_status_t filter_by_rssi(sky_ctx_t *ctx)
{
	int i, reject;
	float band_range, worst;
	float ideal_rssi[MAX_AP_BEACONS + 1];

	if (ctx->ap_len < MAX_AP_BEACONS)
		return SKY_ERROR;

	/* what share of the range of rssi values does each beacon represent */
	band_range = (ctx->beacon[ctx->ap_low + ctx->ap_len - 1].ap.rssi -
		      ctx->beacon[ctx->ap_low].ap.rssi) /
		     (float)ctx->ap_len;

	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "range: %d band: %.2f",
	       (ctx->beacon[ctx->ap_low + ctx->ap_len - 1].ap.rssi -
		ctx->beacon[ctx->ap_low].ap.rssi),
	       band_range);
	/* for each beacon, work out it's ideal rssi value to give an even distribution */
	for (i = 0; i < ctx->ap_len; i++)
		ideal_rssi[i] =
			ctx->beacon[ctx->ap_low].ap.rssi + i * band_range;

	/* find AP with poorest fit to ideal rssi */
	/* always keep lowest and highest rssi */
	for (i = 1, reject = -1, worst = 0; i < ctx->ap_len - 2; i++) {
		if (fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]) > worst) {
			worst = fabs(ctx->beacon[i].ap.rssi - ideal_rssi[i]);
			reject = i;
			logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
			       "reject: %d, ideal %.2f worst %.2f", i,
			       ideal_rssi[i], worst);
		}
	}
	return remove_beacon(ctx, reject);
}

/*! \brief try to reduce AP by filtering out virtual AP
 *
 *  @param ctx Skyhook request context
 *  @param int index 0 based index of last AP added
 *
 *  @return true if AP removed, or false
 */
static sky_status_t filter_virtual_aps(sky_ctx_t *ctx)
{
	int i, j;
	int cmp;

	logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
	       "filter_virtual_aps: ap_low: %d, ap_len: %d of %d", ctx->ap_low,
	       (int)ctx->ap_len, (int)ctx->len);
	dump(ctx);

	if (ctx->ap_len < MAX_AP_BEACONS)
		return SKY_ERROR;

	/* look for any AP beacon that is 'similar' to another */
	if (ctx->beacon[ctx->ap_low].h.type != SKY_BEACON_AP) {
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "filter_virtual_aps: beacon type not AP");
		return SKY_ERROR;
	}

	for (j = ctx->ap_low; j <= ctx->ap_low + ctx->ap_len; j++) {
		for (i = j + 1; i <= ctx->ap_low + ctx->ap_len; i++) {
			if ((cmp = similar(ctx->beacon[i].ap.mac,
					   ctx->beacon[j].ap.mac)) < 0) {
				logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
				       "remove_beacon: %d similar to %d", j, i);
				dump(ctx);
				remove_beacon(ctx, j);
				return SKY_SUCCESS;
			} else if (cmp > 0) {
				logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
				       "remove_beacon: %d similar to %d", i, j);
				dump(ctx);
				remove_beacon(ctx, i);
				return SKY_SUCCESS;
			}
		}
	}
	logfmt(ctx, SKY_LOG_LEVEL_DEBUG, "filter_virtual_aps: no match");
	return SKY_ERROR;
}

/*! \brief add beacon to list
 *  if beacon is AP, filter
 *
 *  @param ctx Skyhook request context
 *  @param sky_errno skyErrno is set to the error code
 *  @param beacon pointer to new beacon
 *  @param is_connected This beacon is currently connected
 *
 *  @return SKY_SUCCESS if beacon successfully added or SKY_ERROR
 */
sky_status_t add_beacon(sky_ctx_t *ctx, sky_errno_t *sky_errno, beacon_t *b,
			bool is_connected)
{
	int i = -1;

	/* check if maximum number of non-AP beacons already added */
	if (b->h.type == SKY_BEACON_AP &&
	    ctx->len - ctx->ap_len > (MAX_BEACONS - MAX_AP_BEACONS)) {
		logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
		       "add_beacon: (b->h.type %d) (ctx->len - ctx->ap_len %d)",
		       b->h.type, ctx->len - ctx->ap_len);
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
			logfmt(ctx, SKY_LOG_LEVEL_DEBUG,
			       "add_beacon: failed to filter");
			return sky_return(sky_errno, SKY_ERROR_BAD_PARAMETERS);
		}

	return sky_return(sky_errno, SKY_ERROR_NONE);
}
