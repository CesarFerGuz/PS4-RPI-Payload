#ifndef LOCAL_INSTALLER_H
#define LOCAL_INSTALLER_H

struct SceBgftTaskProgress {
    unsigned int bits;
    int errorResult;
    unsigned long length;
    unsigned long transferred;
    unsigned long lengthTotal;
    unsigned long transferredTotal;
    unsigned int numIndex;
    unsigned int numTotal;
    unsigned int restSec;
    unsigned int restSecTotal;
    int preparingPercent;
    int localCopyPercent;
};

int installer_enqueue_pkg(const char* url, const char* content_id, unsigned long size, const char* name, const char* icon, const char* package_type, int is_patch, int* out_error);
int installer_get_progress(int task_id, struct SceBgftTaskProgress* progress);
int installer_pause_task(int task_id);
int installer_resume_task(int task_id);
int installer_start_task(int task_id);
int installer_stop_task(int task_id);
int installer_unregister_task(int task_id);

#endif
