#pragma once
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H



int http_client_init(void);
void http_client_term(void);
int http_download_range(const char* url, unsigned long offset, unsigned long size, void* buffer, unsigned long* downloaded);
int http_download_range_to_file(const char* url, unsigned long offset, unsigned long size, int fd, unsigned long* downloaded);

#endif
