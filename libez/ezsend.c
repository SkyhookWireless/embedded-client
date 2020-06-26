#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include "sys/time.h"
#include <unistd.h>

#include "libel.h"
#include "libez.h"

static bool hostname_to_ip(char *hostname, char *ip, uint16_t ip_len)
{
    struct hostent *he = gethostbyname(hostname);

    if (he == NULL) {
        XPS_log_error("Unable to host by name");
        return false;
    }

    struct in_addr **addr_list = (struct in_addr **)he->h_addr_list;

    for (size_t i = 0; addr_list[i] != NULL; i++) {
        //Return the first one;
        strncpy(ip, inet_ntoa(*addr_list[i]), ip_len);
        return true;
    }

    return false;
}

int XPS_send_request(
    char *request, int req_size, uint8_t *response, int resp_size, char *server, uint16_t port)
{
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    char ipaddr[16]; // the char representation of an ipv4 address
    int32_t rc;

    XPS_log_debug("Connecting to server: %s, port: %d", server, port);
    // Lookup server ip address.
    if (!hostname_to_ip(server, ipaddr, sizeof(ipaddr))) {
        XPS_log_error("Could not resolve host %s", server);
        return -XPS_STATUS_ERROR_SERVER_UNAVAILABLE;
    }

    // Init server address struct and set ip and port.
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ipaddr);

    // Open socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        XPS_log_error("Cannot open socket");
        return -XPS_STATUS_ERROR_SERVER_UNAVAILABLE;
    }

    // Set socket timeout to 10 seconds.
    struct timeval tv = { 10, 0 };
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval))) {
        XPS_log_error("setsockopt failed");
        return -XPS_STATUS_ERROR_SERVER_UNAVAILABLE;
    }

    // Connect.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        XPS_log_error("Unable to establish connection to server");
        return -XPS_STATUS_ERROR_SERVER_UNAVAILABLE;
    }

    // Send request.
    if ((rc = send(sockfd, request, (size_t)req_size, 0)) != (int32_t)req_size) {
        close(sockfd);
        XPS_log_error("Sent a different number of bytes (%d) than expected", rc);
        return -XPS_STATUS_ERROR_NETWORK_ERROR;
    }

    // Read response.
    rc = recv(sockfd, response, resp_size, MSG_WAITALL);
    if (rc < 0) {
        XPS_log_error("Bad or no response from server");
        return -XPS_STATUS_ERROR_NETWORK_ERROR;
    }

    return (int)rc;
}
