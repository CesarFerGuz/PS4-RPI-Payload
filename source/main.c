#include "ps4.h"
#include "network.h"

static int running = 1;
static ScePthread server_thid;

void* background_thread(void* arg) {
    UNUSED(arg);
    network_init(12800);
    
    while (running) {
        network_listen_loop();
        sceKernelSleep(1); // Pausa nativa de 1 segundo
    }
    
    return NULL;
}

int _main(struct thread *td) {
    UNUSED(td);

    // 1. Cargar librerías dinámicamente en memoria
    initKernel();
    initLibc();
    initNetwork();
    initPthread();
    initSysUtil();

    // 2. Escalar privilegios
    jailbreak();

    // 3. Notificación nativa del sistema
    printf_notification("RPI Payload\nPuerto 12800 en escucha");

    // 4. Crear el hilo con la API de Sony
    scePthreadCreate(&server_thid, NULL, background_thread, NULL, "rpi_server_thread");

    // Retornamos 0 para liberar el puerto 9090 del BinLoader
    return 0;
}
