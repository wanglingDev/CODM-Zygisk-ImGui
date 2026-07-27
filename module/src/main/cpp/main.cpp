#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include "hook.h"
#include "zygisk.hpp"

// ─────────────────────────────────────────────────────────────────
//  Thread spawner — atomic guard so we only start once per process
// ─────────────────────────────────────────────────────────────────
static std::atomic<bool> s_started{false};

static void spawn_once() {
    if (s_started.exchange(true)) {
        LOGI("[ENI] spawn_once: thread already running, skipping");
        return;
    }
    pthread_t tid;
    if (pthread_create(&tid, nullptr, hack_thread, nullptr) == 0) {
        LOGI("[ENI] hack_thread spawned OK");
        pthread_detach(tid);
    } else {
        LOGE("[ENI] pthread_create failed");
        s_started.store(false);
    }
}

// ─────────────────────────────────────────────────────────────────
//  Zygisk Module — proper API entry point
//  This is the REAL reason the module runs inside CODM's process.
// ─────────────────────────────────────────────────────────────────
class CodmMod : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // args->nice_name is the package name
        const char *pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        isTarget = pkg && strcmp(pkg, "com.garena.game.codm") == 0;
        if (pkg) env->ReleaseStringUTFChars(args->nice_name, pkg);

        if (!isTarget) {
            // Unload ourselves from every non-target process — saves memory
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        } else {
            LOGI("[ENI] Zygisk: target process detected, staying loaded");
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (!isTarget) return;
        LOGI("[ENI] Zygisk: postAppSpecialize → spawning hack_thread");
        spawn_once();
    }

private:
    zygisk::Api *api  = nullptr;
    JNIEnv     *env   = nullptr;
    bool    isTarget  = false;
};

// ─────────────────────────────────────────────────────────────────
//  Register with Zygisk daemon
// ─────────────────────────────────────────────────────────────────
REGISTER_ZYGISK_MODULE(CodmMod)

// ─────────────────────────────────────────────────────────────────
//  Fallback entry points for AndKittyInjector / manual inject
// ─────────────────────────────────────────────────────────────────
__attribute__((constructor)) void lib_main() {
    LOGI("[ENI] constructor fired");
    spawn_once();
}

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM *vm, void *key) {
    if (key == (void *)1337) {
        LOGI("[ENI] JNI_OnLoad: injector confirmed (key=1337)");
        spawn_once();
    }
    return JNI_VERSION_1_6;
}
