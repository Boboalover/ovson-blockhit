#include "PacketHook.h"
#include "BlockHitSound.h"
#include "PacketFilterHook_bytes.h"
#include "../Java.h"
#include "../Utils/Logger.h"
#include "../Chat/ChatHook.h"
#include "../Plugins/PluginLoader.h"

#include <atomic>
#include <string>

namespace {

std::atomic<bool> g_acceptNativeCallbacks{false};
std::atomic<unsigned int> g_activeNativeCallbacks{0};

class NativeCallbackGuard {
public:
    NativeCallbackGuard() {
        if (!g_acceptNativeCallbacks.load(std::memory_order_acquire)) return;
        g_activeNativeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        counted_ = true;
        if (g_acceptNativeCallbacks.load(std::memory_order_acquire)) {
            active_ = true;
            return;
        }
        release();
    }

    ~NativeCallbackGuard() { release(); }
    explicit operator bool() const { return active_; }

private:
    void release() {
        if (!active_ && !counted_) return;
        active_ = false;
        counted_ = false;
        g_activeNativeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        g_activeNativeCallbacks.notify_all();
    }

    bool active_ = false;
    bool counted_ = false;
};

void disableAndDrainNativeCallbacks() {
    g_acceptNativeCallbacks.store(false, std::memory_order_release);
    unsigned int active = g_activeNativeCallbacks.load(std::memory_order_acquire);
    while (active != 0) {
        g_activeNativeCallbacks.wait(active, std::memory_order_acquire);
        active = g_activeNativeCallbacks.load(std::memory_order_acquire);
    }
}

jstring unchangedChatResult(JNIEnv *env, jstring rawJson) noexcept {
    return rawJson ? static_cast<jstring>(env->NewLocalRef(rawJson))
                   : env->NewStringUTF("");
}

void copyJavaString(JNIEnv *env, jstring source, std::string &destination) {
    if (!source) return;
    const char *characters = env->GetStringUTFChars(source, nullptr);
    if (!characters) return;
    try {
        destination.assign(characters);
    } catch (...) {
        env->ReleaseStringUTFChars(source, characters);
        throw;
    }
    env->ReleaseStringUTFChars(source, characters);
}

} // namespace

extern "C" JNIEXPORT void JNICALL Java_net_ovson_api_hook_PacketFilterHook_logDebug
  (JNIEnv *env, jclass, jstring jmsg)
{
    NativeCallbackGuard callback;
    if (!callback) return;
    if (jmsg) {
        const char* msg = env->GetStringUTFChars(jmsg, nullptr);
        if (msg) {
            try {
                Logger::info("%s", msg);
            } catch (...) {
            }
            env->ReleaseStringUTFChars(jmsg, msg);
        }
    }
}

extern "C" JNIEXPORT jstring JNICALL Java_net_ovson_api_hook_PacketFilterHook_processIncomingChat
  (JNIEnv *env, jclass, jstring jUnformatted, jstring jRawJson)
{
    NativeCallbackGuard callback;
    if (!callback) {
        return unchangedChatResult(env, jRawJson);
    }
    try {
        std::string unformatted;
        std::string rawJson;
        copyJavaString(env, jUnformatted, unformatted);
        copyJavaString(env, jRawJson, rawJson);
        const std::string result =
            ChatHook::processIncomingChat(unformatted, rawJson);
        return env->NewStringUTF(result.c_str());
    } catch (...) {
        return unchangedChatResult(env, jRawJson);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_net_ovson_api_hook_PacketFilterHook_onServerPacket(
    JNIEnv *, jclass, jint kind, jint entityId, jint data1, jint data2,
    jint data3, jfloat value1, jfloat value2, jfloat value3) {
    NativeCallbackGuard callback;
    if (!callback) return;
    // This callback runs on Netty's inbound channel thread. Keep it bounded and
    // JNI-free: the render thread performs all game-state inspection later.
    try {
        BlockHitSound::enqueueServerSignal(
            static_cast<BlockHitSound::ServerSignalKind>(kind), entityId,
            data1, data2, data3, value1, value2, value3, GetTickCount64());
    } catch (...) {
        // Never unwind through a JNI/native boundary.
    }
}

static bool s_injected = false;
static jclass s_hookCls = nullptr;
static jobject s_hookObj = nullptr;

void PacketHook::update() {
    JNIEnv* env = lc->getEnv();
    if (!env) return;

    // Run the correlator every render update. Packet-hook discovery itself is
    // intentionally throttled below, but a 2-second detector tick would lose
    // the short swing/hurt/health ordering windows.
    BlockHitSound::update(env);

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return;

    jmethodID getMcMethod = lc->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;", "func_71410_x", "A");
    if (!getMcMethod) return;

    jobject mcObj = env->CallStaticObjectMethod(mcCls, getMcMethod);
    if (!mcObj) return;

    static ULONGLONG lastAttempt = 0;
    ULONGLONG now = GetTickCount64();
    if (now - lastAttempt < 2000) {
        env->DeleteLocalRef(mcObj);
        return;
    }
    lastAttempt = now;

    static jfieldID theWorldField = nullptr;
    static jmethodID getNetHandler = nullptr;
    static jmethodID getNetworkManager = nullptr;
    static jfieldID nmField = nullptr;
    static jfieldID channelField = nullptr;

    if (!theWorldField) {
        theWorldField = lc->GetFieldID(mcCls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;", "func_71441_e", "f");
    }
    jobject theWorld = theWorldField ? env->GetObjectField(mcObj, theWorldField) : nullptr;
    if (env->ExceptionCheck()) env->ExceptionClear();

    if (!theWorld) {
        env->DeleteLocalRef(mcObj);
        return;
    }
    env->DeleteLocalRef(theWorld);

    if (!getNetHandler) {
        getNetHandler = lc->GetMethodID(mcCls, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;", "func_147114_u", "ay", "()Lbcy;");
        if (!getNetHandler) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            getNetHandler = lc->FindMethodBySignature(mcCls, "()Lnet/minecraft/client/network/NetHandlerPlayClient;");
        }
        if (!getNetHandler) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            getNetHandler = lc->FindMethodBySignature(mcCls, "()Lbcy;");
        }
    }
    
    if (!getNetHandler) {
        env->DeleteLocalRef(mcObj);
        return;
    }

    jobject nh = env->CallObjectMethod(mcObj, getNetHandler);
    if (!nh) {
        env->DeleteLocalRef(mcObj);
        return;
    }

    jobject networkManager = nullptr;
    jclass nhCls = env->GetObjectClass(nh);
    
    if (!getNetworkManager && !nmField) {
        getNetworkManager = lc->FindMethodBySignature(nhCls, "()Lnet/minecraft/network/NetworkManager;");
        if (!getNetworkManager) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            getNetworkManager = lc->FindMethodBySignature(nhCls, "()Lej;");
        }
        if (!getNetworkManager) {
            if (env->ExceptionCheck()) env->ExceptionClear();
            nmField = lc->FindFieldBySignature(nhCls, "Lnet/minecraft/network/NetworkManager;");
            if (!nmField) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                nmField = lc->FindFieldBySignature(nhCls, "Lej;");
            }
        }
    }
    
    if (getNetworkManager) {
        networkManager = env->CallObjectMethod(nh, getNetworkManager);
    } else if (nmField) {
        networkManager = env->GetObjectField(nh, nmField);
    }

    if (!networkManager) {
        env->DeleteLocalRef(nhCls);
        env->DeleteLocalRef(nh);
        env->DeleteLocalRef(mcObj);
        return;
    }

    jclass nmCls = env->GetObjectClass(networkManager);
    if (!channelField) {
        channelField = lc->FindFieldBySignature(nmCls, "Lio/netty/channel/Channel;");
    }
    
    if (!channelField) {
        env->DeleteLocalRef(nmCls);
        env->DeleteLocalRef(nhCls);
        env->DeleteLocalRef(networkManager);
        env->DeleteLocalRef(nh);
        env->DeleteLocalRef(mcObj);
        return;
    }

    jobject channel = env->GetObjectField(networkManager, channelField);
    if (!channel) {
        Logger::error("[PacketHook] Failed: Channel field on NetworkManager is null");
        env->DeleteLocalRef(nmCls);
        env->DeleteLocalRef(networkManager);
        env->DeleteLocalRef(nh);
        env->DeleteLocalRef(mcObj);
        return;
    }

    if (!s_hookCls) {
        jmethodID getClassLoader = env->GetMethodID(env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;");
        jclass channelCls = env->GetObjectClass(channel);
        jobject channelLoader = env->CallObjectMethod(channelCls, getClassLoader);
        jobject nmLoader = env->CallObjectMethod(nmCls, getClassLoader);
        env->DeleteLocalRef(channelCls);

        Logger::info("[PacketHook] channelLoader=%p, nmLoader=%p", channelLoader, nmLoader);

        jobject loaders[] = { channelLoader, nmLoader, nullptr };
        const char* loaderNames[] = { "channelLoader (Netty)", "nmLoader (NetworkManager)", "null (bootstrap)" };
        jclass hookClsLocal = nullptr;

        for (int i = 0; i < 3 && !hookClsLocal; i++) {
            hookClsLocal = env->DefineClass("net/ovson/api/hook/PacketFilterHook", loaders[i], (const jbyte*)PacketFilterHook_class, PacketFilterHook_class_len);
            if (env->ExceptionCheck()) {
                jthrowable ex = env->ExceptionOccurred();
                env->ExceptionClear();
                std::string exStr = "Unknown";
                if (ex) {
                    jmethodID toStr = env->GetMethodID(env->GetObjectClass(ex), "toString", "()Ljava/lang/String;");
                    if (toStr) {
                        jstring js = (jstring)env->CallObjectMethod(ex, toStr);
                        if (js) {
                            const char* str = env->GetStringUTFChars(js, nullptr);
                            if (str) { exStr = str; env->ReleaseStringUTFChars(js, str); }
                            env->DeleteLocalRef(js);
                        }
                    }
                    env->DeleteLocalRef(ex);
                }
                Logger::error("[PacketHook] DefineClass with %s failed: %s", loaderNames[i], exStr.c_str());
                hookClsLocal = nullptr;
            } else if (hookClsLocal) {
                Logger::info("[PacketHook] DefineClass succeeded with %s!", loaderNames[i]);
            }
        }

        if (!hookClsLocal) {
            jclass clsCls = env->FindClass("java/lang/Class");
            jmethodID m_forName = clsCls ? env->GetStaticMethodID(clsCls, "forName", "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;") : nullptr;
            jobject tryLoaders[] = { channelLoader, nmLoader };
            for (int i = 0; i < 2 && !hookClsLocal; i++) {
                if (m_forName && tryLoaders[i]) {
                    jstring className = env->NewStringUTF("net.ovson.api.hook.PacketFilterHook");
                    hookClsLocal = (jclass)env->CallStaticObjectMethod(clsCls, m_forName, className, JNI_TRUE, tryLoaders[i]);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        hookClsLocal = nullptr;
                    }
                    env->DeleteLocalRef(className);
                }
            }
            if (hookClsLocal) {
                Logger::info("[PacketHook] Class.forName succeeded (re-inject scenario)");
            } else {
                Logger::error("[PacketHook] All DefineClass and Class.forName attempts failed!");
            }
            if (clsCls) env->DeleteLocalRef(clsCls);
        }

        if (channelLoader) env->DeleteLocalRef(channelLoader);
        if (nmLoader) env->DeleteLocalRef(nmLoader);

        if (hookClsLocal) {
            s_hookCls = (jclass)env->NewGlobalRef(hookClsLocal);
            env->DeleteLocalRef(hookClsLocal);

            if (!s_hookCls) {
                if (env->ExceptionCheck()) env->ExceptionClear();
                Logger::error("[PacketHook] Failed to retain PacketFilterHook class");
                BlockHitSound::setCallbackAcceptance(false);
                g_acceptNativeCallbacks.store(false, std::memory_order_release);
            } else {
                JNINativeMethod methods[] = {
                    {(char*)"processIncomingChat", (char*)"(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)&Java_net_ovson_api_hook_PacketFilterHook_processIncomingChat},
                    {(char*)"logDebug", (char*)"(Ljava/lang/String;)V", (void*)&Java_net_ovson_api_hook_PacketFilterHook_logDebug},
                    {(char*)"onServerPacket", (char*)"(IIIIIFFF)V", (void*)&Java_net_ovson_api_hook_PacketFilterHook_onServerPacket}
                };
                const jint nativeResult = env->RegisterNatives(s_hookCls, methods, 3);
                if (env->ExceptionCheck()) env->ExceptionClear();
                if (nativeResult != JNI_OK) {
                    Logger::error("[PacketHook] Failed to register packet natives (%d); "
                                  "restart Minecraft if an older hook class is already loaded",
                                  nativeResult);
                    BlockHitSound::setCallbackAcceptance(false);
                    g_acceptNativeCallbacks.store(false, std::memory_order_release);
                    env->DeleteGlobalRef(s_hookCls);
                    s_hookCls = nullptr;
                } else {
                    g_acceptNativeCallbacks.store(true, std::memory_order_release);
                    BlockHitSound::setCallbackAcceptance(true);
                }
            }
        } else {
            Logger::error("[PacketHook] Failed to define or find PacketFilterHook class");
        }
    }

    env->DeleteLocalRef(nmCls);

    if (s_hookCls) {
        if (!s_hookObj) {
            jmethodID init = env->GetMethodID(s_hookCls, "<init>", "()V");
            jobject hookObjLocal = init ? env->NewObject(s_hookCls, init) : nullptr;
            if (env->ExceptionCheck()) env->ExceptionClear();
            if (hookObjLocal) {
                s_hookObj = env->NewGlobalRef(hookObjLocal);
                env->DeleteLocalRef(hookObjLocal);
            }
        }

        jmethodID pipelineMethod = s_hookObj ? env->GetMethodID(env->GetObjectClass(channel), "pipeline", "()Lio/netty/channel/ChannelPipeline;") : nullptr;
        if (pipelineMethod) {
            jobject pipeline = env->CallObjectMethod(channel, pipelineMethod);
            if (pipeline) {
                jmethodID addBefore = env->GetMethodID(env->GetObjectClass(pipeline), "addBefore", "(Ljava/lang/String;Ljava/lang/String;Lio/netty/channel/ChannelHandler;)Lio/netty/channel/ChannelPipeline;");
                jmethodID getHandler = env->GetMethodID(env->GetObjectClass(pipeline), "get", "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
                
                if (addBefore && getHandler) {
                    jstring baseName = env->NewStringUTF("packet_handler");
                    jstring hookName = env->NewStringUTF("ovson_packet_filter");
                    
                    jobject existing = env->CallObjectMethod(pipeline, getHandler, hookName);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    
                    if (existing) {
                        s_injected = true;
                        env->DeleteLocalRef(existing);
                    } else {
                        env->CallObjectMethod(pipeline, addBefore, baseName, hookName, s_hookObj);
                        
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear(); 
                            Logger::error("[PacketHook] Failed to inject. Exception occurred during addBefore.");
                            s_injected = false;
                        } else {
                            Logger::info("[PacketHook] Successfully injected PacketFilterHook into Netty pipeline!");
                            s_injected = true;
                        }
                    }
                    env->DeleteLocalRef(baseName);
                    env->DeleteLocalRef(hookName);
                } else {
                    Logger::error("[PacketHook] Could not find addBefore or get method on ChannelPipeline.");
                }
                env->DeleteLocalRef(pipeline);
            }
        }
    }

    env->DeleteLocalRef(channel);
    env->DeleteLocalRef(networkManager);
    env->DeleteLocalRef(nh);
    env->DeleteLocalRef(mcObj);
}

void PacketHook::uninstall() {
    g_acceptNativeCallbacks.store(false, std::memory_order_release);
    BlockHitSound::setCallbackAcceptance(false);
    JNIEnv* env = (lc ? lc->getEnv() : nullptr);
    BlockHitSound::shutdown(env);
    if (!lc || !lc->getEnv()) {
        disableAndDrainNativeCallbacks();
        return;
    }
    if (!env) return;

    const auto finalizeNativeState = [env]() {
        if (s_hookCls) {
            env->UnregisterNatives(s_hookCls);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        disableAndDrainNativeCallbacks();
        if (s_hookObj) {
            env->DeleteGlobalRef(s_hookObj);
            s_hookObj = nullptr;
        }
        if (s_hookCls) {
            env->DeleteGlobalRef(s_hookCls);
            s_hookCls = nullptr;
        }
        s_injected = false;
    };

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) {
        finalizeNativeState();
        return;
    }

    jmethodID getMc = lc->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;", "func_71410_x", "A");
    if (!getMc) {
        env->DeleteLocalRef(mcCls);
        finalizeNativeState();
        return;
    }

    jobject mcObj = env->CallStaticObjectMethod(mcCls, getMc);
    if (!mcObj) {
        env->DeleteLocalRef(mcCls);
        finalizeNativeState();
        return;
    }

    jmethodID getNet = lc->GetMethodID(mcCls, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;", "func_147114_u", "ay", "()Lbcy;");
    if (!getNet) {
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcCls);
        finalizeNativeState();
        return;
    }

    jobject nh = env->CallObjectMethod(mcObj, getNet);
    if (!nh) {
        env->DeleteLocalRef(mcObj);
        env->DeleteLocalRef(mcCls);
        finalizeNativeState();
        return;
    }

    jobject networkManager = nullptr;
    jclass nhCls = env->GetObjectClass(nh);
    jfieldID nmField = lc->FindFieldBySignature(nhCls, "Lnet/minecraft/network/NetworkManager;");
    if (!nmField) nmField = lc->FindFieldBySignature(nhCls, "Lej;");
    if (nmField) networkManager = env->GetObjectField(nh, nmField);

    if (networkManager) {
        jclass nmCls = env->GetObjectClass(networkManager);
        jfieldID channelField = lc->FindFieldBySignature(nmCls, "Lio/netty/channel/Channel;");
        if (channelField) {
            jobject channel = env->GetObjectField(networkManager, channelField);
            if (channel) {
                jmethodID pipelineMethod = env->GetMethodID(env->GetObjectClass(channel), "pipeline", "()Lio/netty/channel/ChannelPipeline;");
                if (pipelineMethod) {
                    jobject pipeline = env->CallObjectMethod(channel, pipelineMethod);
                    if (pipeline) {
                        jmethodID removeMethod = env->GetMethodID(env->GetObjectClass(pipeline), "remove", "(Ljava/lang/String;)Lio/netty/channel/ChannelHandler;");
                        if (removeMethod) {
                            jstring hookName = env->NewStringUTF("ovson_packet_filter");
                            env->CallObjectMethod(pipeline, removeMethod, hookName);
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                            } else {
                                Logger::info("[PacketHook] Successfully removed PacketFilterHook from pipeline.");
                            }
                            env->DeleteLocalRef(hookName);
                        }
                        env->DeleteLocalRef(pipeline);
                    }
                }
                env->DeleteLocalRef(channel);
            }
        }
        env->DeleteLocalRef(nmCls);
        env->DeleteLocalRef(networkManager);
    }

    env->DeleteLocalRef(nhCls);
    env->DeleteLocalRef(nh);
    env->DeleteLocalRef(mcObj);
    env->DeleteLocalRef(mcCls);

    finalizeNativeState();
}

