// Burner Game Control
#include "burner.h"

static char szPlay[4][4]={"p1 ", "p2 ", "p3 ", "p4 "};

#define KEY(x) { pgi->nInput = GIT_SWITCH; pgi->Input.Switch.nCode = (UINT16)(x); }
#define MACRO(x) { pgi->Macro.nMode = 1; pgi->Macro.Switch.nCode = (UINT16)(x); }

// Configure the misc game controls
INT32 GamcMisc(struct GameInp* pgi, char* szi, INT32 nPlayer)
{
	switch (nPlayer) {
		case 0:
			// Set general controls according to Player 1 settings
			if (strcmp(szi, "reset") == 0) {
				KEY(FBK_BACK);
				return 0;
			}
			if (strcmp(szi, "diag") == 0) {
				KEY(FBK_TAB);
				return 0;
			}

			// Player 1 controls
			if (strcmp(szi, "p1 start") == 0) {
				KEY(FBK_RETURN);
				return 0;
			}
			if (strcmp(szi, "p1 select" ) == 0) {
				KEY(FBK_RCONTROL);
				return 0;
			}
			if (strcmp(szi, "p1 coin" ) == 0) {
				KEY(FBK_RCONTROL);
				return 0;
			}


			break;
	}

#if defined(BUILD_SDL2) && !defined(SDL_WINDOWS)
	return 1;
#else
	return 0;
#endif
}

static void SetSliderKey(struct GameInp* pgi, INT32 k0, INT32 k1, INT32 nSlide)
{
}

INT32 GamcAnalogKey(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nSlide)
{
	return 1;
}

INT32 GamcAnalogJoy(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nJoy, INT32 nSlide)
{
	return 1;
}

// Set a Game Input to use Device 'nDevice' if it belongs to 'nPlayer'
// -2 = nothing  -1 == keyboard, 0 == joystick 1, 1 == joystick 2 etc...
INT32 GamcPlayer(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nDevice)
{
	char* szSearch = szPlay[nPlayer & 3];

	if (_strnicmp(szSearch, szi, 3) != 0) {	// Not our player
		return 1;
	}
	szi += 3;

	if (nDevice <= -2) {
		INT32 bOurs = 0;
		if (strcmp(szi, "up") == 0 || strcmp(szi, "y-axis-neg") == 0) {
			bOurs = 1;
		}
		if (strcmp(szi, "down") == 0 || strcmp(szi, "y-axis-pos") == 0) {
			bOurs = 1;
		}
		if (strcmp(szi, "left") == 0 || strcmp(szi, "x-axis-neg") == 0) {
			bOurs = 1;
		}
		if (strcmp(szi, "right") == 0 || strcmp(szi, "x-axis-pos") == 0) {
			bOurs = 1;
		}
		if (strncmp(szi, "fire ", 5) == 0) {
			bOurs = 1;
		}

		if (!bOurs)	{
			return 1;
		}

		pgi->nInput = GIT_CONSTANT;			// Constant zero
		pgi->Input.Constant.nConst = 0;		//

		return 0;
	}

	// Now check the rest of it
	if (nDevice == -1) {
		// Keyboard
		if (strcmp(szi, "up") == 0 || strcmp(szi, "y-axis-neg") == 0) {
			KEY(FBK_UPARROW);
		}
		if (strcmp(szi, "down") == 0 || strcmp(szi, "y-axis-pos") == 0) {
			KEY(FBK_DOWNARROW);
		}
		if (strcmp(szi, "left") == 0 || strcmp(szi, "x-axis-neg") == 0) {
			KEY(FBK_LEFTARROW);
		}
		if (strcmp(szi, "right") == 0 || strcmp(szi, "x-axis-pos") == 0) {
			KEY(FBK_RIGHTARROW);
		}
		if (strcmp(szi, "fire 1") == 0) {
			KEY(FBK_LALT);
		}
		if (strcmp(szi, "fire 2") == 0) {
			KEY(FBK_LCONTROL);
		}
		if (strcmp(szi, "fire 3") == 0) {
			KEY(FBK_E);
		}
		if (strcmp(szi, "fire 4") == 0) {
			KEY(FBK_LSHIFT);
		}
		if (strcmp(szi, "fire 5") == 0) {
			KEY(FBK_SPACE);
		}
		if (strcmp(szi, "fire 6") == 0) {
			KEY(FBK_T);
		}

		return 0;
	}
}

INT32 GamcPlayerHotRod(struct GameInp* pgi, char* szi, INT32 nPlayer, INT32 nFlags, INT32 nSlide)
{
	return 1;												// Couldn't map input
}

#undef MACRO
#undef KEY

