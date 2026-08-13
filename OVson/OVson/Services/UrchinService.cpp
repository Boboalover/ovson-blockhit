#include "UrchinService.h"
#include "../Chat/ChatSDK.h"
#include "../Config/Config.h"
#include "../Logic/StatsTracker.h"
#include "../Net/Http.h"
#include "../Render/NotificationManager.h"
#include "../Utils/Logger.h"
#include "../Utils/SafeGuard.h"
#include "../Utils/ThreadTracker.h"
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "../Services/Hypixel.h"

namespace Urchin {
struct CachedTags {
  PlayerTags data;
  std::chrono::steady_clock::time_point timestamp;
};

static std::unordered_map<std::string, CachedTags> g_cache;
static std::mutex g_cacheMutex;
static const size_t MAX_CACHE_SIZE = 200;
static const int CACHE_EXPIRY_SECONDS = 300;

static bool findJsonArray(const std::string &json, const char *key,
                          size_t &start, size_t &end) {
  std::string pat = std::string("\"") + key + "\"";
  size_t k = json.find(pat);
  if (k == std::string::npos)
    return false;
  size_t arrStart = json.find('[', k);
  if (arrStart == std::string::npos)
    return false;
  int depth = 1;
  size_t i = arrStart + 1;
  while (i < json.size() && depth > 0) {
    if (json[i] == '[')
      depth++;
    else if (json[i] == ']')
      depth--;
    i++;
  }
  if (depth != 0)
    return false;
  start = arrStart;
  end = i;
  return true;
}

static bool findJsonString(const std::string &json, const char *key,
                           std::string &out) {
  std::string pat = std::string("\"") + key + "\"";
  size_t k = json.find(pat);
  if (k == std::string::npos)
    return false;
  size_t q1 = json.find('"', json.find(':', k));
  if (q1 == std::string::npos)
    return false;
  size_t q2 = json.find('"', q1 + 1);
  if (q2 == std::string::npos)
    return false;
  out = json.substr(q1 + 1, q2 - (q1 + 1));
  return true;
}

static std::vector<Tag> parseTags(const std::string &arrJson) {
  std::vector<Tag> tags;
  size_t pos = 0;
  while ((pos = arrJson.find('{', pos)) != std::string::npos) {
    size_t objEnd = arrJson.find('}', pos);
    if (objEnd == std::string::npos)
      break;
    std::string obj = arrJson.substr(pos, objEnd - pos + 1);
    Tag tag;
    findJsonString(obj, "tag_type", tag.type);
    findJsonString(obj, "reason", tag.reason);
    if (!tag.type.empty()) {
      tags.push_back(tag);
    }
    pos = objEnd + 1;
  }
  return tags;
}

static void pruneCacheLocked() {
  auto now = std::chrono::steady_clock::now();

  for (auto it = g_cache.begin(); it != g_cache.end();) {
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
                   now - it->second.timestamp)
                   .count();
    if (age > CACHE_EXPIRY_SECONDS) {
      it = g_cache.erase(it);
    } else {
      ++it;
    }
  }

  while (g_cache.size() > MAX_CACHE_SIZE) {
    auto oldest = g_cache.begin();
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
      if (it->second.timestamp < oldest->second.timestamp) {
        oldest = it;
      }
    }
    g_cache.erase(oldest);
  }
}

static std::string toLower(const std::string &str) {
  std::string s = str;
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

static std::unordered_map<std::string, std::chrono::steady_clock::time_point>
    g_pendingFetches;
static std::mutex g_pendingMutex;

std::optional<PlayerTags> getPlayerTags(const std::string &username,
                                        bool wait) {
  if (username.empty()) return std::nullopt;
  if (!Config::isTagsEnabled()) return std::nullopt;
  std::string activeSvc = Config::getActiveTagService();
  if (activeSvc != "Urchin" && activeSvc != "Both") return std::nullopt;

  std::string lowerUser = toLower(username);
  auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    auto it = g_cache.find(lowerUser);
    if (it != g_cache.end()) {
      auto age = std::chrono::duration_cast<std::chrono::seconds>(
                     now - it->second.timestamp)
                     .count();
      if (age < CACHE_EXPIRY_SECONDS) {
        Logger::tagDebug("[Urchin] Cache hit for '%s' (age %llds, tags: %zu)", username.c_str(), (long long)age, it->second.data.tags.size());
        return it->second.data;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    auto it = g_pendingFetches.find(lowerUser);
    if (it != g_pendingFetches.end()) {
      auto age = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
      if (age < 10) {
        if (!wait) {
          Logger::tagDebug("[Urchin] Async fetch skipped (already pending) for '%s'", username.c_str());
          return std::nullopt;
        }
      }
    }
    g_pendingFetches[lowerUser] = now;
  }

  if (!wait && OVson::isInPreGameLobby()) {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pendingFetches.erase(lowerUser);
    Logger::tagDebug("[Urchin] Async fetch skipped for '%s': In pre-game lobby", username.c_str());
    return std::nullopt;
  }

  if (wait) {
    std::string apiKey = Config::getUrchinApiKey();
    std::string url = "https://api.urchin.gg/v3/player/tags?player=" + username;
    if (!apiKey.empty()) {
      url += "&key=" + apiKey;
    }

    std::string body;
    Logger::tagDebug("[Urchin] Sync GET request for '%s' (API key set: %s)", username.c_str(), apiKey.empty() ? "NO" : "YES");

    bool ok = false;
    int maxRetries = 3;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
      ok = Http::get(url, body, "X-API-Key", apiKey);
      Logger::tagDebug("[Urchin] Http::get attempt %d ok=%d, bodyLen=%zu", attempt + 1, ok ? 1 : 0, body.size());

      if (body.find("Rate limit exceeded") != std::string::npos ||
          body.find("rate limit") != std::string::npos ||
          body.find("429") != std::string::npos) {
        Logger::tagDebug("[Urchin] Rate limit hit for '%s' (attempt %d): %s", username.c_str(), attempt + 1, body.substr(0, 100).c_str());
        if (attempt < maxRetries - 1) {
          std::this_thread::sleep_for(std::chrono::seconds(10));
          continue;
        }
      }
      break;
    }

    PlayerTags result;
    if (ok && !body.empty() && body.find("\"error\"") == std::string::npos) {
      findJsonString(body, "uuid", result.uuid);
      size_t arrStart, arrEnd;
      if (findJsonArray(body, "tags", arrStart, arrEnd)) {
        std::string arrJson = body.substr(arrStart, arrEnd - arrStart);
        result.tags = parseTags(arrJson);
      }
      {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        pruneCacheLocked();
        g_cache[lowerUser] = {result, std::chrono::steady_clock::now()};
      }
      {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingFetches.erase(lowerUser);
      }
      Logger::tagDebug("[Urchin] Sync Success for '%s' (uuid: %s, tags: %zu)", username.c_str(), result.uuid.c_str(), result.tags.size());
      for (const auto &t : result.tags) {
        Logger::tagDebug("  -> Tag: type='%s', reason='%s'", t.type.c_str(), t.reason.c_str());
      }
      return result;
    } else {
      {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        pruneCacheLocked();
        g_cache[lowerUser] = {result, std::chrono::steady_clock::now()};
      }
      {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingFetches.erase(lowerUser);
      }
      Logger::tagDebug("[Urchin] Sync Failed for '%s', ok=%d, bodySnippet: %s", username.c_str(), ok ? 1 : 0, body.substr(0, 150).c_str());
    }
    return std::nullopt;
  }

  ThreadTracker::increment();
  if (ThreadTracker::g_activeThreads.load() > 12) {
    ThreadTracker::decrement();
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pendingFetches.erase(lowerUser);
    Logger::tagDebug("[Urchin] Async thread skipped for '%s': Too many active threads (%d)", username.c_str(), ThreadTracker::g_activeThreads.load());
    return std::nullopt;
  }

  std::thread([username, lowerUser]() {
    SafeGuard::installSehTranslator();
    SafeGuard::run("Urchin::worker", [&]() {
      if (ThreadTracker::shouldStop()) return;
      std::string apiKey = Config::getUrchinApiKey();
      
      std::string url = "https://api.urchin.gg/v3/player/tags?player=" + username;
      if (!apiKey.empty()) {
        url += "&key=" + apiKey;
      }

      std::string body;
      Logger::tagDebug("[Urchin] Async worker started for '%s' (API key set: %s)", username.c_str(), apiKey.empty() ? "NO" : "YES");

      bool ok = false;
      int maxRetries = 3;
      for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (ThreadTracker::shouldStop()) return;
        ok = Http::get(url, body, "X-API-Key", apiKey);
        Logger::tagDebug("[Urchin] Async Http::get attempt %d ok=%d, bodyLen=%zu", attempt + 1, ok ? 1 : 0, body.size());

        if (body.find("Rate limit exceeded") != std::string::npos ||
            body.find("rate limit") != std::string::npos ||
            body.find("429") != std::string::npos) {
          Logger::tagDebug("[Urchin] Async Rate limit hit for '%s' (attempt %d): %s", username.c_str(), attempt + 1, body.substr(0, 100).c_str());
          if (attempt < maxRetries - 1) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            continue;
          }
        }
        break;
      }

      PlayerTags result;
      bool success = false;
      std::string failReason = "Unknown";

      if (ok && !body.empty() && body.find("\"error\"") == std::string::npos) {
        findJsonString(body, "uuid", result.uuid);
        size_t arrStart, arrEnd;
        if (findJsonArray(body, "tags", arrStart, arrEnd)) {
          std::string arrJson = body.substr(arrStart, arrEnd - arrStart);
          result.tags = parseTags(arrJson);
        }
        success = true;
      } else {
        if (!ok)
          failReason = "HTTP request failed";
        else if (body.empty())
          failReason = "Empty response";
        else if (body.find("\"error\"") != std::string::npos)
          failReason = "API error response";
        else
          failReason = "JSON parse failed";
      }

      {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        pruneCacheLocked();
        g_cache[lowerUser] = {result, std::chrono::steady_clock::now()};
      }

      {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        g_pendingFetches.erase(lowerUser);
      }

      if (success) {
        Logger::tagDebug("[Urchin] Async Success for '%s' (uuid: %s, tags: %zu)", username.c_str(), result.uuid.c_str(), result.tags.size());
        for (const auto &t : result.tags) {
          Logger::tagDebug("  -> Tag: type='%s', reason='%s'", t.type.c_str(), t.reason.c_str());
        }
      } else {
        Logger::tagDebug("[Urchin] Async Failed for '%s' - Reason: %s, bodySnippet: %s", username.c_str(), failReason.c_str(), body.substr(0, 150).c_str());
      }
    });
    ThreadTracker::decrement();
  }).detach();

  return std::nullopt;
}

void clearCache() {
  std::lock_guard<std::mutex> lock(g_cacheMutex);
  g_cache.clear();
}

bool hasAnyTags(const std::string &username) {
  auto result = getPlayerTags(username);
  return result.has_value() && !result->tags.empty();
}

} // namespace Urchin
