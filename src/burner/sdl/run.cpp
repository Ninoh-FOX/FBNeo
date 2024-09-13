// Run module
#include "burner.h"
#include <sys/time.h>

int bAlwaysDrawFrames = 0;
static unsigned int nNormalLast = 0; // Último valor de GetTime()
static int nNormalFrac = 0;          // Fracción adicional que se realizó
extern int counter = 0;

bool bAppDoFast = 0; // No se usa en SDL 1.2
bool bAppShowFPS = 0; // No se utiliza con SDL 1.2
bool bAppDoStep = false; // Declarar variable en el ámbito correcto

UINT32 messageFrames = 0;
char lastMessage[MESSAGE_MAX_LENGTH];

int bDrvSaveAll = 0;

char fpsstring[20];

struct timeval start;

unsigned int GetTime(void)
{
    unsigned int ticks;
    struct timeval now;
    gettimeofday(&now, NULL);
    ticks = (now.tv_sec - start.tv_sec) * 1000000 + now.tv_usec - start.tv_usec;
    return ticks;
}

// Ejecuta un solo frame
static int RunFrame(int bDraw, int bPause)
{
    if (!bDrvOkay) {
        return 1;
    }

    if (bPause) {
        InputMake(false);
        VidPaint(0);
    } else {
        nFramesEmulated++;
        nCurrentFrame++;
        InputMake(true);
    }

    if (bDraw) {
        nFramesRendered++;

        if (!bRunAhead || (BurnDrvGetFlags() & BDF_RUNAHEAD_DISABLED)) {
            if (VidFrame()) { // Ejecutar un frame normal
                pBurnDraw = NULL; // Asegurar que no se dibuje una imagen
                BurnDrvFrame();
                AudBlankSound();
            }
        } else {
            pBurnDraw = (BurnDrvGetFlags() & BDF_RUNAHEAD_DRAWSYNC) ? pVidImage : NULL;
				BurnDrvFrame();
				StateRunAheadSave();
				INT16 *pBurnSoundOut_temp = pBurnSoundOut;
				pBurnSoundOut = NULL;
				nCurrentFrame++;
				bBurnRunAheadFrame = 1;

				if (VidFrame()) {
					pBurnDraw = NULL;			// Make sure no image is drawn, since video failed this time 'round.
					BurnDrvFrame();
				}

				bBurnRunAheadFrame = 0;
				nCurrentFrame--;
				StateRunAheadLoad();
				pBurnSoundOut = pBurnSoundOut_temp; // restore pointer, for wav & avi writer
        }

        VidPaint(0); // Pintar la pantalla
    } else {
        pBurnDraw = NULL; // Asegurar que no se dibuje una imagen
        BurnDrvFrame();
    }

    return 0;
}

// Callback para sonido
static int RunGetNextSound(int bDraw)
{
    if (nAudNextSound == NULL) {
        return 1;
    }

    pBurnSoundOut = nAudNextSound;
    RunFrame(bDraw, 0);
    if (bAppDoStep) {
        memset(nAudNextSound, 0, nAudSegLen << 2); // Escribir silencio en el buffer
        bAppDoStep = false; // Paso completado
    }

    return 0;
}

int delay_ticks(int ticks)
{
    // Simplificar la función de retraso para SDL 1.2
    Uint32 startTicks = SDL_GetTicks();
    while ((SDL_GetTicks() - startTicks) <= (Uint32)ticks) { }
    return ticks;
}

int RunIdle()
{
    int nTime, nCount;

    if (bAudPlaying) {
        AudSoundCheck();
        return 0;
    }

    nTime = GetTime() - nNormalLast;
    nCount = (nTime * nAppVirtualFps - nNormalFrac) / 100000;
    if (nCount <= 0) {
        delay_ticks(2);
        return 0;
    }

    nNormalFrac += nCount * 100000;
    nNormalLast += nNormalFrac / nAppVirtualFps;
    nNormalFrac %= nAppVirtualFps;

    if (nCount > 100) {
        nCount = 100;
    }

    RunFrame(1, 0); // End-frame
    return 0;
}

int RunReset()
{
    nNormalLast = 0;
    nNormalFrac = 0;
    if (!bAudPlaying) {
        nNormalLast = GetTime();
    }
    return 0;
}

int RunInit()
{
    gettimeofday(&start, NULL);
    AudSetCallback(RunGetNextSound);
    AudSoundPlay();
    RunReset();
    return 0;
}

int RunExit()
{
    nNormalLast = 0;
    return 0;
}

// Bucle de mensajes principal
int RunMessageLoop()
{
    int quit = 0;

    RunInit();

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                quit = 1;
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    quit = 1;
                    break;
                default:
                    break;
                }
                break;
            }
        }

        RunIdle();
    }

    RunExit();

    return 0;
}
