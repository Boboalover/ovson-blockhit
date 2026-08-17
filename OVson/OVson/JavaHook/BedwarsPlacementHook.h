#pragma once

#include <jni.h>
#include <jvmti.h>
#include <string>

namespace BedwarsPlacementHook {

void initialize(jvmtiEnv *jvmti);
void update();
void shutdown();
bool isInstalled();
std::string lastFailure();

void JNICALL onBreakpoint(jvmtiEnv *jvmti, JNIEnv *env, jthread thread,
                          jmethodID method, jlocation location);

} // namespace BedwarsPlacementHook
