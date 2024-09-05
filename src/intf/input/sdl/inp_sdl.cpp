// Module for input using SDL
#include <SDL.h>

#include "burner.h"
#include "inp_sdl_keys.h"

int SDLtoFBK[512] = { 0 };  // Definición del array en el archivo fuente

void setup_keymaps() {
    SDLtoFBK[273] = FBK_UPARROW;       // SDLK_UP
    SDLtoFBK[274] = FBK_DOWNARROW;     // SDLK_DOWN
    SDLtoFBK[276] = FBK_LEFTARROW;     // SDLK_LEFT
    SDLtoFBK[275] = FBK_RIGHTARROW;    // SDLK_RIGHT
    SDLtoFBK[27]  = FBK_ESCAPE;        // SDLK_ESCAPE
    SDLtoFBK[32]  = FBK_SPACE;         // SDLK_SPACE
    SDLtoFBK[306] = FBK_LCONTROL;      // SDLK_LCTRL
    SDLtoFBK[304] = FBK_LSHIFT;        // SDLK_LSHIFT
    SDLtoFBK[308] = FBK_LALT;          // SDLK_LALT
    SDLtoFBK[101] = FBK_E;             // SDLK_e
    SDLtoFBK[116] = FBK_T;             // SDLK_t
    SDLtoFBK[9]   = FBK_TAB;           // SDLK_TAB
    SDLtoFBK[8]   = FBK_BACK;          // SDLK_BACKSPACE
    SDLtoFBK[305] = FBK_RCONTROL;      // SDLK_RCTRL
    SDLtoFBK[13]  = FBK_RETURN;        // SDLK_RETURN
    SDLtoFBK[312] = FBK_RWIN;          // SDLK_RSUPER
    SDLtoFBK[311] = FBK_LWIN;          // SDLK_LSUPER
    SDLtoFBK[320] = FBK_POWER;         // SDLK_POWER
}

#define MAX_JOYSTICKS (8)

static int FBKtoSDL[512];

static int nInitedSubsytems = 0;
static SDL_Joystick* JoyList[MAX_JOYSTICKS];
static int* JoyPrevAxes = NULL;
static int nJoystickCount = 0;						// Number of joysticks connected to this machine

// Sets up one Joystick (for example the range of the joystick's axes)
static int SDLinpJoystickInit(int i)
{
	JoyList[i] = SDL_JoystickOpen(i);
	return 0;
}


// Set up the keyboard
static int SDLinpKeyboardInit()
{
    setup_keymaps();  // Inicializar el mapeo de teclas

    for (int i = 0; i < 512; i++) {
        if (SDLtoFBK[i] > 0)
            FBKtoSDL[SDLtoFBK[i]] = i;
    }

    return 0;
}

int SDLinpSetCooperativeLevel(bool bExclusive, bool /*bForeGround*/)
{
	SDL_WM_GrabInput((bDrvOkay && (bExclusive || nVidFullscreen)) ? SDL_GRAB_ON : SDL_GRAB_OFF);
	SDL_ShowCursor((bDrvOkay && (bExclusive || nVidFullscreen)) ? SDL_DISABLE : SDL_ENABLE);

	return 0;
}

int SDLinpExit()
{
	// Close all joysticks
	for (int i = 0; i < MAX_JOYSTICKS; i++) {
		if (JoyList[i]) {
			SDL_JoystickClose(JoyList[i]);
			JoyList[i] = NULL;
		}
	}

	nJoystickCount = 0;

	free(JoyPrevAxes);
	JoyPrevAxes = NULL;

	if (!(nInitedSubsytems & SDL_INIT_JOYSTICK)) {
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
	}

	nInitedSubsytems = 0;

	return 0;
}

int SDLinpInit()
{
	int nSize;

	SDLinpExit();

	memset(&JoyList, 0, sizeof(JoyList));

	nSize = MAX_JOYSTICKS * 8 * sizeof(int);
	if ((JoyPrevAxes = (int*)malloc(nSize)) == NULL) {
		SDLinpExit();
		return 1;
	}
	memset(JoyPrevAxes, 0, nSize);

	nInitedSubsytems = SDL_WasInit(SDL_INIT_JOYSTICK);

	if (!(nInitedSubsytems & SDL_INIT_JOYSTICK)) {
		SDL_InitSubSystem(SDL_INIT_JOYSTICK);
	}

	// Set up the joysticks
	nJoystickCount = SDL_NumJoysticks();
	for (int i = 0; i < nJoystickCount; i++) {
		SDLinpJoystickInit(i);
	}
	SDL_JoystickEventState(SDL_IGNORE);

	// Set up the keyboard
	SDLinpKeyboardInit();

	return 0;
}

static unsigned char bKeyboardRead = 0;
static unsigned char* SDLinpKeyboardState;

static unsigned char bJoystickRead = 0;

static unsigned char bMouseRead = 0;
static struct { unsigned char buttons; int xdelta; int ydelta; } SDLinpMouseState;

#define SDL_KEY_DOWN(key) (FBKtoSDL[key] > 0 ? SDLinpKeyboardState[FBKtoSDL[key]] : 0)

// Call before checking for Input in a frame
int SDLinpStart()
{
	// Update SDL event queue
	SDL_PumpEvents();

	// Keyboard not read this frame
	bKeyboardRead = 0;

	// No joysticks have been read for this frame
	bJoystickRead = 0;

	// Mouse not read this frame
	bMouseRead = 0;

	return 0;
}

// Read one of the joysticks
static int ReadJoystick()
{
	return 0;
}

// Read one joystick axis
int SDLinpJoyAxis(int i, int nAxis)
{
	return 0;
}

// Read the keyboard
static int ReadKeyboard()
{
	int numkeys;

	if (bKeyboardRead) {							// already read this frame - ready to go
		return 0;
	}

	SDLinpKeyboardState = SDL_GetKeyState(&numkeys);
	if (SDLinpKeyboardState == NULL) {
		return 1;
	}

	// The keyboard has been successfully Read this frame
	bKeyboardRead = 1;

	return 0;
}

static int ReadMouse()
{
	return 0;
}

// Read one mouse axis
int SDLinpMouseAxis(int i, int nAxis)
{
	return 0;
}

// Check a subcode (the 40xx bit in 4001, 4102 etc) for a joystick input code
static int JoystickState(int i, int nSubCode)
{
	return 0;
}

// Check a subcode (the 80xx bit in 8001, 8102 etc) for a mouse input code
static int CheckMouseState(unsigned int nSubCode)
{
	return 0;
}

// Get the state (pressed = 1, not pressed = 0) of a particular input code
int SDLinpState(int nCode)
{
	if (nCode < 0) {
		return 0;
	}

	if (nCode < 0x100) {
		if (ReadKeyboard() != 0) {							// Check keyboard has been read - return not pressed on error
			return 0;
		}
		return SDL_KEY_DOWN(nCode);							// Return key state
	}

	if (nCode < 0x4000) {
		return 0;
	}

	return 0;
}

// This function finds which key is pressed, and returns its code
int SDLinpFind(bool CreateBaseline)
{
	int nRetVal = -1;										// assume nothing pressed

	// check if any keyboard keys are pressed
	if (ReadKeyboard() == 0) {
		for (int i = 0; i < 0x100; i++) {
			if (SDL_KEY_DOWN(i) > 0) {
				nRetVal = i;
				goto End;
			}
		}
	}

End:

	if (CreateBaseline) {
		for (int i = 0; i < nJoystickCount; i++) {
			for (int j = 0; j < 8; j++) {
				JoyPrevAxes[(i << 3) + j] = SDLinpJoyAxis(i, j);
			}
		}
	}

	return nRetVal;
}

int SDLinpGetControlName(int nCode, TCHAR* pszDeviceName, TCHAR* pszControlName)
{
    return 0;
}

struct InputInOut InputInOutSDL = { SDLinpInit, SDLinpExit, SDLinpSetCooperativeLevel, SDLinpStart, SDLinpState, SDLinpJoyAxis, SDLinpMouseAxis, SDLinpFind, SDLinpGetControlName, NULL, _T("SDL input") };
