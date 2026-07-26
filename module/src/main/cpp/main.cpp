#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include "hook.h"

static std::atomic<bool> s_started{false};

static void spawn_once() {
    if (s_started.exchange(true)) {
        LOGI("[ENI] spawn_once: thread already running, skipping");
        return;
    }
    pthread_t ntid;
    int ret = pthread_create(&ntid, nullptr, hack_thread, nullptr);
    if (ret != 0) {
        LOGE("[ENI] pthread_create FAILED: %s", strerror(ret));
        s_started.store(false);
    } else {
        LOGI("[ENI] hack_thread spawned OK");
        pthread_detach(ntid);
    }
}

// Fires on dlopen() — before JNI_OnLoad
__attribute__((constructor)) void lib_main() {
    LOGI("[ENI] constructor fired");
    spawn_once();
}

// Called by AndKittyInjector after dlopen()
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key) {
    if (key != (void*)1337) {
        LOGI("[ENI] JNI_OnLoad: not injector call, skip");
        return JNI_VERSION_1_6;
    }
    LOGI("[ENI] JNI_OnLoad: injector confirmed (key=1337)");
    spawn_once();   // no-op if constructor already ran; safe fallback if it didn't
    return JNI_VERSION_1_6;
}
