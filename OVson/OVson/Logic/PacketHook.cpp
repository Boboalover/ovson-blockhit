#include "PacketHook.h"
#include "PacketFilterHook_bytes.h"
#include "../Java.h"
#include "../Utils/Logger.h"
#include "../Chat/ChatHook.h"
#include "../Plugins/PluginLoader.h"

extern "C" JNIEXPORT void JNICALL Java_net_ovson_api_hook_PacketFilterHook_logDebug
  (JNIEnv *env, jclass cls, jstring jmsg)
{
    if (jmsg) {
        const char* msg = env->GetStringUTFChars(jmsg, nullptr);
        if (msg) {
            Logger::info("%s", msg);
            env->ReleaseStringUTFChars(jmsg, msg);
        }
    }
}

extern "C" JNIEXPORT jstring JNICALL Java_net_ovson_api_hook_PacketFilterHook_processIncomingChat
  (JNIEnv *env, jclass cls, jstring jUnformatted, jstring jRawJson)
{
    std::string unformatted = "";
    std::string rawJson = "";
    if (jUnformatted) {
        const char* u = env->GetStringUTFChars(jUnformatted, nullptr);
        if (u) { unformatted = u; env->ReleaseStringUTFChars(jUnformatted, u); }
    }
    if (jRawJson) {
        const char* r = env->GetStringUTFChars(jRawJson, nullptr);
        if (r) { rawJson = r; env->ReleaseStringUTFChars(jRawJson, r); }
    }

    std::string res = ChatHook::processIncomingChat(unformatted, rawJson);
    return env->NewStringUTF(res.c_str());
}

#include <fstream>
#include <string>

static void logToFile(const std::string& msg) {
    std::ofstream out("C:/Users/HPC1/Desktop/ovson_packet.log", std::ios::app);
    if (out.is_open()) {
        out << msg << std::endl;
        out.close();
    }
}

static bool s_injected = false;
static jclass s_hookCls = nullptr;
static jobject s_hookObj = nullptr;

void PacketHook::update() {
    JNIEnv* env = lc->getEnv();
    if (!env) return;

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

            JNINativeMethod methods[] = {
                {(char*)"processIncomingChat", (char*)"(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", (void*)&Java_net_ovson_api_hook_PacketFilterHook_processIncomingChat},
                {(char*)"logDebug", (char*)"(Ljava/lang/String;)V", (void*)&Java_net_ovson_api_hook_PacketFilterHook_logDebug}
            };
            env->RegisterNatives(s_hookCls, methods, 2);
            if (env->ExceptionCheck()) env->ExceptionClear();
        } else {
            Logger::error("[PacketHook] Failed to define or find PacketFilterHook class");
        }
    }

    env->DeleteLocalRef(nmCls);

    if (s_hookCls) {
        if (!s_hookObj) {
            jmethodID init = env->GetMethodID(s_hookCls, "<init>", "()V");
            jobject hookObjLocal = env->NewObject(s_hookCls, init);
            s_hookObj = env->NewGlobalRef(hookObjLocal);
            env->DeleteLocalRef(hookObjLocal);
        }

        jmethodID pipelineMethod = env->GetMethodID(env->GetObjectClass(channel), "pipeline", "()Lio/netty/channel/ChannelPipeline;");
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
                            logToFile("[PacketHook] Failed to inject. Exception occurred during addBefore.");
                            Logger::error("[PacketHook] Failed to inject. Exception occurred during addBefore.");
                            s_injected = true;
                        } else {
                            logToFile("[PacketHook] Successfully injected PacketFilterHook into Netty pipeline!");
                            Logger::info("[PacketHook] Successfully injected PacketFilterHook into Netty pipeline!");
                            s_injected = true;
                        }
                    }
                    env->DeleteLocalRef(baseName);
                    env->DeleteLocalRef(hookName);
                } else {
                    logToFile("[PacketHook] Could not find addBefore or get method on ChannelPipeline.");
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
    if (!s_injected || !lc || !lc->getEnv()) return;
    
    JNIEnv* env = lc->getEnv();
    if (!env) return;

    jclass mcCls = lc->GetClass("net.minecraft.client.Minecraft");
    if (!mcCls) return;

    jmethodID getMc = lc->GetStaticMethodID(mcCls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;", "func_71410_x", "A");
    if (!getMc) { env->DeleteLocalRef(mcCls); return; }

    jobject mcObj = env->CallStaticObjectMethod(mcCls, getMc);
    if (!mcObj) { env->DeleteLocalRef(mcCls); return; }

    jmethodID getNet = lc->GetMethodID(mcCls, "getNetHandler", "()Lnet/minecraft/client/network/NetHandlerPlayClient;", "func_147114_u", "u");
    if (!getNet) { env->DeleteLocalRef(mcObj); env->DeleteLocalRef(mcCls); return; }

    jobject nh = env->CallObjectMethod(mcObj, getNet);
    if (!nh) { env->DeleteLocalRef(mcObj); env->DeleteLocalRef(mcCls); return; }

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

    if (s_hookObj) { env->DeleteGlobalRef(s_hookObj); s_hookObj = nullptr; }
    if (s_hookCls) { env->DeleteGlobalRef(s_hookCls); s_hookCls = nullptr; }
    s_injected = false;
}

