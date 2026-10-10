#include "ps4.h"
#include "installer.h"

enum bgft_task_option_t {
    BGFT_TASK_OPTION_NONE = 0x0,
    BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM = 0x10000,
};

struct SceBgftDownloadParam {
    int user_id;
    int entitlement_type;
    const char* id;
    const char* content_url;
    const char* content_ex_url;
    const char* content_name;
    const char* icon_path;
    const char* sku_id;
    int option;
    const char* playgo_scenario_id;
    const char* release_date;
    const char* package_type;
    const char* package_sub_type;
    unsigned long package_size;
};

static int g_bgft_initialized = 0;

static int init_bgft(void) {
    if (g_bgft_initialized) return 0;
    
    int libAppInstUtil = -1;
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceAppInstUtil.sprx", 0, &libAppInstUtil, 0) != 0) return -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0) return -1;
    
    int (*_sceAppInstUtilInitialize)(void) = NULL;
    int (*_sceBgftServiceIntInit)(void*) = NULL;
    syscall(591, libAppInstUtil, "sceAppInstUtilInitialize", (void**)&_sceAppInstUtilInitialize);
    syscall(591, libBgft, "sceBgftServiceIntInit", (void**)&_sceBgftServiceIntInit);
    
    if (_sceAppInstUtilInitialize) _sceAppInstUtilInitialize();
    
    if (_sceBgftServiceIntInit) {
        struct {
            void* mem;
            unsigned long size;
        } bgft_params = {
            .mem = (void*)syscall(477, NULL, 0x100000, 3, 4098, -1, 0),
            .size = 0x100000
        };
        int ret = _sceBgftServiceIntInit(&bgft_params);
        if (ret != 0 && ret != (int)0x80990001) {
            printf_notification("Error: Init BGFT fallo (0x%08X)", ret);
            return -1;
        }
    }
    
    g_bgft_initialized = 1;
    return 0;
}

int installer_enqueue_pkg(const char* url, const char* content_id, unsigned long size, const char* name, const char* icon, const char* package_type, int is_patch, int* out_error) {
    if (out_error) *out_error = 0;
    int ret;
    int task_id = 0;
    int libAppInstUtil = -1;
    int libBgft = -1;

    // Usamos la ruta absoluta que exige el kernel de la PS4
    ret = syscall(594, "/system/common/lib/libSceAppInstUtil.sprx", 0, &libAppInstUtil, 0);
    if (ret != 0 || libAppInstUtil <= 0) {
        printf_notification("Error Fase 1: AppInstUtil (0x%08X)", ret);
        return -1;
    }

    ret = syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0);
    if (ret != 0 || libBgft <= 0) {
        printf_notification("Error Fase 1: libBgft (0x%08X)", ret);
        return -1;
    }
    
    if (init_bgft() != 0) return -1;

    int (*_sceBgftServiceIntDownloadRegisterTask)(const struct SceBgftDownloadParam* param, int* taskId) = NULL;
    int (*_sceBgftServiceIntDebugDownloadRegisterPkg)(const struct SceBgftDownloadParam* param, int* taskId) = NULL;
    int (*_sceBgftServiceDownloadStartTask)(int taskId) = NULL;
    int (*_sceBgftServiceDownloadStartPlayGo)(int taskId) = NULL;

    syscall(591, libBgft, "sceBgftServiceIntDownloadRegisterTask", (void**)&_sceBgftServiceIntDownloadRegisterTask);
    syscall(591, libBgft, "sceBgftServiceIntDebugDownloadRegisterPkg", (void**)&_sceBgftServiceIntDebugDownloadRegisterPkg);
    
    // PlayGo Dynamic Resolution
    syscall(591, libBgft, "sceBgftServiceDownloadStartPlayGoTask", (void**)&_sceBgftServiceDownloadStartPlayGo);
    if (!_sceBgftServiceDownloadStartPlayGo) syscall(591, libBgft, "sceBgftServiceIntDownloadStartPlayGoTask", (void**)&_sceBgftServiceDownloadStartPlayGo);
    if (!_sceBgftServiceDownloadStartPlayGo) syscall(591, libBgft, "sceBgftServiceDownloadStartPlaygoTask", (void**)&_sceBgftServiceDownloadStartPlayGo);
    if (!_sceBgftServiceDownloadStartPlayGo) syscall(591, libBgft, "sceBgftServiceIntDownloadStartPlaygoTask", (void**)&_sceBgftServiceDownloadStartPlayGo);
    if (!_sceBgftServiceDownloadStartPlayGo) syscall(591, libBgft, "sceBgftDownloadStartPlayGo", (void**)&_sceBgftServiceDownloadStartPlayGo);
    
    syscall(591, libBgft, "sceBgftServiceDownloadStartTask", (void**)&_sceBgftServiceDownloadStartTask);
    if (!_sceBgftServiceDownloadStartTask) {
        syscall(591, libBgft, "sceBgftServiceIntDownloadStartTask", (void**)&_sceBgftServiceDownloadStartTask);
    }

    if (!_sceBgftServiceIntDownloadRegisterTask || (!_sceBgftServiceDownloadStartTask && !_sceBgftServiceDownloadStartPlayGo) || !_sceBgftServiceIntDebugDownloadRegisterPkg) {
        printf_notification("Error Fase 2: Punteros nulos");
        return -1;
    }

    int libUserService = -1;
    syscall(594, "libSceUserService.sprx", 0, &libUserService, 0);
    
    int uid = 0x10000000; // Usuario por defecto
    if (libUserService > 0) {
        int (*_sceUserServiceInitialize)(void*) = NULL;
        int (*_sceUserServiceGetForegroundUser)(int*) = NULL;
        int (*_sceUserServiceTerminate)(void) = NULL;
        
        syscall(591, libUserService, "sceUserServiceInitialize", (void**)&_sceUserServiceInitialize);
        syscall(591, libUserService, "sceUserServiceGetForegroundUser", (void**)&_sceUserServiceGetForegroundUser);
        syscall(591, libUserService, "sceUserServiceTerminate", (void**)&_sceUserServiceTerminate);
        
        if (_sceUserServiceInitialize && _sceUserServiceGetForegroundUser && _sceUserServiceTerminate) {
            struct { unsigned int priority; } us_params = { 0x2BC };
            _sceUserServiceInitialize(&us_params);
            _sceUserServiceGetForegroundUser(&uid);
            _sceUserServiceTerminate();
        }
    }

    struct SceBgftDownloadParam param;
    memset(&param, 0, sizeof(param));
    
    param.user_id = uid;
    param.entitlement_type = 5;
    param.id = (content_id && content_id[0]) ? content_id : NULL;
    param.content_url = url;
    param.content_name = name;
    param.icon_path = (icon && icon[0]) ? icon : "";
    param.package_type = package_type;
    param.package_sub_type = "";
    param.playgo_scenario_id = "0";
    param.option = BGFT_TASK_OPTION_DISABLE_CDN_QUERY_PARAM;
    param.package_size = size;

    if (!is_patch) {
        ret = _sceBgftServiceIntDownloadRegisterTask(&param, &task_id);
    } else {
        ret = _sceBgftServiceIntDebugDownloadRegisterPkg(&param, &task_id);
    }
    
    if (ret == 0) {
        int start_ret = 0;
        if (_sceBgftServiceDownloadStartPlayGo) {
            start_ret = _sceBgftServiceDownloadStartPlayGo(task_id);
        } else {
            start_ret = _sceBgftServiceDownloadStartTask(task_id);
        }
        if (start_ret != 0) {
            printf_notification("Error StartTask BGFT: 0x%08X", start_ret);
        }
        
        return task_id;
    } else {
        if (out_error) *out_error = ret;
        if (ret == 0x80990015) {
            printf_notification("Error: Ya instalado en el sistema o en cola");
        } else if (ret == 0x80990085) {
            printf_notification("Error: Sin espacio de almacenamiento");
        } else if (ret == 0x80990004) {
            printf_notification("Error: Argumento invalido (URL o PKG)");
        } else {
            printf_notification("Error Registro BGFT: 0x%08X", ret);
        }
    }

    return -1;
}

int installer_get_progress(int task_id, struct SceBgftTaskProgress* progress) {
    if (!progress) return -1;
    memset(progress, 0, sizeof(*progress));

    int libBgft = -1;
    int ret = syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0);
    if (ret != 0 || libBgft <= 0) return -1;
    
    if (init_bgft() != 0) return -1;

    int (*_sceBgftServiceDownloadGetProgress)(int taskId, struct SceBgftTaskProgress* progress) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadGetProgress", (void**)&_sceBgftServiceDownloadGetProgress);
    
    if (!_sceBgftServiceDownloadGetProgress) {
        syscall(591, libBgft, "sceBgftServiceIntDownloadGetProgress", (void**)&_sceBgftServiceDownloadGetProgress);
    }
    
    if (!_sceBgftServiceDownloadGetProgress) return -1;
    
    ret = _sceBgftServiceDownloadGetProgress(task_id, progress);
    return ret;
}

int installer_pause_task(int task_id) {
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0 || libBgft <= 0) return -1;
    if (init_bgft() != 0) return -1;
    int (*_sceBgftServiceDownloadPauseTask)(int task_id) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadPauseTask", (void**)&_sceBgftServiceDownloadPauseTask);
    if (!_sceBgftServiceDownloadPauseTask) syscall(591, libBgft, "sceBgftServiceIntDownloadPauseTask", (void**)&_sceBgftServiceDownloadPauseTask);
    if (_sceBgftServiceDownloadPauseTask) return _sceBgftServiceDownloadPauseTask(task_id);
    return -1;
}

int installer_resume_task(int task_id) {
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0 || libBgft <= 0) return -1;
    if (init_bgft() != 0) return -1;
    int (*_sceBgftServiceDownloadResumeTask)(int task_id) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadResumeTask", (void**)&_sceBgftServiceDownloadResumeTask);
    if (!_sceBgftServiceDownloadResumeTask) syscall(591, libBgft, "sceBgftServiceIntDownloadResumeTask", (void**)&_sceBgftServiceDownloadResumeTask);
    if (_sceBgftServiceDownloadResumeTask) return _sceBgftServiceDownloadResumeTask(task_id);
    return -1;
}

int installer_start_task(int task_id) {
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0 || libBgft <= 0) return -1;
    if (init_bgft() != 0) return -1;
    int (*_sceBgftServiceDownloadStartTask)(int task_id) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadStartTask", (void**)&_sceBgftServiceDownloadStartTask);
    if (!_sceBgftServiceDownloadStartTask) syscall(591, libBgft, "sceBgftServiceIntDownloadStartTask", (void**)&_sceBgftServiceDownloadStartTask);
    if (_sceBgftServiceDownloadStartTask) return _sceBgftServiceDownloadStartTask(task_id);
    return -1;
}

int installer_stop_task(int task_id) {
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0 || libBgft <= 0) return -1;
    if (init_bgft() != 0) return -1;
    int (*_sceBgftServiceDownloadStopTask)(int task_id) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadStopTask", (void**)&_sceBgftServiceDownloadStopTask);
    if (!_sceBgftServiceDownloadStopTask) syscall(591, libBgft, "sceBgftServiceIntDownloadStopTask", (void**)&_sceBgftServiceDownloadStopTask);
    if (_sceBgftServiceDownloadStopTask) return _sceBgftServiceDownloadStopTask(task_id);
    return -1;
}

int installer_unregister_task(int task_id) {
    int libBgft = -1;
    if (syscall(594, "/system/common/lib/libSceBgft.sprx", 0, &libBgft, 0) != 0 || libBgft <= 0) return -1;
    
    int (*_sceBgftServiceDownloadUnregisterTask)(int task_id) = NULL;
    syscall(591, libBgft, "sceBgftServiceDownloadUnregisterTask", (void**)&_sceBgftServiceDownloadUnregisterTask);
    if (!_sceBgftServiceDownloadUnregisterTask) syscall(591, libBgft, "sceBgftServiceIntDownloadUnregisterTask", (void**)&_sceBgftServiceDownloadUnregisterTask);
    if (_sceBgftServiceDownloadUnregisterTask) return _sceBgftServiceDownloadUnregisterTask(task_id);
    return -1;
}
