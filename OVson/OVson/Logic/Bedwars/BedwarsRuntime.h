#pragma once

#include "BedwarsCore.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace OVson::Bedwars {

namespace Configuration { struct Settings; }

struct RenderSnapshot {
  bool active = false;
  std::vector<std::string> timerLines;
  int timerUrgency = 0;
  std::vector<std::string> upgradeLines;
  std::vector<std::string> resourceLines;
  std::string heightLine;
  int heightUrgency = 0;
  std::string shopLine;
  std::string mapName;
  std::string lifecycleStatus;
  int maximumPlacementY = -1;
  std::uint64_t generation = 0;
};

class Runtime {
public:
  static Runtime &instance();

  void tick();
  void onChatMessage(const std::string &message);
  void onScoreboardLine(const std::string &line);
  void reset(const char *reason);
  void shutdown();
  RenderSnapshot snapshot() const;

private:
  enum class QueuedKind { Chat, Scoreboard };
  enum class NoticeKind { Default, Important, Player, Warning };
  struct QueuedLine {
    QueuedKind kind;
    std::string text;
    Tick received;
  };

  Runtime() = default;
  void drainLines(Tick now);
  void resetState(const char *reason);
  void rebuildSnapshot(Tick now, double playerY);
  void notify(const std::string &title, const std::string &message,
               bool warning, bool playSound,
               NoticeKind kind = NoticeKind::Default,
               const std::vector<MessageSegment> &segments = {});
  void logSummary(Tick now, const Configuration::Settings &settings,
                  std::size_t scannedPlayers, const char *overlayReason);

  mutable std::mutex m_queueMutex;
  BoundedQueue<QueuedLine, 128> m_lines;
  std::atomic<std::uint64_t> m_totalDroppedLines{0};
  mutable std::mutex m_snapshotMutex;
  RenderSnapshot m_snapshot;
  Context m_context;
  ResourceMonitor m_resources;
  ChatMonitor m_chat;
  PlayerMonitor m_players;
  TeamTracker m_teams;
  ResourceSnapshot m_latestResources;
  std::optional<EventCountdown> m_scoreboardEvent;
  Tick m_scoreboardEventObserved = 0;
  Tick m_lastInventoryScan = 0;
  Tick m_lastPlayerScan = 0;
  Tick m_lastItemDump = 0;
  Tick m_lastLifecycleLog = 0;
  Tick m_lastTrapReminder = 0;
  std::uintptr_t m_worldToken = 0;
  void *m_worldReference = nullptr;
  MapHeightResolution m_mapHeight;
  std::string m_mapName;
  std::string m_modeName;
  int m_teamCount = 0;
  double m_playerX = 0.0;
  double m_playerY = 0.0;
  double m_playerZ = 0.0;
  Tick m_lastDiagnosticSummary = 0;
  std::size_t m_lastScannedPlayers = 0;
  std::string m_lastRejectionDetails;
  std::atomic<bool> m_shuttingDown{false};
};

} // namespace OVson::Bedwars
