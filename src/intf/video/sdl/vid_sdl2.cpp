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
#include <sys/mman.h>
#include <linux/fb.h>

static int nScreenW = 640, nScreenH = 480;	// framebuffer size (640x480 or 752x560)
static int nOutW = 0, nOutH = 0;			// game image size after rotation
static unsigned char* RotMem = NULL;		// rotated/flipped copy of pVidImage

// Software filters (RGB565 only), applied before the hardware scaler:
//  bVidScale2x   - AdvMAME2x / Scale2x
//  bVidScanlines - every other screen line darkened by nVidScanIntensity
INT32 bVidScale2x = 0;
static bool bFilterScale2x = false, bFilterScanlines = false;
static bool bFinalPass = false;			// resize (and scanlines) before the hardware scaler
static int nTexW = 0, nTexH = 0;			// texture size after the filters
static UINT16* Scale2xMem = NULL;
static UINT16* ScanMem = NULL;
static UINT16* ScanLUT = NULL;			// darkened colour for every RGB565 value

// On-screen message after a hotkey (the SDL text functions do not work here)
extern const unsigned char* inprint_font_bits();
static char szMiyooMsg[64] = "";
static Uint32 nMiyooMsgEnd = 0;

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
		// Keep the image proportions: square pixels by default, or the
		// aspect ratio the driver asks for (usually 4:3) with bVidCorrectAspect
		int nAspectX = nOutW, nAspectY = nOutH;
		if (bVidCorrectAspect) {
			BurnDrvGetAspect(&nAspectX, &nAspectY);
		}
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

// RenderClear does nothing with this renderer and the parts of the screen
// outside the image are never drawn, so clear both framebuffer pages by hand
// whenever the image size changes
static void MiyooClearFramebuffer()
{
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;
	int fd = open("/dev/fb0", O_RDWR);

	if (fd < 0) {
		return;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) == 0 && ioctl(fd, FBIOGET_FSCREENINFO, &finfo) == 0) {
		size_t nLen = (size_t)finfo.line_length * vinfo.yres_virtual;
		void* pFb = mmap(NULL, nLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (pFb != MAP_FAILED) {
			memset(pFb, 0, nLen);
			munmap(pFb, nLen);
		}
	}
	close(fd);
}

static int MiyooCreateTexture()
{
	if (sdlTexture) {
		SDL_DestroyTexture(sdlTexture);
	}
	sdlTexture = SDL_CreateTexture(sdlRenderer,
		nVidImageDepth == 32 ? SDL_PIXELFORMAT_RGB888 : SDL_PIXELFORMAT_RGB565,
		SDL_TEXTUREACCESS_STREAMING, nTexW, nTexH);
	MiyooClearFramebuffer();
	return sdlTexture ? 0 : 1;
}

static void MiyooShowMessage(const char* szMsg)
{
	strncpy(szMiyooMsg, szMsg, sizeof(szMiyooMsg) - 1);
	szMiyooMsg[sizeof(szMiyooMsg) - 1] = 0;
	nMiyooMsgEnd = SDL_GetTicks() + 2000;
}

// Draw szMiyooMsg in the top left corner of the RGB565 image that goes to the texture
static void MiyooDrawMessage(UINT16* pDst, int w, int h)
{
	if (szMiyooMsg[0] == 0 || pDst == NULL) {
		return;
	}
	if ((Sint32)(SDL_GetTicks() - nMiyooMsgEnd) >= 0) {
		szMiyooMsg[0] = 0;
		return;
	}

	const unsigned char* pFont = inprint_font_bits();
	// about 16x16 screen pixels per character, whatever the texture size:
	// the texture can be scaled differently across and down (e.g. scanlines)
	int nSizeX = (2 * w + dstrect.w / 2) / (dstrect.w > 0 ? dstrect.w : 1);
	int nSizeY = (2 * h + dstrect.h / 2) / (dstrect.h > 0 ? dstrect.h : 1);
	if (nSizeX < 1) nSizeX = 1;
	if (nSizeY < 1) nSizeY = 1;

	int nX0 = 4 * nSizeX, nY0 = 4 * nSizeY;
	for (int pass = 0; pass < 2; pass++) {			// shadow first, then the text
		UINT16 nColour = pass ? 0xFFE0 : 0x0000;	// yellow on black
		int nShiftX = pass ? 0 : nSizeX, nShiftY = pass ? 0 : nSizeY;
		for (int c = 0; szMiyooMsg[c]; c++) {
			int id = (unsigned char)szMiyooMsg[c] & 0x7F;
			int nFontX = (id % 16) * 8, nFontY = (id / 16) * 8;
			for (int fy = 0; fy < 8; fy++) {
				for (int fx = 0; fx < 8; fx++) {
					int nBit = (nFontY + fy) * 128 + nFontX + fx;
					if (pFont[nBit >> 3] & (1 << (nBit & 7))) {
						continue;		// unlit
					}
					for (int sy = 0; sy < nSizeY; sy++) {
						int y = nY0 + nShiftY + fy * nSizeY + sy;
						if (y < 0 || y >= h) continue;
						for (int sx = 0; sx < nSizeX; sx++) {
							int x = nX0 + nShiftX + (c * 8 + fx) * nSizeX + sx;
							if (x >= 0 && x < w) pDst[y * w + x] = nColour;
						}
					}
				}
			}
		}
	}
}

static void MiyooFiltersExit()
{
	free(Scale2xMem);
	free(ScanMem);
	free(ScanLUT);
	Scale2xMem = ScanMem = ScanLUT = NULL;
	bFilterScale2x = bFilterScanlines = bFinalPass = false;
}

// Work out the texture size for the active filters. Must run after MiyooAdjustImageSize().
static void MiyooFiltersInit()
{
	MiyooFiltersExit();

	nTexW = nOutW;
	nTexH = nOutH;
	if (nVidImageBPP != 2) {
		return;			// filters are written for RGB565
	}

	if (bVidScale2x) {
		Scale2xMem = (UINT16*)malloc(nOutW * 2 * nOutH * 2 * sizeof(UINT16));
		if (Scale2xMem) {
			bFilterScale2x = true;
			nTexW = nOutW * 2;
			nTexH = nOutH * 2;
		}
	}

	// The renderer refuses textures bigger than this (640x480 on the Mini)
	int nMaxW = nScreenW, nMaxH = nScreenH;
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(sdlRenderer, &info) == 0 && info.max_texture_width > 0 && info.max_texture_height > 0) {
		nMaxW = info.max_texture_width;
		nMaxH = info.max_texture_height;
	}

	int nFinalW = nTexW, nFinalH = nTexH;
	if (bVidScanlines) {
		// As many lines as the image has on screen, so the dark lines stay
		// one screen line high whatever the scale
		nFinalH = dstrect.h;
	}
	if (nFinalW > nMaxW) nFinalW = dstrect.w < nMaxW ? dstrect.w : nMaxW;
	if (nFinalH > nMaxH) nFinalH = dstrect.h < nMaxH ? dstrect.h : nMaxH;

	if (nFinalW != nTexW || nFinalH != nTexH || bVidScanlines) {
		ScanMem = (UINT16*)malloc(nFinalW * nFinalH * sizeof(UINT16));
		if (bVidScanlines) {
			ScanLUT = (UINT16*)malloc(65536 * sizeof(UINT16));
		}
		if (ScanMem && (ScanLUT || !bVidScanlines)) {
			if (ScanLUT) {
				int nLevel = nVidScanIntensity & 0xFF;		// 0x00 black .. 0xFF no effect
				for (int i = 0; i < 65536; i++) {
					int r = (i >> 11) & 0x1F, g = (i >> 5) & 0x3F, b = i & 0x1F;
					ScanLUT[i] = (UINT16)((((r * nLevel) / 255) << 11) | (((g * nLevel) / 255) << 5) | ((b * nLevel) / 255));
				}
				bFilterScanlines = true;
			}
			bFinalPass = true;
			nTexW = nFinalW;
			nTexH = nFinalH;
		} else {
			free(ScanMem);
			free(ScanLUT);
			ScanMem = ScanLUT = NULL;
			if (bFilterScale2x) {		// can not shrink it: drop Scale2x rather than show nothing
				free(Scale2xMem);
				Scale2xMem = NULL;
				bFilterScale2x = false;
				nTexW = nOutW;
				nTexH = nOutH;
			}
		}
	}
}

// AdvMAME2x: each pixel becomes 2x2, rounding off diagonal edges
static void MiyooScale2x(const UINT16* src, UINT16* dst, int w, int h)
{
	for (int y = 0; y < h; y++) {
		const UINT16* up   = src + (y > 0 ? y - 1 : y) * w;
		const UINT16* mid  = src + y * w;
		const UINT16* down = src + (y < h - 1 ? y + 1 : y) * w;
		UINT16* d0 = dst + (y * 2) * (w * 2);
		UINT16* d1 = d0 + w * 2;
		for (int x = 0; x < w; x++) {
			UINT16 B = up[x], H = down[x], E = mid[x];
			UINT16 D = mid[x > 0 ? x - 1 : x];
			UINT16 F = mid[x < w - 1 ? x + 1 : x];
			if (B != H && D != F) {
				d0[x * 2]     = (D == B) ? D : E;
				d0[x * 2 + 1] = (B == F) ? F : E;
				d1[x * 2]     = (D == H) ? D : E;
				d1[x * 2 + 1] = (H == F) ? F : E;
			} else {
				d0[x * 2] = d0[x * 2 + 1] = d1[x * 2] = d1[x * 2 + 1] = E;
			}
		}
	}
}

// Resize w x h to ow x oh (nearest) and, with scanlines, darken every other line
static void MiyooFinalPass(const UINT16* src, UINT16* dst, int w, int h, int ow, int oh)
{
	static int nMapW = 0, nMapOw = 0;
	static int nMapX[2048];

	if (ow != w && (nMapW != w || nMapOw != ow) && ow <= 2048) {
		for (int x = 0; x < ow; x++) nMapX[x] = x * w / ow;
		nMapW = w;
		nMapOw = ow;
	}
	for (int y = 0; y < oh; y++) {
		const UINT16* s = src + (y * h / oh) * w;
		UINT16* d = dst + y * ow;
		bool bDark = bFilterScanlines && (y & 1);
		if (ow == w) {
			if (bDark) {
				for (int x = 0; x < ow; x++) d[x] = ScanLUT[s[x]];
			} else {
				memcpy(d, s, w * sizeof(UINT16));
			}
		} else if (bDark) {
			for (int x = 0; x < ow; x++) d[x] = ScanLUT[s[nMapX[x]]];
		} else {
			for (int x = 0; x < ow; x++) d[x] = s[nMapX[x]];
		}
	}
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
	MiyooFiltersExit();
#endif
	return 0;
}

#ifdef MIYOO_MINI
static void MiyooApplyVideoSettings()
{
	if (sdlRenderer == NULL || nOutW <= 0) {
		return;
	}
	MiyooAdjustImageSize();
	MiyooFiltersInit();
	MiyooCreateTexture();
}

// MENU + L2: keep the game's proportions (as big as the screen allows) <-> full screen
void MiyooNextImageMode()
{
	bVidFullStretch = bVidFullStretch ? 0 : 1;
	MiyooApplyVideoSettings();
	MiyooShowMessage(bVidFullStretch ? "Image: full screen" : "Image: keep proportions");
}

// MENU + R2: none -> scanlines -> Scale2x -> Scale2x + scanlines
void MiyooNextFilter()
{
	// bit 0 = scanlines, bit 1 = Scale2x
	int nFilter = ((bVidScanlines ? 1 : 0) | (bVidScale2x ? 2 : 0)) + 1;
	bVidScanlines = (nFilter & 1) ? 1 : 0;
	bVidScale2x = (nFilter & 2) ? 1 : 0;
	MiyooApplyVideoSettings();
	static const char* szFilters[4] = { "Filter: none", "Filter: scanlines", "Filter: Scale2x", "Filter: Scale2x + scanlines" };
	MiyooShowMessage(szFilters[(bVidScanlines ? 1 : 0) | (bVidScale2x ? 2 : 0)]);
}
#endif

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

#ifndef MIYOO_MINI
	int nTexW, nTexH;
#endif
	nTexW = nVidImageWidth;
	nTexH = nVidImageHeight;
#ifdef MIYOO_MINI
	// The texture holds the image already turned the right way up (and filtered)
	nOutW = nRotateGame ? nVidImageHeight : nVidImageWidth;
	nOutH = nRotateGame ? nVidImageWidth : nVidImageHeight;
	nVidImageBPP = (nVidImageDepth + 7) >> 3;
	MiyooAdjustImageSize();
	MiyooFiltersInit();
#endif
#ifdef MIYOO_MINI
	MiyooCreateTexture();
#else
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
#endif
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
	const unsigned char* pImage = pVidImage;
	int nPitch = nVidImagePitch;
	if (nMode && RotMem) {
		if (nVidImageBPP == 2) {
			MiyooRotate((const UINT16*)pVidImage, (UINT16*)RotMem, nVidImageWidth, nVidImageHeight, nMode);
		} else {
			MiyooRotate((const UINT32*)pVidImage, (UINT32*)RotMem, nVidImageWidth, nVidImageHeight, nMode);
		}
		pImage = RotMem;
		nPitch = nOutW * nVidImageBPP;
	}

	int nW = nOutW, nH = nOutH;
	if (bFilterScale2x) {
		MiyooScale2x((const UINT16*)pImage, Scale2xMem, nW, nH);
		pImage = (const unsigned char*)Scale2xMem;
		nW *= 2;
		nH *= 2;
		nPitch = nW * 2;
	}
	if (bFinalPass) {
		MiyooFinalPass((const UINT16*)pImage, ScanMem, nW, nH, nTexW, nTexH);
		pImage = (const unsigned char*)ScanMem;
		nPitch = nTexW * 2;
	}
	if (szMiyooMsg[0]) {
		// pImage is pVidImage or one of our buffers, all writable; the emulator
		// draws pVidImage again for the next frame
		MiyooDrawMessage((UINT16*)pImage, nPitch / 2, nTexH);
	}
	SDL_UpdateTexture(sdlTexture, NULL, pImage, nPitch);
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
