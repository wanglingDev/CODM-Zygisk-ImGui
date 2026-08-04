#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include "hook.h"
#include "zygisk.hpp"
#include "input_hook.h"  // JNI RegisterNatives hook → nativeInjectEvent

static std::atomic<bool> s_started{false};

static void spawn_once() {
    if (s_started.exchange(true)) { LOGI("[ENI] spawn_once: already running"); return; }
    pthread_t tid;
    if (pthread_create(&tid, nullptr, hack_thread, nullptr) == 0) {
        LOGI("[ENI] hack_thread spawned OK");
        pthread_detach(tid);
    } else {
        LOGE("[ENI] pthread_create failed");
        s_started.store(false);
    }
}

// ── Zygisk Module ─────────────────────────────────────────────────
class CodmMod : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api; this->env = env;
    }
    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("[ENI] preApp: '%s'", pkg ? pkg : "null");
        isTarget = pkg && strstr(pkg, "com.garena.game.codm");
        if (pkg) env->ReleaseStringUTFChars(args->nice_name, pkg);
        if (!isTarget) api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        else LOGI("[ENI] TARGET detected!");
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (!isTarget) return;
        LOGI("[ENI] postAppSpecialize: spawning hack_thread");
        spawn_once();
    }
private:
    zygisk::Api* api = nullptr;
    JNIEnv*      env = nullptr;
    bool      isTarget = false;
};

REGISTER_ZYGISK_MODULE(CodmMod)
static void companion_handler(int sock) { close(sock); }
REGISTER_ZYGISK_COMPANION(companion_handler)

// ── JNI_OnLoad: install RegisterNatives hook → catches nativeInjectEvent ─────
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key) {
    // Always install JNI hook (works for both Zygisk and manual inject)
    InstallInputHook(vm);
    LOGI("[ENI] JNI_OnLoad: input hook installed");

    if (key == (void*)1337) {
        LOGI("[ENI] JNI_OnLoad: manual injector key");
        spawn_once();
    }
    return JNI_VERSION_1_6;
}

__attribute__((constructor)) void lib_main() {
    LOGI("[ENI] constructor fired");
    spawn_once();
}
