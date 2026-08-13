#define WIN32_LEAN_AND_MEAN
#include "StatsTracker.internal.h"

#include "../Chat/ChatSDK.h"
#include "../Config/Config.h"
#include "../Config/StatColors.h"
#include "../Utils/Anticheat/Anticheat.h"
#include "../Utils/Logger.h"

#include <Windows.h>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <sstream>
#include <iomanip>
#include "../Render/RenderHook.h"

namespace OVson {

void sendTeamStatsReport(bool force, std::string channelOverride) {
  if (Config::isGlobalDebugEnabled() || force) {
    Logger::info("[OVson DEBUG] sendTeamStatsReport triggered. force=%d g_teamReportSent=%d", force, g_teamReportSent);
  }

  if (!force) {
    if (g_teamReportSent || !Config::isTeamReportEnabled())
      return;
    if (g_localTeam.empty()) {
      if (Config::isGlobalDebugEnabled()) {
        Logger::info("[OVson DEBUG] sendTeamStatsReport aborted: g_localTeam is empty! (Is the scoreboard team mapped?)");
      }
      return;
    }
  }

  std::unordered_map<std::string, std::vector<Hypixel::PlayerStats>> teamGroups;
  int unknownCount = 0;
  {
    std::lock_guard<std::mutex> lock(g_statsMutex);
    for (const auto &pair : g_playerStatsMap) {
      const auto &st = pair.second;
      std::string team = "Unknown";
      auto it = g_playerTeamColor.find(pair.first);
      if (it != g_playerTeamColor.end() && !it->second.empty()) {
        team = it->second;
      }
      if (team != "Unknown") {
        teamGroups[team].push_back(st);
      } else {
        unknownCount++;
      }
    }
  }

  if (Config::isGlobalDebugEnabled() || force) {
    Logger::info("[OVson DEBUG] Found %d teams to report. Unknown players: %d", (int)teamGroups.size(), unknownCount);
  }

  if (teamGroups.empty())
    return;

  std::string channel = channelOverride.empty() ? Config::getTeamReportChannel() : channelOverride;

  if (!force) {
    g_teamReportSent = true;
    Logger::info("Automated All-Team Stats Report triggered to %s", channel.c_str());
  }

  std::thread([teamGroups, channel]() {
    for (const auto &tg : teamGroups) {
      const std::string &teamName = tg.first;
      const auto &players = tg.second;

      int totalFk = 0, totalFd = 0, totalWins = 0;
      int count = (int)players.size();
      for (const auto &p : players) {
        totalFk += p.bedwarsFinalKills;
        totalFd += p.bedwarsFinalDeaths;
        totalWins += p.bedwarsWins;
      }

      float avgFkdr =
          (totalFd > 0) ? (float)totalFk / (float)totalFd : (float)totalFk;
      float avgWins = (count > 0) ? (float)totalWins / (float)count : 0.0f;
      float avgFk = (count > 0) ? (float)totalFk / (float)count : 0.0f;

      std::ostringstream oss;
      oss << channel << " [" << teamName << "] "
          << "FKDR: " << std::fixed << std::setprecision(2) << avgFkdr
          << " | Wins: " << (int)avgWins
          << " | FK: " << (int)avgFk;

      std::string msg = oss.str();
      RenderHook::enqueueTask([msg]() { ChatSDK::sendClientChat(msg); });
      Sleep(600);
    }
  }).detach();
}

void pruneStatsCache() {
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  ULONGLONG now = GetTickCount64();

  for (auto it = g_persistentStatsCache.begin();
       it != g_persistentStatsCache.end();) {
    if ((now - it->second.timestamp) > STATS_CACHE_EXPIRY_MS) {
      it = g_persistentStatsCache.erase(it);
    } else {
      ++it;
    }
  }

  while (g_persistentStatsCache.size() > MAX_STATS_CACHE_SIZE) {
    auto oldest = g_persistentStatsCache.begin();
    for (auto it = g_persistentStatsCache.begin();
         it != g_persistentStatsCache.end(); ++it) {
      if (it->second.timestamp < oldest->second.timestamp) {
        oldest = it;
      }
    }
    g_persistentStatsCache.erase(oldest);
  }
}

void resetGameCache() {
  {
    std::lock_guard<std::mutex> lock(g_statsMutex);
    g_playerStatsMap.clear();
    g_playerTeamColor.clear();
  }
  {
    std::lock_guard<std::mutex> lockR(g_stableRankMutex);
    g_stableRankMap.clear();
  }
  g_processedPlayers.clear();
  g_onlinePlayers.clear();
  g_teamReportSent = false;
  g_playerTeamColor.clear();
  g_playerFetchRetries.clear();
  g_player500Retries.clear();
  {
    std::lock_guard<std::mutex> lock(g_alertedMutex);
    g_alertedPlayers.clear();
  }
  {
    std::lock_guard<std::mutex> qlock(g_queueMutex);
    g_queuedPlayers.clear();
  }
  {
    std::lock_guard<std::mutex> aLock(g_activeFetchesMutex);
    g_activeFetches.clear();
  }
  {
    std::lock_guard<std::mutex> lockE(g_eliminatedMutex);
    g_eliminatedPlayers.clear();
  }
  Anticheat::clearAllPlayers();

  g_lastResetTick = GetTickCount64();
  Logger::log(Config::DebugCategory::GameDetection,
              "Game cache reset performed");
}

void cleanupStaleStats() {
  std::vector<std::string> toPrune;
  std::vector<std::string> toResetNicked;

  {
    std::lock_guard<std::mutex> statsLock(g_statsMutex);
    for (auto it = g_playerStatsMap.begin(); it != g_playerStatsMap.end();
         ++it) {
      bool found = false;
      for (const auto &p : g_onlinePlayers) {
        if (p == it->first) {
          found = true;
          break;
        }
      }
      bool isNicked = it->second.isNicked;

      if (g_inHypixelGame) {
        if (isNicked && !found) {
            bool isRealName = false;
            {
                std::lock_guard<std::mutex> lockNick(OVson::g_nickMapMutex);
                for (const auto &np : OVson::g_nickToRealMap) {
                    if (np.second == it->first) {
                        isRealName = true;
                        break;
                    }
                }
            }
            if (!isRealName)
                toPrune.push_back(it->first);
        }
      } else {
        if (!found)
          toPrune.push_back(it->first);
      }
    }

    for (const auto &name : toPrune) {
      g_playerStatsMap.erase(name);
    }
  }

  if (!toResetNicked.empty()) {
    std::lock_guard<std::mutex> qlock(g_queueMutex);
    std::lock_guard<std::mutex> clock(g_cacheMutex);
    for (const auto &name : toResetNicked) {
      g_processedPlayers.erase(name);
      g_persistentStatsCache.erase(name);
      g_playerFetchRetries[name] = 0;
      g_queuedPlayers.erase(name);
    }
  }

  if (!toPrune.empty()) {
    std::lock_guard<std::mutex> qlock(g_queueMutex);
    for (const auto &name : toPrune) {
      g_processedPlayers.erase(name);
      g_playerFetchRetries.erase(name);
      g_queuedPlayers.erase(name);
    }
  }

  Logger::log(Config::DebugCategory::General, "Stale stats cleanup performed");
}

} // namespace OVson
