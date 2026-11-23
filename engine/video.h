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

#ifndef VIDEO_H
#define VIDEO_H

#include "common.h"

#define TOUCHOVERLAY_ALPHAMOD           120

/* Saturn palette banks: every 8-bit surface that holds game graphics may carry a "bank plane" (one byte per
   pixel, 0 = scene palette, n = ALLDAT palette n).  Copies carry the plane along; see video.c SAT_*. */
#define VIDEO_CopySurface(s, sr, t, tr) SAT_Blit((s), (sr), (t), (tr))
#define VIDEO_CopyEntireSurface(s, t)   SAT_Blit((s), NULL, (t), NULL)
/* HD-mirror: when the backup/restore targets gpScreen (dialogue text erase), sync the HD twin too. */
extern void PAL_HDM_Backup(void);
extern void PAL_HDM_Restore(void);
#define VIDEO_BackupScreen(s)           do { SAT_Blit((s), NULL, gpScreenBak, NULL); if ((s) == gpScreen) PAL_HDM_Backup(); } while (0)
#define VIDEO_RestoreScreen(t)          do { SAT_Blit(gpScreenBak, NULL, (t), NULL); if ((t) == gpScreen) PAL_HDM_Restore(); } while (0)
#define VIDEO_FreeSurface(s)            (SAT_BankFree(s), SDL_FreeSurface(s))

PAL_C_LINKAGE_BEGIN

extern BYTE g_bSatTextBank;          /* bank used by the font renderer (Saturn text lives on the UI layer: 3) */
unsigned char *SAT_BankPlane(SDL_Surface *s, BOOL fCreate);
void SAT_BankFree(SDL_Surface *s);
void SAT_BankFill(SDL_Surface *s, const SDL_Rect *r, BYTE bank);
int SAT_Blit(SDL_Surface *src, const SDL_Rect *sr, SDL_Surface *dst, const SDL_Rect *dr);
void SAT_ApplyBanks(SDL_Surface *src, int srcY, int dstY, int rows, int x0, int x1);

extern SDL_Surface *gpScreen;
extern SDL_Surface *gpScreenBak;
extern volatile BOOL g_bRenderPaused;

#if PAL_HAS_GLSL
void Filter_StepParamSlot(int step);
void Filter_StepCurrentParam(int step);
#endif

INT
VIDEO_Startup(
   VOID
);

VOID
VIDEO_Shutdown(
   VOID
);

VOID
VIDEO_UpdateScreen(
   const SDL_Rect  *lpRect
);

VOID
VIDEO_SetPalette(
   SDL_Color        rgPalette[256]
);

VOID
VIDEO_Resize(
   INT             w,
   INT             h
);

SDL_Color *
VIDEO_GetPalette(
   VOID
);

VOID
VIDEO_ToggleFullscreen(
   VOID
);

VOID
VIDEO_ChangeDepth(
   INT             bpp
);

VOID
VIDEO_SaveScreenshot(
   VOID
);

VOID
VIDEO_ShakeScreen(
   WORD           wShakeTime,
   WORD           wShakeLevel
);

VOID
VIDEO_SwitchScreen(
   WORD           wSpeed
);

VOID
VIDEO_FadeScreen(
   WORD           wSpeed
);

void
VIDEO_SetWindowTitle(
	const char*   pszTitle
);

SDL_Surface *
VIDEO_DuplicateSurface(
	SDL_Surface    *pSource,
	const SDL_Rect *pRect
);

SDL_Surface *
VIDEO_CreateCompatibleSurface(
	SDL_Surface    *pSource
);

SDL_Surface *
VIDEO_CreateCompatibleSizedSurface(
	SDL_Surface    *pSource,
	const SDL_Rect *pSize
);

void
VIDEO_UpdateSurfacePalette(
	SDL_Surface    *pSurface
);

VOID
VIDEO_DrawSurfaceToScreen(
    SDL_Surface    *pSurface
);

VOID
VIDEO_RenderCopy(
    VOID
);

VOID
VIDEO_SetupTouchArea(
    int window_w,
    int window_h,
    int draw_w,
    int draw_h
);

#if SDL_VERSION_ATLEAST(3,0,0)
SDL_ScaleMode
VIDEO_GetScaleMode(
	VOID
);
#endif

PAL_C_LINKAGE_END

#endif
