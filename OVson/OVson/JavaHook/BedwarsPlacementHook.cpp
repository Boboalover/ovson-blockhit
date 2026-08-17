#include "BedwarsPlacementHook.h"

#include "../Java.h"
#include "../Logic/Bedwars/BedwarsConfig.h"
#include "../Logic/Bedwars/BedwarsRuntime.h"
#include "../Utils/Logger.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace BedwarsPlacementHook {
namespace {

std::mutex g_mutex;
jvmtiEnv *g_jvmti = nullptr;
std::atomic<jmethodID> g_method{nullptr};
std::atomic<jlocation> g_location{-1};
std::atomic<jmethodID> g_callerMethod{nullptr};
std::atomic<jlocation> g_callerLocation{-1};
std::atomic<bool> g_installed{false};
ULONGLONG g_nextAttempt = 0;
std::string g_lastFailure = "not initialized";

std::uint16_t readU2(const unsigned char *bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[offset]) << 8) |
      static_cast<std::uint16_t>(bytes[offset + 1]));
}

std::int32_t readI4(const unsigned char *bytes, std::size_t offset) {
  return static_cast<std::int32_t>(
      (static_cast<std::uint32_t>(bytes[offset]) << 24) |
      (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
      static_cast<std::uint32_t>(bytes[offset + 3]));
}

bool parseClassNames(const unsigned char *bytes, std::size_t byteCount,
                     jint constantCount,
                     std::unordered_map<std::uint16_t, std::string> &classes) {
  std::vector<std::string> utf8(static_cast<std::size_t>(constantCount));
  std::vector<std::uint16_t> classNameIndexes(
      static_cast<std::size_t>(constantCount));
  std::size_t offset = 0;
  for (jint index = 1; index < constantCount; ++index) {
    if (offset >= byteCount)
      return false;
    const unsigned char tag = bytes[offset++];
    switch (tag) {
    case 1: {
      if (offset + 2 > byteCount) return false;
      const std::uint16_t length = readU2(bytes, offset);
      offset += 2;
      if (offset + length > byteCount) return false;
      utf8[static_cast<std::size_t>(index)] =
          std::string(reinterpret_cast<const char *>(bytes + offset), length);
      offset += length;
      break;
    }
    case 3:
    case 4:
      if (offset + 4 > byteCount) return false;
      offset += 4;
      break;
    case 5:
    case 6:
      if (offset + 8 > byteCount) return false;
      offset += 8;
      ++index;
      break;
    case 7:
      if (offset + 2 > byteCount) return false;
      classNameIndexes[static_cast<std::size_t>(index)] = readU2(bytes, offset);
      offset += 2;
      break;
    case 8:
    case 16:
      if (offset + 2 > byteCount) return false;
      offset += 2;
      break;
    case 9:
    case 10:
    case 11:
    case 12:
    case 17:
    case 18:
      if (offset + 4 > byteCount) return false;
      offset += 4;
      break;
    case 15:
      if (offset + 3 > byteCount) return false;
      offset += 3;
      break;
    default:
      return false;
    }
  }
  for (jint index = 1; index < constantCount; ++index) {
    const std::uint16_t nameIndex =
        classNameIndexes[static_cast<std::size_t>(index)];
    if (nameIndex != 0 && nameIndex < utf8.size())
      classes[static_cast<std::uint16_t>(index)] = utf8[nameIndex];
  }
  return true;
}

std::size_t instructionLength(const unsigned char *code, std::size_t size,
                              std::size_t bci) {
  if (bci >= size) return 0;
  const unsigned char opcode = code[bci];
  switch (opcode) {
  case 0x10:
  case 0x12:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
  case 0x19:
  case 0x36:
  case 0x37:
  case 0x38:
  case 0x39:
  case 0x3a:
  case 0xa9:
  case 0xbc:
    return 2;
  case 0x11:
  case 0x13:
  case 0x14:
  case 0x84:
  case 0x99:
  case 0x9a:
  case 0x9b:
  case 0x9c:
  case 0x9d:
  case 0x9e:
  case 0x9f:
  case 0xa0:
  case 0xa1:
  case 0xa2:
  case 0xa3:
  case 0xa4:
  case 0xa5:
  case 0xa6:
  case 0xa7:
  case 0xa8:
  case 0xc6:
  case 0xc7:
  case 0xb2:
  case 0xb3:
  case 0xb4:
  case 0xb5:
  case 0xb6:
  case 0xb7:
  case 0xb8:
  case 0xbb:
  case 0xbd:
  case 0xc0:
  case 0xc1:
    return 3;
  case 0xb9:
  case 0xba:
  case 0xc8:
  case 0xc9:
    return 5;
  case 0xc5:
    return 4;
  case 0xaa: {
    const std::size_t aligned = (bci + 4U) & ~std::size_t(3U);
    if (aligned + 12 > size) return 0;
    const std::int32_t low = readI4(code, aligned + 4);
    const std::int32_t high = readI4(code, aligned + 8);
    if (high < low || high - low > 65535) return 0;
    return aligned - bci + 12U +
           static_cast<std::size_t>(high - low + 1) * 4U;
  }
  case 0xab: {
    const std::size_t aligned = (bci + 4U) & ~std::size_t(3U);
    if (aligned + 8 > size) return 0;
    const std::int32_t pairs = readI4(code, aligned + 4);
    if (pairs < 0 || pairs > 65535) return 0;
    return aligned - bci + 8U + static_cast<std::size_t>(pairs) * 8U;
  }
  case 0xc4:
    if (bci + 1 >= size) return 0;
    return code[bci + 1] == 0x84 ? 6 : 4;
  default:
    return 1;
  }
}

std::optional<jlocation> findPacketBoundary(
    const unsigned char *code, std::size_t size,
    const std::unordered_map<std::uint16_t, std::string> &classes) {
  std::size_t previous = size;
  std::size_t beforePrevious = size;
  for (std::size_t bci = 0; bci < size;) {
    const std::size_t length = instructionLength(code, size, bci);
    if (length == 0 || bci + length > size)
      return std::nullopt;
    if (code[bci] == 0xbb && length == 3) {
      const auto classIt = classes.find(readU2(code, bci + 1));
      if (classIt != classes.end() &&
          (classIt->second ==
               "net/minecraft/network/play/client/C08PacketPlayerBlockPlacement" ||
           classIt->second == "ja") &&
          previous < size && beforePrevious < size &&
          code[previous] == 0xb4 && code[beforePrevious] == 0x2a)
        return static_cast<jlocation>(beforePrevious);
    }
    beforePrevious = previous;
    previous = bci;
    bci += length;
  }
  return std::nullopt;
}

std::optional<jlocation> findCallerGate(const unsigned char *code,
                                        std::size_t size) {
  for (std::size_t bci = 0; bci < size;) {
    const std::size_t length = instructionLength(code, size, bci);
    if (length == 0 || bci + length > size)
      return std::nullopt;
    // rightClickMouse checks the controller result, clears its local fallback
    // flag, then invokes the player's swing method.  The two earlier boolean
    // controller calls branch to a goto instead and do not match this shape.
    const std::size_t next = bci + length;
    if (code[bci] == 0xb6 && length == 3 && next + 12 <= size &&
        code[next] == 0x99 && code[next + 3] == 0x03 &&
        code[next + 4] == 0x3c && code[next + 5] == 0x2a &&
        code[next + 6] == 0xb4 && code[next + 9] == 0xb6) {
      for (std::size_t probe = next + 12; probe + 3 <= size;) {
        const std::size_t probeLength = instructionLength(code, size, probe);
        if (probeLength == 0 || probe + probeLength > size)
          return std::nullopt;
        if (code[probe] == 0x1b && probe + 1 < size &&
            code[probe + 1] == 0x99)
          return static_cast<jlocation>(bci);
        if (probe - next > 160)
          break;
        probe += probeLength;
      }
    }
    bci += length;
  }
  return std::nullopt;
}

void setFailure(const std::string &reason) {
  if (g_lastFailure != reason) {
    g_lastFailure = reason;
    if (OVson::Bedwars::Configuration::isDebugEnabled())
      Logger::info("[Bedwars] placement hook unavailable: %s", reason.c_str());
  }
  OVson::Bedwars::Runtime::instance().setInputHookAvailable(false);
}

void deleteLocal(JNIEnv *env, jobject value) {
  if (env && value) env->DeleteLocalRef(value);
}

} // namespace

void initialize(jvmtiEnv *jvmti) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_jvmti = jvmti;
  g_method.store(nullptr, std::memory_order_release);
  g_location.store(-1, std::memory_order_release);
  g_callerMethod.store(nullptr, std::memory_order_release);
  g_callerLocation.store(-1, std::memory_order_release);
  g_installed.store(false, std::memory_order_release);
  g_nextAttempt = 0;
  g_lastFailure = jvmti ? "waiting for controller class" : "JVMTI unavailable";
  OVson::Bedwars::Runtime::instance().setInputHookAvailable(false);
}

void update() {
  if (g_installed.load(std::memory_order_acquire))
    return;
  const ULONGLONG now = GetTickCount64();
  if (now < g_nextAttempt)
    return;
  g_nextAttempt = now + 2000;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_jvmti || !lc) {
    setFailure("JVMTI unavailable");
    return;
  }
  jclass controller =
      lc->GetClass("net.minecraft.client.multiplayer.PlayerControllerMP");
  if (!controller) {
    setFailure("PlayerControllerMP class unresolved");
    return;
  }
  const char *mappedDescriptor =
      "(Lnet/minecraft/client/entity/EntityPlayerSP;"
      "Lnet/minecraft/client/multiplayer/WorldClient;"
      "Lnet/minecraft/item/ItemStack;Lnet/minecraft/util/BlockPos;"
      "Lnet/minecraft/util/EnumFacing;Lnet/minecraft/util/Vec3;)Z";
  const char *obfuscatedDescriptor = "(Lbew;Lbdb;Lzx;Lcj;Lcq;Laui;)Z";
  jmethodID method = lc->GetMethodID(
      controller, "onPlayerRightClick", mappedDescriptor, "func_178890_a", "a",
      obfuscatedDescriptor);
  if (!method) {
    setFailure("onPlayerRightClick mapping unresolved");
    return;
  }

  jint codeLength = 0;
  unsigned char *code = nullptr;
  jvmtiError error = g_jvmti->GetBytecodes(method, &codeLength, &code);
  if (error != JVMTI_ERROR_NONE || !code || codeLength <= 0) {
    setFailure("controller bytecode unavailable (error " +
               std::to_string(error) + ")");
    return;
  }
  jclass declaringClass = nullptr;
  error = g_jvmti->GetMethodDeclaringClass(method, &declaringClass);
  jint constantCount = 0;
  jint constantBytes = 0;
  unsigned char *constantPool = nullptr;
  if (error == JVMTI_ERROR_NONE && declaringClass)
    error = g_jvmti->GetConstantPool(declaringClass, &constantCount,
                                     &constantBytes, &constantPool);
  std::unordered_map<std::uint16_t, std::string> classNames;
  const bool poolValid =
      error == JVMTI_ERROR_NONE && constantPool && constantBytes > 0 &&
      parseClassNames(constantPool, static_cast<std::size_t>(constantBytes),
                      constantCount, classNames);
  const auto boundary =
      poolValid ? findPacketBoundary(code, static_cast<std::size_t>(codeLength),
                                     classNames)
                : std::nullopt;
  if (constantPool) g_jvmti->Deallocate(constantPool);
  g_jvmti->Deallocate(code);
  if (declaringClass) {
    if (JNIEnv *env = lc->getEnv())
      env->DeleteLocalRef(declaringClass);
  }
  if (!poolValid || !boundary) {
    setFailure(poolValid ? "C08 send boundary not found"
                         : "controller constant pool unavailable");
    return;
  }

  jclass minecraft = lc->GetClass("net.minecraft.client.Minecraft");
  jmethodID caller = minecraft
                         ? lc->GetMethodID(minecraft, "rightClickMouse", "()V",
                                           "func_147121_ag", "ax")
                         : nullptr;
  jint callerCodeLength = 0;
  unsigned char *callerCode = nullptr;
  if (!caller ||
      g_jvmti->GetBytecodes(caller, &callerCodeLength, &callerCode) !=
          JVMTI_ERROR_NONE ||
      !callerCode || callerCodeLength <= 0) {
    if (callerCode) g_jvmti->Deallocate(callerCode);
    setFailure("rightClickMouse caller layout unavailable");
    return;
  }
  const auto callerGate =
      findCallerGate(callerCode, static_cast<std::size_t>(callerCodeLength));
  g_jvmti->Deallocate(callerCode);
  if (!callerGate) {
    setFailure("rightClickMouse fallback gate not found");
    return;
  }

  error = g_jvmti->SetBreakpoint(method, *boundary);
  if (error != JVMTI_ERROR_NONE && error != JVMTI_ERROR_DUPLICATE) {
    setFailure("SetBreakpoint failed (error " + std::to_string(error) + ")");
    return;
  }
  error = g_jvmti->SetEventNotificationMode(
      JVMTI_ENABLE, JVMTI_EVENT_BREAKPOINT, nullptr);
  if (error != JVMTI_ERROR_NONE) {
    g_jvmti->ClearBreakpoint(method, *boundary);
    setFailure("breakpoint event enable failed (error " +
               std::to_string(error) + ")");
    return;
  }
  g_method.store(method, std::memory_order_release);
  g_location.store(*boundary, std::memory_order_release);
  g_callerMethod.store(caller, std::memory_order_release);
  g_callerLocation.store(*callerGate, std::memory_order_release);
  g_lastFailure.clear();
  g_installed.store(true, std::memory_order_release);
  OVson::Bedwars::Runtime::instance().setInputHookAvailable(true);
  Logger::info(
      "[Bedwars] placement hook installed at controller BCI %lld; caller BCI %lld",
      static_cast<long long>(*boundary), static_cast<long long>(*callerGate));
}

void shutdown() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_jvmti) {
    g_jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_BREAKPOINT,
                                      nullptr);
    const jmethodID method = g_method.load(std::memory_order_acquire);
    const jlocation location = g_location.load(std::memory_order_acquire);
    if (method && location >= 0)
      g_jvmti->ClearBreakpoint(method, location);
  }
  g_method.store(nullptr, std::memory_order_release);
  g_location.store(-1, std::memory_order_release);
  g_callerMethod.store(nullptr, std::memory_order_release);
  g_callerLocation.store(-1, std::memory_order_release);
  g_installed.store(false, std::memory_order_release);
  g_jvmti = nullptr;
  g_lastFailure = "shut down";
  OVson::Bedwars::Runtime::instance().setInputHookAvailable(false);
}

bool isInstalled() { return g_installed.load(std::memory_order_acquire); }

std::string lastFailure() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_lastFailure;
}

void JNICALL onBreakpoint(jvmtiEnv *jvmti, JNIEnv *env, jthread thread,
                          jmethodID method, jlocation location) {
  if (!jvmti || !env || !thread ||
      method != g_method.load(std::memory_order_acquire) ||
      location != g_location.load(std::memory_order_acquire))
    return;

  jint blockActivationHandled = 0;
  jvmtiError error = jvmti->GetLocalInt(thread, 0, 10,
                                        &blockActivationHandled);
  if (error != JVMTI_ERROR_NONE) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      setFailure("controller local state unavailable (error " +
                 std::to_string(error) + ")");
    }
    return;
  }
  if (blockActivationHandled != 0)
    return;

  jobject player = nullptr;
  jobject world = nullptr;
  jobject stack = nullptr;
  jobject target = nullptr;
  jobject side = nullptr;
  error = jvmti->GetLocalObject(thread, 0, 1, &player);
  if (error == JVMTI_ERROR_NONE)
    error = jvmti->GetLocalObject(thread, 0, 2, &world);
  if (error == JVMTI_ERROR_NONE)
    error = jvmti->GetLocalObject(thread, 0, 3, &stack);
  if (error == JVMTI_ERROR_NONE)
    error = jvmti->GetLocalObject(thread, 0, 4, &target);
  if (error == JVMTI_ERROR_NONE)
    error = jvmti->GetLocalObject(thread, 0, 5, &side);
  if (error != JVMTI_ERROR_NONE) {
    deleteLocal(env, side);
    deleteLocal(env, target);
    deleteLocal(env, stack);
    deleteLocal(env, world);
    deleteLocal(env, player);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      setFailure("placement arguments unavailable (error " +
                 std::to_string(error) + ")");
    }
    return;
  }

  const bool cancel =
      OVson::Bedwars::Runtime::instance().shouldCancelObsidianPlacement(
          env, player, world, stack, target, side);
  deleteLocal(env, side);
  deleteLocal(env, target);
  deleteLocal(env, stack);
  deleteLocal(env, world);
  deleteLocal(env, player);
  if (!cancel)
    return;

  jmethodID caller = nullptr;
  jlocation callerLocation = -1;
  error = jvmti->GetFrameLocation(thread, 1, &caller, &callerLocation);
  const jmethodID expectedCaller =
      g_callerMethod.load(std::memory_order_acquire);
  const jlocation expectedLocation =
      g_callerLocation.load(std::memory_order_acquire);
  if (error != JVMTI_ERROR_NONE || caller != expectedCaller ||
      (callerLocation != expectedLocation &&
       callerLocation != expectedLocation + 3)) {
    OVson::Bedwars::Runtime::instance().onPlacementCancellationResult(
        false, error == JVMTI_ERROR_NONE ? -1001 : static_cast<int>(error));
    return;
  }

  error = jvmti->SetLocalInt(
      thread, 1, 1,
      OVson::Bedwars::callerFallbackForCancelledPlacement() ? JNI_TRUE
                                                            : JNI_FALSE);
  if (error == JVMTI_ERROR_NONE) {
    error = jvmti->ForceEarlyReturnInt(
        thread, OVson::Bedwars::controllerResultForCancelledPlacement()
                    ? JNI_TRUE
                    : JNI_FALSE);
  }
  OVson::Bedwars::Runtime::instance().onPlacementCancellationResult(
      error == JVMTI_ERROR_NONE, static_cast<int>(error));
}

} // namespace BedwarsPlacementHook
