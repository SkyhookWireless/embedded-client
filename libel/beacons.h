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

/*! \brief Types of beacon
 */
typedef enum {
    SKY_BEACON_AP = 1,
    SKY_BEACON_BLE = 2,
    SKY_BEACON_CDMA = 3,
    SKY_BEACON_FIRST_CELL_TYPE = SKY_BEACON_CDMA,
    SKY_BEACON_GSM = 4,
    SKY_BEACON_LTE = 5,
    SKY_BEACON_NBIOT = 6,
    SKY_BEACON_UMTS = 7,
    SKY_BEACON_NR = 8,
    SKY_BEACON_LAST_CELL_TYPE = SKY_BEACON_NR,
    SKY_BEACON_MAX, /* add more before this */
} Sky_beacon_type_t;

/*! \brief Access Point data
 */
struct ap {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint8_t mac[MAC_SIZE];
    uint8_t in_cache; /* beacon is in cache */
    uint32_t age;
    uint32_t freq;
    int16_t rssi;
};

// http://wiki.opencellid.org/wiki/API
struct gsm {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    int64_t ci; // id4
    uint32_t age;
    uint16_t mcc; // id1
    uint16_t mnc; // id2
    uint16_t lac; // id3
    int16_t rssi; // -255 unkonwn - map it to - 128
};

// 64-bit aligned due to double
struct cdma {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    uint16_t sid; // id2
    uint16_t nid; // id3
    int64_t bsid; // id4
    int16_t rssi;
};

struct umts {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint16_t lac; // id3
    int64_t ucid; // id4
    uint16_t mcc; // id1
    uint16_t mnc; // id2
    int16_t psc; // id5
    int32_t uarfcn; // id6
    uint32_t age;
    int16_t rssi;
};

struct lte {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    int64_t e_cellid; // id4
    uint16_t mcc; // id1
    uint16_t mnc; // id2
    uint16_t tac; // id3
    int16_t pci; // id5
    int32_t earfcn; // id6
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
    uint16_t mcc; // id1
    uint16_t mnc; // id2
    int64_t e_cellid; // id4
    uint16_t tac; // id3
    int16_t ncid; // id5
    int32_t earfcn; // id6
    int16_t rssi;
};

struct nr {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
    uint32_t age;
    uint16_t mcc; // id1
    uint16_t mnc; // id2
    int64_t nci; // id4
    uint16_t tac; // id3
    uint16_t pci; // id5
    int32_t nrarfcn; // id6
    int16_t rssi;
};

struct header {
    uint16_t magic; /* Indication that this beacon entry is valid */
    uint16_t type; /* sky_beacon_type_t */
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
    struct nr nr;
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
