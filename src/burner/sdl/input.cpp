#include "burner.h"

//static int GameInpConfig(int nPlayer, int nPcDev, int nAnalog);
static void GameInpConfigOne(int nPlayer, int nPcDev, int nAnalog, struct GameInp* pgi, char* szi);
INT32 Mapcoins(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nDevice);

#define KEY(x) { pgi->nInput = GIT_SWITCH; pgi->Input.Switch.nCode = (UINT16)(x); }

static int GameInpConfig(int nPlayer, int nPcDev, int nAnalog) {
	struct GameInp* pgi = NULL;
	unsigned int i;
	for (i = 0, pgi = GameInp; i < nGameInpCount; i++, pgi++) {
		struct BurnInputInfo bii;
		bii.szInfo = NULL;
		BurnDrvGetInputInfo(&bii, i);
		if (bii.pVal == NULL) {
			continue;
		}
		if (bii.szInfo == NULL) {
			bii.szInfo = "";
		}
		GameInpConfigOne(nPlayer, nPcDev, nAnalog, pgi, bii.szInfo);
	}
	return 0;
}

static void GameInpConfigOne(int nPlayer, int nPcDev, int nAnalog, struct GameInp* pgi, char* szi) {
	if (nPcDev == 0) {
		GamcPlayer(pgi, szi, nPlayer, -1);
		GamcAnalogKey(pgi, szi, nPlayer, nAnalog);
		GamcMisc(pgi, szi, nPlayer);
		Mapcoins(pgi, szi, nPlayer, nPcDev);
	}
}

INT32 Mapcoins(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nDevice)
{
	INT32 nJoyBase = 0;
	if (nDevice == 0) return 0;
	nDevice--;
	nJoyBase = 0x4000;
	nJoyBase |= nDevice << 8;

	if (strncmp(szi, "p1 fire ", 7) == 0)
	{
		char* szb = szi + 7;
		INT32 nButton = strtol(szb, NULL, 0);
		if (nButton >= 1)
		{
			nButton--;
		}
	}
	return 0;
}

INT32 display_set_controls()
{
	struct GameInp* pgi = NULL;
	unsigned int i;
	for (i = 0, pgi = GameInp; i < nGameInpCount; i++, pgi++) {
		struct BurnInputInfo bii;
		bii.szInfo = NULL;
		BurnDrvGetInputInfo(&bii, i);
		if (bii.pVal == NULL) {
			continue;
		}
		if (bii.szInfo == NULL) {
			bii.szInfo = "";
		}
		printf("%s %s\n", bii.szInfo, InputCodeDesc(pgi->Input.Switch.nCode));
	}
	return 0;
}

INT32 Init_Joysticks(int p_one_use_joystick)
{

	GameInpConfig(0, 0, 1);
	display_set_controls();
	return 0;
}
