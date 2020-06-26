/*! \file elgsim.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/time.h>
#define __USE_POSIX199309
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include "../.submodules/tiny-AES128-C/aes.h"

#define SKY_LIBEL 1
#include "libel.h"

#include "scans.h"
#include "elgconfig.h"
#include "../libez/ezutil.h"

static char *locationSource_str(XPS_Location_t loc)
{
    switch (loc.location_source) {
    case XPS_LOCATION_SOURCE_UNKNOWN:
        return "unknown";
        break;
    case XPS_LOCATION_SOURCE_HYBRID:
        return "hybrid";
        break;
    case XPS_LOCATION_SOURCE_CELL:
        return "cell";
        break;
    case XPS_LOCATION_SOURCE_WIFI:
        return "wi-fi";
        break;
    case XPS_LOCATION_SOURCE_GNSS:
        return "gnss";
    default:
        return "unknown";
    }
}

/*! \brief validate fundamental functionality of the ELG IoT library
 *
 *  @param argc count
 *  @param argv vector of arguments
 *
 *  @returns 0 for success or negative number for error
 */
int main(int argc, char *argv[])
{
    char *configfile = NULL;
    XPS_StatusCode_t ret;

    Config_t config;

    XPS_Location_t loc;
    XPS_ScannedAP_t *aps = NULL;
    XPS_ScannedCell_t *cells = NULL;
    XPS_GNSS_t *gnss = NULL;
    int num_aps, num_cells;

    if (argc == 0) {
        configfile = "elgsim.conf";
    } else {
        configfile = argv[1];
    }

    // Load the configuration
    if (load_config(configfile, &config) == -1)
        exit(-1);

    print_config(&config);

    if (XPS_set_option("device_id", "5C0E8BA07ED1") != XPS_STATUS_OK ||
        XPS_set_option("Partner_id", "2") != XPS_STATUS_OK ||
        XPS_set_option("key", "000102030405060708090a0b0c0d0e0f") != XPS_STATUS_OK ||
        XPS_set_option("server", "localhost") != XPS_STATUS_OK ||
        XPS_set_option("loglevel", "2") != XPS_STATUS_OK)
        exit(-1);

    // Load test beacons from a file
    if (load_beacons(config.scan_file) == -1)
        exit(-1);

    for (int count = 1; count < config.num_scans + 1; count++) {
        get_next_scan(&num_aps, &aps, &num_cells, &cells, &gnss);

        // Call the library and print result
        if ((ret = XPS_locate(num_aps, aps, num_cells, cells, gnss, &loc)) != XPS_STATUS_OK)
            printf("XPS_locate error %s\n", XPS_perror(ret));
        else
            printf("XPS_locate: %.4f %.4f, hpe %d %s\n", loc.lat, loc.lon, loc.hpe,
                locationSource_str(loc));
    }
}
