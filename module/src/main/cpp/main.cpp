#include <pthread.h>
#include <atomic>
#include <cstring>
#include <jni.h>
#include "hook.h"

// Guard against the constructor + JNI_OnLoad both spawning hack_thread.
// When AndKittyInjector dlopen()s the .so, __attribute__((constructor)) fires
// first (synchronously, inside dlopen), then JNI_OnLoad is called.
// Without the guard, DobbyHook is called twice on the same address →
// the trampoline is overwritten → hack_thread crashes silently before
// eglSwapBuffers is hooked → no menu, no ESP, nothing.
static std::atomic<bool> s_thread_started{false};

static void spawn_hack_thread() {
    // exchange returns the OLD value; if it was already true, bail out.
    if (s_thread_started.exchange(true)) {
        LOGI("spawn_hack_thread: already started, skipping duplicate spawn");
        return;
    }
    pthread_t ntid;
    int ret = pthread_create(&ntid, nullptr, hack_thread, nullptr);
    if (ret != 0) {
        LOGE("spawn_hack_thread: pthread_create failed: %s", strerror(ret));
        s_thread_started.store(false); // allow retry
    } else {
        LOGI("spawn_hack_thread: hack_thread spawned OK");
        pthread_detach(ntid);
    }
}

// ── Entry Point 1: constructor ────────────────────────────────────────────────
// Fires automatically on dlopen() — before JNI_OnLoad is invoked.
__attribute__((constructor)) void lib_main() {
    spawn_hack_thread();
}

// ── Entry Point 2: JNI_OnLoad ─────────────────────────────────────────────────
// Called by AndKittyInjector after dlopen(); key == 1337 means injector call.
// The constructor already fired → thread is already running.
// We do NOT spawn again — just return the correct JNI version.
extern "C" jint JNIEXPORT JNI_OnLoad(JavaVM* vm, void* key) {
    if (key != (void*)1337) {
        LOGI("JNI_OnLoad: not from injector, skipping");
        return JNI_VERSION_1_6;
    }
    LOGI("JNI_OnLoad: injector call confirmed — constructor already spawned thread");
    // spawn_hack_thread() is safe to call again; the atomic guard prevents
    // a second thread from actually starting.
    spawn_hack_thread();
    return JNI_VERSION_1_6;
}
