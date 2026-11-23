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

#include "main.h"

/* HD-mirror fade hooks (scene.c): keep the ×6 TTF twin on screen, dimmed in lockstep, during a
   whole-screen fade so the text does not drop to the low-res bitmap font. No-ops unless PAL_HDFULL. */
extern void PAL_HDM_SetFade(int level);   /* 0..64 brightness; call per fade step */
extern void PAL_HDM_FadeInEnd(void);      /* fade-in done: resume full-brightness HD present */
extern void PAL_HDM_FadeOutEnd(void);     /* fade-out done: blank the HD twin (screen is black now) */

SDL_Color g_rgHdRefPalette[256]; int g_fHdRefPalette = 0;   /* last palette fetched from pat.mkf, undimmed */

SDL_Color *
PAL_GetPalette(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Purpose:

    Get the specified palette in pat.mkf file.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

  Return value:

    Pointer to the palette. NULL if failed.

--*/
{
   static SDL_Color      palette[256];
   PAL_LARGE BYTE        buf[1536];
   INT                   i;
   FILE                 *fp;

   memset(palette, 0xff, sizeof(SDL_Color)*256);

   fp = UTIL_OpenRequiredFile("pat.mkf");

   //
   // Read the palette data from the pat.mkf file
   //
   i = PAL_MKFReadChunk(buf, 1536, iPaletteNum, fp);

   fclose(fp);

   if (i < 0)
   {
      //
      // Read failed
      //
      return NULL;
   }
   else if (i <= 256 * 3)
   {
      //
      // There is no night colors in the palette
      //
      fNight = FALSE;
   }

   for (i = 0; i < 256; i++)
   {
      palette[i].r = (buf[(fNight ? 256 * 3 : 0) + i * 3] >> 1) << 3;   /* Saturn RGB555 -> 8 bit (c5 << 3) */
      palette[i].g = (buf[(fNight ? 256 * 3 : 0) + i * 3 + 1] >> 1) << 3;   /* Saturn RGB555 -> 8 bit (c5 << 3) */
      palette[i].b = (buf[(fNight ? 256 * 3 : 0) + i * 3 + 2] >> 1) << 3;   /* Saturn RGB555 -> 8 bit (c5 << 3) */
#if 0
      palette[i].r += (255 - palette[i].r) / 5;
      palette[i].g += (255 - palette[i].g) / 5;
      palette[i].b += (255 - palette[i].b) / 5;
#endif
   }

   memcpy(g_rgHdRefPalette, palette, sizeof(g_rgHdRefPalette)); g_fHdRefPalette = 1;   /* reference for the HD twin's fade level (video.c) */
   return palette;
}

VOID
PAL_SetPalette(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Purpose:

    Set the screen palette to the specified one.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

  Return value:

    None.

--*/
{
   SDL_Color *p = PAL_GetPalette(iPaletteNum, fNight);

   if (p != NULL)
   {
      VIDEO_SetPalette(p);
   }
}

VOID
PAL_FadeOut(
   INT         iDelay
)
/*++
  Purpose:

    Fadeout screen to black from the specified palette.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iDelay - delay time for each step.

  Return value:

    None.

--*/
{
   int                      i, j;
   UINT                     time;
   PAL_LARGE SDL_Color      palette[256];
   PAL_LARGE SDL_Color      newpalette[256];

   //
   // Get the original palette...
   //
   for (i = 0; i < 256; i++)
   {
      palette[i] = VIDEO_GetPalette()[i];
   }

   memset(newpalette, 0xff, sizeof(SDL_Color)*256);

   //
   // Start fading out...
   //
   time = SDL_GetTicks() + iDelay * 10 * 60;

   while (TRUE)
   {
      //
      // Set the current palette...
      //
      j = (int)(time - SDL_GetTicks()) / iDelay / 10;
      if (j < 0)
      {
         break;
      }

      for (i = 0; i < 256; i++)
      {
         newpalette[i].r = (palette[i].r * j) >> 6;
         newpalette[i].g = (palette[i].g * j) >> 6;
         newpalette[i].b = (palette[i].b * j) >> 6;
      }

      PAL_HDM_SetFade(j);   /* dim the HD twin by the same 0..64 factor */
      VIDEO_SetPalette(newpalette);

      UTIL_Delay(10);
   }

   memset(newpalette, 0, sizeof(newpalette));
   PAL_HDM_SetFade(0);
   VIDEO_SetPalette(newpalette);
   PAL_HDM_FadeOutEnd();   /* screen is black; blank the stale HD twin */
}

VOID
PAL_FadeIn(
   INT         iPaletteNum,
   BOOL        fNight,
   INT         iDelay
)
/*++
  Purpose:

    Fade in the screen to the specified palette.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iDelay - delay time for each step.

  Return value:

    None.

--*/
{
   int                      i, j;
   UINT                     time;
   SDL_Color               *palette;
   PAL_LARGE SDL_Color      newpalette[256];
    
   memset(newpalette, 0xff, sizeof(SDL_Color)*256);

   //
   // Get the new palette...
   //
   palette = PAL_GetPalette(iPaletteNum, fNight);

   //
   // Start fading in...
   //
   time = SDL_GetTicks() + iDelay * 10 * 60;
   while (TRUE)
   {
      //
      // Set the current palette...
      //
      j = (int)(time - SDL_GetTicks()) / iDelay / 10;
      if (j < 0)
      {
         break;
      }

      j = 60 - j;

      for (i = 0; i < 256; i++)
      {
         newpalette[i].r = (palette[i].r * j) >> 6;
         newpalette[i].g = (palette[i].g * j) >> 6;
         newpalette[i].b = (palette[i].b * j) >> 6;
      }

      PAL_HDM_SetFade(j);   /* brighten the HD twin by the same 0..64 factor */
      VIDEO_SetPalette(newpalette);

      UTIL_Delay(10);
   }

   PAL_HDM_SetFade(64);
   VIDEO_SetPalette(palette);
   PAL_HDM_FadeInEnd();   /* content is current: resume full-brightness HD present */
}

static Uint64 s_qwSatSceneFadedOut = 0;   /* time op 0x93 finished a fade-out (Saturn flag 0x06076930) */

VOID
PAL_SceneFade(
   INT         iPaletteNum,
   BOOL        fNight,
   INT         iStep
)
/*++
  Purpose:

    Fade in or fade out the screen. Update the scene during the process.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  iStep - positive to fade in, nagative to fade out.

  Return value:

    None.

--*/
{
   //
   // Saturn op 0x93 (0x06023e10): the operand's size is ignored. operand >= 0 = fade in: 0x06021fd8(80)
   // (20 frames) first, then darkness 31 -> 0; operand < 0 = fade out: darkness 0 -> 31. Two frames (scene
   // draw + 0x06021fd8(1)) per level with the scene updated; darkness d scales every 5-bit channel to floor(c * (31 - d) / 31).
   //
   SDL_Color            *palette, newpalette[256];
   int                   d, j;
   DWORD                 time;

   palette = PAL_GetPalette(iPaletteNum, fNight);

   if (palette == NULL)
   {
      return;
   }

   gpGlobals->fNeedToFadeIn = FALSE;

   if (iStep >= 0)
   {
      time = SDL_GetTicks() + 20 * SAT_SCRIPT_FRAME_TIME;   /* 0x06021fd8(80) = 20 frames; emu entry 4556 (standing): 120 vsync */
      while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
      {
         PAL_ProcessEvent();
         SDL_Delay(5);
      }
   }

   for (d = 0; d < 32; d++)
   {
      int dark = (iStep >= 0) ? 31 - d : d;

      time = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;   /* emu o_sc1 (entry 2996): one level every 6 vsync = one frame when idle */

      PAL_ClearKeyState();
      g_InputState.dir = kDirUnknown;
      g_InputState.prevdir = kDirUnknown;
      PAL_GameUpdate(FALSE);
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);

      for (j = 0; j < 256; j++)
      {
         newpalette[j].r = (Uint8)((((palette[j].r >> 3) * (31 - dark)) / 31) << 3);
         newpalette[j].g = (Uint8)((((palette[j].g >> 3) * (31 - dark)) / 31) << 3);
         newpalette[j].b = (Uint8)((((palette[j].b >> 3) * (31 - dark)) / 31) << 3);
         newpalette[j].a = 0xff;
      }
      VIDEO_SetPalette(newpalette);

      PAL_ProcessEvent();
      while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
      {
         PAL_ProcessEvent();
         SDL_Delay(5);
      }
   }

   s_qwSatSceneFadedOut = (iStep < 0) ? SDL_GetTicks() : 0;
}

VOID
PAL_PaletteFade(
   INT         iPaletteNum,
   BOOL        fNight,
   BOOL        fUpdateScene
)
/*++
  Purpose:

    Fade from the current palette to the specified one.

  Parameters:

    [IN]  iPaletteNum - number of the palette.

    [IN]  fNight - whether use the night palette or not.

    [IN]  fUpdateScene - TRUE if update the scene in the progress.

  Return value:

    None.

--*/
{
   int            i, j;
   UINT           time;
   SDL_Color     *newpalette = PAL_GetPalette(iPaletteNum, fNight);
   PAL_LARGE SDL_Color      palette[256];
   PAL_LARGE SDL_Color		t[256];

   memset(t, 0xff, sizeof(t));

   if (newpalette == NULL)
   {
      return;
   }

   for (i = 0; i < 256; i++)
   {
      palette[i] = VIDEO_GetPalette()[i];
   }

   //
   // Start fading...
   //
   for (i = 0; i < 32; i++)
   {
      time = SDL_GetTicks() + (fUpdateScene ? FRAME_TIME : FRAME_TIME / 4);

      for (j = 0; j < 256; j++)
      {
         t[j].r =
            (BYTE)(((INT)(palette[j].r) * (31 - i) + (INT)(newpalette[j].r) * i) / 31);
         t[j].g =
            (BYTE)(((INT)(palette[j].g) * (31 - i) + (INT)(newpalette[j].g) * i) / 31);
         t[j].b =
            (BYTE)(((INT)(palette[j].b) * (31 - i) + (INT)(newpalette[j].b) * i) / 31);
      }
      VIDEO_SetPalette(t);

      if (fUpdateScene)
      {
         PAL_ClearKeyState();
         g_InputState.dir = kDirUnknown;
         g_InputState.prevdir = kDirUnknown;
         PAL_GameUpdate(FALSE);
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);
      }

      PAL_ProcessEvent();

      while (!SDL_TICKS_PASSED(SDL_GetTicks(), time))
      {
         PAL_ProcessEvent();
         SDL_Delay(5);
      }
   }
}

VOID
PAL_ColorFade(
   INT        iDelay,
   BYTE       bColor,
   BOOL       fFrom
)
/*++
  Purpose:

    Fade the palette from/to the specified color.

  Parameters:

    [IN]  iDelay - the delay time of each step.

    [IN]  bColor - the color to fade from/to.

    [IN]  fFrom - if TRUE then fade from bColor, else fade to bColor.

  Return value:

    None.

--*/
{
   SDL_Color       *palette;
   PAL_LARGE SDL_Color        newpalette[256];
   int              i, j;

   memset(newpalette, 0xff, sizeof(SDL_Color)*255);

   palette = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);

   iDelay *= 10;
   if (iDelay == 0)
   {
      iDelay = 10;
   }

   if (fFrom)
   {
      for (i = 0; i < 256; i++)
      {
         newpalette[i] = palette[bColor];
      }

      for (i = 0; i < 64; i++)
      {
         for (j = 0; j < 256; j++)
         {
            if (newpalette[j].r > palette[j].r)
            {
               newpalette[j].r -= 4;
            }
            else if (newpalette[j].r < palette[j].r)
            {
               newpalette[j].r += 4;
            }

            if (newpalette[j].g > palette[j].g)
            {
               newpalette[j].g -= 4;
            }
            else if (newpalette[j].g < palette[j].g)
            {
               newpalette[j].g += 4;
            }

            if (newpalette[j].b > palette[j].b)
            {
               newpalette[j].b -= 4;
            }
            else if (newpalette[j].b < palette[j].b)
            {
               newpalette[j].b += 4;
            }
         }

         VIDEO_SetPalette(newpalette);
         UTIL_Delay(iDelay);
      }

      VIDEO_SetPalette(palette);
   }
   else
   {
      memcpy(newpalette, palette, sizeof(newpalette));

      for (i = 0; i < 64; i++)
      {
         for (j = 0; j < 256; j++)
         {
            if (newpalette[j].r > palette[bColor].r)
            {
               newpalette[j].r -= 4;
            }
            else if (newpalette[j].r < palette[bColor].r)
            {
               newpalette[j].r += 4;
            }

            if (newpalette[j].g > palette[bColor].g)
            {
               newpalette[j].g -= 4;
            }
            else if (newpalette[j].g < palette[bColor].g)
            {
               newpalette[j].g += 4;
            }

            if (newpalette[j].b > palette[bColor].b)
            {
               newpalette[j].b -= 4;
            }
            else if (newpalette[j].b < palette[bColor].b)
            {
               newpalette[j].b += 4;
            }
         }

         VIDEO_SetPalette(newpalette);
         UTIL_Delay(iDelay);
      }

      for (i = 0; i < 256; i++)
      {
         newpalette[i] = palette[bColor];
      }

      VIDEO_SetPalette(newpalette);
   }
}

VOID
PAL_FadeToRed(
   VOID
)
/*++
  Purpose:

    Fade the whole screen to red color.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   SDL_Color                 *palette;
   PAL_LARGE SDL_Color        newpalette[256];
   int                        i, j;
   BYTE                       color;

   memset(newpalette, 0xff, sizeof(SDL_Color)*255);
    
   palette = PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
   memcpy(newpalette, palette, sizeof(newpalette));

   for (i = 0; i < gpScreen->pitch * gpScreen->h; i++)
   {
      if (((LPBYTE)(gpScreen->pixels))[i] == 0x4F)
      {
         ((LPBYTE)(gpScreen->pixels))[i] = 0x4E; // HACKHACK
      }
   }

   VIDEO_UpdateScreen(NULL);

   for (i = 0; i < 32; i++)
   {
      for (j = 0; j < 256; j++)
      {
         if (j == 0x4F)
         {
            continue; // so that texts will not be affected
         }

         color = ((INT)palette[j].r + (INT)palette[j].g + (INT)palette[j].b) / 4 + 64;

         if (newpalette[j].r > color)
         {
            newpalette[j].r -= (newpalette[j].r - color > 8 ? 8 : newpalette[j].r - color);
         }
         else if (newpalette[j].r < color)
         {
            newpalette[j].r += (color - newpalette[j].r > 8 ? 8 : color - newpalette[j].r);
         }

         if (newpalette[j].g > 0)
         {
            newpalette[j].g -= (newpalette[j].g > 8 ? 8 : newpalette[j].g);
         }

         if (newpalette[j].b > 0)
         {
            newpalette[j].b -= (newpalette[j].b > 8 ? 8 : newpalette[j].b);
         }
      }

      VIDEO_SetPalette(newpalette);
      UTIL_Delay(75);
   }
}

//
// Saturn script fades (emulator 2026-11-08, op 0x50 -> 0x06023914, op 0x51 -> 0x06023c64): the fade is a VDP2 colour
// offset stepping 0,8,16,24 then 31 (5-bit, subtractive). With the usual operand 1 only the -8 step is ever on screen
// (one vsync), then black; the fade-in is instant. A scene change kept the screen black 72 vsync (map load) before
// the new scene appeared -> PAL_SatFadeIn waits until 1200 ms after the fade-out.
//
static Uint64 s_qwSatBlackSince = 0;
static int s_iSatFirstDarkStep = 3;   /* stepped fade-in starts at darkness 8 * this */
static WORD s_wSatFadeScene = 0;
VOID
PAL_SatFadeOut(
   INT         iDelay
)
{
   int                      i;
   PAL_LARGE SDL_Color      palette[256];
   PAL_LARGE SDL_Color      newpalette[256];

   // Saturn 0x06023914: outside battle (byte 0x060769e1 == 0) it first calls 0x06021fd8(20)
   // = 20/4 = 5 scene frames (3 vsync each) before the dim frame (measured 17 vsync after the last step).
   if (!gpGlobals->fInBattle)
   {
      UTIL_Delay(5 * SAT_SCRIPT_FRAME_TIME);   /* 5 frames of g_iSatSynch vsyncs */
   }

   if (iDelay > 3) iDelay /= 4;
   if (iDelay < 1) iDelay = 1;

   for (i = 0; i < 256; i++)
   {
      palette[i] = VIDEO_GetPalette()[i];
      // emulator: 5-bit channel c -> floor(c * 23 / 31) (CRAM fade step 8 of 31), checked on every colour of a frame
      newpalette[i].r = (Uint8)((((palette[i].r >> 3) * 23) / 31) << 3);
      newpalette[i].g = (Uint8)((((palette[i].g >> 3) * 23) / 31) << 3);
      newpalette[i].b = (Uint8)((((palette[i].b >> 3) * 23) / 31) << 3);
      newpalette[i].a = palette[i].a;
   }
   VIDEO_SetPalette(newpalette);
   UTIL_Delay(17 * iDelay);

   memset(newpalette, 0, sizeof(newpalette));
   VIDEO_SetPalette(newpalette);
   s_qwSatBlackSince = SDL_GetTicks();
   s_qwSatSceneFadedOut = 0;
   s_wSatFadeScene = gpGlobals->wNumScene;
}

VOID
PAL_SatFadeIn(
   INT         iPaletteNum,
   BOOL        fNight
)
{
   if (s_qwSatSceneFadedOut != 0)
   {
      //
      // Saturn 0x06023c64 with the fade flag set (emu o_sc2, entry 2996: 0x93 out, 0x76, 0x51): scene drawn,
      // 0x06021fd8(20) wait, then brightness table darkness 24 / 16 / 8 / 0, one frame each. The first level
      // shows ~38 vsync after the fade-out reached black.
      //
      SDL_Color *palette = PAL_GetPalette(iPaletteNum, fNight), newpalette[256];
      Uint64 t = s_qwSatSceneFadedOut + 583 * g_iSatSynch / 6;   /* frames: 583 ms measured at 6 vsync per frame */
      int k, j;
      s_qwSatSceneFadedOut = 0;
      s_qwSatBlackSince = 0;
      if (palette != NULL)
      {
         gpGlobals->fNeedToFadeIn = FALSE;   /* else PAL_MakeScene would re-enter PAL_SatFadeIn */
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);   /* palette is still black */
         while (SDL_GetTicks() < t) UTIL_Delay(5);
         for (k = s_iSatFirstDarkStep, s_iSatFirstDarkStep = 3; k >= 0; k--)
         {
            int dark = k * 8;
            for (j = 0; j < 256; j++)
            {
               newpalette[j].r = (Uint8)((((palette[j].r >> 3) * (31 - dark)) / 31) << 3);
               newpalette[j].g = (Uint8)((((palette[j].g >> 3) * (31 - dark)) / 31) << 3);
               newpalette[j].b = (Uint8)((((palette[j].b >> 3) * (31 - dark)) / 31) << 3);
               newpalette[j].a = 0xff;
            }
            VIDEO_SetPalette(newpalette);
            if (k > 0) UTIL_Delay(SAT_SCRIPT_FRAME_TIME);
         }
         return;
      }
   }
   if (s_qwSatBlackSince != 0 && s_wSatFadeScene == gpGlobals->wNumScene)
   {
      //
      // Saturn: 0x50 then a scene redraw (0x7F / 0x09 frames) in the same scene (emu o_s17f, scene 17 entry 0x1438
      // and 0x1443): black 66 vsync, then darkness 16 (5 vsync), 8 (6 vsync), full -- the stepped fade-in, not the
      // instant one of a scene change.
      //
      s_qwSatSceneFadedOut = s_qwSatBlackSince + (1100 - 583) * g_iSatSynch / 6;
      s_iSatFirstDarkStep = 2;
      PAL_SatFadeIn(iPaletteNum, fNight);
      return;
   }
   if (s_qwSatBlackSince != 0)
   {
      Uint64 t = s_qwSatBlackSince + 1200;
      while (SDL_GetTicks() < t) UTIL_Delay(5);
      s_qwSatBlackSince = 0;
   }
   VIDEO_SetPalette(PAL_GetPalette(iPaletteNum, fNight));
}

static Uint64 s_qwSatReloadFirstStep = 0;

VOID
PAL_SatReloadFirstStepAt(
   Uint64      qwFirstStep
)
/* op 0x78 after a script battle: screen black, the next 0x05 shows darkness 16 at qwFirstStep (then 8, 0) */
{
   s_qwSatBlackSince = SDL_GetTicks();
   s_qwSatSceneFadedOut = 0;
   s_wSatFadeScene = gpGlobals->wNumScene;
   s_qwSatReloadFirstStep = qwFirstStep;
   {
      SDL_Color black[256];
      memset(black, 0, sizeof(black));
      VIDEO_SetPalette(black);
   }
}

BOOL
PAL_SatReloadPending(
   VOID
)
{
   return s_qwSatReloadFirstStep != 0;
}

VOID
PAL_SatReloadCancel(
   VOID
)
{
   if (s_qwSatReloadFirstStep == 0) return;
   s_qwSatReloadFirstStep = 0;
   s_qwSatBlackSince = 0;
   s_qwSatSceneFadedOut = 0;
   VIDEO_SetPalette(PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette));
}

BOOL
PAL_SatRedrawFaded(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Saturn op 0x05 (0x06025740) while the fade flag 0x06076930 is set: 0x06021fd8(120), then the scene is
  presented with the stepped fade-in. emu entry 0x7dd (0x50, 0x05, text): black -> darkness 16 after 246 vsync
  (4100 ms), darkness 8 +5, 0 +6. Only used when the scene has not changed since the fade-out.
  Returns TRUE if the screen was faded in.
--*/
{
   Uint64 tBlack = s_qwSatSceneFadedOut ? s_qwSatSceneFadedOut : s_qwSatBlackSince;
   if (s_qwSatReloadFirstStep != 0)
   {
      s_qwSatSceneFadedOut = s_qwSatReloadFirstStep - 583 * g_iSatSynch / 6;
      s_qwSatReloadFirstStep = 0;
      s_iSatFirstDarkStep = 2;   /* emu o_r22c: darkness 16 (2 vsync), 8 (3), full, then the box */
      PAL_SatFadeIn(iPaletteNum, fNight);
      return TRUE;
   }
   if (tBlack == 0 || (s_qwSatSceneFadedOut == 0 && s_wSatFadeScene != gpGlobals->wNumScene)) return FALSE;
   s_qwSatSceneFadedOut = tBlack + (4050 - 583) * g_iSatSynch / 6;   /* 0x06021fd8(120) etc. are frames; 4050 ms at 6 vsync */
   PAL_SatFadeIn(iPaletteNum, fNight);
   return TRUE;
}

VOID
PAL_SatEmptyEnterScript(
   VOID
)
{
   // the fade-out came from a 0x50 (not the loader): treat the new scene like the faded one
   if (s_qwSatBlackSince != 0 && s_qwSatSceneFadedOut == 0) s_wSatFadeScene = gpGlobals->wNumScene;
}

VOID
PAL_SatEnterScriptEnd(
   VOID
)
{
   //
   // Saturn: an enter script that ran instructions ends with the fade flag cleared -- if it did not draw the scene
   // itself, the scene then appears at once (scene 16 0x102b = 0x4A, scene 20 0x15b4 = 0x49). Draws inside the script
   // still fade in stepwise (scene 2 0xb32: 0x65 0x46 0x15 0x54 0x09 -> emu o_r4g black, darkness 16 4 vsync,
   // 8 6 vsync, full).
   //
   if (s_qwSatBlackSince != 0 && s_qwSatSceneFadedOut == 0 && s_wSatFadeScene == gpGlobals->wNumScene) s_wSatFadeScene = 0;
}

BOOL g_fSatReloadFade = FALSE;

BOOL
PAL_SatReloadFadeIn(
   INT         iPaletteNum,
   BOOL        fNight
)
/*++
  Saturn loader reload after op 0x36 (load flag 0x10): emu o_i194 (2-vsync samples): black 48 vsync, then darkness
  16 (~4 vsync), 8 (~6 vsync), full (no darkness-24 step).  Darkness 16 800 ms after black.
--*/
{
   if (s_qwSatBlackSince == 0) return FALSE;
   s_qwSatSceneFadedOut = s_qwSatBlackSince + 800 - 583 * g_iSatSynch / 6;
   s_iSatFirstDarkStep = 2;
   PAL_SatFadeIn(iPaletteNum, fNight);
   return TRUE;
}

VOID
PAL_SatSceneLoadFade(
   WORD        wNewScene
)
/*++
  Saturn scene loader 0x06026f90 (scene change by op 0x59): when the fade flag 0x06076930 is clear it calls
  0x06023914(0) (the op 0x50 fade-out) before loading; the new scene's enter script then finds the flag set,
  so its 0x05 does the faded redraw.  emu new-game opening: dialog cleared at the script end, black 15 vsync
  later, Li's room fades in 118 vsync after black (3 vsync frames).
--*/
{
   if (s_qwSatBlackSince == 0 && s_qwSatSceneFadedOut == 0)
   {
      PAL_SatFadeOut(0);
      s_wSatFadeScene = wNewScene;   /* only for the loader's own fade (a 0x50 before 0x59 keeps the old rule) */
   }
}
