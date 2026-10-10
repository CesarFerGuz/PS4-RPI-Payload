#include "http_client.h"
#include "ps4.h"

// Raw socket HTTP client using native libPS4 sceNet functions.
// It connects to the ESP32 proxy (HTTP) to fetch specific byte ranges.

int http_client_init(void) {
    return 0; // No initialization needed, libPS4 already does it.
}

void http_client_term(void) {
    // Nothing to terminate.
}

// Parses "http://192.168.68.x/path" -> ip="192.168.68.x", path="/path"
static void parse_url(const char* url, char* ip, int* port, char* path) {
    const char* p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == ':' && p[5] == '/' && p[6] == '/') {
        p += 7;
    }
    int i = 0;
    while (*p != '/' && *p != ':' && *p != '\0' && i < 63) {
        ip[i++] = *p++;
    }
    ip[i] = '\0';
    
    *port = 80;
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }
    
    int j = 0;
    while (*p != '\0' && j < 255) {
        path[j++] = *p++;
    }
    if (j == 0) {
        path[0] = '/';
        path[1] = '\0';
    } else {
        path[j] = '\0';
    }
}

// Convert IP string "192.168.68.103" to integer for sceNet
static unsigned int parse_ip(const char* ip_str) {
    unsigned int ip[4] = {0};
    int c = 0;
    const char* p = ip_str;
    while (*p && c < 4) {
        while (*p >= '0' && *p <= '9') {
            ip[c] = ip[c] * 10 + (*p - '0');
            p++;
        }
        if (*p == '.') p++;
        c++;
    }
    // Network byte order is big-endian
    return (ip[0] | (ip[1] << 8) | (ip[2] << 16) | (ip[3] << 24));
}

int http_download_range(const char* url, unsigned long offset, unsigned long size, void* buffer, unsigned long* downloaded) {
    char ip[64] = {0};
    int port = 80;
    char path[256] = {0};
    parse_url(url, ip, &port, path);

    int sock = sceNetSocket("http_client_sock", AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return sock;

    struct sockaddr_in server_addr;
    server_addr.sin_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = sceNetHtons(port);
    server_addr.sin_addr.s_addr = parse_ip(ip);

    if (sceNetConnect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        sceNetSocketClose(sock);
        return -2;
    }

    char request[512];
    snprintf(request, sizeof(request), 
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Range: bytes=%lu-%lu\r\n"
             "Connection: close\r\n\r\n", 
             path, ip, offset, offset + size - 1);

    sceNetSend(sock, request, strlen(request), 0);

    // Read headers
    char hdr_buf[1024];
    int hdr_pos = 0;
    int header_end = 0;
    while (hdr_pos < 1023) {
        char c;
        if (sceNetRecv(sock, &c, 1, 0) <= 0) break;
        hdr_buf[hdr_pos++] = c;
        if (hdr_pos >= 4 && hdr_buf[hdr_pos-4] == '\r' && hdr_buf[hdr_pos-3] == '\n' && hdr_buf[hdr_pos-2] == '\r' && hdr_buf[hdr_pos-1] == '\n') {
            header_end = 1;
            break;
        }
    }

    if (!header_end) {
        sceNetSocketClose(sock);
        return -3; // Bad response
    }

    unsigned long total_read = 0;
    char* ptr = (char*)buffer;

    while (total_read < size) {
        int read = sceNetRecv(sock, ptr + total_read, size - total_read, 0);
        if (read < 0) {
            sceNetSocketClose(sock);
            return read;
        }
        if (read == 0) {
            break;
        }
        total_read += read;
    }

    if (downloaded) *downloaded = total_read;

    sceNetSocketClose(sock);
    return 0;
}

int http_download_range_to_file(const char* url, unsigned long offset, unsigned long size, int fd, unsigned long* downloaded) {
    char ip[64] = {0};
    int port = 80;
    char path[256] = {0};
    parse_url(url, ip, &port, path);

    int sock = sceNetSocket("http_client_sock", AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return sock;

    struct sockaddr_in server_addr;
    server_addr.sin_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = sceNetHtons(port);
    server_addr.sin_addr.s_addr = parse_ip(ip);

    if (sceNetConnect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        sceNetSocketClose(sock);
        return -2;
    }

    char request[512];
    snprintf(request, sizeof(request), 
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Range: bytes=%lu-%lu\r\n"
             "Connection: close\r\n\r\n", 
             path, ip, offset, offset + size - 1);

    sceNetSend(sock, request, strlen(request), 0);

    char hdr_buf[1024];
    int hdr_pos = 0;
    int header_end = 0;
    while (hdr_pos < 1023) {
        char c;
        if (sceNetRecv(sock, &c, 1, 0) <= 0) break;
        hdr_buf[hdr_pos++] = c;
        if (hdr_pos >= 4 && hdr_buf[hdr_pos-4] == '\r' && hdr_buf[hdr_pos-3] == '\n' && hdr_buf[hdr_pos-2] == '\r' && hdr_buf[hdr_pos-1] == '\n') {
            header_end = 1;
            break;
        }
    }

    if (!header_end) {
        sceNetSocketClose(sock);
        return -3;
    }

    unsigned long total_read = 0;
    char chunk[16384];

    while (total_read < size) {
        unsigned long to_read = (size - total_read > sizeof(chunk)) ? sizeof(chunk) : (size - total_read);
        int read = sceNetRecv(sock, chunk, to_read, 0);
        if (read < 0) {
            sceNetSocketClose(sock);
            return read;
        }
        if (read == 0) {
            break;
        }
        syscall(4, fd, chunk, read); // write to file
        total_read += read;
    }

    if (downloaded) *downloaded = total_read;

    sceNetSocketClose(sock);
    return 0;
}
