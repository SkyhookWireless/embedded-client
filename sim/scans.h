#ifndef SCANS_H
#define SCANS_H

#include <../libez/libez.h>
#include "libs/libjson/json.h"

#define MAX_SCANS 1000

typedef struct wifi_scan {
    int8_t num_aps;
    int8_t num_cells;
    int16_t ap_connected;
    int8_t cell_connected;
    XPS_ScannedAP_t *aps;
    XPS_ScannedCell_t cell;
    XPS_GNSS_t gps;
} WifiScan_t;

int load_beacons(char *filename);

void aps_to_beacons(struct json_object *json_aps, WifiScan_t *scan);

void cell_to_beacon(struct json_object *obj, WifiScan_t *scan);

void gps_to_beacon(struct json_object *json_gps, WifiScan_t *scan);

time_t convert_age(int age, time_t t);

XPS_CellType_t determine_cell_type(char *ctype);

void log_ap(int num, XPS_ScannedAP_t ap, int ap_connected);

void log_cell(WifiScan_t *scan);

void log_gps(WifiScan_t *scan);

void get_next_scan(int *num_aps, XPS_ScannedAP_t **ap, int *num_cells, XPS_ScannedCell_t **cell,
    XPS_GNSS_t **gnss);

#endif
