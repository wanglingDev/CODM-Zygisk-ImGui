#include <pthread.h>
#include <cstring>
#include "hook.h"

// Entry point untuk external injector (Android-GUI-Injector, AndKittyInjector, dll)
// Dipanggil otomatis saat .so di-dlopen ke dalam proses game
__attribute__((constructor)) void lib_main() {
    pthread_t ntid;
    int ret = pthread_create(&ntid, nullptr, hack_thread, nullptr);
    if (ret != 0) {
        LOGE("lib_main: can't create hack_thread: %s", strerror(ret));
    } else {
        LOGI("lib_main: hack_thread spawned");
        pthread_detach(ntid);
    }
}
