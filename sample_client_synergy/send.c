#include <network_thread.h>
#include <nxd_dns.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "send.h"
#include <limits.h>
#include "libel.h"
#include <errno.h>

void print_to_console(const char* msg);

static char *itoa_simple_helper(char *dest, int i) {
  if (i <= -10) {
    dest = itoa_simple_helper(dest, i/10);
  }
  // my_printf("itoa: %c\n", '0' - i%10);
  *dest++ = '0' - i%10;
  return dest;
}

static char *itoa_simple(char *dest, int i) {
  char *s = dest;
  if (i < 0) {
    *s++ = '-';
  } else {
    i = -i;
  }
  *itoa_simple_helper(s, i) = '\0';
  return dest;
}

extern NX_DNS g_dns_client;

bool hostname_to_ip(char *hostname, char *ip, uint16_t ip_len)
{
    UINT status = NX_SUCCESS;
    ULONG dest;
    char tmp[4];

    /* Lookup server name using DNS */
    status = nx_dns_host_by_name_get(&g_dns_client, (UCHAR *)hostname, &dest, TX_WAIT_FOREVER);
    if (status != NX_SUCCESS) {
        /* assume ip address if it wasn't resolved */
        strcpy(ip, hostname);
        return true;
    }
    ip[0] = '\0';
    for (int i = 3; i >= 0; i--) {
        strncat(ip, itoa_simple(tmp, (dest >> (i * 8)) & 0xff), ip_len);
        if (i > 0)
            strncat(ip, ".", ip_len);
    }
    return true;
}


int send_request(
    char *request, int req_size, uint8_t *response, int resp_size, char *server, int port)
{
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    char ipaddr[16]; // the char representation of an ipv4 address

    // Lookup server ip address.
    if (!hostname_to_ip(server, ipaddr, sizeof(ipaddr))) {
        printf("Error: Could not resolve host %s\n", server);
        return -1;
    }

#if 1
    // Init server address struct and set ip and port.
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ipaddr);

    // Open socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("Error: cannot open socket. Error %s\n", strerror(errno));
        return -1;
    }

    // Set socket timeout to 10 seconds.
    struct timeval tv = { 10, 0 };
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval))) {
        printf("Error: setsockopt failed\n");
        return -1;
    }

    // Connect.
    int32_t rc = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (rc < 0) {
        close(sockfd);
        printf("Error: unable to connect to server\n");
        return -1;
    }

    // Send request.
    rc = send(sockfd, request, (size_t)req_size, 0);
    if (rc != (int32_t)req_size) {
        close(sockfd);
        printf("Error: sent a different number of bytes (%d) from expected\n", rc);
        return -1;
    }

    // Read response.
    rc = recv(sockfd, response, resp_size, MSG_DONTWAIT /* MSG_WAITALL */);
    if (rc < 0) {
        printf("Error: unable to receive response\n");
        return -1;
    }
    return (int)rc;
#endif
    return -1;
}
