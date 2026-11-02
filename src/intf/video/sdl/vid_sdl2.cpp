// blitter effects via SDL2
#include "burner.h"
#include "vid_support.h"
#include "vid_softfx.h"

#ifdef INCLUDE_SWITCHRES
#include <switchres_wrapper.h>
#endif

#include <SDL.h>
#include <SDL_image.h>

extern int vsync;
extern char videofiltering[3];

static unsigned char* VidMem = NULL;
extern SDL_Window* sdlWindow;
SDL_Renderer* sdlRenderer = NULL;
static SDL_Texture* sdlTexture = NULL;
static int  nRotateGame = 0;
static bool bFlipped = false;
static SDL_Rect dstrect;
static char Windowtitle[512];
static int display_w = 400, display_h = 300;
Uint32 screenFlags;

extern UINT16 maxLinesMenu;	// sdl2_gui_ingame.cpp: number of lines to show in ingame menus
extern bool didReinitialise;

#ifdef MIYOO_MINI
// The Miyoo Mini SDL2 renderer (steward-fu port) only copies whole textures:
// it ignores the viewport/logical size, scales by whole numbers relative to
// the window and does not implement SDL_RenderCopyEx. So the window is made
// the size of the framebuffer (1:1 copy), the destination rectangle is worked
// out here and rotation/flipping is done in software.
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static int nScreenW = 640, nScreenH = 480;	// framebuffer size (640x480 or 752x560)
static int nOutW = 0, nOutH = 0;			// game image size after rotation
static unsigned char* RotMem = NULL;		// rotated/flipped copy of pVidImage

static void MiyooGetScreenSize()
{
	struct fb_var_screeninfo vinfo;
	int fd = open("/dev/fb0", O_RDONLY);

	if (fd >= 0) {
		if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && vinfo.xres > 0 && vinfo.yres > 0) {
			nScreenW = vinfo.xres;
			nScreenH = vinfo.yres;
		}
		close(fd);
	}
}

static void MiyooAdjustImageSize()
{
	if (nOutW <= 0 || nOutH <= 0) {
		return;
	}
	if (bVidFullStretch) {
		dstrect.w = nScreenW;
		dstrect.h = nScreenH;
	} else if (bIntegerScale) {
		int nScale = nScreenW / nOutW;
		if (nScreenH / nOutH < nScale) nScale = nScreenH / nOutH;
		if (nScale < 1) nScale = 1;
		dstrect.w = nOutW * nScale;
		dstrect.h = nOutH * nScale;
	} else {
		// Fit the game's aspect ratio (e.g. 4:3 or 3:4) inside the screen
		int nAspectX = 4, nAspectY = 3;
		BurnDrvGetAspect(&nAspectX, &nAspectY);
		dstrect.w = nScreenW;
		dstrect.h = nScreenW * nAspectY / nAspectX;
		if (dstrect.h > nScreenH) {
			dstrect.h = nScreenH;
			dstrect.w = nScreenH * nAspectX / nAspectY;
		}
	}
	dstrect.x = (nScreenW - dstrect.w) / 2;
	dstrect.y = (nScreenH - dstrect.h) / 2;
	maxLinesMenu = nScreenH / 10 - 6;
}

// Copy pVidImage into RotMem turned the way the game must be shown.
// nMode: 1 = 90 degrees clockwise, 2 = 180, 3 = 270 (90 anticlockwise)
template <typename T>
static void MiyooRotate(const T* src, T* dst, int w, int h, int nMode)
{
	for (int y = 0; y < h; y++) {
		const T* s = src + y * w;
		for (int x = 0; x < w; x++) {
			switch (nMode) {
				case 1:  dst[x * h + (h - 1 - y)] = s[x]; break;
				case 2:  dst[(h - 1 - y) * w + (w - 1 - x)] = s[x]; break;
				default: dst[(w - 1 - x) * h + y] = s[x]; break;
			}
		}
	}
}

static int MiyooRotateMode()
{
	if (nRotateGame) return bFlipped ? 1 : 3;	// same angles the desktop build passes to SDL_RenderCopyEx (90 / 270)
	return bFlipped ? 2 : 0;
}
#endif

void RenderMessage()
{
#ifdef MIYOO_MINI
	return;		// partial texture copies (used by the font) are not supported
#endif
	// First render anything that is key-held based
	if (bAppDoFast)
	{
		inprint_shadowed(sdlRenderer, "FFWD", 10, 10);
	}
	if (bAppShowFPS)
	{
		if (bAppFullscreen)
		{
			inprint_shadowed(sdlRenderer, fpsstring, 10, 50);
		} 
		else 
		{
			sprintf(Windowtitle, "FBNeo - FPS: %s - %s - %s", fpsstring, BurnDrvGetTextA(DRV_NAME), BurnDrvGetTextA(DRV_FULLNAME));
			SDL_SetWindowTitle(sdlWindow, Windowtitle);
		}
	}

	if (messageFrames > 1)
	{
		inprint_shadowed(sdlRenderer, lastMessage, 10, 30);
		messageFrames--;
	}
}

static int Exit()
{
#ifdef INCLUDE_SWITCHRES
	sr_deinit();
#endif
	kill_inline_font(); //TODO: This is not supposed to be here
	SDL_DestroyTexture(sdlTexture);
	sdlTexture = NULL;
	SDL_DestroyRenderer(sdlRenderer);
	sdlRenderer = NULL;
	SDL_DestroyWindow(sdlWindow);
	sdlWindow = NULL;
	
	if (VidMem)
	{
		free(VidMem);
	}
#ifdef MIYOO_MINI
	free(RotMem);
	RotMem = NULL;
#endif
	return 0;
}

void AdjustImageSize()
{
#ifdef MIYOO_MINI
	MiyooAdjustImageSize();
	return;
#endif
	screenFlags = SDL_GetWindowFlags(sdlWindow);

	// Scale governed by -windowscale command line option.
	// Screen center fix thanks to Woises
	if (nRotateGame) {
		if (screenFlags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			int w;
			int h;
			SDL_GetWindowSize(sdlWindow, &w, &h);
			SDL_SetWindowSize(sdlWindow, (display_w * w / h) * nWindowScale, display_w * nWindowScale);
			SDL_RenderSetLogicalSize(sdlRenderer, (display_w * w / h), display_w);
			dstrect.x = ((display_w * w / h) - display_w) / 2;
		} else {
			SDL_RestoreWindow(sdlWindow);		// If started fullscreen, switching to window can get maximized
			SDL_SetWindowSize(sdlWindow, display_h * nWindowScale, display_w * nWindowScale);
			SDL_RenderSetLogicalSize(sdlRenderer, display_h, display_w);
			dstrect.x = (display_h - display_w) / 2;
		}
		dstrect.y = (display_w - display_h) / 2;
		maxLinesMenu = display_w / 10 - 6;		// Get number of lines to show in ingame menus
	} else {
		if (screenFlags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) {
			int w;
			int h;
			SDL_GetWindowSize(sdlWindow, &w, &h);
			SDL_SetWindowSize(sdlWindow, (display_h * w / h) * nWindowScale, display_h * nWindowScale);

			if (display_w < (display_h * w / h)) {
				SDL_RenderSetLogicalSize(sdlRenderer, (display_h * w / h), display_h);
				dstrect.x = ((display_h * w / h) - display_w) / 2;
				dstrect.y = 0;
			} else {
				SDL_RenderSetLogicalSize(sdlRenderer, display_w, (display_w * h / w));
				dstrect.x = 0;
				dstrect.y = ((display_w * h / w) - display_h) / 2;
			}
		} else {
			SDL_RestoreWindow(sdlWindow);
			SDL_SetWindowSize(sdlWindow, display_w * nWindowScale, display_h * nWindowScale);
			SDL_RenderSetLogicalSize(sdlRenderer, display_w, display_h);
			dstrect.x = 0;
			dstrect.y = 0;
		}
		maxLinesMenu = display_h / 10 - 6;		// Get number of lines to show in ingame menus
	}
	SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	dstrect.w = display_w;
	dstrect.h = display_h;
}

static int Init()
{
	int nMemLen = 0;
	int GameAspectX = 4, GameAspectY = 3;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("vid init error\n");
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't initialize SDL: %s", SDL_GetError());
		return 3;
	}

	nRotateGame = 0;

	if (bDrvOkay)
	{
		// Get the game screen size
		if (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL)
		{
#ifdef FBNEO_DEBUG
			printf("Vertical\n");
#endif
			BurnDrvGetVisibleSize(&nVidImageHeight, &nVidImageWidth);
			BurnDrvGetAspect(&GameAspectY, &GameAspectX);
			nRotateGame = 1;
		}
		else
		{
			BurnDrvGetVisibleSize(&nVidImageWidth, &nVidImageHeight);
			BurnDrvGetAspect(&GameAspectX, &GameAspectY);
		}

#ifdef INCLUDE_SWITCHRES
		// Don't force 4:3 aspect-ratio, until there is a command-line switch
		display_w = nVidImageWidth;
		display_h = nVidImageHeight;
		sr_init();
		if (nRotateGame) sr_set_rotation(1);
#else
		if (nRotateGame) {
			display_w = nVidImageHeight * GameAspectX / GameAspectY;
			display_h = nVidImageHeight;
		} else {
			display_w = nVidImageWidth;
			display_h = nVidImageWidth * GameAspectY / GameAspectX;
		}
#endif

		if (BurnDrvGetFlags() & BDF_ORIENTATION_FLIPPED)
		{
#ifdef FBNEO_DEBUG
			printf("Flipped\n");
#endif
			bFlipped = 1;
		} else {
			bFlipped = 0;
		}
	}

	sprintf(Windowtitle, "FBNeo - %s - %s", BurnDrvGetTextA(DRV_NAME), BurnDrvGetTextA(DRV_FULLNAME));

	if (bAppFullscreen) screenFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP;
	else screenFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;

	//Test refresh rate availability
#ifdef FBNEO_DEBUG
	printf("Game resolution: %dx%d@%f\n", nVidImageWidth, nVidImageHeight, nBurnFPS/100.0);
#endif

#ifdef INCLUDE_SWITCHRES
	sr_mode srm;
	unsigned char interlace = 0; // FBN doesn't handle interlace yet, force it to disabled
	double rr = nBurnFPS / 100.0;
	sr_init_disp();
	sr_add_mode(display_w, display_h, rr, interlace, &srm);
	sr_switch_to_mode(display_w, display_h, rr, interlace, &srm);
#endif

#ifdef MIYOO_MINI
	MiyooGetScreenSize();
	screenFlags = SDL_WINDOW_SHOWN;
	sdlWindow = SDL_CreateWindow(Windowtitle, 0, 0, nScreenW, nScreenH, screenFlags);
#else
	if (nRotateGame)
	{
		sdlWindow = SDL_CreateWindow(
			Windowtitle,
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			display_h,
			display_w,
			screenFlags
		);
	}
	else
	{
		sdlWindow = SDL_CreateWindow(
			Windowtitle,
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			display_w,
			display_h,
			screenFlags
		);
	}
#endif

	// Check that the window was successfully created
	if (sdlWindow == NULL)
	{
		// In the case that the window could not be made...
		printf("Could not create window: %s\n", SDL_GetError());
		return 1;
	}

	Uint32 renderflags = SDL_RENDERER_ACCELERATED;

	if (vsync)
	{
		renderflags = renderflags | SDL_RENDERER_PRESENTVSYNC;
	}

	sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, renderflags);
	if (sdlRenderer == NULL)
	{
		sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE);
		if (sdlRenderer == NULL)
		{	
			// In the case that the window could not be made...
			printf("Could not create renderer: %s\n", SDL_GetError());
			return 1;
		}
	}

	SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_NONE);

	nVidImageDepth = 32;
#ifdef MIYOO_MINI
	nVidImageDepth = 16;	// RGB565 halves the copies per frame
#endif

	if (BurnDrvGetFlags() & BDF_16BIT_ONLY)
	{
		nVidImageDepth = 16;
#ifdef FBNEO_DEBUG
		printf("Forcing 16bit color\n");
#endif
	}
#ifdef FBNEO_DEBUG
	printf("bbp: %d\n", nVidImageDepth);
#endif
	if (bIntegerScale)
	{
		SDL_RenderSetIntegerScale(sdlRenderer, SDL_TRUE);
	}

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, videofiltering);

#ifndef MIYOO_MINI
	AdjustImageSize();
#endif

#ifdef FBNEO_DEBUG
	printf("setting logical size w: %d h: %d\n", display_w, display_h);
#endif
	
	inrenderer(sdlRenderer); //TODO: this is not supposed to be here
	prepare_inline_font();   // TODO: BAD
	incolor(0xFFF000, 0);

	int nTexW = nVidImageWidth, nTexH = nVidImageHeight;
#ifdef MIYOO_MINI
	// The texture holds the image already turned the right way up
	nOutW = nRotateGame ? nVidImageHeight : nVidImageWidth;
	nOutH = nRotateGame ? nVidImageWidth : nVidImageHeight;
	nTexW = nOutW;
	nTexH = nOutH;
	MiyooAdjustImageSize();
#endif
	if (nVidImageDepth == 32)
	{
		sdlTexture = SDL_CreateTexture(sdlRenderer,
			SDL_PIXELFORMAT_RGB888,
			SDL_TEXTUREACCESS_STREAMING,
			nTexW, nTexH);
	}
	else
	{
		sdlTexture = SDL_CreateTexture(sdlRenderer,
			SDL_PIXELFORMAT_RGB565,
			SDL_TEXTUREACCESS_STREAMING,
			nTexW, nTexH);
	}
	if (!sdlTexture)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't create sdlTexture from surface: %s", SDL_GetError());
		return 3;
	}

	nVidImageBPP = (nVidImageDepth + 7) >> 3;
	nBurnBpp = nVidImageBPP;

	SetBurnHighCol(nVidImageDepth);

	nVidImagePitch = nVidImageWidth * nVidImageBPP;
	nBurnPitch = nVidImagePitch;

	nMemLen = nVidImageWidth * nVidImageHeight * nVidImageBPP;
#ifdef FBNEO_DEBUG
	printf("nVidImageWidth=%d nVidImageHeight=%d nVidImagePitch=%d\n", nVidImageWidth, nVidImageHeight, nVidImagePitch);
#endif
	VidMem = (unsigned char*)malloc(nMemLen);
#ifdef MIYOO_MINI
	free(RotMem);
	RotMem = MiyooRotateMode() ? (unsigned char*)malloc(nMemLen) : NULL;
#endif
	if (VidMem)
	{
		memset(VidMem, 0, nMemLen);
		pVidImage = VidMem;
#ifdef FBNEO_DEBUG
		printf("Malloc for video Ok %d\n", nMemLen);
#endif
		return 0;
	}
	else
	{
		pVidImage = NULL;
		return 1;
	}

#ifdef FBNEO_DEBUG
	printf("done vid init");
#endif
	return 0;
}

static int vidScale(RECT*, int, int)
{
	return 0;
}

// Run one frame and render the screen
static int Frame(bool bRedraw)                                          // bRedraw = 0
{
	if ((didReinitialise) && (pVidImage == NULL)) {
		didReinitialise = false;
		Exit();
		Init();
	}
	if (pVidImage == NULL)
	{
		return 1;
	}

	VidFrameCallback(bRedraw);

	return 0;
}

// Paint the BlitFX surface onto the primary surface
static int Paint(int bValidate)
{
#ifdef MIYOO_MINI
	int nMode = MiyooRotateMode();
	if (nMode && RotMem) {
		if (nVidImageBPP == 2) {
			MiyooRotate((const UINT16*)pVidImage, (UINT16*)RotMem, nVidImageWidth, nVidImageHeight, nMode);
		} else {
			MiyooRotate((const UINT32*)pVidImage, (UINT32*)RotMem, nVidImageWidth, nVidImageHeight, nMode);
		}
		SDL_UpdateTexture(sdlTexture, NULL, RotMem, nOutW * nVidImageBPP);
	} else {
		SDL_UpdateTexture(sdlTexture, NULL, pVidImage, nVidImagePitch);
	}
	SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &dstrect);
	SDL_RenderPresent(sdlRenderer);
	return 0;
#endif

	SDL_RenderClear(sdlRenderer);
	SDL_UpdateTexture(sdlTexture, NULL, pVidImage, nVidImagePitch);
	if (nRotateGame)
	{
		SDL_RenderCopyEx(sdlRenderer, sdlTexture, NULL, &dstrect, (bFlipped ? 90 : 270), NULL, SDL_FLIP_NONE);
	}
	else
	{
		if (bFlipped)
		{
			SDL_RenderCopyEx(sdlRenderer, sdlTexture, NULL, &dstrect, 180, NULL, SDL_FLIP_NONE);
		}
		else
		{
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &dstrect);
		}
	}

	RenderMessage();

	SDL_RenderPresent(sdlRenderer);

	return 0;
}

static int GetSettings(InterfaceInfo* pInfo)
{
	return 0;
}

// The Video Output plugin:
struct VidOut VidOutSDL2 = { Init, Exit, Frame, Paint, vidScale, GetSettings, _T("SDL2 video output") };
