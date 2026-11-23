/* -*- mode: c; tab-width: 4; c-basic-offset: 4; c-file-style: "linux" -*- */
//
// Copyright (c) 2009-2011, Wei Mingzhi <whistler_wmz@users.sf.net>.
// Copyright (c) 2011-2026, SDLPAL development team.
// All rights reserved.
//
// This file is part of SDLPAL.
//
// SDLPAL is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <float.h>
#include "main.h"

// Screen buffer
SDL_Surface              *gpScreen           = NULL;

// Backup screen buffer
SDL_Surface              *gpScreenBak        = NULL;

// The global palette
static SDL_Palette       *gpPalette          = NULL;
static int g_iSatLUTDirty;   /* Saturn palette banks, see SAT_* at the end of the file */

#if SDL_VERSION_ATLEAST(2,0,0)
SDL_Window               *gpWindow           = NULL;
SDL_Renderer      *gpRenderer         = NULL;
SDL_Texture       *gpTexture          = NULL;
SDL_Texture       *gpTouchOverlay     = NULL;
SDL_Rect           gOverlayRect;
SDL_Rect           gTextureRect;

static struct RenderBackend {
    void (*Init)();
    void (*Setup)();
    SDL_Texture *(*CreateTexture)(int width, int height);
    void (*RenderCopy)();
} gRenderBackend;
#else
#undef PAL_HAS_GLSL
#endif

// The real screen surface
SDL_Surface       *gpScreenReal       = NULL;

volatile BOOL g_bRenderPaused = FALSE;

static BOOL bScaleScreen = PAL_SCALE_SCREEN;

// Shake times and level
static WORD               g_wShakeTime       = 0;
static WORD               g_wShakeLevel      = 0;

#if PAL_HAS_GLSL
#include "video_glsl.h"
#endif

#if SDL_VERSION_ATLEAST(3,0,0)
SDL_ScaleMode VIDEO_GetScaleMode() {
    static SDL_ScaleMode scaleMode = -1;
    if (scaleMode == -1) {
        if (gConfig.pszScaleQuality == NULL)
            scaleMode = SDL_SCALEMODE_NEAREST;
        else
            scaleMode = SDL_atoi(gConfig.pszScaleQuality);
    }
    return scaleMode;
}
#endif

#if SDL_VERSION_ATLEAST(2, 0, 0)
void VIDEO_SetupTouchArea(int window_w, int window_h, int draw_w, int draw_h)
{
	gOverlayRect.x = (window_w - draw_w) / 2;
	gOverlayRect.y = (window_h - draw_h) / 2;
	gOverlayRect.w = draw_w;
	gOverlayRect.h = draw_h;
#if PAL_HAS_TOUCH
	PAL_SetTouchBounds(window_w, window_h, gOverlayRect);
#endif
}

#if !SDL_VERSION_ATLEAST(3, 0, 0)
#define SDL_SoftStretch SDL_UpperBlit
#endif
static SDL_Texture *VIDEO_CreateTexture(int width, int height)
{
    SDL_Texture* texture = NULL;
	int texture_width, texture_height;
	double ratio = (double)width / (double)height;
	ratio *= (320.0 / PAL_SCREEN_H) * (double)gConfig.dwTextureHeight / (float)gConfig.dwTextureWidth;
	//
	// Check whether to keep the aspect ratio
	//
	if (gConfig.fKeepAspectRatio && fabs(ratio - 320.0 / PAL_SCREEN_H) > FLT_EPSILON)
	{
		if (ratio > 320.0 / PAL_SCREEN_H)
		{
			texture_height = PAL_SCREEN_H;
			texture_width = (int)(PAL_SCREEN_H * ratio) & ~0x3;
			ratio = (float)height / (float)PAL_SCREEN_H;
		}
		else
		{
			texture_width = 320;
			texture_height = (int)(320 / ratio) & ~0x3;
			ratio = (float)width / 320.0f;
		}

		WORD w = (WORD)(ratio * 320.0f) & ~0x3;
		WORD h = (WORD)(ratio * (float)PAL_SCREEN_H) & ~0x3;
		gTextureRect.x = (texture_width - 320) / 2;
		gTextureRect.y = (texture_height - PAL_SCREEN_H) / 2;
		gTextureRect.w = 320; gTextureRect.h = PAL_SCREEN_H;
		
		VIDEO_SetupTouchArea(width,height,w,h);
	}
	else
	{
		texture_width = 320;
		texture_height = PAL_SCREEN_H;
		gTextureRect.x = gTextureRect.y = 0;
		gTextureRect.w = 320; gTextureRect.h = PAL_SCREEN_H;
		
		VIDEO_SetupTouchArea(width,height,width,height);
	}

	//
	// Create texture for screen.
	//
	texture = SDL_CreateTexture(gpRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texture_width, texture_height);
#if SDL_VERSION_ATLEAST(3,0,0) && SDL_MINOR_VERSION < 3
    SDL_SetTextureScaleMode(texture, VIDEO_GetScaleMode());
#endif
    return texture;
}
#endif

void NullFunc() {}

INT
VIDEO_Startup(
   VOID
)
/*++
  Purpose:

    Initialze the video subsystem.

  Parameters:

    None.

  Return value:

    0 = success, -1 = fail to create the screen surface,
    -2 = fail to create screen buffer.

--*/
{
	extern SDL_Surface* STBIMG_Load(const char* file);
	extern char *dirname(char *path);
#if APPIMAGE
	SDL_Surface *surf = STBIMG_Load( PAL_va(0, "%s%s", dirname(dirname(dirname(gExecutablePath))), "/usr/share/icons/hicolor/256x256/apps/sdlpal.png" ) );
#endif

#if SDL_VERSION_ATLEAST(2,0,0)
   int render_w, render_h;

   gRenderBackend.Init = NullFunc;
   gRenderBackend.Setup = NullFunc;
   gRenderBackend.CreateTexture = VIDEO_CreateTexture;
   gRenderBackend.RenderCopy = VIDEO_RenderCopy;

#if PAL_HAS_GLSL
   if( gConfig.fEnableGLSL) {
	   gRenderBackend.Init = VIDEO_GLSL_Init;
	   gRenderBackend.Setup = VIDEO_GLSL_Setup;
	   gRenderBackend.CreateTexture = VIDEO_GLSL_CreateTexture;
	   gRenderBackend.RenderCopy = VIDEO_GLSL_RenderCopy;
   }
#endif
	
   gRenderBackend.Init();

   //
   // Before we can render anything, we need a window and a renderer.
   //
   if (gpWindow == NULL)
#if SDL_VERSION_ATLEAST(3,0,0)
       gpWindow = SDL_CreateWindow("Pal", gConfig.dwScreenWidth, gConfig.dwScreenHeight, PAL_VIDEO_INIT_FLAGS);
   //TODO: Figure Whether Borderless Support Needed?
#else
       gpWindow = SDL_CreateWindow("Pal", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               gConfig.dwScreenWidth, gConfig.dwScreenHeight, PAL_VIDEO_INIT_FLAGS);
#endif

   if (gpWindow == NULL)
   {
      return -1;
   }

# if APPIMAGE
	if(surf){
		SDL_SetWindowIcon(gpWindow, surf);
	}
# endif

#if SDL_VERSION_ATLEAST(3,0,0)
    gpRenderer = SDL_CreateRenderer(gpWindow, NULL);
    UTIL_LogOutput(LOGLEVEL_DEBUG, "renderer backend:%s\n", SDL_GetRendererName(gpRenderer));
    UTIL_LogOutput(LOGLEVEL_DEBUG, "Trying to enable VRR\n");
    if (SDL_SetRenderVSync(gpRenderer, SDL_RENDERER_VSYNC_ADAPTIVE) != SDL_OK) {
        UTIL_LogOutput(LOGLEVEL_DEBUG, "Failed, fallback to vsync\n");
        SDL_SetRenderVSync(gpRenderer, 1);
    }
    else {
        UTIL_LogOutput(LOGLEVEL_DEBUG, "Got VRR works\n");
    }
#else
   gpRenderer = SDL_CreateRenderer(gpWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif

#if SDL_VERSION_ATLEAST(3,3,0)
   SDL_SetDefaultTextureScaleMode(gpRenderer, VIDEO_GetScaleMode());
#endif

   gRenderBackend.Setup();

   if (gpRenderer == NULL)
   {
      return -1;
   }

#if defined (__IOS__)
   SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
   SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 1);
#endif

   //
   // Create the screen buffer and the backup screen buffer.
   //
   gpScreen = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, PAL_SCREEN_H, 8, 0, 0, 0, 0);
   gpScreenBak = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, PAL_SCREEN_H, 8, 0, 0, 0, 0);
   gpScreenReal = SDL_CreateRGBSurface(SDL_SWSURFACE, 320, PAL_SCREEN_H, 32,
                                       0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

   //
   // Create texture for screen.
   //
   SDL_GetRendererOutputSize(gpRenderer, &render_w, &render_h);
#if SDL_MAJOR_VERSION<3
   if(!gConfig.fEnableGLSL)
      SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, gConfig.pszScaleQuality);
#endif
   gpTexture = gRenderBackend.CreateTexture(render_w, render_h);
#if SDL_MAJOR_VERSION<3
   SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif

   //
   // Create palette object
   //
   gpPalette = SDL_AllocPalette(256);

   //
   // Failed?
   //
   if (gpScreen == NULL || gpScreenBak == NULL || gpScreenReal == NULL || gpTexture == NULL || gpPalette == NULL)
   {
      VIDEO_Shutdown();
      return -2;
   }

   //
   // Create texture for overlay.
   //
   if (gConfig.fUseTouchOverlay)
   {
      extern const unsigned char bmpData[];
      extern unsigned int bmpLen;

      void *bmp = UTIL_malloc(bmpLen);
      YJ1_Decompress(bmpData, bmp, bmpLen);
      SDL_Surface *overlay = SDL_LoadBMP_RW(SDL_RWFromConstMem(bmp, bmpLen), 1);
      free(bmp);

      if (overlay != NULL)
      {
#if SDL_VERSION_ATLEAST(3,0,0)
         SDL_SetSurfaceColorKey(overlay, true, SDL_MapRGB(SDL_GetPixelFormatDetails(overlay->format), gpPalette, 255, 0, 255));
#else
         SDL_SetColorKey(overlay, SDL_RLEACCEL, SDL_MapRGB(overlay->format, 255, 0, 255));
#endif
#if SDL_MAJOR_VERSION<3
         SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, gConfig.pszScaleQuality);
#endif
         gpTouchOverlay = SDL_CreateTextureFromSurface(gpRenderer, overlay);
#if SDL_VERSION_ATLEAST(3,0,0) && SDL_MINOR_VERSION < 3
         SDL_SetTextureScaleMode(gpTouchOverlay, VIDEO_GetScaleMode());
#endif
#if SDL_MAJOR_VERSION<3
         SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
         SDL_SetTextureAlphaMod(gpTouchOverlay, TOUCHOVERLAY_ALPHAMOD );
         SDL_FreeSurface(overlay);
      }
   }
# if PAL_HAS_GLSL
	// notice: power of 2
#  define PIXELS 1
	// We need a total empty texture in case of not using touch overlay.
	// Or GL runtime will pick the previous texture - the main screen itself
	// and reuse it - that makes color seems overexposed
	else if( gConfig.fEnableGLSL )
	{
		BYTE pixels[4*PIXELS*PIXELS];
		memset(pixels, 0, sizeof(pixels));
		SDL_Surface *temp = SDL_CreateRGBSurfaceFrom(pixels, PIXELS, PIXELS, 32, 4*PIXELS, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
		gpTouchOverlay = SDL_CreateTextureFromSurface(gpRenderer, temp);
		SDL_FreeSurface(temp);
	}
# endif
#else

# if APPIMAGE
	if(surf){
		SDL_WM_SetIcon(surf, NULL);
	}
# endif

   //
   // Create the screen surface.
   //
   gpScreenReal = SDL_SetVideoMode(gConfig.dwScreenWidth, gConfig.dwScreenHeight, 8, PAL_VIDEO_INIT_FLAGS);

   if (gpScreenReal == NULL)
   {
      //
      // Fall back to 640x480 software mode.
      //
      gpScreenReal = SDL_SetVideoMode(640, 480, 8,
         SDL_SWSURFACE | (gConfig.fFullScreen ? SDL_FULLSCREEN : 0));
   }

   //
   // Still fail?
   //
   if (gpScreenReal == NULL)
   {
      return -1;
   }

   gpPalette = gpScreenReal->format->palette;

   //
   // Create the screen buffer and the backup screen buffer.
   //
   gpScreen = SDL_CreateRGBSurface(gpScreenReal->flags & ~SDL_HWSURFACE, 320, PAL_SCREEN_H, 8,
      gpScreenReal->format->Rmask, gpScreenReal->format->Gmask,
      gpScreenReal->format->Bmask, gpScreenReal->format->Amask);

   gpScreenBak = SDL_CreateRGBSurface(gpScreenReal->flags & ~SDL_HWSURFACE, 320, PAL_SCREEN_H, 8,
      gpScreenReal->format->Rmask, gpScreenReal->format->Gmask,
      gpScreenReal->format->Bmask, gpScreenReal->format->Amask);

   //
   // Failed?
   //
   if (gpScreen == NULL || gpScreenBak == NULL)
   {
      VIDEO_Shutdown();
      return -2;
   }

   if (gConfig.fFullScreen)
   {
      SDL_ShowCursor(FALSE);
   }

#endif

#if APPIMAGE
	if(surf)
		SDL_FreeSurface(surf);
#endif

   return 0;
}

VOID
VIDEO_Shutdown(
   VOID
)
/*++
  Purpose:

    Shutdown the video subsystem.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if PAL_HAS_GLSL
    // since gConfig is cleared already we'd to detect on side effects
	if( gRenderBackend.Init == VIDEO_GLSL_Init ) {
		VIDEO_GLSL_Destroy();
	}
#endif

   if (gpScreen != NULL)
   {
      SDL_FreeSurface(gpScreen);
   }
   gpScreen = NULL;

   if (gpScreenBak != NULL)
   {
      SDL_FreeSurface(gpScreenBak);
   }
   gpScreenBak = NULL;

#if SDL_VERSION_ATLEAST(2,0,0)

   if (gpTouchOverlay)
   {
      SDL_DestroyTexture(gpTouchOverlay);
   }
   gpTouchOverlay = NULL;

   if (gpTexture)
   {
	  SDL_DestroyTexture(gpTexture);
   }
   gpTexture = NULL;

   if (gpRenderer)
   {
      SDL_DestroyRenderer(gpRenderer);
   }
   gpRenderer = NULL;

   if (gpWindow)
   {
      SDL_DestroyWindow(gpWindow);
   }
   gpWindow = NULL;

   if (gpPalette)
   {
      SDL_FreePalette(gpPalette);
   }
#endif
   gpPalette = NULL;

   if (gpScreenReal != NULL)
   {
      SDL_FreeSurface(gpScreenReal);
   }
   gpScreenReal = NULL;
}

static int g_iHdPalLevel = 255;   /* HD twin brightness wanted by the current palette (0..255) */
static int g_iHdPalShown = 255;   /* brightness of the last HD present */

#if SDL_VERSION_ATLEAST(2,0,0)
VOID VIDEO_RenderCopy(VOID);

static VOID
VIDEO_HdPalLevel(
   SDL_Color        rgPalette[256]
)
{  /* HD twin brightness follows the palette. The Saturn fades only step the palette, which the RGB twin
      does not see: it stayed at full brightness through a "black" screen, so the old map redrawn at the
      new position during a scene change flashed up. Level = how far the palette is dimmed from the last
      one fetched from pat.mkf (0 when all black; 255 when it is not a plain dimming of it). A large jump
      is tweened over up to 240 ms so the cut to/from black is a short smooth fade. */
   extern SDL_Color g_rgHdRefPalette[256]; extern int g_fHdRefPalette;
   extern int PAL_HDM_On(void); extern int PAL_HDM_IsStale(void); extern int PAL_HDM_GetFade(void);
   int i, lvl = 255; long sc = 0, sr = 0;
   for (i = 0; i < 256; i++) sc += rgPalette[i].r + rgPalette[i].g + rgPalette[i].b;
   if (sc == 0) lvl = 0;
   else if (g_fHdRefPalette)
   {
      for (i = 0; i < 256; i++) sr += g_rgHdRefPalette[i].r + g_rgHdRefPalette[i].g + g_rgHdRefPalette[i].b;
      if (sr > 0 && sc < sr)
      {
         int ok = 1;
         for (i = 0; i < 256 && ok; i++)
         {
            int dr = (int)rgPalette[i].r - (int)((long)g_rgHdRefPalette[i].r * sc / sr);
            int dg = (int)rgPalette[i].g - (int)((long)g_rgHdRefPalette[i].g * sc / sr);
            int db = (int)rgPalette[i].b - (int)((long)g_rgHdRefPalette[i].b * sc / sr);
            if (dr < -20 || dr > 20 || dg < -20 || dg > 20 || db < -20 || db > 20) ok = 0;
         }
         if (ok) lvl = (int)(sc * 255 / sr);
      }
   }
   if (PAL_HDM_On() && !PAL_HDM_IsStale() && PAL_HDM_GetFade() < 0 && gpRenderer != NULL)
   {
      int from = g_iHdPalShown, d = lvl - from, dur;
      if (d < 0) d = -d;
      dur = 240 * d / 255;
      if (d > 24 && dur >= 16 && !SDL_getenv("PAL_NOHDTWEEN"))
      {
         Uint64 t0 = SDL_GetTicks(), e;
         while ((e = SDL_GetTicks() - t0) < (Uint64)dur)
         {
            g_iHdPalLevel = from + (lvl - from) * (int)e / dur;
            VIDEO_RenderCopy();
            SDL_Delay(8);
         }
      }
   }
   g_iHdPalLevel = lvl;
}

static VOID
VIDEO_DrawAutomap(
   SDL_Surface *pDump
)
{  /* user request: the automap (play.c) floats at the top right of the presented frame, see-through like the
      Diablo automap. A texture of its own laid over the frame: the game picture and the HD twin stay as they are.
      pDump (debug frame dumps only): also blend it into this copy of the frame. */
   extern SDL_Surface *PAL_AutomapGet(int *piSerial);
   static SDL_Texture *s_tex = NULL; static int s_shown = -1;
   int serial = 0, ow = 0, oh = 0;
   SDL_Surface *am = PAL_AutomapGet(&serial);
   if (am == NULL || gpRenderer == NULL) return;
   if (s_tex == NULL)
   {
      s_tex = SDL_CreateTexture(gpRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, am->w, am->h);
      if (s_tex == NULL) return;
      SDL_SetTextureBlendMode(s_tex, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(s_tex, SDL_SCALEMODE_LINEAR);
      SDL_SetTextureAlphaMod(s_tex, 215);
      s_shown = -1;
   }
   if (serial != s_shown) { SDL_UpdateTexture(s_tex, NULL, am->pixels, am->pitch); s_shown = serial; }
   SDL_GetCurrentRenderOutputSize(gpRenderer, &ow, &oh);
   if (ow > 0 && oh > 0)
   {
      SDL_FRect dst;
      dst.w = (float)ow * 0.32f; dst.h = dst.w * (float)am->h / (float)am->w;
      dst.x = (float)ow - dst.w - (float)ow * 0.008f; dst.y = (float)oh * 0.012f;
      SDL_RenderTexture(gpRenderer, s_tex, NULL, &dst);
   }
   if (pDump != NULL)
   {
      SDL_Rect d; d.w = pDump->w * 32 / 100; d.h = d.w * am->h / am->w; d.x = pDump->w - d.w - pDump->w * 8 / 1000; d.y = pDump->h * 200 / 224 * 12 / 1000;
      SDL_SetSurfaceBlendMode(am, SDL_BLENDMODE_BLEND);
      SDL_BlitSurfaceScaled(am, NULL, pDump, &d, SDL_SCALEMODE_LINEAR);
   }
}

VOID
VIDEO_RenderCopy(
   VOID
)
{
	void *texture_pixels;
	int texture_pitch;

	{  // HD mode: present the ×6 HD-composed scene to the window instead of the 320-wide picture.
	   // Only on frames that composed an HD scene (menus/fades fall through to normal low-res present).
	   extern SDL_Surface *PAL_HD_GetScreen(void);
	   extern int PAL_HD_ConsumeFresh(void);
	   static SDL_Texture *s_hdTex = NULL;
	   static int s_hdFull = -1;
	   if (s_hdFull < 0) s_hdFull = SDL_getenv("PAL_HDFULL") ? 1 : 0;
	   extern int PAL_HDM_ConsumeDirty(void);
	   extern int PAL_HDM_GetFade(void);
	   extern int PAL_HDM_IsStale(void);
	   extern int g_hdLoading;   /* 1 while a scene's HD assets stream in -> show low-res */
	   SDL_Surface *hd = (s_hdFull || SDL_getenv("PAL_HDTILES")) ? PAL_HD_GetScreen() : NULL;
	   /* Present the HD twin whenever it reflects the current scene (not stale), on EVERY frame — not only
	      the frames that redrew it. Non-redraw frames (dialogue waits, the gaps between walk steps) used to
	      fall through to the blurry low-res present, so each dialogue advance / step flickered crisp<->blurry.
	      A movie/AVI blits a raw frame to gpScreenReal and marks the twin stale, so those still use the
	      low-res present; a fade presents the twin dimmed (hdFade>=0). Keyed (non-full) mode is unchanged. */
	   int hdFade  = (hd != NULL && s_hdFull) ? PAL_HDM_GetFade() : -1;
	   int hdDirty = (hd != NULL) && (s_hdFull ? PAL_HDM_ConsumeDirty() : PAL_HD_ConsumeFresh());
	   int hdShow  = (hd != NULL) && (s_hdFull ? (!PAL_HDM_IsStale() || hdFade >= 0) : hdDirty);
	   /* (g_hdLoading no longer forces low-res: the scene now composes the HD twin with the ×6-nearest
	      fallback while its AI-HD assets stream in, so a scene load stays crisp HD instead of blurry low-res.) */
	   (void)g_hdLoading;
	   if (hdShow)
	   {
	      int hdNew = 0;
	      if (s_hdTex == NULL)
	         { s_hdTex = SDL_CreateTexture(gpRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, hd->w, hd->h); hdNew = 1; }
	      if (s_hdTex != NULL)
	      {
	         Uint8 v = (hdFade >= 0) ? (Uint8)(((hdFade > 64 ? 64 : hdFade) * 255) / 64) : (Uint8)g_iHdPalLevel; g_iHdPalShown = v;
	         if (hdDirty || hdNew || !s_hdFull) {
            /* #7: the Saturn only displayed work-area lines SAT_WINDOW_Y..SAT_WINDOW_Y_END (12..212); HD-full
               was showing the ~24 overscan rows below that, which revealed the next room at a room's bottom
               edge. Black the overscan band so the HD view matches the Saturn's visible window. (Top stays at
               line 0 -- no vertical shift -- so this does not revive the dialog-advance screen-shake.) */
            if (s_hdFull) { int ovy = hd->h * (SAT_WINDOW_Y_END - SAT_WINDOW_Y) / 224;
               if (ovy > 0 && ovy < hd->h) { SDL_Rect ov = { 0, ovy, hd->w, hd->h - ovy }; SDL_FillRect(hd, &ov, 0xFF000000u); } }
            SDL_UpdateTexture(s_hdTex, NULL, hd->pixels, hd->pitch);  /* re-upload only when the twin changed */
         }
	         SDL_SetTextureColorMod(s_hdTex, v, v, v);   /* dim the HD twin to match the fade step */
	         SDL_RenderClear(gpRenderer);
	         {  /* show only the Saturn work area (the rows above the blacked overscan band) across the whole window:
	            with a 16:10 window (1920x1200) there is no black strip under the picture */
	            int ovh = s_hdFull ? hd->h * (SAT_WINDOW_Y_END - SAT_WINDOW_Y) / 224 : hd->h;
#if SDL_VERSION_ATLEAST(3,0,0)
	            SDL_FRect srcr = { 0, 0, (float)hd->w, (float)ovh };
	            SDL_RenderTexture(gpRenderer, s_hdTex, &srcr, NULL);
#else
	            SDL_Rect srcr = { 0, 0, hd->w, ovh };
	            SDL_RenderCopy(gpRenderer, s_hdTex, &srcr, NULL);
#endif
	         }
	         VIDEO_DrawAutomap(NULL);
	         SDL_RenderPresent(gpRenderer);
	         {  // debug: env PAL_HDSEQ=ms dumps the HD twin itself (works with a hidden / dummy window) to <PAL_SHOTDIR>/hds####.bmp
	            static int sq = -1, sn = 0; static Uint64 sl = 0; const char *sd4 = SDL_getenv("PAL_SHOTDIR");
	            if (sq < 0) sq = SDL_getenv("PAL_HDSEQ") ? SDL_atoi(SDL_getenv("PAL_HDSEQ")) + 1 : 0;
	            if (sq && sd4 && SDL_GetTicks() - sl >= (Uint64)(sq - 1)) { char p4[512]; FILE *f4;
	               SDL_snprintf(p4, sizeof p4, "%s/hds%04d.bmp", sd4, sn);
	               { SDL_Surface *c4 = SDL_DuplicateSurface(hd); if (c4) { VIDEO_DrawAutomap(c4); SDL_SaveBMP(c4, p4); SDL_DestroySurface(c4); } }
	               SDL_snprintf(p4, sizeof p4, "%s/hds.txt", sd4); f4 = fopen(p4, "a");
	               if (f4) { fprintf(f4, "%d %u\n", sn, (unsigned)SDL_GetTicks()); fclose(f4); }
	               sn++; sl = SDL_GetTicks(); }
	         }
	         if (SDL_getenv("PAL_SHOTALL")) {  // debug: dump every HD-presented window frame
	            static int hn = 0; const char *sd2 = SDL_getenv("PAL_SHOTDIR");
	            const char *sf = SDL_getenv("PAL_SHOTFROM"), *st = SDL_getenv("PAL_SHOTTO");   /* optional window in ms */
	            if (sf && SDL_GetTicks() < (Uint64)SDL_atoi(sf)) sd2 = NULL;
	            if (st && SDL_GetTicks() > (Uint64)SDL_atoi(st)) sd2 = NULL;
	            if (sd2) { SDL_Surface *c2 = SDL_RenderReadPixels(gpRenderer, NULL);
	               if (c2) { char pp[512]; SDL_snprintf(pp, sizeof pp, "%s/hdw%04d.bmp", sd2, hn++);
	                  SDL_SaveBMP(c2, pp); SDL_DestroySurface(c2); } }
	         }  /* kept: used to verify the HD opening fixes; gated on PAL_SHOTALL */
	         {  // verify: read back the actual window output (throttled) to <PAL_SHOTDIR>/hd_window.bmp
	            static Uint64 last = 0; const char *sdir = SDL_getenv("PAL_SHOTDIR");
	            if (sdir && SDL_GetTicks() - last >= 500u)
	            {
	               SDL_Surface *cap = SDL_RenderReadPixels(gpRenderer, NULL);   /* SDL3: returns a new surface */
	               if (cap) { char p[512]; SDL_snprintf(p, sizeof p, "%s/hd_window.bmp", sdir);
	                  SDL_SaveBMP(cap, p); SDL_DestroySurface(cap); last = SDL_GetTicks(); }
	               { char p2[512]; SDL_snprintf(p2, sizeof p2, "%s/normal_sameframe.bmp", sdir); SDL_SaveBMP(gpScreenReal, p2); }
	            }
	         }
	         return;
	      }
	   }
	   {  // in HD mode, non-scene frames (menus/fades/other scenes) upscale smoothly, not blocky
	      static int s_lin = 0;
	      if (!s_lin && hd != NULL && gpTexture != NULL) { SDL_SetTextureScaleMode(gpTexture, SDL_SCALEMODE_LINEAR); s_lin = 1; }
	   }
	}

	{  // debug: PAL_HDSEQ also numbers the frames shown without the HD twin (status screen, shops...) as lo####.bmp
	   static int lq = -1, ln = 0; static Uint64 ll = 0; const char *sd5 = SDL_getenv("PAL_SHOTDIR");
	   if (lq < 0) lq = SDL_getenv("PAL_HDSEQ") ? SDL_atoi(SDL_getenv("PAL_HDSEQ")) + 1 : 0;
	   if (lq && sd5 && SDL_GetTicks() - ll >= (Uint64)(lq - 1)) { char p5[512]; SDL_snprintf(p5, sizeof p5, "%s/hds%04d.bmp", sd5, 5000 + ln++); SDL_SaveBMP(gpScreenReal, p5); ll = SDL_GetTicks(); }
	}
	SDL_LockTexture(gpTexture, NULL, &texture_pixels, &texture_pitch);
	memset(texture_pixels, 0, gTextureRect.y * texture_pitch);
	uint8_t *pixels = (uint8_t *)texture_pixels + gTextureRect.y * texture_pitch;
	uint8_t *src = (uint8_t *)gpScreenReal->pixels;
	int left_pitch = gTextureRect.x << 2;
	int right_pitch = texture_pitch - ((gTextureRect.x + gTextureRect.w) << 2);
	/* Saturn display (verified on the original in an emulator, VDP2 regs at a dialog):
	   window 0 = lines 12..212 (WPSY0 0x0C, WPEY0 0xD4), UI bitmap NBG1 scrolled SCYIN1 = -12.
	   So the game's 320x200 work area (gpScreen rows 0..200) is shown at screen lines 12..212,
	   everything else is black. */
	static SDL_Surface *shot = NULL;
	if (shot == NULL) shot = SDL_CreateRGBSurface(0, 320, gTextureRect.h, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
	/* In HD-full mode the HD present fills the window with no letterbox. So when a frame falls through
	   to THIS low-res present (dialog wait frames, movies, fades), fill the same way instead of the
	   12-line Saturn letterbox — otherwise the picture jumps vertically every time the two presents
	   alternate (the "screen shakes on every dialog Enter" bug). */
	static int s_hdFull2 = -1;
	if (s_hdFull2 < 0) s_hdFull2 = SDL_getenv("PAL_HDFULL") ? 1 : 0;
	int woff = s_hdFull2 ? 0 : SAT_WINDOW_Y;
	/* #7: match the HD present -- in HD-full show only the work-area rows (top-aligned), black the overscan
	   band below, instead of the full buffer, so the next room no longer peeks in at a room's bottom edge. */
	int wend = s_hdFull2 ? (SAT_WINDOW_Y_END - SAT_WINDOW_Y) : SAT_WINDOW_Y_END;
	for (int y = 0; y < gTextureRect.h; y++)
	{
		int sy = y - woff;
		uint8_t *row = (uint8_t *)shot->pixels + y * shot->pitch;
		if (sy >= 0 && y <= wend && sy < gpScreenReal->h)
			memcpy(row, src + sy * gpScreenReal->pitch, 320 << 2);
		else
			memset(row, 0, 320 << 2);
		memset(pixels, 0, left_pitch); pixels += left_pitch;
		memcpy(pixels, row, 320 << 2); pixels += 320 << 2;
		memset(pixels, 0, right_pitch); pixels += right_pitch;
	}
	{  // Saturn port debug: periodic frame dump of the final picture (env PAL_SHOTDIR)
		static const char *shotdir = (const char *)-1; static Uint64 last = 0; static int n = 0;
		if (shotdir == (const char *)-1) shotdir = SDL_getenv("PAL_SHOTDIR");
		static int every = -1; if (every < 0) every = SDL_getenv("PAL_SHOTALL") ? 1 : 0;
		static int live = -1; if (live < 0) live = SDL_getenv("PAL_KEYFILE") ? 1 : 0;
		if (shotdir && (every || SDL_GetTicks() - last >= (live ? 200u : 500u))) {
			char path[512]; last = SDL_GetTicks();
			if (live) SDL_snprintf(path, sizeof(path), "%s/live.bmp", shotdir);
			else SDL_snprintf(path, sizeof(path), "%s/f%04d.bmp", shotdir, n++);
			SDL_SaveBMP(shot, path);
			{ static FILE *ft = NULL; if (!ft) { SDL_snprintf(path, sizeof(path), "%s/t.txt", shotdir); ft = fopen(path, "w"); }
			  if (ft) { fprintf(ft, "%d %u\n", n - 1, SDL_GetTicks()); fflush(ft); } }
			{ static FILE *fv = NULL; if (!fv) { SDL_snprintf(path, sizeof(path), "%s/v.txt", shotdir); fv = fopen(path, "w"); }
			  if (fv) { fprintf(fv, "%d %u %d %d\n", n - 1, SDL_GetTicks(), PAL_X(gpGlobals->viewport), PAL_Y(gpGlobals->viewport)); fflush(fv); } }
		}
	}
	memset(pixels, 0, gTextureRect.y * texture_pitch);
	SDL_UnlockTexture(gpTexture);

	SDL_RenderClear(gpRenderer);
	if (s_hdFull2)
	{  /* same crop as the HD present: work-area rows only, stretched over the whole window */
#if SDL_VERSION_ATLEAST(3,0,0)
		SDL_FRect srcr = { (float)gTextureRect.x, (float)gTextureRect.y, 320.0f, (float)(SAT_WINDOW_Y_END - SAT_WINDOW_Y) };
		SDL_RenderTexture(gpRenderer, gpTexture, &srcr, NULL);
#else
		SDL_Rect srcr = { gTextureRect.x, gTextureRect.y, 320, SAT_WINDOW_Y_END - SAT_WINDOW_Y };
		SDL_RenderCopy(gpRenderer, gpTexture, &srcr, NULL);
#endif
	}
	else
	SDL_RenderCopy(gpRenderer, gpTexture, NULL, NULL);
	if (gConfig.fUseTouchOverlay)
	{
#if SDL_VERSION_ATLEAST(3,0,0)
        SDL_FRect fOverlayRect = { gOverlayRect.x, gOverlayRect.y, gOverlayRect.w, gOverlayRect.h };
        SDL_RenderTexture(gpRenderer, gpTouchOverlay, NULL, &fOverlayRect);
#else
		SDL_RenderCopy(gpRenderer, gpTouchOverlay, NULL, &gOverlayRect);
#endif
	}
	VIDEO_DrawAutomap(NULL);
	SDL_RenderPresent(gpRenderer);
	if (SDL_getenv("PAL_SHOTALL")) {  // debug: dump every LOW-RES (fallback) window frame -> fbw####
	   static int fn = 0; const char *sd3 = SDL_getenv("PAL_SHOTDIR");
	   if (sd3) { SDL_Surface *c3 = SDL_RenderReadPixels(gpRenderer, NULL);
	      if (c3) { char p3[512]; SDL_snprintf(p3, sizeof p3, "%s/fbw%04d.bmp", sd3, fn++);
	         SDL_SaveBMP(c3, p3); SDL_DestroySurface(c3); } }
	}
}
#endif

VOID
VIDEO_UpdateScreen(
   const SDL_Rect  *lpRect
)
/*++
  Purpose:

    Update the screen area specified by lpRect.

  Parameters:

    [IN]  lpRect - Screen area to update.

  Return value:

    None.

--*/
{
   SDL_Rect        srcrect, dstrect;
   short           offset = 240 - 200;
   short           screenRealHeight = gpScreenReal->h;
   short           screenRealY = 0;

   {  // debug: env PAL_LOSEQ=ms numbers the low-res screen itself (every screen, HD twin or not) as <PAL_SHOTDIR>/lo####.bmp
      static int lq = -1, ln = 0; static Uint64 ll = 0; const char *sd6 = SDL_getenv("PAL_SHOTDIR");
      if (lq < 0) lq = SDL_getenv("PAL_LOSEQ") ? SDL_atoi(SDL_getenv("PAL_LOSEQ")) + 1 : 0;
      if (lq && sd6 && SDL_GetTicks() - ll >= (Uint64)(lq - 1)) { char p6[512]; SDL_snprintf(p6, sizeof p6, "%s/lo%04d.bmp", sd6, ln++); SDL_SaveBMP(gpScreen, p6); ll = SDL_GetTicks(); }
   }

#if SDL_VERSION_ATLEAST(2,0,0)
   if (g_bRenderPaused)
   {
	   return;
   }
#endif


   //
   // Lock surface if needed
   //
   if (SDL_MUSTLOCK(gpScreenReal))
   {
      if (SDL_LockSurface(gpScreenReal) < 0)
         return;
   }

   if (!bScaleScreen)
   {
      screenRealHeight -= offset;
      screenRealY = offset / 2;
   }

   if (lpRect != NULL)
   {
      dstrect.x = (SHORT)((INT)(lpRect->x) * gpScreenReal->w / gpScreen->w);
      dstrect.y = (SHORT)((INT)(screenRealY + lpRect->y) * screenRealHeight / gpScreen->h);
      dstrect.w = (WORD)((DWORD)(lpRect->w) * gpScreenReal->w / gpScreen->w);
      dstrect.h = (WORD)((DWORD)(lpRect->h) * screenRealHeight / gpScreen->h);

      SDL_SoftStretch(gpScreen, (SDL_Rect *)lpRect, gpScreenReal, &dstrect);
      SAT_ApplyBanks(gpScreen, lpRect->y, dstrect.y, lpRect->h, lpRect->x, lpRect->x + lpRect->w);
   }
   else if (g_wShakeTime != 0)
   {
      //
      // Shake the screen
      //
      srcrect.x = 0;
      srcrect.y = 0;
      srcrect.w = 320;
      srcrect.h = PAL_SCREEN_H - g_wShakeLevel;

      dstrect.x = 0;
      dstrect.y = screenRealY;
      dstrect.w = 320 * gpScreenReal->w / gpScreen->w;
      dstrect.h = (PAL_SCREEN_H - g_wShakeLevel) * screenRealHeight / gpScreen->h;

      if (g_wShakeTime & 1)
      {
         srcrect.y = g_wShakeLevel;
      }
      else
      {
         dstrect.y = (screenRealY + g_wShakeLevel) * screenRealHeight / gpScreen->h;
      }

      SDL_SoftStretch(gpScreen, &srcrect, gpScreenReal, &dstrect);
      SAT_ApplyBanks(gpScreen, srcrect.y, dstrect.y, srcrect.h, 0, 320);

      if (g_wShakeTime & 1)
      {
         dstrect.y = (screenRealY + screenRealHeight - g_wShakeLevel) * screenRealHeight / gpScreen->h;
      }
      else
      {
         dstrect.y = screenRealY;
      }

      dstrect.h = g_wShakeLevel * screenRealHeight / gpScreen->h;

      SDL_FillRect(gpScreenReal, &dstrect, 0);

#if SDL_MAJOR_VERSION == 1 && SDL_MINOR_VERSION <= 2
      dstrect.x = dstrect.y = 0;
      dstrect.w = gpScreenReal->w;
      dstrect.h = gpScreenReal->h;
#endif
      g_wShakeTime--;
   }
   else
   {
      dstrect.x = 0;
      dstrect.y = screenRealY;
      dstrect.w = gpScreenReal->w;
      dstrect.h = screenRealHeight;

      SDL_SoftStretch(gpScreen, NULL, gpScreenReal, &dstrect);
      SAT_ApplyBanks(gpScreen, 0, screenRealY, gpScreen->h, 0, 320);

#if SDL_MAJOR_VERSION == 1 && SDL_MINOR_VERSION <= 2
      dstrect.x = dstrect.y = 0;
      dstrect.w = gpScreenReal->w;
      dstrect.h = gpScreenReal->h;
#endif
   }

#if SDL_VERSION_ATLEAST(2,0,0)
   gRenderBackend.RenderCopy();
#else
   SDL_UpdateRect(gpScreenReal, dstrect.x, dstrect.y, dstrect.w, dstrect.h);
#endif

   if (SDL_MUSTLOCK(gpScreenReal))
   {
	   SDL_UnlockSurface(gpScreenReal);
   }
}

VOID
VIDEO_SetPalette(
   SDL_Color        rgPalette[256]
)
/*++
  Purpose:

    Set the palette of the screen.

  Parameters:

    [IN]  rgPalette - array of 256 colors.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2,0,0)
   SDL_Rect rect;

   SDL_SetPaletteColors(gpPalette, rgPalette, 0, 256);
   g_iSatLUTDirty = 1;

   SDL_SetSurfacePalette(gpScreen, gpPalette);
   SDL_SetSurfacePalette(gpScreenBak, gpPalette);

   //
   // HACKHACK: need to invalidate gpScreen->map otherwise the palette
   // would not be effective during blit
   //
   SDL_SetSurfaceColorMod(gpScreen, 0, 0, 0);
   SDL_SetSurfaceColorMod(gpScreen, 0xFF, 0xFF, 0xFF);
   SDL_SetSurfaceColorMod(gpScreenBak, 0, 0, 0);
   SDL_SetSurfaceColorMod(gpScreenBak, 0xFF, 0xFF, 0xFF);

   rect.x = 0;
   rect.y = 0;
   rect.w = 320;
   rect.h = PAL_SCREEN_H;

   VIDEO_HdPalLevel(rgPalette);
   VIDEO_UpdateScreen(&rect);
#else
   SDL_SetPalette(gpScreen, SDL_LOGPAL | SDL_PHYSPAL, rgPalette, 0, 256);
   SDL_SetPalette(gpScreenBak, SDL_LOGPAL | SDL_PHYSPAL, rgPalette, 0, 256);
   SDL_SetPalette(gpScreenReal, SDL_LOGPAL | SDL_PHYSPAL, rgPalette, 0, 256);
# if defined(PAL_FORCE_UPDATE_ON_PALETTE_SET)
   {
      static UINT32 time = 0;
      if (SDL_GetTicks() - time > 50)
      {
	      SDL_UpdateRect(gpScreenReal, 0, 0, gpScreenReal->w, gpScreenReal->h);
	      time = SDL_GetTicks();
      }
   }
# endif
#endif
}

VOID
VIDEO_Resize(
   INT             w,
   INT             h
)
/*++
  Purpose:

    This function is called when user resized the window.

  Parameters:

    [IN]  w - width of the window after resizing.

    [IN]  h - height of the window after resizing.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2,0,0)
   SDL_Rect rect;

   if (gpTexture)
   {
      SDL_DestroyTexture(gpTexture);
   }

   gpTexture = gRenderBackend.CreateTexture(w, h);

   if (gpTexture == NULL)
   {
      TerminateOnError("Re-creating texture failed on window resize!\n");
   }

   rect.x = 0;
   rect.y = 0;
   rect.w = 320;
   rect.h = PAL_SCREEN_H;

   VIDEO_UpdateScreen(&rect);
#else
   DWORD                    flags;
   PAL_LARGE SDL_Color      palette[256];
   int                      i, bpp;

   //
   // Get the original palette.
   //
   if (gpScreenReal->format->palette != NULL)
   {
      for (i = 0; i < gpScreenReal->format->palette->ncolors; i++)
      {
         palette[i] = gpScreenReal->format->palette->colors[i];
      }
   }
   else i = 0;

   //
   // Create the screen surface.
   //
   flags = gpScreenReal->flags;
   bpp = gpScreenReal->format->BitsPerPixel;

   SDL_FreeSurface(gpScreenReal);
   gpScreenReal = SDL_SetVideoMode(w, h, bpp, flags);

   if (gpScreenReal == NULL)
   {
      //
      // Fall back to software windowed mode in default size.
      //
      gpScreenReal = SDL_SetVideoMode(PAL_DEFAULT_WINDOW_WIDTH, PAL_DEFAULT_WINDOW_HEIGHT, bpp, SDL_SWSURFACE);
   }

   SDL_SetPalette(gpScreenReal, SDL_PHYSPAL | SDL_LOGPAL, palette, 0, i);
   VIDEO_UpdateScreen(NULL);

   gpPalette = gpScreenReal->format->palette;
#endif
}

SDL_Color *
VIDEO_GetPalette(
   VOID
)
/*++
  Purpose:

    Get the current palette of the screen.

  Parameters:

    None.

  Return value:

    Pointer to the current palette.

--*/
{
   return gpPalette->colors;
}

VOID
VIDEO_ToggleScaleScreen(
   VOID
)
/*++
  Purpose:

    Toggle scalescreen mode, only used in some platforms.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   bScaleScreen = !bScaleScreen;
   VIDEO_Resize(PAL_DEFAULT_WINDOW_WIDTH, PAL_DEFAULT_WINDOW_HEIGHT);
   VIDEO_UpdateScreen(NULL);
}

VOID
VIDEO_ToggleFullscreen(
   VOID
)
/*++
  Purpose:

    Toggle fullscreen mode.

  Parameters:

    None.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2,0,0)
	if (gConfig.fFullScreen)
	{
		SDL_SetWindowFullscreen(gpWindow, 0);
		gConfig.fFullScreen = FALSE;
	}
	else
	{
		SDL_SetWindowFullscreen(gpWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
		gConfig.fFullScreen = TRUE;
	}
#else
   DWORD                    flags;
   PAL_LARGE SDL_Color      palette[256];
   int                      i, bpp;

   //
   // Get the original palette.
   //
   if (gpScreenReal->format->palette != NULL)
   {
      for (i = 0; i < gpScreenReal->format->palette->ncolors; i++)
      {
         palette[i] = gpScreenReal->format->palette->colors[i];
      }
   }

   //
   // Get the flags and bpp of the original screen surface
   //
   flags = gpScreenReal->flags;
   bpp = gpScreenReal->format->BitsPerPixel;

   if (flags & SDL_FULLSCREEN)
   {
      //
      // Already in fullscreen mode. Remove the fullscreen flag.
      //
      flags &= ~SDL_FULLSCREEN;
      flags |= SDL_RESIZABLE;
      SDL_ShowCursor(TRUE);
   }
   else
   {
      //
      // Not in fullscreen mode. Set the fullscreen flag.
      //
      flags |= SDL_FULLSCREEN;
      SDL_ShowCursor(FALSE);
   }

   //
   // Free the original screen surface
   //
   SDL_FreeSurface(gpScreenReal);

   //
   // ... and create a new one
   //
   if (gConfig.dwScreenWidth == 640 && gConfig.dwScreenHeight == 400 && (flags & SDL_FULLSCREEN))
   {
      gpScreenReal = SDL_SetVideoMode(640, 480, bpp, flags);
   }
   else if (gConfig.dwScreenWidth == 640 && gConfig.dwScreenHeight == 480 && !(flags & SDL_FULLSCREEN))
   {
      gpScreenReal = SDL_SetVideoMode(640, 400, bpp, flags);
   }
   else
   {
      gpScreenReal = SDL_SetVideoMode(gConfig.dwScreenWidth, gConfig.dwScreenHeight, bpp, flags);
   }

   VIDEO_SetPalette(palette);

   //
   // Update the screen
   //
   VIDEO_UpdateScreen(NULL);
#endif
}

VOID
VIDEO_ChangeDepth(
   INT             bpp
)
/*++
  Purpose:

    Change screen color depth.

  Parameters:

    [IN]  bpp - bits per pixel (0 = default).

  Return value:

    None.

--*/
{
#if !SDL_VERSION_ATLEAST(2,0,0)
   DWORD                    flags;
   int                      w, h;

   //
   // Get the flags and resolution of the original screen surface
   //
   flags = gpScreenReal->flags;
   w = gpScreenReal->w;
   h = gpScreenReal->h;

   //
   // Clear the screen surface.
   //
   SDL_FillRect(gpScreenReal, NULL, 0);

   //
   // Create the screen surface.
   //
   SDL_FreeSurface(gpScreenReal);
   gpScreenReal = SDL_SetVideoMode(w, h, (bpp == 0)?8:bpp, flags);

   if (gpScreenReal == NULL)
   {
      //
      // Fall back to software windowed mode in default size.
      //
      gpScreenReal = SDL_SetVideoMode(PAL_DEFAULT_WINDOW_WIDTH, PAL_DEFAULT_WINDOW_HEIGHT, (bpp == 0)?8:bpp, SDL_SWSURFACE);
   }

   gpPalette = gpScreenReal->format->palette;
#endif
}

VOID
VIDEO_SaveScreenshot(
   VOID
)
/*++
  Purpose:

    Save the screenshot of current screen to a BMP file.

  Parameters:

    None.

  Return value:

    None.

--*/
{
	char filename[80];
#ifdef _WIN32
	SYSTEMTIME st;
	GetLocalTime(&st);
	sprintf(filename, "%04d%02d%02d%02d%02d%02d%03d.bmp", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#elif !defined( GEKKO )
	struct timeval tv;
	struct tm *ptm;
	gettimeofday(&tv, NULL);
	ptm = localtime(&tv.tv_sec);
	sprintf(filename, "%04d%02d%02d%02d%02d%02d%03d.bmp", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_hour, ptm->tm_min, ptm->tm_sec, (int)(tv.tv_usec / 1000));
#endif

	//
	// Save the screenshot.
	//
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_SaveBMP(gpScreen, PAL_CombinePath(0, gConfig.pszSavePath, filename));
#else
	SDL_SaveBMP(gpScreenReal, PAL_CombinePath(0, gConfig.pszSavePath, filename));
#endif
}

VOID
VIDEO_ShakeScreen(
   WORD           wShakeTime,
   WORD           wShakeLevel
)
/*++
  Purpose:

    Set the screen shake time and level.

  Parameters:

    [IN]  wShakeTime - how many times should we shake the screen.

    [IN]  wShakeLevel - level of shaking.

  Return value:

    None.

--*/
{
   g_wShakeTime = wShakeTime;
   g_wShakeLevel = wShakeLevel;
}

VOID
VIDEO_SwitchScreen(
   WORD           wSpeed
)
/*++
  Purpose:

    Switch the screen from the backup screen buffer to the current screen buffer.
    NOTE: This will destroy the backup buffer.

  Parameters:

    [IN]  wSpeed - speed of fading (the larger value, the slower).

  Return value:

    None.

--*/
{
   int               i, j;
   const int         rgIndex[6] = {0, 3, 1, 5, 2, 4};
   SDL_Rect          dstrect;

   short             offset = 240 - 200;
   short             screenRealHeight = gpScreenReal->h;
   short             screenRealY = 0;

   if (!bScaleScreen)
   {
      screenRealHeight -= offset;
      screenRealY = offset / 2;
   }

   wSpeed++;
   wSpeed *= 10;

   unsigned char *pbScr = SAT_BankPlane(gpScreen, FALSE), *pbBak = SAT_BankPlane(gpScreenBak, pbScr != NULL);

   for (i = 0; i < 6; i++)
   {
      for (j = rgIndex[i]; j < gpScreen->pitch * gpScreen->h; j += 6)
      {
         ((LPBYTE)(gpScreenBak->pixels))[j] = ((LPBYTE)(gpScreen->pixels))[j];
         if (pbScr && pbBak) pbBak[j] = pbScr[j];
      }

      //
      // Draw the backup buffer to the screen
      //
      dstrect.x = 0;
      dstrect.y = screenRealY;
      dstrect.w = gpScreenReal->w;
      dstrect.h = screenRealHeight;

	  if (SDL_MUSTLOCK(gpScreenReal))
	  {
		  if (SDL_LockSurface(gpScreenReal) < 0)
			  return;
	  }

      SDL_SoftStretch(gpScreenBak, NULL, gpScreenReal, &dstrect);
      SAT_ApplyBanks(gpScreenBak, 0, dstrect.y, gpScreenBak->h, 0, 320);
#if SDL_VERSION_ATLEAST(2, 0, 0)
      gRenderBackend.RenderCopy();
#else
      SDL_UpdateRect(gpScreenReal, 0, 0, gpScreenReal->w, gpScreenReal->h);
#endif

	  if (SDL_MUSTLOCK(gpScreenReal))
	  {
		  SDL_UnlockSurface(gpScreenReal);
	  }

      UTIL_Delay(wSpeed);
   }
}

VOID
VIDEO_FadeScreen(
   WORD           wSpeed
)
/*++
  Purpose:

    Fade from the backup screen buffer to the current screen buffer.
    NOTE: This will destroy the backup buffer.

  Parameters:

    [IN]  wSpeed - speed of fading (the larger value, the slower).

  Return value:

    None.

--*/
{
   int               i, j, k;
   DWORD             time;
   BYTE              a, b;
   const int         rgIndex[6] = {0, 3, 1, 5, 2, 4};
   SDL_Rect          dstrect;
   short             offset = 240 - 200;
   short             screenRealHeight = gpScreenReal->h;
   short             screenRealY = 0;

   //
   // Lock surface if needed
   //
   if (SDL_MUSTLOCK(gpScreenReal))
   {
      if (SDL_LockSurface(gpScreenReal) < 0)
         return;
   }

   if (!bScaleScreen)
   {
      screenRealHeight -= offset;
      screenRealY = offset / 2;
   }

   time = SDL_GetTicks();

   wSpeed++;
   wSpeed *= 10;

   unsigned char *pbScr2 = SAT_BankPlane(gpScreen, FALSE), *pbBak2 = SAT_BankPlane(gpScreenBak, pbScr2 != NULL);

   for (i = 0; i < 12; i++)
   {
      for (j = 0; j < 6; j++)
      {
         PAL_ProcessEvent();
         while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
         {
            PAL_ProcessEvent();
            SDL_Delay(5);
         }
         time = SDL_GetTicks() + wSpeed;

         //
         // Blend the pixels in the 2 buffers, and put the result into the
         // backup buffer
         //
         for (k = rgIndex[j]; k < gpScreen->pitch * gpScreen->h; k += 6)
         {
            a = ((LPBYTE)(gpScreen->pixels))[k];
            b = ((LPBYTE)(gpScreenBak->pixels))[k];

            if (i > 0)
            {
               if ((a & 0x0F) > (b & 0x0F))
               {
                  b++;
               }
               else if ((a & 0x0F) < (b & 0x0F))
               {
                  b--;
               }
            }

            ((LPBYTE)(gpScreenBak->pixels))[k] = ((a & 0xF0) | (b & 0x0F));
            if (pbScr2 && pbBak2) pbBak2[k] = pbScr2[k];
         }

         //
         // Draw the backup buffer to the screen
         //
         if (g_wShakeTime != 0)
         {
            //
            // Shake the screen
            //
            SDL_Rect srcrect, dstrect;

            srcrect.x = 0;
            srcrect.y = 0;
            srcrect.w = 320;
            srcrect.h = PAL_SCREEN_H - g_wShakeLevel;

            dstrect.x = 0;
            dstrect.y = screenRealY;
            dstrect.w = 320 * gpScreenReal->w / gpScreen->w;
            dstrect.h = (PAL_SCREEN_H - g_wShakeLevel) * screenRealHeight / gpScreen->h;

            if (g_wShakeTime & 1)
            {
               srcrect.y = g_wShakeLevel;
            }
            else
            {
               dstrect.y = (screenRealY + g_wShakeLevel) * screenRealHeight / gpScreen->h;
            }

            SDL_SoftStretch(gpScreenBak, &srcrect, gpScreenReal, &dstrect);
            SAT_ApplyBanks(gpScreenBak, srcrect.y, dstrect.y, srcrect.h, 0, 320);

            if (g_wShakeTime & 1)
            {
               dstrect.y = (screenRealY + screenRealHeight - g_wShakeLevel) * screenRealHeight / gpScreen->h;
            }
            else
            {
               dstrect.y = screenRealY;
            }

            dstrect.h = g_wShakeLevel * screenRealHeight / gpScreen->h;

            SDL_FillRect(gpScreenReal, &dstrect, 0);
#if SDL_VERSION_ATLEAST(2, 0, 0)
            gRenderBackend.RenderCopy();
#else
			SDL_UpdateRect(gpScreenReal, 0, 0, gpScreenReal->w, gpScreenReal->h);
#endif
            g_wShakeTime--;
         }
         else
         {
            dstrect.x = 0;
            dstrect.y = screenRealY;
            dstrect.w = gpScreenReal->w;
            dstrect.h = screenRealHeight;

            SDL_SoftStretch(gpScreenBak, NULL, gpScreenReal, &dstrect);
      SAT_ApplyBanks(gpScreenBak, 0, dstrect.y, gpScreenBak->h, 0, 320);
#if SDL_VERSION_ATLEAST(2, 0, 0)
            gRenderBackend.RenderCopy();
#else
            SDL_UpdateRect(gpScreenReal, 0, 0, gpScreenReal->w, gpScreenReal->h);
#endif
         }
      }
   }

   if (SDL_MUSTLOCK(gpScreenReal))
   {
      SDL_UnlockSurface(gpScreenReal);
   }

   //
   // Draw the result buffer to the screen as the final step
   //
   VIDEO_UpdateScreen(NULL);
}

void
VIDEO_SetWindowTitle(
	const char*     pszTitle
)
/*++
  Purpose:

    Set the caption of the window.

  Parameters:

    [IN]  pszTitle - the new caption of the window.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_SetWindowTitle(gpWindow, PAL_CONVERT_UTF8(pszTitle));
#else
	SDL_WM_SetCaption(pszTitle, NULL);
#endif
}

SDL_Surface *
VIDEO_CreateCompatibleSurface(
	SDL_Surface    *pSource
)
{
	return VIDEO_CreateCompatibleSizedSurface(pSource, NULL);
}

SDL_Surface *
VIDEO_CreateCompatibleSizedSurface(
	SDL_Surface    *pSource,
	const SDL_Rect *pSize
)
/*++
  Purpose:

    Create a surface that compatible with the source surface.

  Parameters:

    [IN]  pSource   - the source surface from which attributes are taken.
    [IN]  pSize     - the size (width & height) of the created surface.

  Return value:

    None.

--*/
{
	//
	// Create the surface
	//
#if SDL_VERSION_ATLEAST(3,0,0)
    SDL_Surface* dest = SDL_CreateSurface(
        pSize ? pSize->w : pSource->w,
        pSize ? pSize->h : pSource->h,
        SDL_GetPixelFormatDetails(pSource->format)->format);
#else
	SDL_Surface *dest = SDL_CreateRGBSurface(pSource->flags,
		pSize ? pSize->w : pSource->w,
		pSize ? pSize->h : pSource->h,
		pSource->format->BitsPerPixel,
		pSource->format->Rmask, pSource->format->Gmask,
		pSource->format->Bmask, pSource->format->Amask);
#endif

	if (dest)
	{
		VIDEO_UpdateSurfacePalette(dest);
	}

	return dest;
}

SDL_Surface *
VIDEO_DuplicateSurface(
	SDL_Surface    *pSource,
	const SDL_Rect *pRect
)
/*++
  Purpose:

    Duplicate the selected area from the source surface into new surface.

  Parameters:

    [IN]  pSource - the source surface.
	[IN]  pRect   - the area to be duplicated, NULL for entire surface.

  Return value:

    None.

--*/
{
	SDL_Surface* dest = VIDEO_CreateCompatibleSizedSurface(pSource, pRect);

	if (dest)
	{
		VIDEO_CopySurface(pSource, pRect, dest, NULL);
	}

	return dest;
}

void
VIDEO_UpdateSurfacePalette(
	SDL_Surface    *pSurface
)
/*++
  Purpose:

    Use the global palette to update the palette of pSurface.

  Parameters:

    [IN]  pSurface - the surface whose palette should be updated.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_SetSurfacePalette(pSurface, gpPalette);
#else
	if (gpPalette != NULL)
	{
		SDL_SetPalette(pSurface, SDL_PHYSPAL | SDL_LOGPAL, gpPalette->colors, 0, 256);
	}
#endif
}

VOID
VIDEO_DrawSurfaceToScreen(
    SDL_Surface    *pSurface
)
/*++
  Purpose:

    Draw a surface directly to screen.

  Parameters:

    [IN]  pSurface - the surface which needs to be drawn to screen.

  Return value:

    None.

--*/
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
   //
   // Draw the surface to screen.
   //
   if (g_bRenderPaused)
   {
      return;
   }
   SDL_BlitScaled(pSurface, NULL, gpScreenReal, NULL);
   { extern void PAL_HDM_SetStale(int s); PAL_HDM_SetStale(1); }  /* movie frame is in gpScreenReal, not the HD twin -> use the low-res present */
   {  // Saturn port debug: dump AVI frames too (env PAL_SHOTDIR)
      static Uint64 last = 0; static int n = 0; const char *dir = SDL_getenv("PAL_SHOTDIR");
      if (dir && SDL_GetTicks() - last >= 2000) {
         char path[512]; last = SDL_GetTicks();
         SDL_snprintf(path, sizeof(path), "%s/avi%04d.bmp", dir, n++);
         SDL_SaveBMP(pSurface, path);
      }
   }
   gRenderBackend.RenderCopy();
#else
   SDL_Surface   *pCompatSurface;
   SDL_Rect       rect;

   rect.x = rect.y = 0;
   rect.w = pSurface->w;
   rect.h = pSurface->h;

   pCompatSurface = VIDEO_CreateCompatibleSizedSurface(gpScreenReal, &rect);

   //
   // First convert the surface to compatible format.
   //
   SDL_BlitSurface(pSurface, NULL, pCompatSurface, NULL);

   //
   // Draw the surface to screen.
   //
   SDL_SoftStretch(pCompatSurface, NULL, gpScreenReal, NULL);

   SDL_UpdateRect(gpScreenReal, 0, 0, gpScreenReal->w, gpScreenReal->h);
   SDL_FreeSurface(pCompatSurface);
#endif
}

/* ------------------------------------------------------------------------------------------------
   Saturn palette banks.  The Saturn draws map sprites with CRAM bank 1, battle sprites with bank 2 and
   the whole UI layer (NBG1: portraits, boxes, icons, text) with bank 3 (ALLDAT palettes 1..3), while the
   map tiles use bank 0 (= the scene palette, pat.mkf #0).  The port keeps the raw colour index in the
   8-bit surface plus a per-pixel bank number in a side plane, and resolves the colour when presenting.
   Fades scale the bank colours by the same per-channel ratio as the scene palette.
   ------------------------------------------------------------------------------------------------ */
#define SAT_MAX_PLANES 64
typedef struct { SDL_Surface *s; unsigned char *b; int w, h; } SATPLANE;
static SATPLANE g_rgSatPlane[SAT_MAX_PLANES];
BYTE g_bSatTextBank = 3;
#define SAT_NUM_BANKS 17                 /* 0..7 = ALLDAT palettes, 8..15 = FBP pictures (own palettes), 16 = last movie frame */
static SDL_Color g_rgSatBankBase[SAT_NUM_BANKS][256];
static uint32_t g_rgSatBankLUT[SAT_NUM_BANKS][256];
static uint32_t g_rgSatBankLUTFull[SAT_NUM_BANKS][256];   /* same banks at FULL brightness (no fade factor) */
FILE *g_fpSatFBP = NULL;
static int g_rgSatFBPOfBank[SAT_NUM_BANKS];
static int g_iSatBankLoaded = 0, g_iSatLUTDirty = 1;
static void SAT_LoadBanks(void);

unsigned char *
SAT_BankPlane(SDL_Surface *s, BOOL fCreate)
{
   int i, slot = -1;
   if (s == NULL || s->w <= 0 || s->h <= 0) return NULL;
   for (i = 0; i < SAT_MAX_PLANES; i++)
   {
      if (g_rgSatPlane[i].s == s)
      {
         if (g_rgSatPlane[i].w == s->w && g_rgSatPlane[i].h == s->h) return g_rgSatPlane[i].b;
         free(g_rgSatPlane[i].b);
         g_rgSatPlane[i].s = NULL; g_rgSatPlane[i].b = NULL;
         slot = i;
         break;
      }
   }
   if (!fCreate) return NULL;
   if (slot < 0)
      for (i = 0; i < SAT_MAX_PLANES; i++) if (g_rgSatPlane[i].s == NULL) { slot = i; break; }
   if (slot < 0) return NULL;
   g_rgSatPlane[slot].b = (unsigned char *)calloc((size_t)s->w * s->h, 1);
   if (g_rgSatPlane[slot].b == NULL) return NULL;
   g_rgSatPlane[slot].s = s; g_rgSatPlane[slot].w = s->w; g_rgSatPlane[slot].h = s->h;
   return g_rgSatPlane[slot].b;
}

void
SAT_BankFree(SDL_Surface *s)
{
   int i;
   for (i = 0; i < SAT_MAX_PLANES; i++)
      if (s != NULL && g_rgSatPlane[i].s == s)
      {
         free(g_rgSatPlane[i].b);
         g_rgSatPlane[i].s = NULL; g_rgSatPlane[i].b = NULL;
      }
}

void
SAT_BankFill(SDL_Surface *s, const SDL_Rect *r, BYTE bank)
{
   unsigned char *p = SAT_BankPlane(s, bank != 0);
   int x0 = 0, y0 = 0, x1, y1, y;
   if (p == NULL) return;
   x1 = s->w; y1 = s->h;
   if (r) { x0 = r->x; y0 = r->y; x1 = r->x + r->w; y1 = r->y + r->h; }
   if (x0 < 0) x0 = 0;
   if (y0 < 0) y0 = 0;
   if (x1 > s->w) x1 = s->w;
   if (y1 > s->h) y1 = s->h;
   for (y = y0; y < y1; y++) if (x1 > x0) memset(p + y * s->w + x0, bank, x1 - x0);
}

int
SAT_Blit(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, const SDL_Rect *dr)
{
   SDL_Rect a, b;
   int ret, sx, sy, w, h, dx, dy, y;
   unsigned char *ps, *pd;
   if (sr) a = *sr;
   if (dr) b = *dr;
   ret = SDL_BlitSurface(src, sr ? &a : NULL, dst, dr ? &b : NULL);
   if (src == NULL || dst == NULL) return ret;
   ps = SAT_BankPlane(src, FALSE);
   pd = SAT_BankPlane(dst, ps != NULL);
   if (pd == NULL) return ret;
   sx = sr ? sr->x : 0; sy = sr ? sr->y : 0;
   w = sr ? sr->w : src->w; h = sr ? sr->h : src->h;
   dx = dr ? dr->x : 0; dy = dr ? dr->y : 0;
   if (sx < 0) { w += sx; dx -= sx; sx = 0; }
   if (sy < 0) { h += sy; dy -= sy; sy = 0; }
   if (sx + w > src->w) w = src->w - sx;
   if (sy + h > src->h) h = src->h - sy;
   if (dx < 0) { w += dx; sx -= dx; dx = 0; }
   if (dy < 0) { h += dy; sy -= dy; dy = 0; }
   if (dx + w > dst->w) w = dst->w - dx;
   if (dy + h > dst->h) h = dst->h - dy;
   if (w <= 0 || h <= 0) return ret;
   for (y = 0; y < h; y++)
   {
      if (ps) memcpy(pd + (dy + y) * dst->w + dx, ps + (sy + y) * src->w + sx, w);
      else memset(pd + (dy + y) * dst->w + dx, 0, w);
   }
   return ret;
}

int
SAT_MovieBank(const SDL_Color *pal)
{
   /* Saturn: the movie layer keeps its own colours after the movie (rngpal.dat), whatever the scene palette is */
   int i;
   if (!g_iSatBankLoaded) SAT_LoadBanks();
   for (i = 0; i < 256; i++) g_rgSatBankBase[16][i] = pal[i];
   g_iSatLUTDirty = 1;
   return 16;
}

int
SAT_FBPBank(int iChunk)
{
   /* FBP #n is drawn with its own palette (fbppal.mkf #n, written by tools/convert.py); bank 8 + (n & 7) */
   static FILE *fp = NULL;
   static int tried = 0;
   BYTE buf[768];
   int bank, i;
   if (iChunk < 0) return 0;
   if (!g_iSatBankLoaded) SAT_LoadBanks();
   bank = 8 + (iChunk & 7);
   if (g_rgSatFBPOfBank[bank] == iChunk + 1) return bank;
   if (!tried) { fp = UTIL_OpenFile("fbppal.mkf"); tried = 1; }
   if (fp == NULL || PAL_MKFReadChunk(buf, sizeof(buf), iChunk, fp) < 768) return 0;
   for (i = 0; i < 256; i++)
   {
      g_rgSatBankBase[bank][i].r = (buf[i * 3] >> 1) << 3;
      g_rgSatBankBase[bank][i].g = (buf[i * 3 + 1] >> 1) << 3;
      g_rgSatBankBase[bank][i].b = (buf[i * 3 + 2] >> 1) << 3;
   }
   g_rgSatFBPOfBank[bank] = iChunk + 1;
   g_iSatLUTDirty = 1;
   return bank;
}

static void
SAT_LoadBanks(void)
{
   FILE *fp = UTIL_OpenRequiredFile("pat.mkf");
   BYTE buf[1536];
   int b, i;
   for (b = 0; b < 8; b++)
   {
      memset(buf, 0, sizeof(buf));
      PAL_MKFReadChunk(buf, sizeof(buf), b, fp);
      for (i = 0; i < 256; i++)
      {
         g_rgSatBankBase[b][i].r = (buf[i * 3] >> 1) << 3;
         g_rgSatBankBase[b][i].g = (buf[i * 3 + 1] >> 1) << 3;
         g_rgSatBankBase[b][i].b = (buf[i * 3 + 2] >> 1) << 3;
      }
   }
   fclose(fp);
   g_iSatBankLoaded = 1;
}

static void
SAT_UpdateLUT(void)
{
   /* per-channel fade ratio = current scene palette / unfaded scene palette */
   SDL_Color *base;
   SDL_Color *cur = gpPalette->colors;
   double sc[3] = { 0, 0, 0 }, sb[3] = { 0, 0, 0 }, f[3];
   int b, i, k;
   if (!g_iSatBankLoaded) SAT_LoadBanks();
   base = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
   for (i = 0; i < 256; i++)
   {
      sc[0] += cur[i].r; sc[1] += cur[i].g; sc[2] += cur[i].b;
      if (base) { sb[0] += base[i].r; sb[1] += base[i].g; sb[2] += base[i].b; }
   }
   for (k = 0; k < 3; k++)
   {
      f[k] = sb[k] > 0 ? sc[k] / sb[k] : 1.0;
      if (f[k] > 1.0) f[k] = 1.0;
   }
   for (b = 0; b < SAT_NUM_BANKS; b++)
      for (i = 0; i < 256; i++)
      {
         SDL_Color c = g_rgSatBankBase[b][i];
         g_rgSatBankLUT[b][i] = 0xFF000000u | ((uint32_t)(c.r * f[0] + 0.5) << 16) |
            ((uint32_t)(c.g * f[1] + 0.5) << 8) | (uint32_t)(c.b * f[2] + 0.5);
         /* full-bright twin for the HD overlay path: the live palette's fade factor (f) must NOT bake into
            HD sprites/fills, or they go dark/black while the prebuilt full-bright HD tiles stay bright (seen
            as "李逍遥 is a black silhouette on scene load, during the fade-in"). The HD present dims the whole
            frame uniformly via colormod instead, so here keep the banks at full brightness. */
         g_rgSatBankLUTFull[b][i] = 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
      }
   g_iSatLUTDirty = 0;
}

/* Saturn battle: VDP2 colour calculation on the sprite layer (CCCTL SPCCEN, ratio 2). Battle characters
   (ABC/FFF, bank 2) are mixed with the battle field below them. Emulator-fitted (yabause Titan, alpha6 59):
   out = (top8 * 239 / 255 + back8 * 16 / 255), shown as RGB555 (& 0xF8). 9858/9858 pixels. */
SDL_Surface *g_pSatCCBack = NULL;

static uint32_t
SAT_CCMix(uint32_t t, uint32_t b)
{
   uint32_t r = (((t >> 16) & 0xFF) * 239 / 255 + ((b >> 16) & 0xFF) * 16 / 255) & 0xF8;
   uint32_t g = (((t >> 8) & 0xFF) * 239 / 255 + ((b >> 8) & 0xFF) * 16 / 255) & 0xF8;
   uint32_t l = ((t & 0xFF) * 239 / 255 + (b & 0xFF) * 16 / 255) & 0xF8;
   return 0xFF000000u | (r << 16) | (g << 8) | l;
}

void
SAT_ApplyBanks(SDL_Surface *src, int srcY, int dstY, int rows, int x0, int x1)
{
   unsigned char *pl = SAT_BankPlane(src, FALSE);
   SDL_Surface *cb = (src == gpScreen) ? g_pSatCCBack : NULL;
   unsigned char *cbp = cb ? SAT_BankPlane(cb, FALSE) : NULL;
   int y, x;
   if (pl == NULL) return;
   if (g_iSatLUTDirty) SAT_UpdateLUT();
   for (y = 0; y < rows; y++)
   {
      int sy = srcY + y, dy = dstY + y;
      const unsigned char *bp, *ip;
      uint32_t *op;
      if (sy < 0 || sy >= src->h || dy < 0 || dy >= gpScreenReal->h) continue;
      bp = pl + sy * src->w;
      ip = (const unsigned char *)src->pixels + sy * src->pitch;
      op = (uint32_t *)((unsigned char *)gpScreenReal->pixels + dy * gpScreenReal->pitch);
      for (x = x0; x < x1; x++)
         if ((bp[x] & 0x40) && cb != NULL && sy < cb->h && x < cb->w)
         {
            /* Saturn target blink 0x0603b664: the targeted enemy on odd ticks = ghost, bg*239/255 + sprite*16/255 (measured) */
            BYTE bk = bp[x] & 0x3F;
            uint32_t e, bc;
            BYTE bi = ((const BYTE *)cb->pixels)[sy * cb->pitch + x];
            BYTE bb = cbp ? cbp[sy * cb->w + x] : 0;
            if (bk && bk < SAT_NUM_BANKS) e = g_rgSatBankLUT[bk][ip[x]];
            else { SDL_Color c = gpPalette->colors[ip[x]]; e = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
            if (bb && bb < SAT_NUM_BANKS) bc = g_rgSatBankLUT[bb][bi];
            else { SDL_Color c = gpPalette->colors[bi]; bc = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
            op[x] = SAT_CCMix(bc, e);
         }
         else if (bp[x] && bp[x] < SAT_NUM_BANKS)
         {
            uint32_t t = g_rgSatBankLUT[bp[x]][ip[x]];
            if (bp[x] == 2 && cb != NULL && sy < cb->h && x < cb->w)
            {
               BYTE bi = ((const BYTE *)cb->pixels)[sy * cb->pitch + x];
               BYTE bb = cbp ? cbp[sy * cb->w + x] : 0;
               uint32_t bc;
               if (bb && bb < SAT_NUM_BANKS) bc = g_rgSatBankLUT[bb][bi];
               else { SDL_Color c = gpPalette->colors[bi]; bc = ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b; }
               t = SAT_CCMix(t, bc);
            }
            op[x] = t;
         }
   }
}

/* Expose a bank's final display LUT (ARGB per index) for asset extraction. bank 0..7 = ALLDAT palettes. */
const uint32_t *SAT_GetBankLUT(int bank)
{
   if (!g_iSatBankLoaded) SAT_LoadBanks();
   if (g_iSatLUTDirty) SAT_UpdateLUT();
   if (bank < 0 || bank >= SAT_NUM_BANKS) return NULL;
   return g_rgSatBankLUT[bank];
}

/* full-brightness bank LUT for the HD overlay path (sprites/fills): never carries the live fade factor,
   so HD sprites stay bright on scene load / during fade-in (the whole HD frame is dimmed via colormod). */
const uint32_t *SAT_GetBankLUTFull(int bank)
{
   if (!g_iSatBankLoaded) SAT_LoadBanks();
   if (g_iSatLUTDirty) SAT_UpdateLUT();
   if (bank < 0 || bank >= SAT_NUM_BANKS) return NULL;
   return g_rgSatBankLUTFull[bank];
}
