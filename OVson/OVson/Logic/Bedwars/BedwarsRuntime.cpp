#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "BedwarsRuntime.h"

#include "BedwarsConfig.h"
#include "../../Config/Config.h"
#include "../../Java.h"
#include "../../Render/NotificationManager.h"
#include "../../SDK/McAccess.h"
#include "../../Utils/Logger.h"
#include "../BedDefense/BedDefenseManager.h"
#include "../../JavaHook/BedwarsPlacementHook.h"
#include "../StatsTracker.internal.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace OVson::Bedwars {
namespace {

void clearException(JNIEnv *env) {
  if (env && env->ExceptionCheck())
    env->ExceptionClear();
}

std::string fromJavaString(JNIEnv *env, jstring value) {
  if (!env || !value)
    return {};
  const char *characters = env->GetStringUTFChars(value, nullptr);
  if (!characters) {
    clearException(env);
    return {};
  }
  std::string result(characters);
  env->ReleaseStringUTFChars(value, characters);
  return result;
}

std::string itemName(JNIEnv *env, jobject stack) {
  if (!env || !stack || !lc)
    return {};
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  if (!stackClass)
    return {};
  jmethodID getName = lc->GetMethodID(stackClass, "getUnlocalizedName",
                                      "()Ljava/lang/String;", "func_77977_a",
                                      "a");
  if (!getName)
    return {};
  jstring name = static_cast<jstring>(env->CallObjectMethod(stack, getName));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return {};
  }
  std::string result = fromJavaString(env, name);
  if (name)
    env->DeleteLocalRef(name);
  std::replace(result.begin(), result.end(), '_', ' ');
  const std::size_t dot = result.find_last_of('.');
  if (dot != std::string::npos && dot + 1 < result.size())
    result = result.substr(dot + 1);
  return result;
}

std::string itemDisplayName(JNIEnv *env, jobject stack) {
  if (!env || !stack || !lc)
    return {};
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  jmethodID method = stackClass
                         ? lc->GetMethodID(stackClass, "getDisplayName",
                                           "()Ljava/lang/String;",
                                           "func_82833_r", "q")
                         : nullptr;
  if (!method)
    return {};
  jstring name = static_cast<jstring>(env->CallObjectMethod(stack, method));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return {};
  }
  std::string result = fromJavaString(env, name);
  if (name)
    env->DeleteLocalRef(name);
  return stripFormatting(result).substr(0, 64);
}

int itemMetadata(JNIEnv *env, jobject stack) {
  if (!env || !stack || !lc)
    return -1;
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  jmethodID method = stackClass
                         ? lc->GetMethodID(stackClass, "getItemDamage", "()I",
                                           "func_77960_j", "j")
                         : nullptr;
  if (!method)
    return -1;
  const int result = env->CallIntMethod(stack, method);
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return -1;
  }
  return result;
}

bool isObsidianStack(JNIEnv *env, jobject stack) {
  if (!env || !stack || !lc)
    return false;
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  jclass itemClass = lc->GetClass("net.minecraft.item.Item");
  if (!stackClass || !itemClass)
    return false;
  jmethodID getItem = lc->GetMethodID(
      stackClass, "getItem", "()Lnet/minecraft/item/Item;",
      "func_77973_b", "b", "()Lzw;");
  jmethodID byName = lc->GetStaticMethodID(
      itemClass, "getByNameOrId", "(Ljava/lang/String;)Lnet/minecraft/item/Item;",
      "func_111206_d", "d", "(Ljava/lang/String;)Lzw;");
  if (!getItem || !byName)
    return false;
  jobject heldItem = env->CallObjectMethod(stack, getItem);
  clearException(env);
  jstring registryName = env->NewStringUTF("minecraft:obsidian");
  jobject obsidianItem = registryName
                             ? env->CallStaticObjectMethod(itemClass, byName,
                                                           registryName)
                             : nullptr;
  const bool result = heldItem && obsidianItem &&
                      env->IsSameObject(heldItem, obsidianItem) == JNI_TRUE;
  clearException(env);
  if (obsidianItem) env->DeleteLocalRef(obsidianItem);
  if (registryName) env->DeleteLocalRef(registryName);
  if (heldItem) env->DeleteLocalRef(heldItem);
  return result;
}

bool blockPosition(JNIEnv *env, jobject position, BlockPosition &result) {
  if (!env || !position || !lc)
    return false;
  jclass blockPosClass = lc->GetClass("net.minecraft.util.BlockPos");
  if (!blockPosClass)
    return false;
  jmethodID getX = lc->GetMethodID(blockPosClass, "getX", "()I",
                                   "func_177958_n", "n");
  jmethodID getY = lc->GetMethodID(blockPosClass, "getY", "()I",
                                   "func_177956_o", "o");
  jmethodID getZ = lc->GetMethodID(blockPosClass, "getZ", "()I",
                                   "func_177952_p", "p");
  if (!getX || !getY || !getZ)
    return false;
  result = {env->CallIntMethod(position, getX),
            env->CallIntMethod(position, getY),
            env->CallIntMethod(position, getZ)};
  const bool valid = !env->ExceptionCheck();
  clearException(env);
  return valid;
}

bool facingOffset(JNIEnv *env, jobject side, BlockPosition &result) {
  if (!env || !side || !lc)
    return false;
  jclass facingClass = lc->GetClass("net.minecraft.util.EnumFacing");
  if (!facingClass)
    return false;
  jmethodID getX = lc->GetMethodID(facingClass, "getFrontOffsetX", "()I",
                                   "func_82601_c", "c");
  jmethodID getY = lc->GetMethodID(facingClass, "getFrontOffsetY", "()I",
                                   "func_96559_d", "d");
  jmethodID getZ = lc->GetMethodID(facingClass, "getFrontOffsetZ", "()I",
                                   "func_82599_e", "e");
  if (!getX || !getY || !getZ)
    return false;
  result = {env->CallIntMethod(side, getX), env->CallIntMethod(side, getY),
            env->CallIntMethod(side, getZ)};
  const bool valid = !env->ExceptionCheck();
  clearException(env);
  return valid;
}

std::optional<bool> isTargetReplaceable(JNIEnv *env, jobject world,
                                        jobject target) {
  if (!env || !world || !target || !lc)
    return std::nullopt;
  jclass worldClass = lc->GetClass("net.minecraft.world.World");
  jclass stateClass = lc->GetClass("net.minecraft.block.state.IBlockState");
  jclass blockClass = lc->GetClass("net.minecraft.block.Block");
  if (!worldClass || !stateClass || !blockClass)
    return std::nullopt;
  jmethodID getState = lc->GetMethodID(
      worldClass, "getBlockState",
      "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;",
      "func_180495_p", "p", "(Lcj;)Lalz;");
  jmethodID getBlock = lc->GetMethodID(
      stateClass, "getBlock", "()Lnet/minecraft/block/Block;",
      "func_177230_c", "c", "()Lafh;");
  jmethodID replaceable = lc->GetMethodID(
      blockClass, "isReplaceable",
      "(Lnet/minecraft/world/World;Lnet/minecraft/util/BlockPos;)Z",
      "func_176200_f", "a", "(Ladm;Lcj;)Z");
  if (!getState || !getBlock || !replaceable)
    return std::nullopt;
  jobject state = env->CallObjectMethod(world, getState, target);
  clearException(env);
  jobject block = state ? env->CallObjectMethod(state, getBlock) : nullptr;
  clearException(env);
  const jboolean result = block
                              ? env->CallBooleanMethod(block, replaceable,
                                                       world, target)
                              : JNI_FALSE;
  const bool valid = block && !env->ExceptionCheck();
  clearException(env);
  if (block) env->DeleteLocalRef(block);
  if (state) env->DeleteLocalRef(state);
  return valid ? std::optional<bool>(result == JNI_TRUE) : std::nullopt;
}

std::optional<Resource> classifyResource(const std::string &rawName) {
  const std::string name = normalizeText(rawName);
  if (name.find("ingotiron") != std::string::npos ||
      name.find("iron ingot") != std::string::npos || name == "iron")
    return Resource::Iron;
  if (name.find("ingotgold") != std::string::npos ||
      name.find("gold ingot") != std::string::npos || name == "gold")
    return Resource::Gold;
  if (name.find("diamond") != std::string::npos)
    return Resource::Diamond;
  if (name.find("emerald") != std::string::npos)
    return Resource::Emerald;
  return std::nullopt;
}

ResourceSnapshot scanInventory(JNIEnv *env, jobject player) {
  ResourceSnapshot snapshot;
  if (!env || !player || !lc)
    return snapshot;
  jclass playerClass = lc->GetClass("net.minecraft.entity.player.EntityPlayer");
  jclass inventoryClass =
      lc->GetClass("net.minecraft.entity.player.InventoryPlayer");
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  if (!playerClass || !inventoryClass || !stackClass)
    return snapshot;

  jfieldID inventoryField = lc->GetFieldID(
      playerClass, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;",
      "field_71071_by", "bi", "Lwm;");
  if (!inventoryField)
    inventoryField = lc->FindFieldBySignature(playerClass, "Lwm;");
  if (!inventoryField)
    return snapshot;
  jobject inventory = env->GetObjectField(player, inventoryField);
  clearException(env);
  if (!inventory)
    return snapshot;

  jfieldID mainField = lc->GetFieldID(
      inventoryClass, "mainInventory", "[Lnet/minecraft/item/ItemStack;",
      "field_70462_a", "a", "[Lzx;");
  if (!mainField)
    mainField = lc->FindFieldBySignature(inventoryClass, "[Lzx;");
  jfieldID sizeField = lc->GetFieldID(stackClass, "stackSize", "I",
                                      "field_77994_a", "b");
  if (!mainField || !sizeField) {
    env->DeleteLocalRef(inventory);
    return snapshot;
  }

  auto array = static_cast<jobjectArray>(env->GetObjectField(inventory, mainField));
  clearException(env);
  if (array) {
    const jsize length = std::min<jsize>(env->GetArrayLength(array), 36);
    for (jsize i = 0; i < length; ++i) {
      jobject stack = env->GetObjectArrayElement(array, i);
      if (!stack)
        continue;
      const std::string name = itemName(env, stack);
      const auto resource = classifyResource(name);
      if (resource) {
        const jint count = env->GetIntField(stack, sizeField);
        clearException(env);
        if (count > 0 && count <= 2304)
          snapshot.inventory[static_cast<std::size_t>(*resource)] += count;
      }
      env->DeleteLocalRef(stack);
    }
    env->DeleteLocalRef(array);
    snapshot.inventoryValid = true;
  }
  env->DeleteLocalRef(inventory);

  jobject minecraft = Mc::theMinecraft(env);
  jclass minecraftClass = Mc::minecraftClass();
  if (minecraft && minecraftClass) {
    jfieldID screenField = lc->GetFieldID(
        minecraftClass, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;",
        "field_71462_r", "m", "Laxu;");
    jobject screen = screenField ? env->GetObjectField(minecraft, screenField)
                                 : nullptr;
    clearException(env);
    if (screen) {
      jclass containerClass =
          lc->GetClass("net.minecraft.client.gui.inventory.GuiContainer");
      snapshot.containerOpen =
          containerClass && env->IsInstanceOf(screen, containerClass);
      env->DeleteLocalRef(screen);
    }
    env->DeleteLocalRef(minecraft);
  }
  return snapshot;
}

ArmorTier armorTierFor(JNIEnv *env, jobject player) {
  if (!env || !player || !lc)
    return ArmorTier::None;
  jclass playerClass = lc->GetClass("net.minecraft.entity.player.EntityPlayer");
  if (!playerClass)
    return ArmorTier::None;
  jmethodID getArmor = lc->GetMethodID(
      playerClass, "getCurrentArmor", "(I)Lnet/minecraft/item/ItemStack;",
      "func_71124_b", "q", "(I)Lzx;");
  if (!getArmor)
    return ArmorTier::None;
  ArmorTier best = ArmorTier::None;
  for (int slot = 0; slot < 4; ++slot) {
    jobject stack = env->CallObjectMethod(player, getArmor, slot);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }
    if (!stack)
      continue;
    const std::string name = normalizeText(itemName(env, stack));
    if (name.find("diamond") != std::string::npos)
      best = ArmorTier::Diamond;
    else if (name.find("iron") != std::string::npos && best < ArmorTier::Iron)
      best = ArmorTier::Iron;
    else if (name.find("chain") != std::string::npos && best < ArmorTier::Chain)
      best = ArmorTier::Chain;
    else if (name.find("leather") != std::string::npos &&
             best < ArmorTier::Leather)
      best = ArmorTier::Leather;
    env->DeleteLocalRef(stack);
  }
  return best;
}

// The starting Bedwars kit's chestplate and helmet are leather pieces dyed
// to the wearer's own team color, and unlike boots/leggings they are never
// swapped out by the Iron/Diamond armor shop upgrades -- they stay the
// same dyed leather for the whole match. That makes their dye color a
// reliable, always-available way to learn our own team that doesn't
// depend on catching the one-time "You are on the X Team!" chat line or
// on the scoreboard sidebar having resolved a prefix yet. This only ever
// fills in g_localTeam as a fallback (see setTeamColorSticky) -- it never
// overrides an already-known value, and it's a no-op once one is known.
void detectLocalTeamFromArmor(JNIEnv *env, jobject localPlayer) {
  if (!env || !localPlayer || !lc || !g_localTeam.empty() ||
      g_localName.empty())
    return;
  jclass playerClass = lc->GetClass("net.minecraft.entity.player.EntityPlayer");
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  jclass armorClass = lc->GetClass("net.minecraft.item.ItemArmor");
  if (!playerClass || !stackClass || !armorClass)
    return;
  jmethodID getArmor = lc->GetMethodID(
      playerClass, "getCurrentArmor", "(I)Lnet/minecraft/item/ItemStack;",
      "func_71124_b", "q", "(I)Lzx;");
  jmethodID getItem = lc->GetMethodID(stackClass, "getItem",
                                      "()Lnet/minecraft/item/Item;",
                                      "func_77973_b", "b");
  jmethodID getColor = lc->GetMethodID(
      armorClass, "getColor", "(Lnet/minecraft/item/ItemStack;)I",
      "func_82814_b", "b");
  if (!getArmor || !getItem || !getColor)
    return;
  // getCurrentArmor's slot order is boots=0, leggings=1, chestplate=2,
  // helmet=3; helmet is checked first since it's the piece already relied
  // on elsewhere for this kind of dye-color detection.
  for (const int slot : {3, 2}) {
    jobject stack = env->CallObjectMethod(localPlayer, getArmor, slot);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      continue;
    }
    if (!stack)
      continue;
    jobject item = env->CallObjectMethod(stack, getItem);
    clearException(env);
    if (item && env->IsInstanceOf(item, armorClass)) {
      const int color = env->CallIntMethod(item, getColor, stack);
      clearException(env);
      const std::string team = closestTeamColor(color);
      if (!team.empty()) {
        setTeamColorSticky(g_localName, team);
        if (item)
          env->DeleteLocalRef(item);
        env->DeleteLocalRef(stack);
        return;
      }
    }
    if (item)
      env->DeleteLocalRef(item);
    env->DeleteLocalRef(stack);
  }
}

std::vector<PlayerObservation> scanPlayers(JNIEnv *env, jobject world,
                                           jobject localPlayer, double localX,
                                           double localY, double localZ,
                                           VisibilityMode visibilityMode,
                                           float cameraViewDegrees) {
  std::vector<PlayerObservation> result;
  if (!env || !world || !localPlayer || !lc)
    return result;
  jclass worldClass = lc->GetClass("net.minecraft.world.World");
  jclass entityClass = lc->GetClass("net.minecraft.entity.Entity");
  jclass playerClass = lc->GetClass("net.minecraft.entity.player.EntityPlayer");
  jclass otherPlayerClass =
      lc->GetClass("net.minecraft.client.entity.EntityOtherPlayerMP");
  if (!worldClass || !entityClass || !playerClass || !otherPlayerClass)
    return result;
  jfieldID playersField = lc->GetFieldID(worldClass, "playerEntities",
                                         "Ljava/util/List;", "field_73010_i",
                                         "j");
  if (!playersField)
    return result;
  jobject players = env->GetObjectField(world, playersField);
  clearException(env);
  if (!players)
    return result;
  jclass listClass = env->FindClass("java/util/List");
  jmethodID listSize =
      listClass ? env->GetMethodID(listClass, "size", "()I") : nullptr;
  jmethodID listGet = listClass
                          ? env->GetMethodID(listClass, "get",
                                             "(I)Ljava/lang/Object;")
                          : nullptr;
  if (!listSize || !listGet) {
    clearException(env);
    if (listClass)
      env->DeleteLocalRef(listClass);
    env->DeleteLocalRef(players);
    return result;
  }

  jfieldID posX = lc->GetFieldID(entityClass, "posX", "D", "field_70165_t", "s");
  jfieldID posY = lc->GetFieldID(entityClass, "posY", "D", "field_70163_u", "t");
  jfieldID posZ = lc->GetFieldID(entityClass, "posZ", "D", "field_70161_v", "u");
  jmethodID getId = lc->GetMethodID(entityClass, "getEntityId", "()I",
                                    "func_145782_y", "F");
  jmethodID getName = lc->GetMethodID(entityClass, "getName",
                                      "()Ljava/lang/String;", "func_70005_c_",
                                      "e_");
  jmethodID isAlive = lc->GetMethodID(entityClass, "isEntityAlive", "()Z",
                                      "func_70089_S", "ai");
  jmethodID canSee = lc->GetMethodID(
      playerClass, "canEntityBeSeen", "(Lnet/minecraft/entity/Entity;)Z",
      "func_70685_l", "t", "(Lpk;)Z");
  jmethodID getHeld = lc->GetMethodID(
      playerClass, "getHeldItem", "()Lnet/minecraft/item/ItemStack;",
      "func_70694_bm", "bA", "()Lzx;");
  jmethodID isUsing = lc->GetMethodID(playerClass, "isUsingItem", "()Z",
                                      "func_71039_bw", "bS");
  jmethodID isSpectator = lc->GetMethodID(
      otherPlayerClass, "isSpectator", "()Z", "func_175149_v", "v");
  jclass stackClass = lc->GetClass("net.minecraft.item.ItemStack");
  jmethodID hasEffect = stackClass
                            ? lc->GetMethodID(stackClass, "hasEffect", "()Z",
                                              "func_77948_v", "w")
                            : nullptr;
  jfieldID rotationYaw = lc->GetFieldID(entityClass, "rotationYaw", "F",
                                         "field_70177_z", "y");
  jfieldID rotationPitch = lc->GetFieldID(entityClass, "rotationPitch", "F",
                                           "field_70125_A", "z");
  if (!playersField || !posX || !posY || !posZ || !getId || !getName ||
      !isAlive || !getHeld || !isSpectator) {
    static ULONGLONG lastBindingLog = 0;
    const ULONGLONG now = GetTickCount64();
    if (Configuration::isDebugEnabled() &&
        (lastBindingLog == 0 || now - lastBindingLog >= 10000)) {
      lastBindingLog = now;
      Logger::info("[Bedwars] player scan disabled: essential 1.8.9 binding "
                   "unavailable (list=%d pos=%d id=%d name=%d alive=%d "
                   "held=%d spectator=%d)",
                   playersField ? 1 : 0,
                   posX && posY && posZ ? 1 : 0, getId ? 1 : 0,
                   getName ? 1 : 0, isAlive ? 1 : 0, getHeld ? 1 : 0,
                   isSpectator ? 1 : 0);
    }
    env->DeleteLocalRef(listClass);
    env->DeleteLocalRef(players);
    return result;
  }
  const bool cameraMappingKnown = rotationYaw && rotationPitch;
  float localYaw = rotationYaw ? env->GetFloatField(localPlayer, rotationYaw) : 0.0F;
  float localPitch = rotationPitch ? env->GetFloatField(localPlayer, rotationPitch) : 0.0F;
  clearException(env);
  constexpr double pi = 3.14159265358979323846;
  const double yaw = static_cast<double>(localYaw) * pi / 180.0;
  const double pitch = static_cast<double>(localPitch) * pi / 180.0;
  const double forwardX = -std::sin(yaw) * std::cos(pitch);
  const double forwardY = -std::sin(pitch);
  const double forwardZ = std::cos(yaw) * std::cos(pitch);
  const double cameraThreshold = std::cos(
      std::clamp(static_cast<double>(cameraViewDegrees), 30.0, 170.0) *
      0.5 * pi / 180.0);

  const jint count = std::min<jint>(env->CallIntMethod(players, listSize), 128);
  clearException(env);
  result.reserve(static_cast<std::size_t>(std::max<jint>(0, count)));
  for (jint i = 0; i < count; ++i) {
    jobject player = env->CallObjectMethod(players, listGet, i);
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      break;
    }
    if (!player)
      continue;
    if (!env->IsInstanceOf(player, playerClass)) {
      env->DeleteLocalRef(player);
      continue;
    }
    PlayerObservation observation;
    observation.localPlayer = env->IsSameObject(player, localPlayer);
    observation.entityId = getId ? env->CallIntMethod(player, getId) : -1;
    clearException(env);
    if (getName) {
      jstring name = static_cast<jstring>(env->CallObjectMethod(player, getName));
      clearException(env);
      observation.identity = fromJavaString(env, name);
      if (name)
        env->DeleteLocalRef(name);
    }
    const double x = posX ? env->GetDoubleField(player, posX) : localX;
    const double y = posY ? env->GetDoubleField(player, posY) : localY;
    const double z = posZ ? env->GetDoubleField(player, posZ) : localZ;
    clearException(env);
    const double dx = x - localX;
    const double dy = y - localY;
    const double dz = z - localZ;
    observation.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (visibilityMode != VisibilityMode::RangeOnly && canSee) {
      observation.lineOfSightKnown = true;
      observation.hasLineOfSight =
          env->CallBooleanMethod(localPlayer, canSee, player) == JNI_TRUE;
      if (env->ExceptionCheck()) {
        env->ExceptionClear();
        observation.lineOfSightKnown = false;
        observation.hasLineOfSight = false;
      }
    }
    if (visibilityMode == VisibilityMode::CameraView && cameraMappingKnown &&
        std::isfinite(observation.distance) && observation.distance > 0.001) {
      const double cameraDistance =
          std::sqrt(dx * dx + (dy + 0.9) * (dy + 0.9) + dz * dz);
      if (std::isfinite(cameraDistance) && cameraDistance > 0.001) {
        observation.cameraViewKnown = true;
        const double dot = (dx * forwardX + (dy + 0.9) * forwardY +
                            dz * forwardZ) /
                           cameraDistance;
        observation.insideCameraView =
            std::isfinite(dot) && dot >= cameraThreshold;
      }
    }
    observation.alive = !isAlive || env->CallBooleanMethod(player, isAlive);
    clearException(env);
    observation.spectator =
        env->IsInstanceOf(player, otherPlayerClass) &&
        env->CallBooleanMethod(player, isSpectator) == JNI_TRUE;
    clearException(env);
    observation.armor = armorTierFor(env, player);
    jobject held = getHeld ? env->CallObjectMethod(player, getHeld) : nullptr;
    clearException(env);
    if (held) {
      observation.heldItem.typeName = itemName(env, held);
      observation.heldItem.displayName = itemDisplayName(env, held);
      observation.heldItem.metadata = itemMetadata(env, held);
      observation.heldItem.enchanted =
          hasEffect && env->CallBooleanMethod(held, hasEffect) == JNI_TRUE;
      clearException(env);
      env->DeleteLocalRef(held);
    }
    observation.usingItem =
        isUsing && env->CallBooleanMethod(player, isUsing) == JNI_TRUE;
    clearException(env);
    if (!observation.identity.empty()) {
      std::lock_guard<std::mutex> lock(g_statsMutex);
      const auto team = g_playerTeamColor.find(observation.identity);
      if (team != g_playerTeamColor.end()) {
        observation.team = normalizeTeam(team->second);
        observation.teamAuthoritative = observation.team != TeamId::Unknown;
      }
      const TeamId localTeam = normalizeTeam(g_localTeam);
      if (observation.teamAuthoritative && localTeam != TeamId::Unknown) {
        observation.teammateKnown = true;
        observation.teammate = observation.team == localTeam;
      }
    }
    result.push_back(std::move(observation));
    env->DeleteLocalRef(player);
  }
  env->DeleteLocalRef(listClass);
  env->DeleteLocalRef(players);
  return result;
}

std::string formatDuration(Tick milliseconds) {
  const Tick totalSeconds = (milliseconds + 999) / 1000;
  const Tick minutes = totalSeconds / 60;
  const Tick seconds = totalSeconds % 60;
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "%llu:%02llu",
                static_cast<unsigned long long>(minutes),
                static_cast<unsigned long long>(seconds));
  return buffer;
}

} // namespace

Runtime &Runtime::instance() {
  static Runtime runtime;
  return runtime;
}

void Runtime::onChatMessage(const std::string &message) {
  if (message.empty() || m_shuttingDown.load(std::memory_order_acquire))
    return;
  const std::string bounded = message.substr(0, 512);
  std::lock_guard<std::mutex> lock(m_queueMutex);
  if (m_lines.push({QueuedKind::Chat, bounded, GetTickCount64()}))
    m_totalDroppedLines.fetch_add(1, std::memory_order_relaxed);
}

void Runtime::onScoreboardLine(const std::string &line) {
  if (line.empty() || m_shuttingDown.load(std::memory_order_acquire))
    return;
  const std::string bounded = line.substr(0, 512);
  std::lock_guard<std::mutex> lock(m_queueMutex);
  if (m_lines.push({QueuedKind::Scoreboard, bounded, GetTickCount64()}))
    m_totalDroppedLines.fetch_add(1, std::memory_order_relaxed);
}

void Runtime::drainLines(Tick now) {
  std::deque<QueuedLine> lines;
  std::uint64_t dropped = 0;
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    lines = m_lines.take();
    dropped = m_lines.consumeDropped();
  }
  const auto settings = Configuration::get();
  if (!settings.masterEnabled)
    return;
  for (const auto &line : lines) {
    if (line.kind == QueuedKind::Scoreboard) {
      const std::string clean = stripFormatting(line.text);
      const std::string normalized = normalizeText(clean);
      if (const auto map = parseMapScoreboardLine(clean)) {
        if (m_mapName != *map) {
          m_mapName = *map;
          m_mapHeight = resolveMapHeight(m_mapName,
                                         settings.mapPlacementOverrides);
          if (settings.debug)
            Logger::info("[Bedwars] map=%s placementLimit=%d source=%s",
                         m_mapName.c_str(),
                         m_mapHeight.maximumPlacementY.value_or(-1),
                         m_mapHeight.overridden ? "override" :
                         (m_mapHeight.maximumPlacementY ? "built-in" : "unknown"));
        }
      }
      const std::size_t mode = normalized.find("mode:");
      if (mode != std::string::npos) {
        const std::size_t colon = clean.find(':');
        if (colon != std::string::npos)
          m_modeName = clean.substr(colon + 1);
        const std::string lowerMode = normalizeText(m_modeName);
        if (lowerMode.find("solo") != std::string::npos ||
            lowerMode.find("double") != std::string::npos)
          m_teamCount = 8;
        else if (lowerMode.find("3v3") != std::string::npos ||
                 lowerMode.find("4v4v4v4") != std::string::npos)
          m_teamCount = 4;
        else if (lowerMode.find("4v4") != std::string::npos)
          m_teamCount = 2;
      }
      if (const auto event = EventSchedule::parseScoreboardLine(clean)) {
        m_scoreboardEvent = event;
        m_scoreboardEventObserved = line.received;
      }
      continue;
    }

    if (m_context.phase() != GamePhase::Active &&
        m_context.phase() != GamePhase::Spectator)
      continue;
    if (const auto destroyed = parseDestroyedBedTeam(line.text)) {
      m_teams.observeBed(*destroyed, BedState::Destroyed, line.received,
                         "server chat");
      const TeamId localTeam = normalizeTeam(g_localTeam);
      if (localTeam != TeamId::Unknown && *destroyed == localTeam) {
        m_ownBedConfirmedDestroyed = true;
        if (m_ownBed)
          m_ownBed->alive = false;
      }
      if (settings.debug)
        Logger::info("[Bedwars] %s bed marked destroyed from chat",
                     teamName(*destroyed));
    }
    if (const auto victim = parseDeathVictim(line.text)) {
      // On respawn a player's gear resets (starting sword, no armor, no
      // held potions/tools), but our per-player dedup only ever alerts once
      // per item/tier for the whole match. Without this, someone who was
      // alerted for e.g. an Invisibility Potion before dying would never
      // get alerted again after respawning and picking one up a second
      // time. Forgetting them here lets the next sighting be treated as a
      // fresh first-look, same as when a player is seen for the very first
      // time.
      m_players.forgetPlayer(*victim);
      if (settings.debug)
        Logger::info("[Bedwars] cleared alert history for %s (died)",
                     victim->c_str());
    }
    const auto signal = m_chat.observe(line.text, line.received);
    if (!signal)
      continue;
    if (signal->kind == ChatSignal::Kind::Upgrade &&
        settings.enabled(Module::UpgradeAlerts)) {
      notify("Team Upgrade", signal->label + " confirmed from chat", false,
             true);
    } else if (signal->kind == ChatSignal::Kind::TrapTriggered &&
               settings.enabled(Module::TrapNotifier)) {
      notify("Trap", "Your trap was triggered", true, true);
      m_lastTrapReminder = now;
    } else if (signal->kind == ChatSignal::Kind::TrapQueued &&
               settings.enabled(Module::TrapNotifier)) {
      notify("Trap", "Trap purchase observed", false, false);
      m_lastTrapReminder = 0;
    } else if (signal->kind == ChatSignal::Kind::TrapMissing &&
               settings.enabled(Module::TrapNotifier)) {
      notify("Trap", "No trap is currently queued", true, false);
      m_lastTrapReminder = now;
    }
  }

  if (settings.debug && dropped > 0) {
    Logger::info("[Bedwars] Bounded input queue dropped %llu old lines",
                 static_cast<unsigned long long>(dropped));
  }
  (void)now;
}

void Runtime::tick() {
  if (m_shuttingDown.load(std::memory_order_acquire) || !lc)
    return;
  JNIEnv *env = lc->getEnv();
  if (!env)
    return;
  const Tick now = GetTickCount64();
  const auto settings = Configuration::get();

  jobject world = Mc::theWorld(env);
  jobject player = Mc::thePlayer(env);
  if (player)
    detectLocalTeamFromArmor(env, player);
  if (world) {
    auto globalWorld = static_cast<jobject>(m_worldReference);
    if (!globalWorld || !env->IsSameObject(globalWorld, world)) {
      if (globalWorld)
        env->DeleteGlobalRef(globalWorld);
      m_worldReference = env->NewGlobalRef(world);
      ++m_worldToken;
    }
  } else if (m_worldReference) {
    env->DeleteGlobalRef(static_cast<jobject>(m_worldReference));
    m_worldReference = nullptr;
    ++m_worldToken;
  }

  int entityId = -1;
  float health = 0.0F;
  int dimension = 0;
  bool playerStateValid = false;
  if (player) {
    jclass entityClass = lc->GetClass("net.minecraft.entity.Entity");
    jclass livingClass = lc->GetClass("net.minecraft.entity.EntityLivingBase");
    bool positionValid = false;
    bool entityIdValid = false;
    bool healthValid = false;
    if (entityClass) {
      jfieldID posX = lc->GetFieldID(entityClass, "posX", "D", "field_70165_t", "s");
      jfieldID posY = lc->GetFieldID(entityClass, "posY", "D", "field_70163_u", "t");
      jfieldID posZ = lc->GetFieldID(entityClass, "posZ", "D", "field_70161_v", "u");
      jmethodID getId = lc->GetMethodID(entityClass, "getEntityId", "()I",
                                        "func_145782_y", "F");
      jfieldID dimensionField = lc->GetFieldID(
          entityClass, "dimension", "I", "field_71093_bK", "am");
      if (posX && posY && posZ) {
        m_playerX = env->GetDoubleField(player, posX);
        m_playerY = env->GetDoubleField(player, posY);
        m_playerZ = env->GetDoubleField(player, posZ);
        positionValid = !env->ExceptionCheck() && std::isfinite(m_playerX) &&
                        std::isfinite(m_playerY) && std::isfinite(m_playerZ);
        clearException(env);
      }
      if (getId) {
        entityId = env->CallIntMethod(player, getId);
        entityIdValid = !env->ExceptionCheck() && entityId >= 0;
        clearException(env);
      }
      if (dimensionField) {
        dimension = env->GetIntField(player, dimensionField);
        clearException(env);
      }
    }
    if (livingClass) {
      jmethodID getHealth = lc->GetMethodID(livingClass, "getHealth", "()F",
                                            "func_110143_aJ", "bn");
      if (getHealth) {
        health = env->CallFloatMethod(player, getHealth);
        healthValid = !env->ExceptionCheck() && std::isfinite(health);
        clearException(env);
      }
    }
    playerStateValid = positionValid && entityIdValid && healthValid;
  }

  SessionObservation observation;
  observation.featureEnabled = settings.masterEnabled;
  observation.worldValid = world != nullptr;
  observation.playerValid = player != nullptr && playerStateValid;
  observation.onHypixel = g_inHypixelGame || g_inPreGameLobby;
  observation.bedwarsMode = g_mode == 0;
  observation.inGame = g_inHypixelGame;
  observation.preGame = g_inPreGameLobby;
  observation.replay = g_inReplay;
  observation.dead = playerStateValid && health <= 0.0F;
  observation.dimension = dimension;
  observation.localEntityId = entityId;
  observation.worldIdentity = m_worldToken;
  observation.mapName = m_mapName;
  observation.modeName = m_modeName;
  observation.localTeam = g_localTeam;
  observation.teamCount = m_teamCount;
  const LifecycleTransition transition = m_context.observe(observation, now);
  if (transition.reset && !transition.preservedGameClock) {
    m_mapName.clear();
    m_modeName.clear();
    m_teamCount = 0;
  }
  if (transition.reset || transition.gameStarted || transition.gameEnded)
    resetState(transition.reset
                   ? transition.reason.c_str()
                   : (transition.gameStarted ? "game started" : "game ended"));
  if (settings.debug && (transition.reset || transition.gameStarted ||
                         transition.gameEnded)) {
    Logger::info("[Bedwars] lifecycle reset=%d start=%d end=%d reason=%s",
                 transition.reset ? 1 : 0, transition.gameStarted ? 1 : 0,
                 transition.gameEnded ? 1 : 0, transition.reason.c_str());
  }

  drainLines(now);
  const bool active = m_context.phase() == GamePhase::Active ||
                      m_context.phase() == GamePhase::Spectator;
  auto *bedManager = BedDefense::BedDefenseManager::getInstance();
  const bool needBeds = active &&
                        (settings.enabled(Module::BedTracker) ||
                         settings.enabled(Module::AntiMisplace));
  if (needBeds && !bedManager->isEnabled())
    bedManager->enable();
  else if (!needBeds && !Config::isBedDefenseEnabled() && bedManager->isEnabled())
    bedManager->disable();
  if (!needBeds) {
    if (!m_ownBedConfirmedDestroyed)
      m_ownBed.reset();
    m_lastBedScan = 0;
  }

  const bool needResources = active &&
                             (settings.enabled(Module::ResourceTracker) ||
                              settings.enabled(Module::PickupAlerts) ||
                              settings.enabled(Module::ShopHelper));
  if (!needResources) {
    m_resources.reset();
    m_latestResources = {};
    m_lastInventoryScan = 0;
  }

  if (needResources && player &&
      (m_lastInventoryScan == 0 || now - m_lastInventoryScan >= 250)) {
    m_lastInventoryScan = now;
    m_latestResources = scanInventory(env, player);
    const auto deltas = m_resources.observe(m_latestResources);
    std::vector<std::string> resourceMessages;
    for (const auto &delta : deltas) {
      const std::size_t index = static_cast<std::size_t>(delta.resource);
      if (index >= settings.resources.size() || !settings.resources[index])
        continue;
      if (settings.enabled(Module::ResourceTracker) ||
          settings.enabled(Module::PickupAlerts)) {
        const std::string message = "+" + std::to_string(delta.amount) + " " +
                                    resourceName(delta.resource);
        if (settings.stackedResourceAlerts) {
          notify(delta.enderChest ? "Ender Chest" : "Resources", message,
                 false, settings.enabled(Module::PickupAlerts));
        } else {
          resourceMessages.push_back(message);
        }
      }
    }
    if (!settings.stackedResourceAlerts && !resourceMessages.empty()) {
      std::string combined;
      for (const auto &message : resourceMessages) {
        if (!combined.empty())
          combined += ", ";
        combined += message;
      }
      notify("Resources", combined, false,
             settings.enabled(Module::PickupAlerts));
    }
  }

  const bool needPlayers = active &&
                           (settings.enabled(Module::ArmorAlerts) ||
                            settings.enabled(Module::UpgradeAlerts) ||
                            settings.enabled(Module::ConsumeAlerts) ||
                            settings.enabled(Module::ItemAlerts));
  if (!needPlayers) {
    m_players.reset();
    m_lastPlayerScan = 0;
    m_lastScannedPlayers = 0;
  }
  if (needPlayers && world && player &&
      (m_lastPlayerScan == 0 || now - m_lastPlayerScan >= 300)) {
    m_lastPlayerScan = now;
    PlayerAlertOptions options;
    options.armor = settings.enabled(Module::ArmorAlerts);
    options.upgrades = settings.enabled(Module::UpgradeAlerts);
    options.consumes = settings.enabled(Module::ConsumeAlerts);
    options.items = settings.enabled(Module::ItemAlerts);
    options.maximumDistance = settings.playerAlertRange;
    options.cooldownMs = static_cast<Tick>(settings.playerAlertCooldownMs);
    options.visibility = settings.visibilityMode;
    const auto scanned = scanPlayers(env, world, player, m_playerX, m_playerY,
                                     m_playerZ, settings.visibilityMode,
                                     settings.cameraViewDegrees);
    m_lastScannedPlayers = scanned.size();
    const auto alerts = m_players.observe(scanned, options, now, &m_teams);
    m_teams.expire(now);
    for (const auto &alert : alerts) {
      const bool warning = alert.kind == PlayerAlert::Kind::KnockbackStick;
      notify(alert.kind == PlayerAlert::Kind::Upgrade ? "Team Upgrade"
                                                     : "Player Alert",
             alert.text, warning, true,
             alert.kind == PlayerAlert::Kind::Upgrade ? NoticeKind::Important
                                                       : NoticeKind::Player,
             alert.segments);
    }
  }

  if (active && needBeds &&
      (m_lastBedScan == 0 ||
       now - m_lastBedScan >= static_cast<Tick>(settings.bedScanIntervalMs))) {
    m_lastBedScan = now;
    if (!m_ownBedConfirmedDestroyed)
      m_ownBed.reset();
    std::lock_guard<std::mutex> lock(bedManager->getMutex());
    std::optional<OwnBed> ownCandidate;
    bool ownCandidateAmbiguous = false;
    const TeamId localTeam = normalizeTeam(g_localTeam);
    for (const auto &[key, bed] : bedManager->getBeds()) {
      (void)key;
      const TeamId bedTeam = normalizeTeam(bed.teamColor);
      if (bedTeam != TeamId::Unknown)
        m_teams.observeBed(bedTeam, BedState::Alive, now,
                           bed.teamAssignmentConfident ? "defense color"
                                                       : "unconfirmed");
      if (!m_ownBedConfirmedDestroyed && localTeam != TeamId::Unknown &&
          bedTeam == localTeam &&
          bed.teamAssignmentConfident && bed.twoBlockStructure) {
        OwnBed own;
        own.head = {bed.x, bed.y, bed.z};
        own.foot = {bed.secondX, bed.secondY, bed.secondZ};
        own.axis = bed.x != bed.secondX ? BedAxis::X : BedAxis::Z;
        own.team = bedTeam;
        own.confident = true;
        own.alive = true;
        own.confidenceSource = "two-block structure and defense color";
        own.worldGeneration = m_context.generation();
        if (ownCandidate)
          ownCandidateAmbiguous = true;
        else
          ownCandidate = own;
      }
    }
    if (!ownCandidateAmbiguous && ownCandidate)
      m_ownBed = std::move(ownCandidate);
  }

  const BedDistanceResult bedDistance =
      evaluateBedDistance(m_ownBed ? std::optional<BlockPosition>(m_ownBed->head)
                                   : std::nullopt,
                          m_playerX, m_playerY, m_playerZ,
                          settings.bedWarningRange);
  if (active && settings.enabled(Module::BedTracker) &&
      bedDistance.outsideWarningRange &&
      (m_lastBedWarning == 0 || now - m_lastBedWarning >= 15000)) {
    m_lastBedWarning = now;
    notify("Bed Range", "You are outside the configured bed range", true,
           true);
  }

  if (active && settings.enabled(Module::TrapNotifier) &&
      m_chat.trapState() == TrapState::Missing) {
    const Tick interval = static_cast<Tick>(settings.trapReminderSeconds) * 1000;
    if (m_context.elapsed(now) >= interval &&
        (m_lastTrapReminder == 0 || now - m_lastTrapReminder >= interval)) {
      m_lastTrapReminder = now;
      notify("Trap", "No trap is currently queued", true, false);
    }
  }

  rebuildSnapshot(now, m_playerY, bedDistance);
  logSummary(now, settings, m_lastScannedPlayers,
             active ? "active" : "lifecycle inactive");
  if (player)
    env->DeleteLocalRef(player);
  if (world)
    env->DeleteLocalRef(world);
}

void Runtime::rebuildSnapshot(Tick now, double playerY,
                              const BedDistanceResult &bedDistance) {
  const auto settings = Configuration::get();
  RenderSnapshot snapshot;
  snapshot.active = m_context.phase() == GamePhase::Active ||
                    m_context.phase() == GamePhase::Spectator;
  snapshot.generation = m_context.generation();
  snapshot.mapName = m_mapName;
  switch (m_context.phase()) {
  case GamePhase::Inactive:
    snapshot.lifecycleStatus = !settings.masterEnabled ? "Disabled" : "Waiting for Hypixel";
    break;
  case GamePhase::Lobby: snapshot.lifecycleStatus = "In Bedwars lobby"; break;
  case GamePhase::PreGame: snapshot.lifecycleStatus = "In Bedwars lobby"; break;
  case GamePhase::Active: snapshot.lifecycleStatus = "Active in match"; break;
  case GamePhase::Spectator: snapshot.lifecycleStatus = "Active (spectating)"; break;
  case GamePhase::PostGame: snapshot.lifecycleStatus = "Post-game"; break;
  }
  if (m_context.lastObservation().replay)
    snapshot.lifecycleStatus = "In replay";
  PlacementObservation readiness;
  readiness.masterEnabled = settings.masterEnabled;
  readiness.moduleEnabled =
      settings.modules[static_cast<std::size_t>(Module::AntiMisplace)];
  readiness.hookAvailable = inputHookAvailable();
  readiness.activeMatch = m_context.phase() == GamePhase::Active;
  readiness.worldCurrent =
      m_ownBed && m_ownBed->worldGeneration == m_context.generation();
  readiness.ownBedDestroyed = m_ownBedConfirmedDestroyed;
  readiness.localTeam = normalizeTeam(g_localTeam);
  readiness.ownBed = m_ownBed;
  snapshot.antiMisplaceStatus =
      antiMisplaceStatusName(antiMisplaceStatus(readiness));
  std::ostringstream antiDetails;
  antiDetails << "Team " << teamName(readiness.localTeam) << "; hook "
              << (readiness.hookAvailable ? "installed" : "unavailable")
              << "; generation " << m_context.generation();
  if (!readiness.hookAvailable) {
    const std::string failure = BedwarsPlacementHook::lastFailure();
    if (!failure.empty()) antiDetails << "; hook reason " << failure;
  }
  if (Config::isForgeEnvironment())
    antiDetails << "; own-bed scanner unavailable in Forge environment";
  if (m_ownBed) {
    antiDetails << "; bed " << m_ownBed->head.x << ',' << m_ownBed->head.y
                << ',' << m_ownBed->head.z << " to " << m_ownBed->foot.x
                << ',' << m_ownBed->foot.y << ',' << m_ownBed->foot.z
                << "; axis " << (m_ownBed->axis == BedAxis::X ? 'X' : 'Z')
                << "; source "
                << (m_ownBed->confidenceSource.empty()
                        ? "unknown"
                        : m_ownBed->confidenceSource);
  } else {
    antiDetails << "; bed unknown";
  }
  if (!m_lastPlacementReason.empty())
    antiDetails << "; last " << m_lastPlacementReason;
  snapshot.antiMisplaceDetails = antiDetails.str();
  if (!snapshot.active) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_snapshot = std::move(snapshot);
    return;
  }

  if (settings.enabled(Module::EventTimers)) {
    if (m_scoreboardEvent && now >= m_scoreboardEventObserved &&
        now - m_scoreboardEventObserved <= 2000) {
      const Tick age = now - m_scoreboardEventObserved;
      const Tick remaining = age >= m_scoreboardEvent->remainingMs
                                 ? 0
                                 : m_scoreboardEvent->remainingMs - age;
      snapshot.timerLines.push_back(m_scoreboardEvent->label + "  " +
                                    formatDuration(remaining));
      snapshot.timerUrgency = remaining <= 60000 ? 2 :
                              remaining <= 180000 ? 1 : 0;
    } else if (m_context.gameClockKnown()) {
      for (const auto &event : EventSchedule::countdowns(
               m_context.elapsed(now), settings.onlyNextEvent)) {
        if (snapshot.timerLines.empty())
          snapshot.timerUrgency = event.remainingMs <= 60000 ? 2 :
                                  event.remainingMs <= 180000 ? 1 : 0;
        snapshot.timerLines.push_back(event.label + "  " +
                                      formatDuration(event.remainingMs));
      }
    } else {
      snapshot.timerLines.push_back("Timing unknown (join observed late)");
    }
  }

  if (settings.enabled(Module::HeightOverlay)) {
    m_mapHeight = resolveMapHeight(m_mapName, settings.mapPlacementOverrides);
    if (!m_mapHeight.maximumPlacementY && settings.heightLimitOverride > 0) {
      m_mapHeight.maximumPlacementY = settings.heightLimitOverride;
      m_mapHeight.maximumPlayerY = settings.heightLimitOverride + 1;
      m_mapHeight.overridden = true;
    }
    const int currentY = std::isfinite(playerY)
                             ? static_cast<int>(std::floor(playerY))
                             : 0;
    snapshot.heightLine = "Y " + std::to_string(currentY) + "  Map " +
                          (m_mapName.empty() ? "Unknown" : m_mapName);
    if (m_mapHeight.maximumPlacementY) {
      snapshot.maximumPlacementY = *m_mapHeight.maximumPlacementY;
      const int remaining =
          std::max(0, *m_mapHeight.maximumPlacementY - currentY);
      snapshot.heightLine += "  Build " +
                             std::to_string(*m_mapHeight.maximumPlacementY) +
                             "  Remaining " + std::to_string(remaining);
      snapshot.heightUrgency = remaining <= 5 ? 2 : remaining <= 15 ? 1 : 0;
    } else {
      snapshot.heightLine += "  Build Unknown";
    }
  }

  if (settings.enabled(Module::UpgradeHud)) {
    const UpgradeState &upgrade = m_chat.upgrades();
    auto knownLevel = [](int level, bool known) {
      return known ? std::to_string(level) : std::string("?");
    };
    snapshot.upgradeLines.push_back(
        std::string(settings.shortUpgradeLabels ? "Sharp " : "Sharpness ") +
        knownLevel(upgrade.sharpness, upgrade.sharpnessKnown));
    snapshot.upgradeLines.push_back(
        std::string(settings.shortUpgradeLabels ? "Prot " : "Protection ") +
        knownLevel(upgrade.protection, upgrade.protectionKnown));
    snapshot.upgradeLines.push_back(
        "Forge " + knownLevel(upgrade.forge, upgrade.forgeKnown));
    snapshot.upgradeLines.push_back(
        "Haste " + knownLevel(upgrade.haste, upgrade.hasteKnown));
    snapshot.upgradeLines.push_back(
        std::string(settings.shortUpgradeLabels ? "Feather "
                                                : "Feather Falling ") +
        knownLevel(upgrade.featherFalling, upgrade.featherFallingKnown));
    if (upgrade.healPool)
      snapshot.upgradeLines.push_back("Heal Pool");
    if (upgrade.dragonBuff)
      snapshot.upgradeLines.push_back("Dragon Buff");
    snapshot.upgradeLines.push_back(
        m_chat.trapState() == TrapState::Queued
            ? "Trap queued"
            : m_chat.trapState() == TrapState::Missing ? "Trap missing"
                                                       : "Trap ?");
  }

  for (const auto &team : m_teams.teams()) {
    std::string line = std::string(teamName(team.id)) + "  Bed ";
    line += team.bed == BedState::Alive
                ? "Alive"
                : team.bed == BedState::Destroyed ? "Destroyed" : "Unknown";
    line += team.sharpnessObserved ? "  Sharpness" : "";
    snapshot.bedStatusLines.push_back(std::move(line));
  }

  if (settings.enabled(Module::ResourceTracker) &&
      m_latestResources.inventoryValid) {
    for (std::size_t i = 0; i < kResourceCount; ++i) {
      if (settings.resources[i])
        snapshot.resourceLines.push_back(
            std::string(resourceName(static_cast<Resource>(i))) + " " +
            std::to_string(m_latestResources.inventory[i]));
    }
  }

  if (settings.enabled(Module::BedTracker)) {
    if (bedDistance.known) {
      std::ostringstream text;
      text << std::fixed << std::setprecision(1) << "Bed "
           << bedDistance.distance << "m";
      snapshot.bedLine = text.str();
    } else {
      snapshot.bedLine = "Bed unknown";
    }
  }
  std::lock_guard<std::mutex> lock(m_snapshotMutex);
  m_snapshot = std::move(snapshot);
}

void Runtime::notify(const std::string &title, const std::string &message,
                     bool warning, bool playSound, NoticeKind kind,
                     const std::vector<MessageSegment> &segments) {
  const auto settings = Configuration::get();
  float duration = settings.defaultNotificationSeconds;
  switch (kind) {
  case NoticeKind::Important: duration = settings.importantNotificationSeconds; break;
  case NoticeKind::Player: duration = settings.playerNotificationSeconds; break;
  case NoticeKind::Warning: duration = settings.warningNotificationSeconds; break;
  default: break;
  }
  if (warning)
    duration = settings.warningNotificationSeconds;
  if (segments.empty()) {
    Render::NotificationManager::getInstance()->add(
        title, message, warning ? Render::NotificationType::Warning
                                : Render::NotificationType::Info,
        duration, static_cast<std::size_t>(settings.maximumVisibleNotifications));
  } else {
    std::vector<Render::NotificationSegment> rich;
    rich.reserve(segments.size());
    for (const auto &segment : segments)
      rich.push_back({segment.text, segment.argb});
    Render::NotificationManager::getInstance()->addRich(
        title, rich, warning ? Render::NotificationType::Warning
                             : Render::NotificationType::Info,
        duration, static_cast<std::size_t>(settings.maximumVisibleNotifications));
  }
  if (settings.debug)
    Logger::info("[Bedwars] notification title=%s duration=%.1fs queueLimit=%d",
                 title.c_str(), duration, settings.maximumVisibleNotifications);
  if (!playSound || !settings.sounds || !lc)
    return;
  JNIEnv *env = lc->getEnv();
  jobject player = env ? Mc::thePlayer(env) : nullptr;
  if (!env || !player)
    return;
  jclass entityClass = lc->GetClass("net.minecraft.entity.Entity");
  jmethodID sound = entityClass
                        ? lc->GetMethodID(entityClass, "playSound",
                                          "(Ljava/lang/String;FF)V",
                                          "func_85030_a", "a")
                        : nullptr;
  if (sound) {
    jstring name = env->NewStringUTF("random.orb");
    if (name) {
      env->CallVoidMethod(player, sound, name, 0.35F, 1.15F);
      clearException(env);
      env->DeleteLocalRef(name);
    }
  }
  env->DeleteLocalRef(player);
}

void Runtime::logSummary(Tick now, const Configuration::Settings &settings,
                         std::size_t scannedPlayers,
                         const char *overlayReason) {
  if (!settings.debug ||
      (m_lastDiagnosticSummary != 0 && now >= m_lastDiagnosticSummary &&
       now - m_lastDiagnosticSummary < 5000))
    return;
  m_lastDiagnosticSummary = now;
  const auto &reject = m_players.rejectionCounts();
  const auto &playerDiag = m_players.diagnostics();
  auto *beds = BedDefense::BedDefenseManager::getInstance();
  std::size_t bedCount = 0;
  std::size_t confidentBeds = 0;
  {
    std::lock_guard<std::mutex> lock(beds->getMutex());
    bedCount = beds->getBeds().size();
    for (const auto &[key, bed] : beds->getBeds()) {
      (void)key;
      if (bed.teamAssignmentConfident) ++confidentBeds;
    }
  }
  Logger::info(
      "[Bedwars] summary master=%d phase=%d hypixel=%d mode=%d replay=%d "
      "localEntity=%d localTeam=%s map=%s limit=%d players=%d local=%d "
      "teammate=%d dead=%d spectator=%d range=%d los=%d camera=%d "
      "visibilityUnknown=%d invalidIdentity=%d emitted=%d cooldown=%d "
      "swordGlint=%d sharpDedupe=%d potions=%d potionDedupe=%d kb=%d "
      "kbDedupe=%d unknownItems=%d ignoredItems=%d itemDedupe=%d beds=%d "
      "confidentBeds=%d ownBed=%d antiHook=%d resourcesValid=%d overlay=%s "
      "modules=%d%d%d%d%d%d%d%d%d%d%d%d%d "
      "hud=%d%d%d%d%d%d queueDrops=%llu",
      settings.masterEnabled ? 1 : 0, static_cast<int>(m_context.phase()),
      m_context.lastObservation().onHypixel ? 1 : 0,
      m_context.lastObservation().bedwarsMode ? 1 : 0,
      m_context.lastObservation().replay ? 1 : 0,
      m_context.lastObservation().localEntityId,
      m_context.lastObservation().localTeam.empty()
          ? "Unknown"
          : m_context.lastObservation().localTeam.c_str(),
      m_mapName.empty() ? "Unknown" : m_mapName.c_str(),
      m_mapHeight.maximumPlacementY.value_or(-1),
      static_cast<int>(scannedPlayers),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::LocalPlayer)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::Teammate)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::Dead)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::Spectator)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::OutOfRange)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::NoLineOfSight)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::OutsideCameraView)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::UnknownVisibility)]),
      static_cast<int>(reject[static_cast<std::size_t>(PlayerRejectReason::InvalidIdentity)]),
      static_cast<int>(playerDiag.emitted),
      static_cast<int>(playerDiag.cooldownRejected),
      static_cast<int>(playerDiag.observedSwordGlints),
      static_cast<int>(playerDiag.sharpnessDuplicates),
      static_cast<int>(playerDiag.classifiedPotions),
      static_cast<int>(playerDiag.potionDuplicates),
      static_cast<int>(playerDiag.classifiedKnockback),
      static_cast<int>(playerDiag.knockbackDuplicates),
      static_cast<int>(playerDiag.unknownItems),
      static_cast<int>(playerDiag.ignoredItems),
      static_cast<int>(playerDiag.itemDuplicates), static_cast<int>(bedCount),
      static_cast<int>(confidentBeds), m_ownBed ? 1 : 0,
      inputHookAvailable() ? 1 : 0,
      m_latestResources.inventoryValid ? 1 : 0,
      overlayReason ? overlayReason : "unknown",
      settings.enabled(Module::EventTimers) ? 1 : 0,
      settings.enabled(Module::ShopHelper) ? 1 : 0,
      settings.enabled(Module::AntiMisplace) ? 1 : 0,
      settings.enabled(Module::BedTracker) ? 1 : 0,
      settings.enabled(Module::HeightOverlay) ? 1 : 0,
      settings.enabled(Module::UpgradeAlerts) ? 1 : 0,
      settings.enabled(Module::ConsumeAlerts) ? 1 : 0,
      settings.enabled(Module::PickupAlerts) ? 1 : 0,
      settings.enabled(Module::ArmorAlerts) ? 1 : 0,
      settings.enabled(Module::TrapNotifier) ? 1 : 0,
      settings.enabled(Module::ResourceTracker) ? 1 : 0,
      settings.enabled(Module::ItemAlerts) ? 1 : 0,
      settings.enabled(Module::UpgradeHud) ? 1 : 0,
      settings.hud[0].visible ? 1 : 0, settings.hud[1].visible ? 1 : 0,
      settings.hud[2].visible ? 1 : 0, settings.hud[3].visible ? 1 : 0,
      settings.hud[4].visible ? 1 : 0, settings.hud[5].visible ? 1 : 0,
      static_cast<unsigned long long>(m_totalDroppedLines.load()));

  std::string rejectionDetails;
  const auto &details = m_players.rejectionDetails();
  for (std::size_t i = 0; i < details.size() && i < 8; ++i) {
    if (!rejectionDetails.empty()) rejectionDetails += ", ";
    rejectionDetails += details[i].identity.empty()
                            ? "entity#" + std::to_string(details[i].entityId)
                            : details[i].identity;
    rejectionDetails += ":";
    rejectionDetails += playerRejectReasonName(details[i].reason);
  }
  if (rejectionDetails.empty()) rejectionDetails = "none";
  if (rejectionDetails != m_lastRejectionDetails) {
    m_lastRejectionDetails = rejectionDetails;
    Logger::info("[Bedwars] player rejection details: %s",
                 rejectionDetails.c_str());
  }
}

void Runtime::resetState(const char *reason) {
  BedDefense::BedDefenseManager::getInstance()->onWorldChange();
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_lines.clear();
  }
  m_totalDroppedLines.store(0, std::memory_order_release);
  m_resources.reset();
  m_chat.reset();
  m_players.reset();
  m_teams.reset();
  m_latestResources = {};
  m_scoreboardEvent.reset();
  m_scoreboardEventObserved = 0;
  m_lastInventoryScan = 0;
  m_lastPlayerScan = 0;
  m_lastBedScan = 0;
  m_lastTrapReminder = 0;
  m_lastBedWarning = 0;
  m_lastScannedPlayers = 0;
  m_mapHeight = {};
  m_ownBed.reset();
  m_ownBedConfirmedDestroyed = false;
  m_lastPlacementReason.clear();
  m_lastRejectionDetails.clear();
  m_lastPlacementLog = 0;
  m_lastPlacementNotice = 0;
  if (Configuration::isDebugEnabled())
    Logger::info("[Bedwars] state reset: %s", reason ? reason : "unknown");
}

void Runtime::reset(const char *reason) {
  resetState(reason);
  m_context.reset(reason);
  m_mapName.clear();
  m_modeName.clear();
  m_teamCount = 0;
  std::lock_guard<std::mutex> lock(m_snapshotMutex);
  m_snapshot = {};
}

void Runtime::setInputHookAvailable(bool available) {
  m_inputHookAvailable.store(available, std::memory_order_release);
}

bool Runtime::shouldCancelObsidianPlacement(void *envPtr, void *playerPtr,
                                            void *worldPtr, void *stackPtr,
                                            void *targetPtr, void *sidePtr) {
  const auto settings = Configuration::get();
  JNIEnv *env = static_cast<JNIEnv *>(envPtr);
  jobject player = static_cast<jobject>(playerPtr);
  jobject world = static_cast<jobject>(worldPtr);
  jobject stack = static_cast<jobject>(stackPtr);
  jobject target = static_cast<jobject>(targetPtr);
  jobject side = static_cast<jobject>(sidePtr);
  PlacementObservation observation;
  observation.masterEnabled = settings.masterEnabled;
  observation.moduleEnabled =
      settings.modules[static_cast<std::size_t>(Module::AntiMisplace)];
  observation.hookAvailable = inputHookAvailable();
  observation.activeMatch = m_context.phase() == GamePhase::Active;
  observation.worldCurrent = world && m_ownBed &&
                             m_ownBed->worldGeneration == m_context.generation();
  observation.ownBedDestroyed = m_ownBedConfirmedDestroyed;
  observation.localTeam = normalizeTeam(g_localTeam);
  observation.ownBed = m_ownBed;
  observation.obsidianHeld = env && isObsidianStack(env, stack);
  BlockPosition targetPosition;
  BlockPosition offset;
  const auto replaceable = isTargetReplaceable(env, world, target);
  if (blockPosition(env, target, targetPosition) &&
      facingOffset(env, side, offset) && replaceable.has_value()) {
    observation.resultingPosition = resultingPlacementPosition(
        targetPosition, offset, *replaceable);
    observation.targetKnown = true;
  }

  const PlacementDecision decision = evaluateObsidianPlacement(observation);
  const Tick now = GetTickCount64();
  const bool reasonChanged = m_lastPlacementReason != decision.reason;
  m_lastPlacementReason = decision.reason;
  if (settings.debug &&
      (reasonChanged || m_lastPlacementLog == 0 ||
       now - m_lastPlacementLog >= 2000)) {
    m_lastPlacementLog = now;
    Logger::info("[Bedwars] placement callback held=%s target=%d,%d,%d "
                 "face=%d,%d,%d replaceable=%d result=%d,%d,%d cancel=%d "
                 "reason=%s propagated=pending",
                 observation.obsidianHeld ? "minecraft:obsidian" : "other",
                 targetPosition.x, targetPosition.y, targetPosition.z,
                 offset.x, offset.y, offset.z,
                 replaceable.value_or(false) ? 1 : 0,
                 observation.resultingPosition.x,
                 observation.resultingPosition.y,
                 observation.resultingPosition.z,
                 decision.cancel ? 1 : 0, decision.reason);
  }
  (void)player;
  return decision.cancel;
}

void Runtime::onPlacementCancellationResult(bool propagated, int errorCode) {
  const auto settings = Configuration::get();
  const Tick now = GetTickCount64();
  m_lastPlacementReason = propagated
                              ? "outside own-bed shell; cancellation propagated"
                              : "cancellation failed open";
  if (settings.debug)
    Logger::info("[Bedwars] placement cancellation propagated=%d jvmtiError=%d",
                 propagated ? 1 : 0, errorCode);
  if (propagated &&
      (m_lastPlacementNotice == 0 || now < m_lastPlacementNotice ||
       now - m_lastPlacementNotice >= 750)) {
    m_lastPlacementNotice = now;
    notify("Anti Misplace", "Obsidian must stay in the own-bed shell", true,
           false, NoticeKind::Warning);
  }
}

void Runtime::shutdown() {
  m_shuttingDown.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_lines.clear();
  }
  if (m_worldReference && lc) {
    if (JNIEnv *env = lc->getEnv())
      env->DeleteGlobalRef(static_cast<jobject>(m_worldReference));
  }
  m_worldReference = nullptr;
  m_inputHookAvailable.store(false, std::memory_order_release);
  reset("shutdown");
}

RenderSnapshot Runtime::snapshot() const {
  std::lock_guard<std::mutex> lock(m_snapshotMutex);
  return m_snapshot;
}

} // namespace OVson::Bedwars
