#include "ps4.h"
#include "network.h"
#include "installer.h"
#include "http_client.h"
#include "pkg_parser.h"

static int server_socket = -1;

int network_init(int port) {
    struct sockaddr_in server_addr;

    server_socket = sceNetSocket("rpi_server_sock", AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) return -1;

    int opt = 1;
    sceNetSetsockopt(server_socket, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_len = sizeof(server_addr);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = sceNetHtonl(IN_ADDR_ANY);
    server_addr.sin_port = sceNetHtons(port);

    if (sceNetBind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        sceNetSocketClose(server_socket);
        return -1;
    }

    if (sceNetListen(server_socket, 10) < 0) {
        sceNetSocketClose(server_socket);
        return -1;
    }

    return 0;
}

void network_listen_loop(void) {
    if (server_socket < 0) return;

    struct sockaddr_in client_addr;
    unsigned int client_len = sizeof(client_addr);
    
    int client_sock = sceNetAccept(server_socket, (struct sockaddr*)&client_addr, &client_len);
    if (client_sock >= 0) {
        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        
        int received = sceNetRecv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (received > 0) {
            // Si el header dice Content-Length pero no vemos el body (no hay llaves o http), leer un poco mas
            if (strstr(buffer, "Content-Length:") && !strstr(buffer, "{") && !strstr(buffer, "http")) {
                sceKernelUsleep(50000); // Esperar 50ms para que llegue el body
                int more = sceNetRecv(client_sock, buffer + received, sizeof(buffer) - 1 - received, 0);
                if (more > 0) received += more;
            }
            buffer[received] = '\0';
            
            // Manejar preflight CORS (OPTIONS)
            if (strncmp(buffer, "OPTIONS", 7) == 0) {
                const char* cors_resp = "HTTP/1.1 200 OK\r\n"
                                        "Access-Control-Allow-Origin: *\r\n"
                                        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
                                        "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
                                        "Content-Length: 0\r\n\r\n";
                sceNetSend(client_sock, cors_resp, strlen(cors_resp), 0);
                sceNetSocketClose(client_sock);
                return; // En el loop vuelve a escuchar
            }
            
            int task_id = -1;
            char* tid_ptr = strstr(buffer, "\"task_id\"");
            if (tid_ptr) {
                tid_ptr = strchr(tid_ptr, ':');
                if (tid_ptr) {
                    while (*tid_ptr == ':' || *tid_ptr == ' ' || *tid_ptr == '"') tid_ptr++;
                    task_id = 0;
                    while (*tid_ptr >= '0' && *tid_ptr <= '9') {
                        task_id = task_id * 10 + (*tid_ptr - '0');
                        tid_ptr++;
                    }
                }
            }
            
            // 1. RUTA: PROGRESO
            if (strstr(buffer, "get_task_progress")) {
                struct SceBgftTaskProgress prog;
                int ret = installer_get_progress(task_id, &prog);
                
                char response[512];
                if (ret == 0) {
                    snprintf(response, sizeof(response),
                        "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n"
                        "{\"status\":\"success\",\"length\":%lu,\"transferred\":%lu,\"length_total\":%lu,\"transferred_total\":%lu,\"rest_sec\":%u,\"rest_sec_total\":%u,\"preparing_percent\":%d,\"error\":%d}",
                        prog.length, prog.transferred, prog.lengthTotal, prog.transferredTotal, prog.restSec, prog.restSecTotal, prog.preparingPercent, prog.errorResult);
                } else {
                    snprintf(response, sizeof(response),
                        "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n"
                        "{\"status\":\"fail\",\"error\":-1}");
                }
                sceNetSend(client_sock, response, strlen(response), 0);
                sceNetSocketClose(client_sock);
                return;
            }
            
            // 2. OTRAS RUTAS DE CONTROL (PAUSE, RESUME, STOP)
            if (strstr(buffer, "pause_task") || strstr(buffer, "resume_task") || strstr(buffer, "stop_task") || strstr(buffer, "start_task") || strstr(buffer, "unregister_task")) {
                int ret = -1;
                if (strstr(buffer, "pause_task")) ret = installer_pause_task(task_id);
                else if (strstr(buffer, "resume_task")) ret = installer_resume_task(task_id);
                else if (strstr(buffer, "start_task")) ret = installer_start_task(task_id);
                else if (strstr(buffer, "stop_task")) ret = installer_stop_task(task_id);
                else if (strstr(buffer, "unregister_task")) ret = installer_unregister_task(task_id);
                
                char response[128];
                if (ret == 0) {
                    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n{\"status\":\"success\"}");
                } else {
                    snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n{\"status\":\"fail\"}");
                }
                sceNetSend(client_sock, response, strlen(response), 0);
                sceNetSocketClose(client_sock);
                return;
            }
            
            // 2. RUTA: INSTALL (Por defecto todo lo demás que traiga URL)
            char* url_start = strstr(buffer, "https://");
            if (!url_start) url_start = strstr(buffer, "http://");
            if (!url_start) url_start = strstr(buffer, "https:\\/\\/");
            if (!url_start) url_start = strstr(buffer, "http:\\/\\/");
            
            if (url_start) {
                printf_notification("Iniciando instalacion...");
                
                // NOW truncate the URL (we just need the URL)
                char* url_end = url_start;
                while (*url_end != '\0' && *url_end != '"' && *url_end != ' ' && *url_end != '\r' && *url_end != '\n') {
                    url_end++;
                }
                if (*url_end != '\0') *url_end = '\0';

                // UNESCAPE JSON slashes
                char clean_url[1024];
                int j = 0;
                for (int i = 0; url_start[i] != '\0' && j < sizeof(clean_url) - 1; i++) {
                    if (url_start[i] == '\\' && url_start[i+1] == '/') {
                        clean_url[j++] = '/';
                        i++; // skip the literal forward slash
                    } else {
                        clean_url[j++] = url_start[i];
                    }
                }
                clean_url[j] = '\0';
                url_start = clean_url; // Use clean unescaped string

                char content_id[40] = {0};
                uint64_t pkg_size = 0;
                char content_name[128] = "Remote Package";
                char icon_path[256] = {0};
                char package_type[16] = "PS4GD";
                int is_patch = 0;
                
                // Extraer todo el metadata nativamente desde el PKG
                pkg_extract_metadata(url_start, content_name, sizeof(content_name), icon_path, sizeof(icon_path), content_id, sizeof(content_id), &pkg_size, &is_patch, package_type);


                // Lanzamos el proceso de instalacion
                int out_error = 0;
                int task_id = installer_enqueue_pkg(url_start, content_id, pkg_size, content_name, icon_path, package_type, is_patch, &out_error);
                
                // 2. Respondemos a CURL (ESP32) con el task_id
                char response[256];
                if (task_id >= 0) {
                    snprintf(response, sizeof(response), 
                        "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n"
                        "{\"status\":\"success\",\"task_id\":%d,\"title\":\"%s\"}", task_id, content_name);
                } else {
                    snprintf(response, sizeof(response), 
                        "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n"
                        "{\"status\":\"fail\",\"error\":%d,\"message\":\"Error al registrar\"}", out_error);
                }
                sceNetSend(client_sock, response, strlen(response), 0);
                
                // 3. Cerramos el socket
                sceNetSocketClose(client_sock);
                client_sock = -1;
                
            } else {
                const char* response = "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: application/json\r\n\r\n{\"status\":\"fail\",\"message\":\"No se encontro URL\"}";
                sceNetSend(client_sock, response, strlen(response), 0);
            }
        }
        
        if (client_sock >= 0) {
            sceNetSocketClose(client_sock);
        }
    }
}
