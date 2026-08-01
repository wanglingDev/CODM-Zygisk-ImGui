#include <pthread.h>
#include <atomic>
#include <jni.h>
#include <unistd.h>
#include "hook.h"
#include "zygisk.hpp"
#include "touch_input.h"

// ── nativeInjectEvent hook (needs zygisk::Api + JNI scope) ───────
static void InstallNativeInjectHook(zygisk::Api* api, JNIEnv* env) {
    JNINativeMethod methods[] = {
        {"nativeInjectEvent", "(Landroid/view/InputEvent;)Z",  (void*)hook_nativeInjectEvent},
        {"nativeInjectEvent", "(Landroid/view/InputEvent;)V",  (void*)hook_nativeInjectEvent},
        {"injectInputEvent",  "(Landroid/view/InputEvent;II)Z",(void*)hook_nativeInjectEvent},
    };
    api->hookJniNativeMethods(env, "com/unity3d/player/UnityPlayer", methods, 3);
    LOGI("[ENI] hookJniNativeMethods: nativeInjectEvent registered");
}

static std::atomic<bool> s_started{false};
extern int g_companion_sock;

static void spawn_once(int sock = -1) {
    if (s_started.exchange(true)) { LOGI("[ENI] spawn_once: already running"); return; }
    g_companion_sock = sock;
    pthread_t tid;
    if (pthread_create(&tid, nullptr, hack_thread, nullptr) == 0) {
        LOGI("[ENI] hack_thread spawned OK");
        pthread_detach(tid);
    } else {
        LOGE("[ENI] pthread_create failed");
        s_started.store(false);
    }
}

class CodmMod : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        this->api = api; this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        const char* pkg = env->GetStringUTFChars(args->nice_name, nullptr);
        isTarget = pkg && strcmp(pkg, "com.garena.game.codm") == 0;
        if (pkg) env->ReleaseStringUTFChars(args->nice_name, pkg);
        if (!isTarget) api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        else LOGI("[ENI] Zygisk: target detected");
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (!isTarget) return;

        // ── Install nativeInjectEvent hook (JNI, runs as game process) ──
        // This intercepts ALL Unity touch input before game processes it
        InstallNativeInjectHook(api, env);

        LOGI("[ENI] Zygisk: postAppSpecialize → spawning hack_thread");
        spawn_once(-1);
    }

private:
    zygisk::Api* api   = nullptr;
    JNIEnv*      env   = nullptr;
    bool      isTarget = false;
};

REGISTER_ZYGISK_MODULE(CodmMod)

// Companion no longer needed for touch (kept for future use)
static void companion_handler(int sock) { close(sock); }
REGISTER_ZYGISK_COMPANION(companion_handler)

__attribute__((constructor)) void lib_main() {
    LOGI("[ENI] constructor fired");
    spawn_once(-1);
}
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM*, void* key) {
    if (key == (void*)1337) { LOGI("[ENI] JNI_OnLoad key=1337"); spawn_once(-1); }
    return JNI_VERSION_1_6;
}
