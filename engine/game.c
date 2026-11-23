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

int g_iSatSynch = 3;
int g_iSatVpX = 0, g_iSatVpY = 0;

VOID
PAL_SatGridSnap(
   int *px,
   int *py
)
{
   /* Saturn 0x06033dd8 (pixel -> lattice cell) followed by x = gx*32 + h*16, y = gy*16 + h*8 */
   int a = *px - 2 * *py - 16, b = *px + 2 * *py + 16;
   int u = b / 32, v, s, h, gx, gy;
   if (a <= 0) v = a / 32; else { v = a / 32 + 1; if ((a & 31) == 0) v--; }
   s = u + v;
   h = s & 1; if (h) s--;
   gx = s / 2; gy = gx - v;
   *px = gx * 32 + h * 16;
   *py = gy * 16 + h * 8;
}

BOOL g_fSatRngReload = FALSE;   /* Saturn op 0x36 sets load flag 0x10 (0x060299cc): reload this scene after the script */
BOOL g_fSatRngReloading = FALSE;
WORD g_wSatPrevScene = 0;   /* scene before the last op 0x59 (res.c loader fade) */   /* Saturn SGL frame count (vsyncs per frame), see game.h */

VOID
PAL_GameMain(
   VOID
)
/*++
  Purpose:

    The game entry routine.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   DWORD       dwTime, dwStart;

   //
   // Show the opening menu.
   //
   {  /* debug: env PAL_LOADSLOT=n skips the opening menu and loads n.rpg (test save states) */
      const char *ls = SDL_getenv("PAL_LOADSLOT");
      gpGlobals->bCurrentSaveSlot = ls ? (BYTE)SDL_atoi(ls) : (BYTE)PAL_OpeningMenu();
   }
   gpGlobals->fInMainGame = TRUE;

   //
   // Initialize game data and set the flags to load the game resources.
   //
   PAL_ReloadInNextTick(gpGlobals->bCurrentSaveSlot);

   //
   // Run the main game loop.
   //
   dwTime = SDL_GetTicks();
   while (TRUE)
   {
      //
      // Load the game resources if needed.
      //
      if (g_fSatRngReload)
      {
         //
         // Saturn 0x060299cc: op 0x36 loads the RNG and ORs 0x10 into the load flags 0x060768dc; once the script has
         // ended the loader 0x06026f90 reloads the current scene with the 0x06023914 fade (emu o_i194: dialog gone,
         // black 30 vsync later for 48 vsync, then stepped fade-in).
         //
         // A scene change (op 0x59) already pending is the same load: the new scene is loaded once (emu o_r4d
         // script 0xa98: 0x36 ... 0x50 ... 0x59 2 -> bedroom enter script 0xb06 fades in, then Li's dialog).
         //
         g_fSatRngReload = FALSE;
         if (!gpGlobals->fEnteringScene)
         {
            g_fSatRngReloading = TRUE;
            g_wSatPrevScene = gpGlobals->wNumScene;
            PAL_SetLoadFlags(kLoadScene);
         }
      }
      PAL_LoadResources();
      {  /* debug: env PAL_TELEPORT0=vx,vy,dir teleports once right after the save is loaded (before roaming enemies move) */
         static BOOL fTp0 = FALSE; const char *tp = SDL_getenv("PAL_TELEPORT0");
         if (!fTp0 && tp != NULL) { int ti, vx, vy, dr, tdx, tdy; extern int g_iSatVpX, g_iSatVpY; fTp0 = TRUE;
            vx = (int)SDL_strtol(tp, (char **)&tp, 0); vy = (*tp == ',') ? (int)SDL_strtol(tp + 1, (char **)&tp, 0) : 0; dr = (*tp == ',') ? SDL_atoi(tp + 1) : gpGlobals->wPartyDirection;
            tdx = vx - PAL_X(gpGlobals->viewport); tdy = vy - PAL_Y(gpGlobals->viewport);
            for (ti = 0; ti < MAX_PLAYABLE_PLAYER_ROLES; ti++) { gpGlobals->rgTrail[ti].x += tdx; gpGlobals->rgTrail[ti].y += tdy; if (SDL_getenv("PAL_TELEDIRALL")) gpGlobals->rgTrail[ti].wDirection = (WORD)dr; }   /* PAL_TELEDIRALL: followers face dr too (as after walking there) */
            gpGlobals->viewport = PAL_XY(vx, vy); g_iSatVpX = vx; g_iSatVpY = vy; gpGlobals->wPartyDirection = (WORD)dr; PAL_UpdatePartyGestures(FALSE); }
      }
      g_fSatRngReloading = FALSE;

      //
      // Clear the input state of previous frame.
      //
      PAL_ClearKeyState();

      //
      // Wait for the time of one frame. Accept input here.
      //
      PAL_DelayUntil(dwTime);

      //
      // Set the time of the next frame.
      //
      dwStart = SDL_GetTicks();

      //
      // Run the main frame routine.  Saturn: the frame lasts g_iSatSynch vsyncs (6 idle / 3 walking), set in it.
      //
      g_iSatVpX = PAL_X(gpGlobals->viewport); g_iSatVpY = PAL_Y(gpGlobals->viewport);   /* 0x06026ddc */
      {  /* debug: env PAL_FRLOG=file logs every field frame (ms scene vx vy dir inputdir) */
         static FILE *ffr = (FILE *)-1;
         if (ffr == (FILE *)-1) { const char *p = SDL_getenv("PAL_FRLOG"); ffr = p ? fopen(p, "w") : NULL; }
         if (ffr)
         {  /* env PAL_OBJLOG=n appends event object n's vanish timer */
            static int ob = -2; if (ob == -2) { const char *q = SDL_getenv("PAL_OBJLOG"); ob = q ? SDL_atoi(q) : 0; }
            fprintf(ffr, "%u %d %x %x %d %d %d %x\n", SDL_GetTicks(), gpGlobals->wNumScene, PAL_X(gpGlobals->viewport), PAL_Y(gpGlobals->viewport), gpGlobals->wPartyDirection, g_InputState.dir, ob > 0 ? gpGlobals->g.lprgEventObject[ob - 1].sVanishTime : 0, (unsigned)g_InputState.dwKeyPress);
            if (ob > 0) fprintf(ffr, "  obj %d %d %d %d\n", gpGlobals->g.lprgEventObject[ob - 1].x, gpGlobals->g.lprgEventObject[ob - 1].y, gpGlobals->g.lprgEventObject[ob - 1].wDirection, gpGlobals->g.lprgEventObject[ob - 1].sState);
            fflush(ffr);
         }
      }
      {  /* debug: env PAL_FORCEBATTLE=team starts one battle after a delay (HD-battle testing).
            Optional PAL_FORCEBATTLE_MS sets the delay (default 3500ms) so the battle can be fired
            after a new game is under way (real party loaded), not during the intro. */
         static int fb = -2; static Uint32 fbms = 3500u;
         if (fb == -2) { const char *e = SDL_getenv("PAL_FORCEBATTLE"); fb = e ? SDL_atoi(e) : -1;
            const char *d = SDL_getenv("PAL_FORCEBATTLE_MS"); if (d) fbms = (Uint32)SDL_atoi(d); }
         if (fb >= 0 && SDL_GetTicks() > fbms) { WORD t = (WORD)fb; fb = -1; PAL_StartBattle(t, FALSE); }
      }
      { extern VOID PAL_AutoSaveCheck(VOID); PAL_AutoSaveCheck(); }   /* auto save after each scene change */
      PAL_StartFrame();
      dwTime = dwStart + SAT_SCRIPT_FRAME_TIME;
   }
}
