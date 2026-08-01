#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include <unistd.h>
#include "hook.h"
#include "zygisk.hpp"
#include "touch_input.h"

// ── Thread spawner ────────────────────────────────────────────────
static std::atomic<bool> s_started{false};
static int g_companion_sock = -1;

struct HackArgs { int sock; };

static void* hack_entry(void* arg) {
    HackArgs* a = (HackArgs*)arg;
    int sock = a ? a->sock : -1;
    delete a;
    // pass companion sock to touch system before hack_thread logic
    extern void* hack_thread(void*);
    // store sock globally so hack_thread can use it
    g_companion_sock = sock;
    return hack_thread(nullptr);
}

static void spawn_once(int companion_sock = -1) {
    if (s_started.exchange(true)) {
        LOGI("[ENI] spawn_once: already running");
        return;
    }
    pthread_t tid;
    HackArgs* args = new HackArgs{companion_sock};
    if (pthread_create(&tid, nullptr, hack_entry, args) == 0) {
        LOGI("[ENI] hack_thread spawned OK");
        pthread_detach(tid);
    } else {
        LOGE("[ENI] pthread_create failed");
        s_started.store(false);
        delete args;
    }
}

// ── Zygisk Module ─────────────────────────────────────────────────
class CodmMod : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        isTarget = pkg && strcmp(pkg, "com.garena.game.codm") == 0;
        if (pkg) env->ReleaseStringUTFChars(args->nice_name, pkg);
        if (!isTarget)
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        else
            LOGI("[ENI] Zygisk: target detected");
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (!isTarget) return;

        // Connect to companion (root) → get touch fd via SCM_RIGHTS
        int sock = api->connectCompanion();
        LOGI("[ENI] companion sock: %d", sock);

        LOGI("[ENI] Zygisk: postAppSpecialize → spawning hack_thread");
        spawn_once(sock);
    }

private:
    zygisk::Api* api  = nullptr;
    JNIEnv*      env  = nullptr;
    bool      isTarget = false;
};

REGISTER_ZYGISK_MODULE(CodmMod)

// ── Companion handler (runs as root, can open /dev/input/) ────────
static void companion_handler(int sock) {
    HandleCompanion(sock);
}

REGISTER_ZYGISK_COMPANION(companion_handler)

// ── Fallback entry points for manual inject ───────────────────────
__attribute__((constructor)) void lib_main() {
    LOGI("[ENI] constructor fired");
    spawn_once(-1); // no companion when manually injected
}

extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM*, void* key) {
    if (key == (void*)1337) {
        LOGI("[ENI] JNI_OnLoad: injector key=1337");
        spawn_once(-1);
    }
    return JNI_VERSION_1_6;
}
