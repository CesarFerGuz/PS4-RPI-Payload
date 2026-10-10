#include "pkg_parser.h"
#include "http_client.h"
#include "ps4.h"

#define SWAP32(x) ((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | (((x) & 0xFF0000) >> 8) | (((x) & 0xFF000000) >> 24))

int pkg_extract_metadata(const char* url, char* title_out, int title_out_size, char* icon_path_out, int icon_path_out_size, char* content_id_out, int content_id_size, uint64_t* out_pkg_size, int* out_is_patch, char* out_package_type) {
    unsigned long downloaded = 0;
    unsigned char header[0x2000];
    
    int res = http_download_range(url, 0, sizeof(header), header, &downloaded);
    if (res != 0 || downloaded < 0x20) {
        return -1;
    }

    if (header[0] != 0x7F || header[1] != 'C' || header[2] != 'N' || header[3] != 'T') {
        return -1;
    }

    uint32_t content_type = SWAP32(*(uint32_t*)(header + 0x74));
    uint32_t content_flags = SWAP32(*(uint32_t*)(header + 0x78));

    if (out_is_patch) {
        *out_is_patch = 0;
        if ((content_flags & 0x00100000) || (content_flags & 0x40000000) || (content_flags & 0x41000000) || (content_flags & 0x60000000)) {
            *out_is_patch = 1;
        }
    }

    if (out_package_type) {
        switch (content_type) {
            case 0x1A: strcpy(out_package_type, "PS4GD"); break;
            case 0x1B: strcpy(out_package_type, "PS4AC"); break;
            case 0x1C: strcpy(out_package_type, "PS4AL"); break;
            case 0x1E: strcpy(out_package_type, "PS4DP"); break;
            default: strcpy(out_package_type, "PS4GD"); break;
        }
    }

    uint32_t entry_count = SWAP32(*(uint32_t*)(header + 0x10));
    uint32_t table_offset = SWAP32(*(uint32_t*)(header + 0x18));

    if (entry_count > 10000) {
        return -1;
    }
    
    unsigned char* entry_table = NULL;
    int must_free_table = 0;
    
    if (table_offset + entry_count * 0x20 <= downloaded) {
        entry_table = header + table_offset;
    } else {
        entry_table = (unsigned char*)malloc(entry_count * 0x20);
        if (!entry_table) return -1;
        must_free_table = 1;
        res = http_download_range(url, table_offset, entry_count * 0x20, entry_table, &downloaded);
        if (res != 0 || downloaded != entry_count * 0x20) {
            free(entry_table);
            return -1;
        }
    }

    uint32_t sfo_offset = 0, sfo_size = 0;
    uint32_t icon_offset = 0, icon_size = 0;

    for (uint32_t i = 0; i < entry_count; i++) {
        uint32_t id = SWAP32(*(uint32_t*)(entry_table + (i * 0x20) + 0x00));
        uint32_t off = SWAP32(*(uint32_t*)(entry_table + (i * 0x20) + 0x10));
        uint32_t sz = SWAP32(*(uint32_t*)(entry_table + (i * 0x20) + 0x14));
        
        if (id == PKG_ENTRY_ID__PARAM_SFO) {
            sfo_offset = off; sfo_size = sz;
        } else if (id == PKG_ENTRY_ID__ICON0_PNG) {
            icon_offset = off; icon_size = sz;
        }
    }

    if (must_free_table) free(entry_table);

    if (sfo_offset > 0 && sfo_size > 0 && sfo_size < 100000) {
        unsigned char* sfo_data = (unsigned char*)malloc(sfo_size);
        if (sfo_data) {
            if (http_download_range(url, sfo_offset, sfo_size, sfo_data, &downloaded) == 0 && downloaded == sfo_size) {
                if (sfo_data[0] == 0 && sfo_data[1] == 'P' && sfo_data[2] == 'S' && sfo_data[3] == 'F') {
                    uint32_t key_table_offset = *(uint32_t*)(sfo_data + 0x08);
                    uint32_t value_table_offset = *(uint32_t*)(sfo_data + 0x0C);
                    uint32_t sfo_entry_count = *(uint32_t*)(sfo_data + 0x10);
                    
                    unsigned char* sfo_entry_table = sfo_data + 0x14;
                    for (uint32_t i = 0; i < sfo_entry_count; i++) {
                        uint16_t key_offset = *(uint16_t*)(sfo_entry_table + (i * 0x10) + 0x00);
                        uint32_t value_offset = *(uint32_t*)(sfo_entry_table + (i * 0x10) + 0x0C);
                        char* key = (char*)(sfo_data + key_table_offset + key_offset);
                        char* value = (char*)(sfo_data + value_table_offset + value_offset);
                        
                        if (strcmp(key, "TITLE") == 0) {
                            strncpy(title_out, value, title_out_size - 1);
                            title_out[title_out_size - 1] = '\0';
                            break;
                        }
                    }
                }
            }
            free(sfo_data);
        }
    }

    if (content_id_out && content_id_size > 36) {
        memcpy(content_id_out, header + 0x40, 36);
        content_id_out[36] = '\0';
    }
    
    if (out_pkg_size) {
        uint64_t p_size = 0;
        for (int i = 0; i < 8; i++) {
            p_size = (p_size << 8) | header[0x430 + i];
        }
        *out_pkg_size = p_size;
    }

    if (icon_offset > 0 && icon_size > 0) {
        char icon_file[128];
        char temp_id[40] = "unknown";
        if (content_id_out && content_id_size > 36 && content_id_out[0] != '\0') {
            strcpy(temp_id, content_id_out);
        }
        snprintf(icon_file, sizeof(icon_file), "/user/appmeta/icon_%s.png", temp_id);
        
        int fd = syscall(5, icon_file, 0x0001 | 0x0200 | 0x0400, 0777); 
        if (fd >= 0) {
            int res = http_download_range_to_file(url, icon_offset, icon_size, fd, &downloaded);
            syscall(6, fd); 
            
            if (res == 0 && downloaded == icon_size) {
                strncpy(icon_path_out, icon_file, icon_path_out_size - 1);
                icon_path_out[icon_path_out_size - 1] = '\0';
            }
        }
    }

    return 0;
}
