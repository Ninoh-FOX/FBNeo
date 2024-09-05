#include "burner.h"

// The automatic save
int QuickState(int bSave)
{
	static TCHAR szName[MAX_PATH] = _T("");
	int nRet;

	_stprintf(szName, _T("./config/games/quick_%s.fs"), BurnDrvGetText(DRV_NAME));


	if (bSave == 0)
	{
		nRet = BurnStateLoad(szName, 1, NULL);		// Load ram
	}
	else
	{
		nRet = BurnStateSave(szName, 1);				// Save ram
	}

	return nRet;
}
