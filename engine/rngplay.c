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
// Portions based on PalLibrary by Lou Yihua <louyihua@21cn.com>.
// Copyright (c) 2006-2007, Lou Yihua.
//

#include "main.h"

/* Saturn movies have frames of up to 67851 bytes (rng 29, 38, 43); with the 65000 of the DOS version those failed
   to load and the rest of the range was dropped (0x6450, rng 38 frames 94..107: two A Nu, Li Xiaoyao in mid-air). */
#define PAL_RNG_BUFSIZE 0x40000

static INT
PAL_RNGReadFrame(
   LPBYTE          lpBuffer,
   UINT            uiBufferSize,
   UINT            uiRngNum,
   UINT            uiFrameNum,
   FILE           *fpRngMKF
)
/*++
  Purpose:

    Read a frame from a RNG animation.

  Parameters:

    [OUT] lpBuffer - pointer to the destination buffer.

    [IN]  uiBufferSize - size of the destination buffer.

    [IN]  uiRngNum - the number of the RNG animation in the MKF archive.

    [IN]  uiFrameNum - frame number in the RNG animation.

    [IN]  fpRngMKF - pointer to the fopen'ed MKF file.

  Return value:

    Integer value which indicates the size of the chunk.
    -1 if there are error in parameters.
    -2 if buffer size is not enough.

--*/
{
   UINT         uiOffset       = 0;
   UINT         uiSubOffset    = 0;
   UINT         uiNextOffset   = 0;
   UINT         uiChunkCount   = 0;
   INT          iChunkLen      = 0;

   if (lpBuffer == NULL || fpRngMKF == NULL || uiBufferSize == 0)
   {
      return -1;
   }

   //
   // Get the total number of chunks.
   //
   uiChunkCount = PAL_MKFGetChunkCount(fpRngMKF);
   if (uiRngNum >= uiChunkCount)
   {
      return -1;
   }

   //
   // Get the offset of the chunk.
   //
   fseek(fpRngMKF, 4 * uiRngNum, SEEK_SET);
   PAL_fread(&uiOffset, sizeof(UINT), 1, fpRngMKF);
   PAL_fread(&uiNextOffset, sizeof(UINT), 1, fpRngMKF);
   uiOffset = SDL_SwapLE32(uiOffset);
   uiNextOffset = SDL_SwapLE32(uiNextOffset);

   //
   // Get the length of the chunk.
   //
   iChunkLen = uiNextOffset - uiOffset;
   if (iChunkLen != 0)
   {
      fseek(fpRngMKF, uiOffset, SEEK_SET);
   }
   else
   {
      return -1;
   }

   //
   // Get the number of sub chunks.
   //
   PAL_fread(&uiChunkCount, sizeof(UINT), 1, fpRngMKF);
   uiChunkCount = (SDL_SwapLE32(uiChunkCount) - 4) / 4;
   if (uiFrameNum >= uiChunkCount)
   {
      return -1;
   }

   //
   // Get the offset of the sub chunk.
   //
   fseek(fpRngMKF, uiOffset + 4 * uiFrameNum, SEEK_SET);
   PAL_fread(&uiSubOffset, sizeof(UINT), 1, fpRngMKF);
   PAL_fread(&uiNextOffset, sizeof(UINT), 1, fpRngMKF);
   uiSubOffset = SDL_SwapLE32(uiSubOffset);
   uiNextOffset = SDL_SwapLE32(uiNextOffset);

   //
   // Get the length of the sub chunk.
   //
   iChunkLen = uiNextOffset - uiSubOffset;
   if ((UINT)iChunkLen > uiBufferSize)
   {
      return -2;
   }

   if (iChunkLen != 0)
   {
      fseek(fpRngMKF, uiOffset + uiSubOffset, SEEK_SET);
      return (int)fread(lpBuffer, 1, iChunkLen, fpRngMKF);
   }

   return -1;
}

static INT
PAL_RNGBlitToSurface(
   const uint8_t   *rng,
   int              length,
   SDL_Surface     *lpDstSurface
)
/*++
  Purpose:

    Blit one frame in an RNG animation to an SDL surface.
    The surface should contain the last frame of the RNG, or blank if it's the first
    frame.

    NOTE: Assume the surface is already locked, and the surface is a 320x200 8-bit one.

  Parameters:

    [IN]  rng - Pointer to the RNG data.

    [IN]  length - Length of the RNG data.

    [OUT] lpDstSurface - pointer to the destination SDL surface.

  Return value:

    0 = success, -1 = error.

--*/
{
   int                   ptr         = 0;
   int                   dst_ptr     = 0;
   uint16_t              wdata       = 0;
   int                   x, y, i, n;

   //
   // Check for invalid parameters.
   //
   if (lpDstSurface == NULL || length < 0)
   {
      return -1;
   }

   //
   // Draw the frame to the surface.
   // FIXME: Dirty and ineffective code, needs to be cleaned up
   //
   while (ptr < length)
   {
      uint8_t data = rng[ptr++];
      switch (data)
      {
      case 0x00:
      case 0x13:
         //
         // End
         //
         goto end;

      case 0x02:
         dst_ptr += 2;
         break;

      case 0x03:
         data = rng[ptr++];
         dst_ptr += (data + 1) * 2;
         break;

      case 0x04:
         wdata = rng[ptr] | (rng[ptr + 1] << 8);
         ptr += 2;
         dst_ptr += ((unsigned int)wdata + 1) * 2;
         break;

      case 0x0a:
         x = dst_ptr % 320;
         y = dst_ptr / 320;
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         if (++x >= 320)
         {
            x = 0;
            ++y;
         }
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         dst_ptr += 2;

      case 0x09:
         x = dst_ptr % 320;
         y = dst_ptr / 320;
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         if (++x >= 320)
         {
            x = 0;
            ++y;
         }
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         dst_ptr += 2;

      case 0x08:
         x = dst_ptr % 320;
         y = dst_ptr / 320;
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         if (++x >= 320)
         {
            x = 0;
            ++y;
         }
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         dst_ptr += 2;

      case 0x07:
         x = dst_ptr % 320;
         y = dst_ptr / 320;
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         if (++x >= 320)
         {
            x = 0;
            ++y;
         }
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         dst_ptr += 2;

      case 0x06:
         x = dst_ptr % 320;
         y = dst_ptr / 320;
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         if (++x >= 320)
         {
            x = 0;
            ++y;
         }
         ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
         dst_ptr += 2;
         break;

      case 0x0b:
         data = *(rng + ptr++);
         for (i = 0; i <= data; i++)
         {
            x = dst_ptr % 320;
            y = dst_ptr / 320;
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
            if (++x >= 320)
            {
               x = 0;
               ++y;
            }
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
            dst_ptr += 2;
         }
         break;

      case 0x0c:
         wdata = rng[ptr] | (rng[ptr + 1] << 8);
         ptr += 2;
         for (i = 0; i <= wdata; i++)
         {
            x = dst_ptr % 320;
            y = dst_ptr / 320;
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
            if (++x >= 320)
            {
               x = 0;
               ++y;
            }
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr++];
            dst_ptr += 2;
         }
         break;

      case 0x0d:
      case 0x0e:
      case 0x0f:
      case 0x10:
         for (i = 0; i < data - (0x0d - 2); i++)
         {
            x = dst_ptr % 320;
            y = dst_ptr / 320;
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr];
            if (++x >= 320)
            {
               x = 0;
               ++y;
            }
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr + 1];
            dst_ptr += 2;
         }
         ptr += 2;
         break;

      case 0x11:
    	 data = *(rng + ptr++);
         for (i = 0; i <= data; i++)
         {
            x = dst_ptr % 320;
            y = dst_ptr / 320;
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr];
            if (++x >= 320)
            {
               x = 0;
               ++y;
            }
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr + 1];
            dst_ptr += 2;
         }
         ptr += 2;
         break;

      case 0x12:
         n = (rng[ptr] | (rng[ptr + 1] << 8)) + 1;
         ptr += 2;
         for (i = 0; i < n; i++)
         {
            x = dst_ptr % 320;
            y = dst_ptr / 320;
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr];
            if (++x >= 320)
            {
               x = 0;
               ++y;
            }
            ((LPBYTE)(lpDstSurface->pixels))[y * lpDstSurface->pitch + x] = rng[ptr + 1];
            dst_ptr += 2;
         }
         ptr += 2;
         break;
      }
   }

end:
   return 0;
}

//
// Saturn: after a movie (0x37) outside battle its last frame stays the backdrop: 0x05 puts that frame back instead of
// drawing the map, and a new dialog box does not erase the previous one (emu o_r4d, script 0xa98: Li's lower box stays
// while Li Da Niang's upper box is added; 0x05 clears both and leaves the movie frame). The map comes back with the
// next PAL_MakeScene (0x50/0x59 ...).
//
BOOL           g_fSatRNGBackdrop = FALSE;
static SDL_Color s_rgSatRNGHdPal[256];   /* colours of the movie frame last laid into the HD twin */
static BOOL      s_fSatRNGHdPal = FALSE;
extern SDL_Surface *g_pSatRNGShown;

VOID
PAL_SatRNGShownToHD(
   VOID
)
/*
  HD twin: show the bare movie frame that the low-res screen shows (g_pSatRNGShown) -- used where a dialog box over
  the movie is cleared (op 0x05 / 0x09). Built from the low-res frame itself, so the two always agree, also when a
  0x37 range showed no frame of its own (start == end: only the hidden end frame is decoded).
*/
{
   extern void PAL_HDM_FromIndexed(SDL_Surface *s8, const SDL_Color *pal);
   extern void PAL_HDM_MovieBaseSave(void);
   extern void PAL_HDM_MovieBaseRestore(void);
   if (g_pSatRNGShown == NULL) return;
   if (!s_fSatRNGHdPal) { PAL_HDM_MovieBaseRestore(); return; }   /* no frame of this movie in the twin yet */
   PAL_HDM_FromIndexed(g_pSatRNGShown, s_rgSatRNGHdPal);
   PAL_HDM_MovieBaseSave();
}
SDL_Surface   *g_pSatRNGBackdrop = NULL;   /* decoding state of the movie */
SDL_Surface   *g_pSatRNGShown = NULL;      /* the movie frame on screen (0x05 puts it back) */

VOID
PAL_RNGPlay(
   INT           iNumRNG,
   INT           iStartFrame,
   INT           iEndFrame,
   INT           iSpeed
)
/*++
  Purpose:

    Play a RNG movie.

  Parameters:

    [IN]  iNumRNG - number of the RNG movie.

    [IN]  iStartFrame - start frame number.

    [IN]  iEndFrame - end frame number.

    [IN]  iSpeed - speed of playing.

  Return value:

    None.

--*/
{
   double         iDelay = (double)SDL_GetPerformanceFrequency() / (iSpeed == 0 ? 16 : iSpeed);
   uint8_t        *rng = (uint8_t *)malloc(PAL_RNG_BUFSIZE);
   uint8_t        *buf = (uint8_t *)malloc(PAL_RNG_BUFSIZE);
   FILE           *fp = UTIL_OpenRequiredFile("rng.mkf");
   // Saturn port: each movie has its own palette (rngpal.dat = n x 768 bytes, 6-bit RGB)
   SDL_Color       rgMoviePal[256], rgSavedPal[256];
   BOOL            fMoviePal = FALSE;
   {
      FILE *fpp = UTIL_OpenFile("rngpal.dat");
      if (fpp)
      {
         BYTE raw[768];
         if (fseek(fpp, iNumRNG * 768, SEEK_SET) == 0 && fread(raw, 1, 768, fpp) == 768)
         {
            int k;
            for (k = 0; k < 256; k++)
            {
               rgMoviePal[k].r = (raw[k * 3] << 2) | (raw[k * 3] >> 4);
               rgMoviePal[k].g = (raw[k * 3 + 1] << 2) | (raw[k * 3 + 1] >> 4);
               rgMoviePal[k].b = (raw[k * 3 + 2] << 2) | (raw[k * 3 + 2] >> 4);
               rgMoviePal[k].a = 255;
            }
            memcpy(rgSavedPal, VIDEO_GetPalette(), sizeof(rgSavedPal));
            fMoviePal = TRUE;
         }
         UTIL_CloseFile(fpp);
      }
   }

   //
   // Saturn (emu o_c194): the movie covers rows 0..199; screen row 212 (bitmap row 200) is black during the movie,
   // so drop whatever the last dialog box left there.
   //
   //
   // Saturn: outside battle the movie lives on its own layer (g_pSatRNGBackdrop). A dialog box that is up when the
   // movie starts stays on top, untouched by the frames (emu o_r4d k08/k09: Li Da Niang's box over frames 0x1b..0x1d),
   // and the next 0x05 shows the bare movie frame.
   //
   SDL_Surface *pTarget = gpScreen, *pBoxBase = NULL, *pBoxSnap = NULL;
   BOOL fLastHidden = FALSE, fShownSaved = FALSE, fHdDlgSaved = FALSE;
   unsigned char *pBoxMask = NULL;
   if (!gpGlobals->fInBattle)
   {
      extern SDL_Surface *PAL_SatDialogBaseIfShown(VOID);
      if (g_pSatRNGBackdrop == NULL) g_pSatRNGBackdrop = VIDEO_CreateCompatibleSurface(gpScreen);
      if (g_pSatRNGBackdrop != NULL)
      {
         pTarget = g_pSatRNGBackdrop;
         if (g_pSatRNGShown == NULL) g_pSatRNGShown = VIDEO_CreateCompatibleSurface(gpScreen);
         pBoxBase = PAL_SatDialogBaseIfShown();
         if (SDL_getenv("PAL_WKLOG")) { FILE *fw = fopen(SDL_getenv("PAL_WKLOG"), "a"); if (fw) { fprintf(fw, "rng %d %d-%d box %p bd %d t %u\n", iNumRNG, iStartFrame, iEndFrame, (void *)pBoxBase, g_fSatRNGBackdrop, (unsigned)SDL_GetTicks()); fclose(fw); } }
         if (!g_fSatRNGBackdrop) s_fSatRNGHdPal = FALSE;   /* a new backdrop: nothing of it in the HD twin yet */
         /* ...so the bare picture to go back to (0x05 before any frame is shown) is the twin as it is now */
         if (!g_fSatRNGBackdrop && pBoxBase == NULL) { extern void PAL_HDM_MovieBaseSave(void); PAL_HDM_MovieBaseSave(); }
         if (!g_fSatRNGBackdrop) VIDEO_CopySurface(pBoxBase ? pBoxBase : gpScreen, NULL, pTarget, NULL);
         if (pBoxBase != NULL && (pBoxSnap = VIDEO_CreateCompatibleSurface(gpScreen)) != NULL &&
             (pBoxMask = (unsigned char *)malloc((size_t)gpScreen->w * gpScreen->h)) != NULL)
         {
            const unsigned char *bs = SAT_BankPlane(gpScreen, FALSE), *bb = SAT_BankPlane(pBoxBase, FALSE);
            int x, y;
            VIDEO_CopySurface(gpScreen, NULL, pBoxSnap, NULL);
            for (y = 0; y < gpScreen->h; y++)
               for (x = 0; x < gpScreen->w; x++)
               {
                  int i = y * gpScreen->w + x;
                  pBoxMask[i] = ((BYTE *)gpScreen->pixels)[y * gpScreen->pitch + x] != ((BYTE *)pBoxBase->pixels)[y * pBoxBase->pitch + x] ||
                     (bs ? bs[i] : 0) != (bb ? bb[i] : 0);
               }
         }
         else pBoxBase = NULL;
      }
   }
   if (pTarget->h > 200)
   {
      SDL_Rect rb = { 0, 200, 320, (Uint16)(pTarget->h - 200) };
      SDL_FillRect(pTarget, &rb, 0);
   }
   SAT_BankFill(pTarget, NULL, 0);   // movie pixels use the movie palette only: drop bank tags of the previous scene
   BOOL fFadePending = gpGlobals->fNeedToFadeIn;

   //
   // Avoid losing the last frame
   //
   if (iEndFrame > 0) iEndFrame++;

   for (double iTime = SDL_GetPerformanceCounter(); rng && buf && iStartFrame != iEndFrame; iStartFrame++)
   {
      iTime += iDelay;

      //
      // Read, decompress and render the frame
      //
      //
      // Saturn (outside battle): the end frame of an explicit range is decoded (the next range builds on it) but never
      // shown -- 0x37 1b 1d leaves frame 0x1c on screen, also after 0x05 (emu o_r4d k11..k13). Played to the end
      // (operand 0) the last frame is shown (0x37 1e 0 -> 0x1f, k18..k20).
      //
      fLastHidden = pTarget != gpScreen && iEndFrame > 0 && iStartFrame == iEndFrame - 1;
      if (fLastHidden && g_pSatRNGShown != NULL) VIDEO_CopySurface(pTarget, NULL, g_pSatRNGShown, NULL);

      if (PAL_RNGReadFrame(buf, PAL_RNG_BUFSIZE, iNumRNG, iStartFrame, fp) < 0 ||
          PAL_RNGBlitToSurface(rng, Decompress(buf, rng, PAL_RNG_BUFSIZE), pTarget) == -1)
      {
         //
         // Failed to get the frame, don't go further
         //
         break;
      }
      {  /* debug: env PAL_RNGDUMP=dir saves every decoded movie frame (r<rng>_<seq>_<frame>[h].bmp, h = hidden) */
         const char *rd = SDL_getenv("PAL_RNGDUMP");
         if (rd) { static int seq = 0; char pp[512];
            SDL_snprintf(pp, sizeof pp, "%s/r%02d_%04d_%03d%s.bmp", rd, iNumRNG, seq++, iStartFrame, fLastHidden ? "h" : "");
            SDL_SaveBMP(pTarget, pp); }
      }
      if (fLastHidden)
      {
         fShownSaved = TRUE;
         break;
      }
      if (pTarget != gpScreen)
      {
         VIDEO_CopySurface(pTarget, NULL, gpScreen, NULL);
         if (pBoxMask != NULL)
         {
            unsigned char *bd = SAT_BankPlane(gpScreen, TRUE);
            const unsigned char *bn = SAT_BankPlane(pBoxSnap, FALSE);
            int x, y;
            for (y = 0; y < gpScreen->h; y++)
               for (x = 0; x < gpScreen->w; x++)
               {
                  int i = y * gpScreen->w + x;
                  if (!pBoxMask[i]) continue;
                  ((BYTE *)gpScreen->pixels)[y * gpScreen->pitch + x] = ((BYTE *)pBoxSnap->pixels)[y * pBoxSnap->pitch + x];
                  if (bd) bd[i] = bn ? bn[i] : 0;
               }
         }
      }

      {  /* HD twin: this movie frame as the base, the HD dialog box (if one is up) kept on top */
         extern void PAL_HDM_FromIndexed(SDL_Surface *s8, const SDL_Color *pal);
         extern void PAL_HDM_DlgSave(void); extern void PAL_HDM_DlgReapply(void);
         /* the box that is up, face and name included, taken once against the dialog-free twin: against the
            backup (which holds the face and name once a text line is shown) only the text came back and the
            portrait and name vanished from the HD picture as soon as the movie moved (0x1dc4.., 0x1eb4) */
         if (pBoxMask != NULL && !fHdDlgSaved) { extern void PAL_HDM_DlgSaveAll(void); PAL_HDM_DlgSaveAll(); fHdDlgSaved = TRUE; }
         { extern int g_hdmKeepBackup; g_hdmKeepBackup = (pBoxMask != NULL); }
         PAL_HDM_FromIndexed(pTarget, fMoviePal ? rgMoviePal : VIDEO_GetPalette());
         { extern int g_hdmKeepBackup; g_hdmKeepBackup = 0; }
         memcpy(s_rgSatRNGHdPal, fMoviePal ? rgMoviePal : VIDEO_GetPalette(), sizeof(s_rgSatRNGHdPal)); s_fSatRNGHdPal = TRUE;
         { extern void PAL_HDM_MovieBaseSave(void); if (pTarget != gpScreen) PAL_HDM_MovieBaseSave(); }
         if (pBoxMask != NULL) { extern void PAL_HDM_DlgReapplyKeep(void); PAL_HDM_DlgReapplyKeep(); }
      }

      if (fMoviePal)
      {
         VIDEO_SetPalette(rgMoviePal);
         gpGlobals->fNeedToFadeIn = FALSE;
      }

      //
      // Update the screen
      //
      VIDEO_UpdateScreen(NULL);

      //
      // Fade in the screen if needed
      //
      if (gpGlobals->fNeedToFadeIn)
      {
         PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
         gpGlobals->fNeedToFadeIn = FALSE;
         iTime = SDL_GetPerformanceCounter() + iDelay / 2;
      }

      //
      // Delay for a while
      //
	  PAL_DelayUntilPC(iTime);
   }

   fclose(fp);
   free(rng);
   free(buf);
   if (pTarget != gpScreen)
   {
      g_fSatRNGBackdrop = TRUE;
      if (!fShownSaved && g_pSatRNGShown != NULL) VIDEO_CopySurface(pTarget, NULL, g_pSatRNGShown, NULL);
      if (fMoviePal)
      {
         //
         // Saturn: the movie layer keeps the movie colours after the movie (the dialog boxes on top use their own
         // banks); tag the movie pixels with the movie bank so restoring the scene palette leaves them alone.
         //
         extern int SAT_MovieBank(const SDL_Color *pal);
         BYTE bank = (BYTE)SAT_MovieBank(rgMoviePal);
         SDL_Rect rm = { 0, 0, 320, 200 };
         unsigned char *bd;
         SAT_BankFill(pTarget, &rm, bank);
         if (g_pSatRNGShown != NULL) SAT_BankFill(g_pSatRNGShown, &rm, bank);
         if ((bd = SAT_BankPlane(gpScreen, TRUE)) != NULL)
         {
            int x, y;
            for (y = 0; y < 200 && y < gpScreen->h; y++)
               for (x = 0; x < gpScreen->w; x++)
                  if (pBoxMask == NULL || !pBoxMask[y * gpScreen->w + x]) bd[y * gpScreen->w + x] = bank;
         }
      }
   }
   if (fMoviePal)
   {
      //
      // A movie played from a scene's enter script (0x1dc3: 0x36 0c / 0x37 00 1a before the first 0x05) replaces the
      // pending scene fade-in: the saved palette is still black there, so bring the scene palette back in full
      // (emu o_r33a k00_0590: the dialog box is shown in full colour over the last frame).
      //
      if (fFadePending)
         VIDEO_SetPalette(PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette));
      else
         VIDEO_SetPalette(rgSavedPal);
   }
   { extern VOID PAL_DbgHdmDump(VOID); PAL_DbgHdmDump(); }   /* debug (PAL_HDMCHECK): state after each range */
   if (pBoxSnap != NULL) { SAT_BankFree(pBoxSnap); VIDEO_FreeSurface(pBoxSnap); }
   free(pBoxMask);
}
