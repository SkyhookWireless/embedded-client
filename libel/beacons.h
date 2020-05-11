/*! \file libel/beacons.h
 *  \brief Skyhook Embedded Library
 *
 * Copyright (c) 2019 Skyhook, Inc.
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
#ifndef SKY_BEACONS_H
#define SKY_BEACONS_H

#include <inttypes.h>
#include <time.h>

#define BEACON_MAGIC 0xf0f0

#define MAC_SIZE 6

#define NUM_CELLS(p) ((p)->len - (p)->ap_len)
#define NUM_APS(p) ((p)->ap_len)
#define NUM_BEACONS(p) ((p)->len)
#define IMPLIES(a, b) (!(a) || (b))

/*! \brief Types of beacon
 */
typedef enum {
    SKY_BEACON_AP = 1,
    SKY_BEACON_BLE,
    SKY_BEACON_CDMA,
    SKY_BEACON_GSM,
    SKY_BEACON_LTE,
    SKY_BEACON_NBIOT,
    SKY_BEACON_UMTS,
    SKY_BEACON_MAX, /* add more before this */
} Sky_beacon_type_t;

/*! \brief Property of beacon
 */
typedef struct {
    uint8_t in_cache : 1;
    uint8_t used : 1;
} Sky_beacon_property_t;

struct header {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
};
/*! \brief Access Point data
 */
struct ap {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint8_t mac[MAC_SIZE];
    Sky_beacon_property_t property; /* beacon is in cache and used? */
    uint32_t age;
    uint32_t freq;
    int16_t rssi;
    uint8_t vg_len;
    uint8_t vg[MAX_VAP / MAX_AP_BEACONS]; /* mac delta of collapsed Virtual APs */
};

// http://wiki.opencellid.org/wiki/API
struct gsm {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t ci;
    uint32_t age;
    uint16_t mcc; // country
    uint16_t mnc;
    uint16_t lac;
    int16_t rssi; // -255 unkonwn - map it to - 128
};

// 64-bit aligned due to double
struct cdma {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    uint16_t sid;
    uint16_t nid;
    uint16_t bsid;
    int16_t rssi;
};

struct umts {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint16_t lac;
    uint32_t ucid;
    uint16_t mcc; // country
    uint16_t mnc;
    uint32_t age;
    int16_t rssi;
};

struct lte {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    uint32_t e_cellid;
    uint16_t mcc;
    uint16_t mnc;
    uint16_t tac;
    int16_t rssi;
};

// blue tooth
struct ble {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint16_t major;
    uint16_t minor;
    uint8_t mac[MAC_SIZE];
    uint8_t uuid[16];
    int16_t rssi;
};

struct nbiot {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    uint16_t mcc;
    uint16_t mnc;
    uint32_t e_cellid;
    uint16_t tac;
    int16_t rssi;
};

typedef union beacon {
    struct header h;
    struct ap ap;
    struct ble ble;
    struct cdma cdma;
    struct gsm gsm;
    struct lte lte;
    struct nbiot nbiot;
    struct umts umts;
} Beacon_t;

typedef struct gps {
    double lat;
    double lon;
    uint32_t hpe;
    float alt; // altitude
    uint32_t vpe;
    float speed;
    float bearing;
    uint32_t nsat;
    uint32_t age;
} Gps_t;

#endif
