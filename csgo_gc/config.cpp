#include "stdafx.h"
#include "config.h"
#include "keyvalue.h"
#include "random.h"

#include <cstdlib>
#include <sstream>

constexpr const char *ConfigFilePath = "csgo_gc/config.txt";

static void LoadEnvOverrides(GCConfig &cfg)
{
    // Try environment variables first
    const char *envHost = std::getenv("GC_WS_HOST");
    if (!envHost) envHost = std::getenv("GC_HOST");
    const char *envPort = std::getenv("GC_WS_PORT");
    if (!envPort) envPort = std::getenv("GC_PORT");
    const char *envToken = std::getenv("GC_SERVER_TOKEN");
    if (!envToken) envToken = std::getenv("SERVER_TOKEN");

    if (envHost && envHost[0] != '\0')
    {
        cfg.m_remoteGcHost = envHost;
        cfg.m_remoteGcEnabled = true;
    }
    if (envPort && envPort[0] != '\0')
    {
        cfg.m_remoteGcPort = static_cast<uint16_t>(std::atoi(envPort));
    }
    if (envToken && envToken[0] != '\0')
    {
        cfg.m_remoteGcToken = envToken;
    }

    // Check for csgo_gc.env, csgo_gc/csgo_gc.env, .env
    const char *envFiles[] = { "csgo_gc.env", "csgo_gc/csgo_gc.env", ".env" };
    for (const char *path : envFiles)
    {
        std::string content = LoadFile(path);
        if (content.empty())
        {
            continue;
        }

        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line))
        {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos || line[start] == '#' || (line[start] == '/' && line[start + 1] == '/'))
            {
                continue;
            }

            size_t eqPos = line.find('=', start);
            if (eqPos == std::string::npos)
            {
                continue;
            }

            std::string key = line.substr(start, eqPos - start);
            key.erase(key.find_last_not_of(" \t\r\n") + 1);

            size_t valStart = line.find_first_not_of(" \t\r\n", eqPos + 1);
            std::string val;
            if (valStart != std::string::npos)
            {
                val = line.substr(valStart);
                val.erase(val.find_last_not_of(" \t\r\n") + 1);
            }

            if (key == "GC_WS_HOST" || key == "GC_HOST")
            {
                cfg.m_remoteGcHost = val;
                cfg.m_remoteGcEnabled = true;
            }
            else if (key == "GC_WS_PORT" || key == "GC_PORT")
            {
                cfg.m_remoteGcPort = static_cast<uint16_t>(std::atoi(val.c_str()));
            }
            else if (key == "GC_SERVER_TOKEN" || key == "SERVER_TOKEN")
            {
                cfg.m_remoteGcToken = val;
            }
            else if (key == "GC_ENABLED")
            {
                cfg.m_remoteGcEnabled = (val == "1" || val == "true");
            }
        }
        break;
    }
}

const GCConfig &GetConfig()
{
    static GCConfig instance;

    // Publish diagnostics only after construction has finished. Platform::Print
    // calls GetConfig(), so mark them reported before entering the logger.
    static std::atomic<bool> diagnosticsReported{ false };
    bool expected = false;
    if (!diagnosticsReported.load(std::memory_order_relaxed)
        && diagnosticsReported.compare_exchange_strong(expected, true, std::memory_order_relaxed))
    {
        instance.PrintDiagnostics();
    }
    return instance;
}

GCConfig::GCConfig()
{
    KeyValue config{ "config" };

    if (!config.ParseFromFile(ConfigFilePath))
    {
        LoadEnvOverrides(*this);
        return;
    }

    const KeyValue *remoteGc = config.GetSubkey("remote_gc");
    if (remoteGc)
    {
        m_remoteGcEnabled = remoteGc->GetNumber("enabled", m_remoteGcEnabled);
        m_remoteGcHost = remoteGc->GetString("host", m_remoteGcHost);
        m_remoteGcPort = remoteGc->GetNumber("port", m_remoteGcPort);
        m_remoteGcToken = remoteGc->GetString("token", m_remoteGcToken);
    }

    m_logOutput = config.GetNumber("log_output", m_logOutput);

    m_appIdOverride = config.GetNumber("appid_override", m_appIdOverride);
    m_showCsgoGCServersOnly = config.GetNumber("show_csgo_gc_servers_only", m_showCsgoGCServersOnly);
    m_overwatchEnabled = config.GetNumber("overwatch_enabled", m_overwatchEnabled);
    m_allowUnsignedPanorama = config.GetNumber("allow_unsigned_panorama", m_allowUnsignedPanorama);

    const KeyValue *rcon = config.GetSubkey("rcon");
    if (rcon)
    {
        m_rconEnabled = rcon->GetNumber("enabled", m_rconEnabled);
        m_rconBindAddress = rcon->GetString("bind_address", m_rconBindAddress);
        m_rconPort = rcon->GetNumber("port", m_rconPort);
        m_rconPassword = rcon->GetString("password", m_rconPassword);
    }

    const KeyValue *ranks = config.GetSubkey("ranks");
    if (ranks)
    {
        m_competitiveRank = ranks->GetNumber("competitive_rank", m_competitiveRank);
        m_competitiveWins = ranks->GetNumber("competitive_wins", m_competitiveWins);

        m_wingmanRank = ranks->GetNumber("wingman_rank", m_wingmanRank);
        m_wingmanWins = ranks->GetNumber("wingman_wins", m_wingmanWins);

        m_dangerZoneRank = ranks->GetNumber("dangerzone_rank", m_dangerZoneRank);
        m_dangerZoneWins = ranks->GetNumber("dangerzone_wins", m_dangerZoneWins);
    }

    m_itemSchemaPath = config.GetString("item_schema_path", m_itemSchemaPath);

    m_destroyUsedItems = config.GetNumber("destroy_used_items", m_destroyUsedItems);
    m_primeStatus = config.GetNumber("prime_status", m_primeStatus);

    std::string_view musicKitStatTrak =
        config.GetString("music_kit_stattrak", std::string_view{});
    if (musicKitStatTrak == "always")
    {
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::Always;
    }
    else if (musicKitStatTrak == "competitive" || musicKitStatTrak.empty())
    {
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::CompetitiveRuleset;
    }
    else
    {
        m_unknownMusicKitStatTrak = musicKitStatTrak;
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::CompetitiveRuleset;
    }

    const KeyValue *rarityWeights = config.GetSubkey("rarity_weights");
    if (rarityWeights)
    {
        m_rarityWeights.clear();
        m_rarityWeights.reserve(rarityWeights->SubkeyCount());

        for (const KeyValue &subkey : *rarityWeights)
        {
            RarityWeight weight;
            weight.rarity = FromString<uint32_t>(subkey.Name());
            weight.weight = FromString<float>(subkey.String());
            m_rarityWeights.push_back(weight);
        }
    }

    m_vacBanned = config.GetNumber("vac_banned", m_vacBanned);
    m_commendedFriendly = config.GetNumber("cmd_friendly", m_commendedFriendly);
    m_commendedTeaching = config.GetNumber("cmd_teaching", m_commendedTeaching);
    m_commendedLeader = config.GetNumber("cmd_leader", m_commendedLeader);
    m_level = config.GetNumber("player_level", m_level);
    m_xp = config.GetNumber("player_cur_xp", m_xp);

    // .env and environment variables take precedence
    LoadEnvOverrides(*this);
}

void GCConfig::PrintDiagnostics() const
{
    if (!m_unknownMusicKitStatTrak.empty())
    {
        Platform::Print("config: unknown music_kit_stattrak value '%s', using competitive\n",
            m_unknownMusicKitStatTrak.c_str());
    }
}

float GCConfig::GetRarityWeight(uint32_t rarity) const
{
    for (const RarityWeight &weight : m_rarityWeights)
    {
        if (weight.rarity == rarity)
        {
            return weight.weight;
        }
    }

    return 0;
}
