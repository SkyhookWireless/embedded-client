#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#define __USE_XOPEN
#include <sys/time.h>
#include <math.h>
#define SKY_LIBEL 1
#include "libel.h"
#include "libez.h"

#include "scans.h"

#define connected(c) (c == 1 ? "true" : "false")

WifiScan_t scans[MAX_SCANS];
int index = 0;
int scan_count = 0;

int load_beacons(char *filename)
{
    json_object *obj;
    char buf[100000];
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        XPS_log_error("Error: unable to open config file - %s", filename);
        return -1;
    }

    XPS_log_debug("Loading scans from - %s", filename);
    while (fgets(buf, sizeof(buf), fp)) {
        XPS_log_debug("Scan %d:", scan_count);
        obj = json_tokener_parse(buf);
        struct json_object *json_aps = json_object_object_get(obj, "aps");
        if (json_aps != NULL) {
            aps_to_beacons(json_aps, &scans[scan_count]);
        }

        struct json_object *obj_cell = json_object_object_get(obj, "cell");
        if (obj_cell != NULL) {
            cell_to_beacon(obj_cell, &scans[scan_count]);
            log_cell(&scans[scan_count]);
        } else {
            scans[scan_count].num_cells = 0;
        }

        struct json_object *json_gps = json_object_object_get(obj, "gps");
        if (json_gps != NULL) {
            gps_to_beacon(json_gps, &scans[scan_count]);
        }
        if (json_aps != NULL || obj_cell != NULL || json_gps != NULL) {
            scan_count++;
        } else
            XPS_log_debug("Scan %d: Empty!", scan_count);
        if (scan_count == MAX_SCANS)
            break;
    }
    fclose(fp);
    XPS_log_debug("Total scans loaded: %d", scan_count);
    return 0;
}

void aps_to_beacons(struct json_object *json_aps, WifiScan_t *scan)
{
    int num_aps = json_object_array_length(json_aps);
    scan->num_aps = num_aps;
    scan->aps = malloc(sizeof(XPS_ScannedAP_t) * num_aps);
    scan->ap_connected = -1;

    for (int i = 0; i < num_aps; i++) {
        json_object *obj = json_object_array_get_idx(json_aps, i);
        char *mac = (char *)json_object_get_string(json_object_object_get(obj, "mac"));
        char str[2];
        int str_idx = 0;
        for (int m = 0; m < MAC_SIZE; m++) {
            strncpy(str, &mac[str_idx], 2);
            scan->aps[i].mac[m] = strtoul(str, NULL, 16);
            str_idx += 2;
        }
        time_t ts = time(NULL);
        scan->aps[i].timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), ts);
        scan->aps[i].freq = (uint32_t)json_object_get_int(json_object_object_get(obj, "freq"));
        scan->aps[i].rssi = (int8_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        int conn = json_object_get_int(json_object_object_get(obj, "connected"));
        if (conn == 1)
            scan->ap_connected = i;
        log_ap(i, scan->aps[i], scan->ap_connected);
    }
}

void cell_to_beacon(struct json_object *obj, WifiScan_t *scan)
{
    scan->cell.type =
        determine_cell_type((char *)json_object_get_string(json_object_object_get(obj, "type")));
    scan->cell_connected = (int8_t)json_object_get_int(json_object_object_get(obj, "connected"));
    switch (scan->cell.type) {
    case XPS_CELL_TYPE_GSM:
        scan->cell.id1 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mcc"));
        scan->cell.id2 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mnc"));
        scan->cell.id4 = (uint16_t)json_object_get_int(json_object_object_get(obj, "lac"));
        scan->cell.id3 = (uint32_t)json_object_get_int(json_object_object_get(obj, "ci"));
        scan->cell.timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
        scan->cell.ss = (int16_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        break;
    case XPS_CELL_TYPE_UMTS:
        scan->cell.id1 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mcc"));
        scan->cell.id2 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mnc"));
        scan->cell.id4 = (uint16_t)json_object_get_int(json_object_object_get(obj, "lac"));
        scan->cell.id3 = (uint32_t)json_object_get_int(json_object_object_get(obj, "ucid"));
        scan->cell.timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
        scan->cell.ss = (int16_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        break;
    case XPS_CELL_TYPE_LTE:
        scan->cell.id1 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mcc"));
        scan->cell.id2 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mnc"));
        scan->cell.id4 = (uint16_t)json_object_get_int(json_object_object_get(obj, "tac"));
        scan->cell.id3 = (uint32_t)json_object_get_int(json_object_object_get(obj, "eucid"));
        scan->cell.timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
        scan->cell.ss = (int16_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        break;
    case XPS_CELL_TYPE_CDMA:
        scan->cell.id1 = (uint16_t)json_object_get_int(json_object_object_get(obj, "sid"));
        scan->cell.id2 = (uint16_t)json_object_get_int(json_object_object_get(obj, "nid"));
        scan->cell.id3 = (uint16_t)json_object_get_int(json_object_object_get(obj, "bsid"));
        scan->cell.timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
        scan->cell.ss = (int16_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        break;
    case XPS_CELL_TYPE_NB_IOT:
        scan->cell.id1 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mcc"));
        scan->cell.id2 = (uint16_t)json_object_get_int(json_object_object_get(obj, "mnc"));
        scan->cell.id4 = (uint16_t)json_object_get_int(json_object_object_get(obj, "tac"));
        scan->cell.id3 = (uint32_t)json_object_get_int(json_object_object_get(obj, "e_cellid"));
        scan->cell.timestamp =
            convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
        scan->cell.ss = (int16_t)json_object_get_int(json_object_object_get(obj, "rssi"));
        break;

    default:
        XPS_log_error("Error loading cell data");
        return;
    }
}

void gps_to_beacon(struct json_object *obj, WifiScan_t *scan)
{
    scan->gps.lat = json_object_get_double(json_object_object_get(obj, "lat"));
    scan->gps.lon = json_object_get_double(json_object_object_get(obj, "lon"));
    if (json_object_object_get(obj, "alt") != NULL)
        scan->gps.altitude = (float)json_object_get_double(json_object_object_get(obj, "alt"));
    else
        scan->gps.altitude = NAN;
    if (json_object_object_get(obj, "hpe") != NULL)
        scan->gps.hpe = (uint32_t)json_object_get_double(json_object_object_get(obj, "hpe"));
    else
        scan->gps.hpe = 0;
    if (json_object_object_get(obj, "vpe") != NULL)
        scan->gps.vpe = (uint32_t)json_object_get_int(json_object_object_get(obj, "vpe"));
    else
        scan->gps.vpe = 0;
    if (json_object_object_get(obj, "speed") != NULL)
        scan->gps.speed = (float)json_object_get_double(json_object_object_get(obj, "speed"));
    else
        scan->gps.speed = NAN;
    if (json_object_object_get(obj, "bearing") != NULL)
        scan->gps.bearing = (float)json_object_get_double(json_object_object_get(obj, "bearing"));
    else
        scan->gps.bearing = NAN;
    if (json_object_object_get(obj, "nsat") != NULL)
        scan->gps.nsat = (uint32_t)json_object_get_int(json_object_object_get(obj, "nsat"));
    else
        scan->gps.nsat = 0;
    scan->gps.timestamp =
        convert_age(json_object_get_int(json_object_object_get(obj, "age")), time(NULL));
}

time_t convert_age(int age, time_t t)
{
    return t - age;
}

XPS_CellType_t determine_cell_type(char *ctype)
{
    char *upper = ctype;
    while (*upper) {
        *upper = toupper((unsigned char)*upper);
        upper++;
    }
    if (strcmp(ctype, "GSM") == 0)
        return XPS_CELL_TYPE_GSM;
    else if (strcmp(ctype, "UMTS") == 0)
        return XPS_CELL_TYPE_UMTS;
    else if (strcmp(ctype, "LTE") == 0)
        return XPS_CELL_TYPE_LTE;
    else if (strcmp(ctype, "CDMA") == 0)
        return XPS_CELL_TYPE_CDMA;
    else if (strcmp(ctype, "NBIOT") == 0)
        return XPS_CELL_TYPE_NB_IOT;
    else {
        XPS_log_error("Error - Bad cell type found: (%s)", ctype);
    }
    return 0;
}

void log_ap(int num, XPS_ScannedAP_t ap, int ap_connected)
{
    if (num == ap_connected)
        ap_connected = 1;
    else
        ap_connected = 0;

    XPS_log_debug(
        "AP #%d - mac: %02X%02X%02X%02X%02X%02X, chan: %u, rssi: %d, age: %u, connected: %s",
        num + 1, ap.mac[0], ap.mac[1], ap.mac[2], ap.mac[3], ap.mac[4], ap.mac[5], ap.freq, ap.rssi,
        (uint32_t)ap.timestamp, connected(ap_connected));
}

void log_cell(WifiScan_t *scan)
{
    switch (scan->cell.type) {
    case XPS_CELL_TYPE_GSM:
        XPS_log_debug(
            "Cell - type: GSM, mcc: %d, mnc: %d, lac: %d, ci: %d, rssi: %d, age: %u, connected: %s",
            scan->cell.id1, scan->cell.id2, scan->cell.id4, scan->cell.id3, scan->cell.ss,
            (uint32_t)scan->cell.timestamp, connected(scan->cell_connected));
        break;
    case XPS_CELL_TYPE_UMTS:
        XPS_log_debug(
            "Cell - type: UMTS, mcc: %d, mnc: %d, lac: %d, ci: %d, rssi: %d, age: %u, connected: %s",
            scan->cell.id1, scan->cell.id2, scan->cell.id4, scan->cell.id3, scan->cell.ss,
            (uint32_t)scan->cell.timestamp, connected(scan->cell_connected));
        break;
    case XPS_CELL_TYPE_LTE:
        XPS_log_debug(
            "Cell - type: LTE, mcc: %d, mnc: %d, tac: %d, eucid: %d, rssi: %d, age: %u, connected: %s",
            scan->cell.id1, scan->cell.id2, scan->cell.id4, scan->cell.id3, scan->cell.ss,
            (uint32_t)scan->cell.timestamp, connected(scan->cell_connected));
        break;
    case XPS_CELL_TYPE_CDMA:
        XPS_log_debug(
            "Cell - type: CDMA, sid: %d, nid: %d, bsid: %d, rssi: %d, age: %u, connected: %s",
            scan->cell.id1, scan->cell.id2, scan->cell.id3, scan->cell.ss,
            (uint32_t)scan->cell.timestamp, connected(scan->cell_connected));
        break;
    case XPS_CELL_TYPE_NB_IOT:
        XPS_log_debug(
            "Cell - type: NBIOT, mcc: %d, mnc: %d, tac: %d, e_cellid: %d, rssi: %d, age: %u, connected: %s",
            scan->cell.id1, scan->cell.id2, scan->cell.id4, scan->cell.id3, scan->cell.ss,
            (uint32_t)scan->cell.timestamp, connected(scan->cell_connected));
        break;
    default:
        XPS_log_debug("Error: Bad cell type: %d", scan->cell.type);
    }
}

void log_gps(WifiScan_t *scan)
{
    XPS_log_debug(
        "GPS -, lat: %f, lon: %f, hpe: %d, alt: %f, vpe: %d, speed: %f, bearing: %f, nsat: %d, age: %u",
        scan->gps.lat, scan->gps.lon, scan->gps.hpe, scan->gps.altitude, scan->gps.vpe,
        scan->gps.speed, scan->gps.bearing, scan->gps.nsat, (uint32_t)scan->gps.timestamp);
}

void get_next_scan(int *num_aps, XPS_ScannedAP_t **aps, int *num_cells, XPS_ScannedCell_t **cells,
    XPS_GNSS_t **gnss)
{
    *num_aps = scans[index].num_aps;
    *aps = scans[index].aps;
    *num_cells = scans[index].num_cells ? 1 : 0;
    *cells = &scans[index].cell;
    *gnss = &scans[index].gps;
    if (++index == scan_count)
        index = 0;
}
