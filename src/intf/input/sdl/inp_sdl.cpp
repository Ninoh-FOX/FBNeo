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
	return 0;
}

int SDLinpExit()
{
	return 0;
}

int SDLinpInit()
{
	SDLinpExit();

	// Set up the keyboard
	SDLinpKeyboardInit();

	return 0;
}

static unsigned char bKeyboardRead = 0;
static unsigned char* SDLinpKeyboardState;


#define SDL_KEY_DOWN(key) (FBKtoSDL[key] > 0 ? SDLinpKeyboardState[FBKtoSDL[key]] : 0)

// Call before checking for Input in a frame
int SDLinpStart()
{
	// Update SDL event queue
	SDL_PumpEvents();

	// Keyboard not read this frame
	bKeyboardRead = 0;

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

	return nRetVal;
}

struct InputInOut InputInOutSDL = { SDLinpInit, SDLinpExit, SDLinpSetCooperativeLevel, SDLinpStart, SDLinpState, NULL, NULL, SDLinpFind, NULL, NULL, _T("SDL input") };
