#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include "hook.h"

static std::atomic<bool> s_started{false};

static void spawn_once() {
    if (s_started.exchange(true)) {
        LOGI("[ENI] spawn_once: already running, skipping");
        return;
    }
    pthread_t tid;
    if (pthread_create(&tid, nullptr, hack_thread, nullptr) == 0) {
        LOGI("lib_main: hack_thread spawned");
        pthread_detach(tid);
    } else {
        LOGE("lib_main: can\'t create hack_thread: %s", strerror(0));
        s_started.store(false);
    }
}

__attribute__((constructor)) void lib_main() { spawn_once(); }

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key) {
    if (key == (void*)1337) {
        LOGI("[ENI] JNI_OnLoad: injector call");
        spawn_once();
    }
    return JNI_VERSION_1_6;
}
