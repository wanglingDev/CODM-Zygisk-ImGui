// ── Zygisk module entry point ─────────────────────────────────────────────────
// This replaces the raw constructor + JNI_OnLoad approach.
// With REGISTER_ZYGISK_MODULE, our code ONLY runs inside the target game
// process — not in zygote, system_server, or any other app.
// Without this, enabling Zygisk + constructor = .so fires in every process.

#include <jni.h>
#include <pthread.h>
#include <atomic>
#include <cstring>
#include "hook.h"
#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static std::atomic<bool> s_started{false};

static void spawn_once() {
    if (s_started.exchange(true)) return;
    pthread_t tid;
    if (pthread_create(&tid, nullptr, hack_thread, nullptr) == 0) {
        LOGI("[ENI] hack_thread spawned from Zygisk postAppSpecialize");
        pthread_detach(tid);
    } else {
        LOGI("[ENI] pthread_create failed");
        s_started.store(false);
    }
}

class CODMModule : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        this->api = api;
        this->env = env;
    }

    // Called before the process is specialized — process name is available here
    void preAppSpecialize(AppSpecializeArgs* args) override {
        if (!args || !args->app_data_dir) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        // Only act on CODM Garena — unload ourselves from everything else
        if (!isGame(env, args->app_data_dir)) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }

    // Called after specialization — process is now the target app
    void postAppSpecialize(const AppSpecializeArgs* args) override {
        if (!enable_hack) return;   // isGame() sets enable_hack = 1
        spawn_once();
    }

private:
    Api*    api = nullptr;
    JNIEnv* env = nullptr;
};

REGISTER_ZYGISK_MODULE(CODMModule)
