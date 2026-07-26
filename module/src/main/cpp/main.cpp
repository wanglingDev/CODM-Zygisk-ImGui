#include <pthread.h>
#include <cstring>
#include <jni.h>
#include "hook.h"

// ================================================================
// Entry Point 1: __attribute__((constructor))
// Dipanggil OTOMATIS saat .so di-dlopen ke dalam proses game
// Works dengan: AndKittyInjector, manual dlopen, dll
// ================================================================
__attribute__((constructor)) void lib_main() {
    pthread_t ntid;
    int ret = pthread_create(&ntid, nullptr, hack_thread, nullptr);
    if (ret != 0) {
        LOGE("lib_main: can't create hack_thread: %s", strerror(ret));
    } else {
        LOGI("lib_main: hack_thread spawned via constructor");
        pthread_detach(ntid);
    }
}

// ================================================================
// Entry Point 2: JNI_OnLoad
// Dipanggil oleh AndKittyInjector setelah dlopen
// key == 1337 artinya dipanggil oleh injector (bukan game biasa)
// ================================================================
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key) {
    // Kalau dipanggil oleh game biasa (bukan injector), skip
    if (key != (void*)1337) {
        LOGI("JNI_OnLoad: called by game, skipping");
        return JNI_VERSION_1_6;
    }

    LOGI("JNI_OnLoad: called by AndKittyInjector!");

    // Constructor sudah spawn thread, tapi kalau belum (race condition), spawn lagi
    pthread_t ntid;
    pthread_create(&ntid, nullptr, hack_thread, nullptr);
    pthread_detach(ntid);

    return JNI_VERSION_1_6;
}
