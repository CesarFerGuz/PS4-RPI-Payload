#pragma once

#ifndef PKG_PARSER_H
#define PKG_PARSER_H

#define PKG_ENTRY_ID__PARAM_SFO 0x1000
#define PKG_ENTRY_ID__ICON0_PNG 0x1200

#include <stdint.h>
int pkg_extract_metadata(const char* url, char* title_out, int title_out_size, char* icon_path_out, int icon_path_out_size, char* content_id_out, int content_id_size, uint64_t* out_pkg_size, int* out_is_patch, char* out_package_type);

#endif
