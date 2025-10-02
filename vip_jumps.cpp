#include <stdio.h>
#include "vip_jumps.h"
#include "schemasystem/schemasystem.h"
#include "tier1/strtools.h"

vip_jumps g_vip_jumps;

IVIPApi* g_pVIPCore;
IUtilsApi* g_pUtils;

IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
IServerGameDLL* g_pServerGameDLL = nullptr;

// GameFrame hook declaration
SH_DECL_HOOK3_void(IServerGameDLL, GameFrame, SH_NOATTRIB, 0, bool, bool, bool);

struct User
{
    int LastButtons;
    int LastFlags;
    int JumpsCount;
    int NumberOfJumps;
	bool Jumping;
	float LastVelocityZ;  // Для отслеживания изменений вертикальной скорости
	bool WasOnGround;     // Для отслеживания смены состояния земли
};

CGameEntitySystem* GameEntitySystem()
{
    return g_pUtils->GetCGameEntitySystem();
};

User UserSettings[64];

PLUGIN_EXPOSE(vip_jumps, g_vip_jumps);
bool vip_jumps::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);

	g_SMAPI->AddListener( this, this );

	// Устанавливаем GameFrame hook
	SH_ADD_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pServerGameDLL, this, &vip_jumps::Hook_GameFrame, true);

	return true;
}

bool vip_jumps::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK_MEMFUNC(IServerGameDLL, GameFrame, g_pServerGameDLL, this, &vip_jumps::Hook_GameFrame, true);
	delete g_pVIPCore;
	delete g_pUtils;
	return true;
}

void OnStartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pGameEntitySystem;
}

void OnPlayerSpawn(int iSlot, int iTeam, bool bIsVIP)
{
	if(bIsVIP)
	{
		UserSettings[iSlot].NumberOfJumps = g_pVIPCore->VIP_GetClientFeatureInt(iSlot, "jumps");
	}

	UserSettings[iSlot].JumpsCount = 0;
	UserSettings[iSlot].Jumping = false;
	UserSettings[iSlot].LastButtons = 0;
	UserSettings[iSlot].LastFlags = 0;
	UserSettings[iSlot].LastVelocityZ = 0.0f;
	UserSettings[iSlot].WasOnGround = true;
}

void vip_jumps::AllPluginsLoaded()
{
	int ret;
	g_pUtils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		char error[64];
		V_strncpy(error, "Failed to lookup utils api. Aborting", 64);
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pVIPCore = (IVIPApi*)g_SMAPI->MetaFactory(VIP_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_pUtils->ErrorLog("[%s] Failed to lookup vip core. Aborting", GetLogTag());
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	g_pUtils->StartupServer(g_PLID, OnStartupServer);
	g_pVIPCore->VIP_OnPlayerSpawn(OnPlayerSpawn);
	g_pVIPCore->VIP_RegisterFeature("jumps", VIP_INT, TOGGLABLE);
}

const char *vip_jumps::GetLicense()
{
	return "Public";
}

const char *vip_jumps::GetVersion()
{
	return "1.0";
}

const char *vip_jumps::GetDate()
{
	return __DATE__;
}

const char *vip_jumps::GetLogTag()
{
	return "[VIP-JUMPS]";
}

const char *vip_jumps::GetAuthor()
{
	return "Pisex x Nipos";
}

const char *vip_jumps::GetDescription()
{
	return "";
}

const char *vip_jumps::GetName()
{
	return "[VIP] JUMPS";
}

const char *vip_jumps::GetURL()
{
	return "https://discord.com/invite/g798xERK5Y";
}

// GameFrame hook - вызывается каждый игровой фрейм
void vip_jumps::Hook_GameFrame(bool simulating, bool bFirstTick, bool bLastTick)
{
    if (!simulating || !g_pVIPCore || !g_pVIPCore->VIP_IsVIPLoaded())
    {
        RETURN_META(MRES_IGNORED);
    }

    for (int i = 0; i < 65; i++)
    {
        CCSPlayerController* pPlayerController = CCSPlayerController::FromSlot(i);
        if(!pPlayerController) continue;
        CCSPlayerPawnBase* pPlayerPawn = pPlayerController->m_hPlayerPawn();
        if (!pPlayerPawn || pPlayerPawn->m_lifeState() != LIFE_ALIVE)
            continue;
        if(!g_pVIPCore->VIP_GetClientFeatureBool(i, "jumps")) continue;

        int flags = pPlayerPawn->m_fFlags();
        bool isOnGround = (flags & (1<<0)) != 0;
        float velocityZ = pPlayerPawn->m_vecAbsVelocity().z;
        int buttons = pPlayerPawn->m_pMovementServices()->m_nButtons().m_pButtonStates()[0];

        // ГИБРИДНАЯ ЛОГИКА: Физическая детекция + кнопочная детекция

        // Детекция физического прыжка (любой способ - колесо, пробел, команда)
        bool didPhysicalJump = false;
        if (velocityZ > UserSettings[i].LastVelocityZ + 200.0f && velocityZ > 100.0f)
        {
            didPhysicalJump = true;
        }

        // Детекция нажатия кнопки прыжка (только пробел)
        bool didPressJump = false;
        if ((UserSettings[i].LastButtons & (1 << 1)) == 0 && (buttons & (1 << 1)) != 0)
        {
            didPressJump = true;
        }

        // Проверка приземления
        if (isOnGround && !UserSettings[i].WasOnGround)  // только что приземлился
        {
            if(UserSettings[i].JumpsCount > 0) {
                char debug_msg[128];
                sprintf(debug_msg, "echo \"[DEBUG] Player %d: Landing, reset count from %d to 0\"", i, UserSettings[i].JumpsCount);
                engine->ServerCommand(debug_msg);
            }
            UserSettings[i].JumpsCount = 0;
        }
        // Первый прыжок с земли (физический) - колесо или пробел
        else if (didPhysicalJump && UserSettings[i].JumpsCount == 0 && UserSettings[i].WasOnGround)
        {
            char debug_msg[128];
            sprintf(debug_msg, "echo \"[DEBUG] Player %d: First jump from ground (vel: %.1f)\"", i, velocityZ);
            engine->ServerCommand(debug_msg);
            // НЕ увеличиваем счетчик и НЕ даем дополнительную скорость
        }
        // Дополнительные прыжки в воздухе (кнопочная детекция) - только пробел
        else if (didPressJump && !isOnGround && UserSettings[i].JumpsCount < UserSettings[i].NumberOfJumps)
        {
            UserSettings[i].JumpsCount++;
            pPlayerPawn->m_vecAbsVelocity().z = 300;
            char debug_msg[128];
            sprintf(debug_msg, "echo \"[DEBUG] Player %d: Air jump #%d (max: %d, button press)\"", i, UserSettings[i].JumpsCount, UserSettings[i].NumberOfJumps);
            engine->ServerCommand(debug_msg);
        }

        UserSettings[i].LastFlags = flags;
        UserSettings[i].LastVelocityZ = velocityZ;
        UserSettings[i].WasOnGround = isOnGround;
        UserSettings[i].LastButtons = buttons;
    }

    RETURN_META(MRES_IGNORED);
}

