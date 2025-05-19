#include <stdio.h>
#include "PlayersInfo.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

PlayersInfo g_PlayersInfo;
PLUGIN_EXPOSE(PlayersInfo, g_PlayersInfo);
IVEngineServer2 *engine = nullptr;
CGameEntitySystem *g_pGameEntitySystem = nullptr;
CEntitySystem *g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

ILRApi *g_pLRCore;
IUtilsApi *g_pUtils;
IPlayersApi *g_pPlayers;

int g_iConnection[64];

int g_iCTScore = 0;
int g_iTScore = 0;

CGameEntitySystem *GameEntitySystem()
{
    return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = g_pUtils->GetCEntitySystem();
    gpGlobals = g_pUtils->GetCGlobalVars();

    for (int i = 0; i < 64; i++)
    {
        g_iConnection[i] = std::time(0);
    }
}

void GetTeamScore()
{
    CTeam *pTeam = nullptr;
    std::vector<CEntityInstance *> teams = UTIL_FindEntityByClassnameAll("cs_team_manager");
    for (int i = 0; i < teams.size(); i++)
    {
        pTeam = (CTeam *)teams[i];
        if (pTeam->m_iTeamNum() == 3)
        {
            g_iCTScore = pTeam->m_iScore();
        }
        else if (pTeam->m_iTeamNum() == 2)
        {
            g_iTScore = pTeam->m_iScore();
        }
    }
}

bool CheckPrime(uint64 SteamID)
{
    CSteamID steamID(SteamID);
    return SteamGameServer()->UserHasLicenseForApp(steamID, 624820) == 0 ||
           SteamGameServer()->UserHasLicenseForApp(steamID, 54029) == 0;
}

json GetServerInfo()
{
    char szBuffer[64];
    g_SMAPI->Format(szBuffer, sizeof(szBuffer), "%s", gpGlobals->mapname);
    json jdata;
    jdata["time"] = std::time(0);
    jdata["current_map"] = szBuffer;
    GetTeamScore();
    jdata["score_ct"] = g_iCTScore;
    jdata["score_t"] = g_iTScore;
    json jPlayers;
    for (int i = 0; i < 64; i++)
    {
        if (g_iConnection[i] == 0)
            continue;
        CCSPlayerController *pPlayerController = CCSPlayerController::FromSlot(i);
        if (!pPlayerController)
            continue;
        CCSPlayerPawn *pPlayerPawn = pPlayerController->m_hPlayerPawn();
        if (!pPlayerPawn)
            continue;
        json jPlayer;
        const char *szName = g_pPlayers->GetPlayerName(i);
        jPlayer["userid"] = i;
        jPlayer["name"] = szName[0] == '\0' ? "Unknown" : szName;
        jPlayer["team"] = pPlayerController->GetTeam();
        char szSteamID[64];
        g_SMAPI->Format(szSteamID, sizeof(szSteamID), "%lld", g_pPlayers->GetSteamID64(i));
        jPlayer["steamid"] = szSteamID;
        CCSPlayerController_ActionTrackingServices *m_ATS = pPlayerController->m_pActionTrackingServices();
        if (m_ATS)
        {
            jPlayer["kills"] = m_ATS->m_matchStats().m_iKills();
            jPlayer["death"] = m_ATS->m_matchStats().m_iDeaths();
            jPlayer["headshots"] = m_ATS->m_matchStats().m_iHeadShotKills();
        }
        jPlayer["ping"] = pPlayerController->m_iPing();
        jPlayer["playtime"] = std::time(0) - g_iConnection[i];
        jPlayer["prime"] = CheckPrime(engine->GetClientXUID(i));
        if (g_pLRCore)
            jPlayer["rank"] = g_pLRCore->GetClientInfo(i, ST_RANK);

        jPlayers.push_back(jPlayer);
    }
    jdata["players"] = jPlayers;

    return jdata;
}

CON_COMMAND_F(mm_getinfo, "", FCVAR_GAMEDLL)
{
    json jdata = GetServerInfo();
    std::string dump = jdata.dump();
    META_CONPRINT("%s\n", dump.c_str());
}

bool PlayersInfo::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);

    g_SMAPI->AddListener(this, this);

    ConVar_Register(FCVAR_RELEASE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

    return true;
}

bool PlayersInfo::Unload(char *error, size_t maxlen)
{
    ConVar_Unregister();

    return true;
}

void OnPlayerConnect(const char *szName, IGameEvent *pEvent, bool bDontBroadcast)
{
    int iSlot = pEvent->GetInt("userid");
    g_iConnection[iSlot] = std::time(0);
}

void OnPlayerDisconnect(const char *szName, IGameEvent *pEvent, bool bDontBroadcast)
{
    int iSlot = pEvent->GetInt("userid");
    g_iConnection[iSlot] = 0;
}

void PlayersInfo::AllPluginsLoaded()
{
    char error[64];
    int ret;
    g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        g_pUtils->ErrorLog("[%s] Missing Players system plugin", GetLogTag());

        std::string sBuffer = "meta unload " + std::to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    g_pLRCore = (ILRApi *)g_SMAPI->MetaFactory(LR_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
        g_pLRCore = nullptr;

    g_pUtils->StartupServer(g_PLID, StartupServer);
    g_pUtils->HookEvent(g_PLID, "player_connect", OnPlayerConnect);
    g_pUtils->HookEvent(g_PLID, "player_disconnect", OnPlayerDisconnect);
}

///////////////////////////////////////
const char *PlayersInfo::GetLicense()
{
    return "GPL";
}

const char *PlayersInfo::GetVersion()
{
    return "1.0.1";
}

const char *PlayersInfo::GetDate()
{
    return __DATE__;
}

const char *PlayersInfo::GetLogTag()
{
    return "PlayersInfo";
}

const char *PlayersInfo::GetAuthor()
{
    return "Pisex";
}

const char *PlayersInfo::GetDescription()
{
    return "PlayersInfo";
}

const char *PlayersInfo::GetName()
{
    return "PlayersInfo";
}

const char *PlayersInfo::GetURL()
{
    return "https://discord.gg/g798xERK5Y";
}
