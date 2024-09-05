// vid_sdlfx.cpp
#include "burner.h"
#include "vid_support.h"
#include "vid_softfx.h"
#include <SDL.h>
#include <mi_sys.h>           // Librería de sistema para Miyoo Mini
#include <mi_gfx.h>           // Librería de gráficos para Miyoo Mini
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

// Variables globales
static int nInitedSubsytems = 0;
static int nGameWidth = 0, nGameHeight = 0;  // Tamaño de la pantalla
SDL_Surface* sdlsBlitFX[2] = { NULL, };      // Superficies de imagen
SDL_Surface* sdlsFramebuf = NULL;

int fd_fb = 0;
void* fb_addr;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
MI_GFX_Surface_t sHW;
MI_GFX_Rect_t sHWRect;
MI_GFX_Opt_t sHWOpt;

static int nSize;
static int nUseBlitter;
static int nUseSys;
static int nDirectAccess = 1;
static int nRotateGame = 0;

// Inicializar subsistemas gráficos específicos de Miyoo Mini
static int InitMiyooGraphics()
{
    if (MI_SYS_Init() != MI_SUCCESS) {
        printf("Error: No se pudo inicializar MI_SYS.\n");
        return 1;
    }

    if (MI_GFX_Open() != MI_SUCCESS) {
        printf("Error: No se pudo inicializar MI_GFX.\n");
        return 1;
    }

    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0) {
        printf("Error: No se pudo abrir /dev/fb0.\n");
        return 1;
    }

    ioctl(fd_fb, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo);

    fb_addr = mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
    if (fb_addr == MAP_FAILED) {
        printf("Error: No se pudo mapear el framebuffer.\n");
        close(fd_fb);
        return 1;
    }

    return 0;
}

//
//	Get GFX_ColorFmt from SDL_Surface
//
static inline MI_GFX_ColorFmt_e	GFX_ColorFmt(SDL_Surface *surface) {
	if (surface) {
		if (surface->format->BytesPerPixel == 2) {
			if (surface->format->Amask == 0x0000) return E_MI_GFX_FMT_RGB565;
			if (surface->format->Amask == 0x8000) return E_MI_GFX_FMT_ARGB1555;
			if (surface->format->Amask == 0xF000) return E_MI_GFX_FMT_ARGB4444;
			if (surface->format->Amask == 0x0001) return E_MI_GFX_FMT_RGBA5551;
			if (surface->format->Amask == 0x000F) return E_MI_GFX_FMT_RGBA4444;
			return E_MI_GFX_FMT_RGB565;
		}
		if (surface->format->Bmask == 0x000000FF) return E_MI_GFX_FMT_ARGB8888;
		if (surface->format->Rmask == 0x000000FF) return E_MI_GFX_FMT_ABGR8888;
	}
	return E_MI_GFX_FMT_ARGB8888;
}

//
//	Get SYS_PixelFormat from SDL_Surface
//
static inline MI_SYS_PixelFormat_e	SYS_PixelFormat(SDL_Surface *surface) {
	if (surface) {
		if (surface->format->BytesPerPixel == 2) {
			if (surface->format->Amask == 0x0000) return E_MI_SYS_PIXEL_FRAME_RGB565;
			if (surface->format->Amask == 0x8000) return E_MI_SYS_PIXEL_FRAME_ARGB1555;
			if (surface->format->Amask == 0xF000) return E_MI_SYS_PIXEL_FRAME_ARGB4444;
			return E_MI_SYS_PIXEL_FRAME_RGB565;
		}
		if (surface->format->Bmask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_ARGB8888;
		if (surface->format->Rmask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_ABGR8888;
		if (surface->format->Amask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_BGRA8888;
	}
	return E_MI_SYS_PIXEL_FRAME_ARGB8888;
}


static int PrimClear()
{
    MI_GFX_Surface_t stDstSurface;
    MI_GFX_Rect_t stDstRect;
    MI_GFX_Opt_t stOpt;

    memset(&stDstSurface, 0, sizeof(MI_GFX_Surface_t));
    memset(&stDstRect, 0, sizeof(MI_GFX_Rect_t));
    memset(&stOpt, 0, sizeof(MI_GFX_Opt_t));

    stDstSurface.u32Width = nGameWidth;
    stDstSurface.u32Height = nGameHeight;
    stDstSurface.eColorFmt = GFX_ColorFmt(sdlsFramebuf);
    MI_PHY phyAddr;

    if (MI_SYS_MMA_Alloc(NULL, stDstSurface.u32Width * stDstSurface.u32Height * 4, &phyAddr) != MI_SUCCESS) {
        dprintf("Error: Could not allocate physical memory.\n");
        return 1;
    }

    stDstSurface.phyAddr = phyAddr;
    stDstRect.s32Xpos = 0;
    stDstRect.s32Ypos = 0;
    stDstRect.u32Width = nGameWidth;
    stDstRect.u32Height = nGameHeight;

    if (MI_GFX_QuickFill(&stDstSurface, &stDstRect, 0x00000000, NULL) != MI_SUCCESS) {
        dprintf("Error: Could not fill the screen with MI_GFX_QuickFill.\n");
        return 1;
    }

    return 0;
}



// Crear una superficie secundaria para la pantalla
static int BlitFXMakeSurf()
{
    sdlsBlitFX[0] = NULL;
    sdlsBlitFX[1] = NULL;

    // Asignar el buffer en la memoria de video si es posible
    if (nUseSys == 0) {
        sdlsBlitFX[0] = SDL_CreateRGBSurface(SDL_HWSURFACE, nGameWidth * nSize, nGameHeight * nSize, 
                                             sdlsFramebuf->format->BitsPerPixel, sdlsFramebuf->format->Rmask, 
                                             sdlsFramebuf->format->Gmask, sdlsFramebuf->format->Bmask, 
                                             sdlsFramebuf->format->Amask);
        if (sdlsBlitFX[0] == NULL) {
            nDirectAccess = 0;
            nUseSys = 1;
        }
    }

    if (nDirectAccess == 0) {
        sdlsBlitFX[1] = SDL_CreateRGBSurface(SDL_SWSURFACE, nGameWidth * nSize, nGameHeight * nSize, 
                                             sdlsFramebuf->format->BitsPerPixel, sdlsFramebuf->format->Rmask, 
                                             sdlsFramebuf->format->Gmask, sdlsFramebuf->format->Bmask, 
                                             sdlsFramebuf->format->Amask);
        if (sdlsBlitFX[1] == NULL) {
            return 1;
        }
    }

    // Llenar la superficie con negro
    if (PrimClear()) {
        return 1;
    }

    return 0;
}

// Liberar recursos
static int BlitFXExit()
{
    MI_GFX_Close();  // Cerrar subsistema de gráficos específico de mi_gfx
    MI_SYS_Exit();  // Cerrar el subsistema del sistema

    if (sdlsBlitFX[0]) {
        SDL_FreeSurface(sdlsBlitFX[0]);
        sdlsBlitFX[0] = NULL;
    }
    if (sdlsBlitFX[1]) {
        SDL_FreeSurface(sdlsBlitFX[1]);
        sdlsBlitFX[1] = NULL;
    }

    if (fd_fb) {
        close(fd_fb);
        fd_fb = 0;
    }

    if (fb_addr) {
        munmap(fb_addr, finfo.smem_len);
        fb_addr = NULL;
    }

    VidSFreeVidImage();
    nRotateGame = 0;
    return 0;
}

// Inicialización de efectos de Blit
static int BlitFXInit()
{
    if (nRotateGame & 1) {
        nVidImageWidth = nGameHeight;
        nVidImageHeight = nGameWidth;
    } else {
        nVidImageWidth = nGameWidth;
        nVidImageHeight = nGameHeight;
    }

    nVidImageDepth = sdlsFramebuf->format->BitsPerPixel;
    nVidImageBPP = sdlsFramebuf->format->BytesPerPixel;
    nBurnBpp = nVidImageBPP; // Establecer los bytes por pixel de la biblioteca Burn

    SetBurnHighCol(nVidImageDepth);

    if (VidSAllocVidImage()) {
        BlitFXExit();
        return 1;
    }

    if (BlitFXMakeSurf()) {
        BlitFXExit();
        return 1;
    }

    return 0;
}

// Función de salida
static int Exit()
{
    BlitFXExit();
    if (!(nInitedSubsytems & SDL_INIT_VIDEO)) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    nInitedSubsytems = 0;
    return 0;
}

// Inicialización de video
static int Init()
{
    nInitedSubsytems = SDL_WasInit(SDL_INIT_VIDEO);
    if (!(nInitedSubsytems & SDL_INIT_VIDEO)) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    if (InitMiyooGraphics() != 0) {
        return 1;  // Error al inicializar gráficos específicos de Miyoo Mini
    }

    nUseBlitter = nVidBlitterOpt[nVidSelect] & 0xFF;
    nGameWidth = nVidImageWidth; 
    nGameHeight = nVidImageHeight;
    nRotateGame = 0;

    if (bDrvOkay) {
        BurnDrvGetVisibleSize(&nGameWidth, &nGameHeight);
        if (BurnDrvGetFlags() & BDF_ORIENTATION_VERTICAL) {
            if (nVidRotationAdjust & 1) {
                int n = nGameWidth;
                nGameWidth = nGameHeight;
                nGameHeight = n;
                nRotateGame |= (nVidRotationAdjust & 2);
            } else {
                nRotateGame |= 1;
            }
        }
        if (BurnDrvGetFlags() & BDF_ORIENTATION_FLIPPED) {
            nRotateGame ^= 2;
        }
    }

    nSize = VidSoftFXGetZoom(nUseBlitter);
    bVidScanlines = 0;

    if (nVidFullscreen) {
        nVidScrnWidth = nVidWidth; 
        nVidScrnHeight = nVidHeight;
        if ((sdlsFramebuf = SDL_SetVideoMode(nVidWidth, nVidHeight, nVidDepth, 
                                             SDL_HWSURFACE | SDL_ANYFORMAT | SDL_DOUBLEBUF | SDL_FULLSCREEN)) == NULL) {
            printf("*** Couldn't enter fullscreen mode.\n");
            return 1;
        }
    } else {
        if ((sdlsFramebuf = SDL_SetVideoMode(nGameWidth * nSize, nGameHeight * nSize, 0, 
                                             SDL_SWSURFACE | SDL_DOUBLEBUF)) == NULL) {
            return 1;
        }
    }

    SDL_SetClipRect(sdlsFramebuf, NULL);

    BlitFXInit();

    if (VidSoftFXInit(nUseBlitter, nRotateGame)) {
        if (VidSoftFXInit(0, nRotateGame)) {
            Exit();
            return 1;
        }
    }

    return 0;
}

// Escalado de video
static int vidScale(RECT*, int, int)
{
    return 0;
}

// Transferencia de memoria a superficie
static int MemToSurf() {
    VidSoftFXApplyEffectSDL(sdlsBlitFX[1 ^ nDirectAccess]);

    if (nUseSys == 0 && nDirectAccess == 0) {
        if (SDL_LockSurface(sdlsBlitFX[1]) == 0) {
            uint8_t* Surf = (uint8_t*)sdlsBlitFX[1]->pixels;

            if (SDL_LockSurface(sdlsBlitFX[0]) == 0) {
                uint8_t* VidSurf = (uint8_t*)sdlsBlitFX[0]->pixels;

                MI_GFX_Surface_t stSrcSurface, stDstSurface;
                MI_GFX_Rect_t stSrcRect, stDstRect;
                MI_GFX_Opt_t stOpt;
                MI_U16 fence = 0;

                // Initialize and set up surfaces and rectangles
                memset(&stSrcSurface, 0, sizeof(MI_GFX_Surface_t));
                memset(&stDstSurface, 0, sizeof(MI_GFX_Surface_t));
                memset(&stSrcRect, 0, sizeof(MI_GFX_Rect_t));
                memset(&stDstRect, 0, sizeof(MI_GFX_Rect_t));
                memset(&stOpt, 0, sizeof(MI_GFX_Opt_t));

                // Configure source surface
                stSrcSurface.phyAddr = (MI_PHY)Surf;
                stSrcSurface.u32Width = sdlsBlitFX[1]->w;
                stSrcSurface.u32Height = sdlsBlitFX[1]->h;
                stSrcSurface.eColorFmt = GFX_ColorFmt(sdlsBlitFX[1]);

                // Configure destination surface
                stDstSurface.phyAddr = (MI_PHY)VidSurf;
                stDstSurface.u32Width = sdlsBlitFX[0]->w;
                stDstSurface.u32Height = sdlsBlitFX[0]->h;
                stDstSurface.eColorFmt = GFX_ColorFmt(sdlsBlitFX[0]);

                if (MI_GFX_BitBlit(&stSrcSurface, &stSrcRect, &stDstSurface, &stDstRect, &stOpt, &fence) != MI_SUCCESS) {
                    printf("Error: MI_GFX_BitBlit failed.\n");
                }

                SDL_UnlockSurface(sdlsBlitFX[0]);
            }
            SDL_UnlockSurface(sdlsBlitFX[1]);
        }
    }
    return 0;
}



// Procesar el frame de video
static int Frame(bool bRedraw)
{
    if (pVidImage == NULL) {
        return 1;
    }

    VidFrameCallback(bRedraw);
    MemToSurf();

    return 0;
}

// Pintar el video
static int Paint(int bValidate)
{
    SDL_Rect sdlrDest = { 0, 0, nGameWidth * nSize, nGameHeight * nSize };

    if (bValidate & 2) {
        MemToSurf();
    }

    if (nVidFullscreen) {
        sdlrDest.x = (nVidScrnWidth - nGameWidth * nSize) / 2;
        sdlrDest.y = (nVidScrnHeight - nGameHeight * nSize) / 2;

        MI_GFX_BitBlit((MI_GFX_Surface_t*)sdlsBlitFX[nUseSys], NULL, (MI_GFX_Surface_t*)sdlsFramebuf, NULL, NULL, 0);  // Blitting optimizado para hardware
    } else {
        if (SDL_BlitSurface(sdlsBlitFX[nUseSys], NULL, sdlsFramebuf, &sdlrDest)) {
            return 1;
        }
        SDL_UpdateRect(sdlsFramebuf, 0, 0, 0, 0);
    }

    return 0;
}

// Obtener configuraciones
static int GetSettings(InterfaceInfo* pInfo)
{
    TCHAR szString[MAX_PATH] = _T("");
    _sntprintf(szString, MAX_PATH, _T("Prescaling using %s (%i× zoom)"), VidSoftFXGetEffect(nUseBlitter), nSize);
    IntInfoAddStringModule(pInfo, szString);

    if (nRotateGame) {
        IntInfoAddStringModule(pInfo, _T("Using software rotation"));
    }
    return 0;
}

// Definición de la estructura VidOut
struct VidOut VidOutSDLFX = { Init, Exit, Frame, Paint, vidScale, GetSettings, _T("SDL Software Effects video output") };
