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

/* Port (user request): the name of the map in small text at the top left for 3 seconds after entering it.
   The game data has no place names; scenenames.h is a hand-made table indexed by scene number. */
#include "scenenames.h"
static BOOL   s_fMapNamePending = FALSE;
static WORD   s_wMapNameScene = 0; static WORD s_wMapNameLast = 0;
static Uint64 s_qwMapNameUntil = 0;

static VOID
PAL_MapNameDraw(
   VOID
)
{
   extern VOID PAL_DrawSmallText(LPCWSTR lpszText, int x, int y, int px, BYTE bColor, BOOL fRight);
   LPCWSTR name;

   if (s_fMapNamePending || s_wMapNameLast != gpGlobals->wNumScene)   /* scene entered, or a save loaded */
   {
      /* the 3 seconds start with the first free-roam frame, not during the entry cutscene */
      s_fMapNamePending = FALSE;
      s_wMapNameScene = gpGlobals->wNumScene; s_wMapNameLast = gpGlobals->wNumScene;
      s_qwMapNameUntil = SDL_GetTicks() + 3000;
   }
   if (s_wMapNameScene != gpGlobals->wNumScene || SDL_GetTicks() >= s_qwMapNameUntil) return;
   if (s_wMapNameScene >= sizeof(g_rgwszSceneName) / sizeof(g_rgwszSceneName[0])) return;
   name = g_rgwszSceneName[s_wMapNameScene];
   if (name == NULL || name[0] == 0) return;
   PAL_DrawSmallText(PAL_Tr(name), 5, 4, 10, MENUITEM_COLOR, FALSE);
}

/* Port (user request): system menu toggle "no encounter". While it is on, the monsters roaming the field do not
   start a battle when they touch the party. A roaming monster = an object triggered by touch whose trigger
   script starts with the battle op 0x0007 and has a flee branch (operand 2); bosses and story fights have no
   flee branch or start with other ops, so they are not affected. Off at every launch. */
BOOL g_fNoEncounter = FALSE;

static BOOL
PAL_IsFieldMonster(
   LPEVENTOBJECT p
)
{
   LPSCRIPTENTRY s;
   if (p->wTriggerScript == 0 || p->wTriggerMode < kTriggerTouchNear) return FALSE;
   s = &gpGlobals->g.lprgScriptEntry[p->wTriggerScript];
   return s->wOperation == 0x0007 && s->rgwOperand[2] != 0;
}

/* ---------------------------------------------------------------------------------------------------------------
   Port (user request): automap. In the scenes with roaming monsters (the mazes) a floating map is shown at the
   top right of the picture, in the manner of the Diablo automap: no panel or background, only the outline of
   the walkable ground as thin lines over the game picture, drawn for the parts the party has already seen,
   centred on the party, which is an arrow pointing the way it faces. Exits that have been seen are small
   diamonds. Tab hides / shows it. What has been seen is kept per scene in "automap.dat" beside the saves.

   The map is drawn here into an ARGB surface (twice the shown size, so the scaled lines are smooth) and laid
   over the presented frame by video.c; neither the game picture nor its HD twin is touched.
   --------------------------------------------------------------------------------------------------------------- */
#define AM_W        960
#define AM_H        660
#define AM_NUM      6            /* surface pixels per world pixel = AM_NUM / AM_DEN */
#define AM_DEN      4
#define AM_MAXEXIT  64

int           g_fAutomapUserOff = 0;      /* Tab */
static int    s_fAmFrame = 0;             /* 1 only while the free-roam frame is presented */
static SDL_Surface *s_pAmSurf = NULL;
static int    s_iAmSerial = 0;
static WORD   s_wAmScene = 0;
static BOOL   s_fAmEligible = FALSE;
static BYTE   s_rgAmSeen[128][64][2];
static BYTE   s_rgAmReach[128][64][2];
static BOOL   s_fAmSeenDirty = FALSE;
static WORD   s_rgAmExit[AM_MAXEXIT];
static int    s_nAmExit = 0;
static int    s_iAmLastX = -1, s_iAmLastY = -1, s_iAmLastDir = -1, s_iAmLastState = -1;

static BOOL
AM_Walkable(int x, int y, int h)
{
   if (x < 0 || x >= 64 || y < 0 || y >= 128) return FALSE;
   return !PAL_MapTileIsBlocked((BYTE)x, (BYTE)y, (BYTE)h, PAL_GetCurrentMap());
}

static VOID
AM_Neighbor(int x, int y, int h, int k, int *nx, int *ny, int *nh)
{  /* k: 0 = NE, 1 = SE, 2 = SW, 3 = NW */
   static const int d0[4][2] = { { 0, -1 }, { 0, 0 }, { -1, 0 }, { -1, -1 } };
   static const int d1[4][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } };
   if (h == 0) { *nx = x + d0[k][0]; *ny = y + d0[k][1]; *nh = 1; }
   else        { *nx = x + d1[k][0]; *ny = y + d1[k][1]; *nh = 0; }
}

static VOID
AM_PixelToTile(int px, int py, int *tx, int *ty, int *th)
{  /* as PAL_CheckObstacle */
   int x = px / 32, y = py / 16, h = 0, xr = px % 32, yr = py % 16;
   if (xr + yr * 2 >= 16)
   {
      if (xr + yr * 2 >= 48) { x++; y++; }
      else if (32 - xr + yr * 2 < 16) x++;
      else if (32 - xr + yr * 2 < 48) h = 1;
      else y++;
   }
   *tx = x; *ty = y; *th = h;
}

static VOID
AM_Flood(int sx, int sy, int sh)
{  /* the ground that can be walked to from here: only that is ever drawn */
   static unsigned short st[128 * 64 * 2];
   int n = 0, k;
   if (!AM_Walkable(sx, sy, sh) || s_rgAmReach[sy][sx][sh]) return;
   s_rgAmReach[sy][sx][sh] = 1; st[n++] = (unsigned short)((sy << 7) | (sx << 1) | sh);
   while (n > 0)
   {
      int v = st[--n], x = (v >> 1) & 63, y = v >> 7, h = v & 1;
      for (k = 0; k < 4; k++)
      {
         int nx, ny, nh;
         AM_Neighbor(x, y, h, k, &nx, &ny, &nh);
         if (AM_Walkable(nx, ny, nh) && !s_rgAmReach[ny][nx][nh])
         {
            s_rgAmReach[ny][nx][nh] = 1; st[n++] = (unsigned short)((ny << 7) | (nx << 1) | nh);
         }
      }
   }
}

static VOID
AM_Store(VOID)
{
   FILE *fp;
   if (!s_fAmSeenDirty || s_wAmScene == 0) return;
   s_fAmSeenDirty = FALSE;
   fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, "automap.dat", "r+b");
   if (fp == NULL) fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, "automap.dat", "w+b");
   if (fp == NULL) return;
   {  /* one 2048-byte bit block per scene */
      static BYTE blk[2048]; int x, y, h, i = 0;
      memset(blk, 0, sizeof(blk));
      for (y = 0; y < 128; y++) for (x = 0; x < 64; x++) for (h = 0; h < 2; h++, i++)
         if (s_rgAmSeen[y][x][h]) blk[i >> 3] |= (BYTE)(1 << (i & 7));
      if (fseek(fp, (long)s_wAmScene * 2048L, SEEK_SET) == 0) fwrite(blk, 1, sizeof(blk), fp);
   }
   fclose(fp);
}

VOID
PAL_AutomapFlush(VOID)
{
   AM_Store();
}

static VOID
AM_EnterScene(VOID)
{
   FILE *fp; WORD id; int x, y, h, i = 0, nMon = 0;
   AM_Store();
   s_wAmScene = gpGlobals->wNumScene;
   memset(s_rgAmSeen, 0, sizeof(s_rgAmSeen));
   memset(s_rgAmReach, 0, sizeof(s_rgAmReach));
   s_nAmExit = 0; s_iAmLastX = -1;
   for (id = gpGlobals->g.rgScene[s_wAmScene - 1].wEventObjectIndex + 1;
        id <= gpGlobals->g.rgScene[s_wAmScene].wEventObjectIndex; id++)
   {
      LPEVENTOBJECT p = &gpGlobals->g.lprgEventObject[id - 1];
      WORD e = p->wTriggerScript; int k;
      if (PAL_IsFieldMonster(p)) { nMon++; continue; }
      for (k = 0; e != 0 && k < 24; k++)   /* an exit: its script changes the scene */
      {
         LPSCRIPTENTRY s = &gpGlobals->g.lprgScriptEntry[e];
         if (s->wOperation == 0x0059) { if (s_nAmExit < AM_MAXEXIT) s_rgAmExit[s_nAmExit++] = id; break; }
         if (s->wOperation == 0x0000 || s->wOperation == 0x0002 || s->wOperation == 0xFFFF) break;
         e = (s->wOperation == 0x0003) ? s->rgwOperand[0] : (WORD)(e + 1);
      }
   }
   s_fAmEligible = (nMon > 0);
   if (!s_fAmEligible) return;
   fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, "automap.dat");
   if (fp != NULL)
   {
      static BYTE blk[2048];
      if (fseek(fp, (long)s_wAmScene * 2048L, SEEK_SET) == 0 && fread(blk, 1, sizeof(blk), fp) == sizeof(blk))
         for (y = 0; y < 128; y++) for (x = 0; x < 64; x++) for (h = 0; h < 2; h++, i++)
            s_rgAmSeen[y][x][h] = (blk[i >> 3] >> (i & 7)) & 1;
      fclose(fp);
   }
}

static VOID
AM_Put(int x, int y, Uint32 argb)
{  /* a line pixel replaces anything; a shadow pixel only goes where nothing is yet. Fades out towards the edge. */
   Uint32 *p; int a = (int)(argb >> 24), e, m = 48;
   if (x < 0 || y < 0 || x >= AM_W || y >= AM_H) return;
   e = x; if (AM_W - 1 - x < e) e = AM_W - 1 - x; if (y < e) e = y; if (AM_H - 1 - y < e) e = AM_H - 1 - y;
   if (e < m) a = a * e / m;
   p = (Uint32 *)((Uint8 *)s_pAmSurf->pixels + y * s_pAmSurf->pitch) + x;
   if ((argb & 0x00FFFFFF) == 0 && *p != 0) return;
   *p = ((Uint32)a << 24) | (argb & 0x00FFFFFF);
}

static VOID
AM_Line(int x0, int y0, int x1, int y1, int t, Uint32 argb)
{
   int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy, i, j;
   if ((x0 < -t && x1 < -t) || (y0 < -t && y1 < -t) || (x0 > AM_W + t && x1 > AM_W + t) || (y0 > AM_H + t && y1 > AM_H + t)) return;
   for (;;)
   {
      for (j = -(t / 2); j < t - t / 2; j++) for (i = -(t / 2); i < t - t / 2; i++) AM_Put(x0 + i, y0 + j, argb);
      if (x0 == x1 && y0 == y1) break;
      { int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; } }
   }
}

static VOID
AM_Redraw(int px, int py)
{
   static const int vx[4] = { 0, 16, 0, -16 }, vy[4] = { -8, 0, 8, 0 };   /* diamond: top, right, bottom, left */
   const Uint32 cShadow = 0x78000000u, cWall = 0xE6D8B870u, cExit = 0xF080D0FFu, cMe = 0xFFFFFFFFu;
   int pass, x, y, h, k, tx0, ty0, i;
   int rx = (AM_W / 2) * AM_DEN / AM_NUM + 32, ry = (AM_H / 2) * AM_DEN / AM_NUM + 16;
   SDL_FillRect(s_pAmSurf, NULL, 0);
   tx0 = px / 32; ty0 = py / 16;
   for (pass = 0; pass < 2; pass++)   /* 0 = dark shadow under everything, 1 = the lines */
   {
      for (y = ty0 - ry / 16 - 1; y <= ty0 + ry / 16 + 1; y++)
      {
         if (y < 0 || y >= 128) continue;
         for (x = tx0 - rx / 32 - 1; x <= tx0 + rx / 32 + 1; x++)
         {
            if (x < 0 || x >= 64) continue;
            for (h = 0; h < 2; h++)
            {
               int cx, cy;
               if (!s_rgAmSeen[y][x][h] || !s_rgAmReach[y][x][h]) continue;
               cx = x * 32 + h * 16 - px; cy = y * 16 + h * 8 - py;
               for (k = 0; k < 4; k++)   /* an edge wherever the ground ends */
               {
                  int nx, ny, nh, k2 = (k + 1) & 3;
                  AM_Neighbor(x, y, h, k, &nx, &ny, &nh);
                  if (AM_Walkable(nx, ny, nh)) continue;
                  AM_Line(AM_W / 2 + (cx + vx[k]) * AM_NUM / AM_DEN, AM_H / 2 + (cy + vy[k]) * AM_NUM / AM_DEN,
                          AM_W / 2 + (cx + vx[k2]) * AM_NUM / AM_DEN, AM_H / 2 + (cy + vy[k2]) * AM_NUM / AM_DEN,
                          pass == 0 ? 7 : 3, pass == 0 ? cShadow : cWall);
               }
            }
         }
      }
      for (i = 0; i < s_nAmExit; i++)   /* exits already seen */
      {
         LPEVENTOBJECT p = &gpGlobals->g.lprgEventObject[s_rgAmExit[i] - 1];
         int ex, ey, eh, sx, sy, r;
         if (p->sState == kObjStateHidden || p->sVanishTime != 0) continue;
         AM_PixelToTile(p->x, p->y, &ex, &ey, &eh);
         if (ex < 0 || ex >= 64 || ey < 0 || ey >= 128 || !s_rgAmSeen[ey][ex][eh]) continue;
         sx = AM_W / 2 + (p->x - px) * AM_NUM / AM_DEN; sy = AM_H / 2 + (p->y - py) * AM_NUM / AM_DEN;
         for (r = (pass == 0 ? 18 : 14); r >= 0; r -= 2)
         {
            Uint32 c = pass == 0 ? cShadow : cExit; int t = pass == 0 ? 4 : 2;
            AM_Line(sx, sy - r / 2, sx + r, sy, t, c); AM_Line(sx + r, sy, sx, sy + r / 2, t, c);
            AM_Line(sx, sy + r / 2, sx - r, sy, t, c); AM_Line(sx - r, sy, sx, sy - r / 2, t, c);
         }
      }
      {  /* the party: an arrow along the direction it faces (iso 2:1) */
         static const int ddx[4] = { -2, -2, 2, 2 }, ddy[4] = { 1, -1, -1, 1 };   /* south, west, north, east */
         int d = gpGlobals->wPartyDirection & 3, ax = ddx[d], ay = ddy[d];
         int cx = AM_W / 2, cy = AM_H / 2, hx = cx + ax * 11, hy = cy + ay * 11, bx = cx - ax * 9, by = cy - ay * 9;
         Uint32 c = pass == 0 ? cShadow : cMe; int t = pass == 0 ? 8 : 4;
         AM_Line(bx, by, hx, hy, t, c);
         AM_Line(hx, hy, hx - ax * 5 - ay * 3, hy - ay * 5 + ax * 3, t, c);   /* barbs: back along the shaft, out to either side */
         AM_Line(hx, hy, hx - ax * 5 + ay * 3, hy - ay * 5 - ax * 3, t, c);
      }
   }
   s_iAmSerial++;
}

VOID
PAL_AutomapUpdate(VOID)
/* once per free-roam frame, before it is presented */
{
   int px, py, tx, ty, th, x, y, h, st;
   BOOL fNew = FALSE;
   s_fAmFrame = 0;
   if (gpGlobals->wNumScene == 0 || PAL_GetCurrentMap() == NULL) return;
   if (s_wAmScene != gpGlobals->wNumScene) AM_EnterScene();
   if (!s_fAmEligible || g_fAutomapUserOff || gpGlobals->fInBattle) return;
   if (s_pAmSurf == NULL)
      s_pAmSurf = SDL_CreateRGBSurface(0, AM_W, AM_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (s_pAmSurf == NULL) return;

   px = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
   py = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);
   AM_PixelToTile(px, py, &tx, &ty, &th);
   if (tx >= 0 && tx < 64 && ty >= 0 && ty < 128 && !s_rgAmReach[ty][tx][th] && AM_Walkable(tx, ty, th))
   {
      AM_Flood(tx, ty, th); fNew = TRUE;
   }

   /* seen = what the picture shows around the party */
   for (y = py / 16 - 8; y <= py / 16 + 8; y++)
   {
      if (y < 0 || y >= 128) continue;
      for (x = px / 32 - 6; x <= px / 32 + 6; x++)
      {
         if (x < 0 || x >= 64) continue;
         for (h = 0; h < 2; h++)
         {
            int dx = x * 32 + h * 16 - px, dy = y * 16 + h * 8 - py;
            if (dx < -168 || dx > 168 || dy < -104 || dy > 100 || s_rgAmSeen[y][x][h]) continue;
            s_rgAmSeen[y][x][h] = 1; fNew = TRUE; s_fAmSeenDirty = TRUE;
         }
      }
   }
   for (st = 0, x = 0; x < s_nAmExit; x++)
      st = st * 31 + (gpGlobals->g.lprgEventObject[s_rgAmExit[x] - 1].sState != 0) + (gpGlobals->g.lprgEventObject[s_rgAmExit[x] - 1].sVanishTime != 0) * 2;
   if (fNew || px != s_iAmLastX || py != s_iAmLastY || s_iAmLastDir != gpGlobals->wPartyDirection || st != s_iAmLastState)
   {
      s_iAmLastX = px; s_iAmLastY = py; s_iAmLastDir = gpGlobals->wPartyDirection; s_iAmLastState = st;
      AM_Redraw(px, py);
   }
   s_fAmFrame = 1;
}

VOID PAL_AutomapFrameEnd(VOID) { s_fAmFrame = 0; }

SDL_Surface *
PAL_AutomapGet(int *piSerial)
/* for video.c: the surface to lay over this frame, or NULL */
{
   if (!s_fAmFrame || s_pAmSurf == NULL || g_fAutomapUserOff) return NULL;
   if (piSerial) *piSerial = s_iAmSerial;
   return s_pAmSurf;
}

int g_fHighlightOK = 0;   /* #2: TRUE only during the free-roam PAL_MakeScene (set in PAL_StartFrame), so the
                             interactable highlight (#5) never shows during a scripted cutscene or dialog. */

VOID
PAL_GameUpdate(
   BOOL       fTrigger
)
/*++
  Purpose:

    The main game logic routine. Update the status of everything.

  Parameters:

    [IN]  fTrigger - whether to process trigger events or not.

  Return value:

    None.

--*/
{
   WORD            wEventObjectID, wDir;
   int             i;
   LPEVENTOBJECT   p;
   WORD            wResult;
   BOOL            fSatEnterRuns = FALSE;

   //
   // Check for trigger events
   //
   if (fTrigger)
   {
      //
      // Check if we are entering a new scene
      //
      if (gpGlobals->fEnteringScene)
      {
         //
         // Run the script for entering the scene
         //
         gpGlobals->fEnteringScene = FALSE;

         i = gpGlobals->wNumScene - 1;
         {
            // Saturn: after a 0x50 fade-out an enter script that runs no instruction (first op 0, e.g. scene 19
            // re-entered: 0x1031) leaves the fade flag set -> stepped fade-in like a same-scene redraw
            // (emu o_r19f: black 66 vsync, darkness 16 5 vsync, 8 6 vsync, full). Scene 16 (0x102b: 0x4A) is instant.
            extern VOID PAL_SatEmptyEnterScript(VOID);
            fSatEnterRuns = gpGlobals->g.lprgScriptEntry[gpGlobals->g.rgScene[i].wScriptOnEnter].wOperation != 0;
            PAL_SatEmptyEnterScript();   /* draws while the flag is set fade in stepwise (also inside the script) */
         }
         gpGlobals->g.rgScene[i].wScriptOnEnter = PAL_RunTriggerScript(gpGlobals->g.rgScene[i].wScriptOnEnter, 0xFFFF);
         if (fSatEnterRuns) { extern VOID PAL_SatEnterScriptEnd(VOID); PAL_SatEnterScriptEnd(); }

         if (gpGlobals->fEnteringScene)
         {
            //
            // Don't go further as we're switching to another scene
            //
            return;
         }

         PAL_ClearKeyState();
         PAL_ClearDirState();             /* Saturn: a direction held across the scene change is ignored until released */
         PAL_UpdatePartyGestures(FALSE);  /* Saturn: the party arrives in the standing pose */
         PAL_MakeScene();
         s_fMapNamePending = TRUE;        /* user request: show the map name */
      }

      //
      // Loop through all event objects in the current scene
      //
      for (wEventObjectID = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex + 1;
         wEventObjectID <= gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex;
         wEventObjectID++)
      {
         p = &gpGlobals->g.lprgEventObject[wEventObjectID - 1];

         if (p->sVanishTime != 0)
         {
            //
            // Update the vanish time for all event objects
            //
            // Saturn 0x06035490: only on frames where the frame counter 0x06076a00 % 20 == 0
            if (gpGlobals->dwFrameNum % 20 == 0)
               p->sVanishTime += ((p->sVanishTime < 0) ? 1 : -1);
            continue;
         }

         if (p->sState < 0)
         {
            if (p->x < PAL_X(gpGlobals->viewport) ||
               p->x > PAL_X(gpGlobals->viewport) + 320 ||
               p->y < PAL_Y(gpGlobals->viewport) ||
               p->y > PAL_Y(gpGlobals->viewport) + 220)   /* Saturn 0x060357f0: 0xdc (DOS 320) */
            {
               p->sState = abs(p->sState);
               p->wCurrentFrameNum = 0;
            }
         }
         else if (p->sState > 0 && p->wTriggerMode >= kTriggerTouchNear &&
                  !(g_fNoEncounter && PAL_IsFieldMonster(p)))
         {
            //
            // This event object can be triggered without manually exploring
            //
            if (abs(PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset) - p->x) +
               abs(PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset) - p->y) * 2 <
               (p->wTriggerMode - kTriggerTouchNear) * 32 + 16)
            {
               //
               // Player is in the trigger zone.
               //

               if (p->nSpriteFrames)
               {
                  //
                  // The sprite has multiple frames. Try to adjust the direction.
                  //
                  int                xOffset, yOffset;

                  p->wCurrentFrameNum = 0;

                  xOffset = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset) - p->x;
                  yOffset = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset) - p->y;

                  //
                  // Saturn 0x060356bc: 0x0603358c (later rules win, (0,0) keeps the direction) -- differs from
                  // DOS on the axes, e.g. (+32, 0) faces east instead of north.
                  //
                  if (xOffset <= 0 && yOffset > 0) p->wDirection = kDirSouth;
                  if (xOffset < 0 && yOffset <= 0) p->wDirection = kDirWest;
                  if (xOffset >= 0 && yOffset < 0) p->wDirection = kDirNorth;
                  if (xOffset > 0 && yOffset >= 0) p->wDirection = kDirEast;

                  //
                  // Redraw the scene
                  //
                  PAL_UpdatePartyGestures(FALSE);

                  PAL_MakeScene();
                  VIDEO_UpdateScreen(NULL);
               }

               //
               // Execute the script.
               //
               p->wTriggerScript = PAL_RunTriggerScript(p->wTriggerScript, wEventObjectID);

               PAL_ClearKeyState();

               if (gpGlobals->fEnteringScene)
               {
                  //
                  // Don't go further on scene switching
                  //
                  return;
               }
            }
         }
      }
   }

   //
   // Run autoscript for each event objects
   //
   for (wEventObjectID = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex + 1;
      wEventObjectID <= gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex;
      wEventObjectID++)
   {
      p = &gpGlobals->g.lprgEventObject[wEventObjectID - 1];

      if (p->sState > 0 && p->sVanishTime == 0)
      {
         WORD wScriptEntry = p->wAutoScript;
         if (wScriptEntry != 0)
         {
            p->wAutoScript = PAL_RunAutoScript(wScriptEntry, wEventObjectID);
            if (gpGlobals->fEnteringScene)
            {
               //
               // Don't go further on scene switching
               //
               return;
            }
         }
      }

      //
      // Check if the player is in the way
      //
      // Saturn 0x0603596e: state == 2 exactly; tries the directions dir+1..dir+3 only -- but it computes the
      // candidate cell into locals and calls the obstacle check 0x06033fa0 with a NULL position pointer (r4 = 0),
      // and never writes the party position: the party is never pushed (emu st_corr: object 59 walks straight
      // through Li at (1296,1368) and the party stays). Disabled to match.
      if (FALSE && fTrigger && p->sState == kObjStateBlocker && p->wSpriteNum != 0 &&
         abs(p->x - PAL_X(gpGlobals->viewport) - PAL_X(gpGlobals->partyoffset)) +
         abs(p->y - PAL_Y(gpGlobals->viewport) - PAL_Y(gpGlobals->partyoffset)) * 2 <= 12)
      {
         //
         // Player is in the way, try to move a step
         //
         wDir = (p->wDirection + 1) % 4;
         for (i = 0; i < 3; i++)
         {
            int              x, y;
            PAL_POS          pos;

            x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
            y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);

            x += ((wDir == kDirWest || wDir == kDirSouth) ? -16 : 16);
            y += ((wDir == kDirWest || wDir == kDirNorth) ? -8 : 8);

            pos = PAL_XY(x, y);

            if (!PAL_CheckObstacleWithRange(pos, TRUE, 0, TRUE))
            {
               {  /* debug: env PAL_PUSHLOG=file logs which object pushed the party */
                  static FILE *fp = (FILE *)-1;
                  if (fp == (FILE *)-1) { const char *q = SDL_getenv("PAL_PUSHLOG"); fp = q ? fopen(q, "w") : NULL; }
                  if (fp) { fprintf(fp, "%u obj %d (%d,%d) dir %d -> party dir %d\n", SDL_GetTicks(), (int)(p - gpGlobals->g.lprgEventObject) + 1, p->x, p->y, p->wDirection, wDir); fflush(fp); }
               }
               //
               // move here
               //
               gpGlobals->viewport = PAL_XY(
                  PAL_X(pos) - PAL_X(gpGlobals->partyoffset),
                  PAL_Y(pos) - PAL_Y(gpGlobals->partyoffset));

               break;
            }

            wDir = (wDir + 1) % 4;
         }
      }
   }

   if (--gpGlobals->wChasespeedChangeCycles == 0)
   {
      gpGlobals->wChaseRange = 1;
   }

   gpGlobals->dwFrameNum++;
}

VOID
PAL_ShowUseFeedback(
   WORD         wObject,
   BOOL         fMagic
)
/* #7: brief centered toast naming the item/magic just used, so every use has visible feedback
   (some use-scripts show nothing of their own). Object id doubles as its name word. */
{
   WCHAR s[80];
   PAL_swprintf(s, sizeof(s) / sizeof(WCHAR),
      fMagic ? PAL_Tr(L"施展 %ls") : PAL_Tr(L"使用 %ls"), PAL_GetWord(wObject));   /* "施展/使用 <名>" */
   PAL_StartDialog(kDialogCenterWindow, 0, 0, FALSE);
   PAL_ShowDialogText(s);
}

VOID
PAL_GameUseItem(
   VOID
)
/*++
  Purpose:

    Allow player use an item in the game.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   WORD         wObject;

   while (TRUE)
   {
      wObject = PAL_ItemSelectMenu(NULL, kItemFlagUsable);

      if (wObject == 0)
      {
         return;
      }

      if (!(gpGlobals->g.rgObject[wObject].item.wFlags & kItemFlagApplyToAll))
      {
         //
         // Select the player to use the item on
         //
         WORD     wPlayer = 0;

         while (TRUE)
         {
            wPlayer = PAL_ItemUseMenu(wObject);

            if (wPlayer == MENUITEM_VALUE_CANCELLED)
            {
               break;
            }

            //
            // Feedback (#7): announce the use before the effect script runs.
            //
            PAL_ShowUseFeedback(wObject, FALSE);

            //
            // Run the script
            //
            gpGlobals->g.rgObject[wObject].item.wScriptOnUse =
               PAL_RunTriggerScript(gpGlobals->g.rgObject[wObject].item.wScriptOnUse, wPlayer);

            //
            // Remove the item if the item is consuming and the script succeeded
            //
            if ((gpGlobals->g.rgObject[wObject].item.wFlags & kItemFlagConsuming) &&
               g_fScriptSuccess)
            {
               PAL_AddItemToInventory(wObject, -1);
            }
         }
      }
      else
      {
         //
         // Saturn: the item menu is closed and the map redrawn before the use script runs (emu o_r50e: fishing rod at
         // the river -> menu gone, map ~40 vsync, then the search trigger 0x2582's 0x50 dims the map, not the menu).
         //
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);

         //
         // Feedback (#7): announce the use before the effect script runs.
         //
         PAL_ShowUseFeedback(wObject, FALSE);

         //
         // Run the script
         //
         gpGlobals->g.rgObject[wObject].item.wScriptOnUse =
            PAL_RunTriggerScript(gpGlobals->g.rgObject[wObject].item.wScriptOnUse, 0xFFFF);

         //
         // Remove the item if the item is consuming and the script succeeded
         //
         if ((gpGlobals->g.rgObject[wObject].item.wFlags & kItemFlagConsuming) &&
            g_fScriptSuccess)
         {
            PAL_AddItemToInventory(wObject, -1);
         }

         return;
      }
   }
}

VOID
PAL_GameEquipItem(
   VOID
)
/*++
  Purpose:

    Allow player equip an item in the game.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   WORD      wObject;

   while (TRUE)
   {
      wObject = PAL_ItemSelectMenu(NULL, kItemFlagEquipable);

      if (wObject == 0)
      {
         return;
      }

      PAL_EquipItemMenu(wObject);
   }
}

TRIGGERRANGE
PAL_GetSearchTriggerRange(
   VOID
)
/*++
  Purpose:

    Calculate 13 checkpoint coordinates for manual event search.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int                x, y, xOffset, yOffset, i;
   LPEVENTOBJECT      p;
   TRIGGERRANGE       rgRange;

   //
   // Get the party location
   //
   x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
   y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);

   if (gpGlobals->wPartyDirection == kDirNorth || gpGlobals->wPartyDirection == kDirEast)
   {
      xOffset = 16;
   }
   else
   {
      xOffset = -16;
   }

   if (gpGlobals->wPartyDirection == kDirEast || gpGlobals->wPartyDirection == kDirSouth)
   {
      yOffset = 8;
   }
   else
   {
      yOffset = -8;
   }

   rgRange.rgPos[0] = PAL_XY(x, y);

   for (i = 0; i < 4; i++)
   {
      rgRange.rgPos[i * 3 + 1] = PAL_XY(x + xOffset, y + yOffset);
      rgRange.rgPos[i * 3 + 2] = PAL_XY(x, y + yOffset * 2);
      rgRange.rgPos[i * 3 + 3] = PAL_XY(x + 2 * xOffset, y);
      x += xOffset;
      y += yOffset;
   }

   return rgRange;
}

static BOOL
PAL_ScriptHasEffect(
   WORD wEntry
)
/*++
   #10: is pressing confirm on this trigger script an EFFECTIVE interaction right now, i.e. can it change the
   game (give an item / money, open a door, move the story on), as opposed to only showing text or playing a
   little scene?  E.g. the 密道 at the start (李大娘 just calls you out), the 酒菜 before 李大娘 gives the order,
   "此門已上鎖", an empty chest, a notice board: none of these should carry the hint; the same objects DO get it
   once a script re-points their trigger at the real one.
   Static walk of every instruction reachable from wEntry (both sides of each conditional, called scripts
   included), bounded; TRUE as soon as a state-changing instruction is found.  Nothing is executed.
--*/
{
#define HE_MAXSTEP  600
#define HE_MAXSTACK 32
   WORD   rgStack[HE_MAXSTACK], rgSeen[HE_MAXSTEP];
   int    nStack = 0, nSeen = 0, j;
   WORD   i;

   rgStack[nStack++] = wEntry;
   while (nStack > 0)
   {
      i = rgStack[--nStack];
      for (;;)
      {
         LPSCRIPTENTRY p;
         WORD          op, wJump = 0;

         if (i == 0 || i >= gpGlobals->g.nScriptEntry) break;
         for (j = 0; j < nSeen && rgSeen[j] != i; j++);
         if (j < nSeen) break;                       /* already walked */
         if (nSeen >= HE_MAXSTEP) return TRUE;       /* too big to judge: a long script is a real event */
         rgSeen[nSeen++] = i;

         p = &gpGlobals->g.lprgScriptEntry[i];
         op = p->wOperation;

         switch (op)
         {
         case 0x0007:                                              /* battle */
         case 0x0017: case 0x0018: case 0x0019: case 0x001A:       /* player attributes / equip */
         case 0x001B: case 0x001C: case 0x001D:                    /* HP / MP */
         case 0x001E: case 0x001F: case 0x0020:                    /* cash, add / remove item */
         case 0x0022: case 0x0023:                                 /* revive, unequip */
         case 0x0024: case 0x0025:                                 /* set an object's auto / trigger script */
         case 0x0026: case 0x0027:                                 /* shop */
         case 0x0038:                                              /* teleport out */
         case 0x0040:                                              /* set an object's trigger mode */
         case 0x0049:                                              /* set an object's state (door opens...) */
         case 0x004E:                                              /* load game */
         case 0x0055: case 0x0056:                                 /* magic */
         case 0x0059:                                              /* change scene */
         case 0x006D: case 0x006F:                                 /* scene scripts, sync object state */
         case 0x0075:                                              /* party */
         case 0x008D:                                              /* level up */
         case 0x0098: case 0x0099: case 0x009A:                    /* follower, map, multi-object state */
            return TRUE;
         }

         if (op == 0x0000 || op == 0x0001 || op == 0x0002) break;  /* script ends here */

         if (op == 0x0003)                                         /* jump (operand 1: loop count -> may fall through) */
         {
            if (p->rgwOperand[1] != 0 && nStack < HE_MAXSTACK) rgStack[nStack++] = i + 1;
            i = p->rgwOperand[0];
            continue;
         }

         switch (op)                                               /* branches: also walk the jump target */
         {
         case 0x0004: case 0x000A: case 0x0061: case 0x0074:
            wJump = p->rgwOperand[0]; break;
         case 0x0006: case 0x005D: case 0x0079: case 0x0095:
            wJump = p->rgwOperand[1]; break;
         case 0x0058: case 0x0081: case 0x0083: case 0x0084: case 0x0086: case 0x0094:
            wJump = p->rgwOperand[2]; break;
         case 0x00A2:                                              /* random jump to one of the next N */
            for (j = 1; j <= (int)p->rgwOperand[0] && nStack < HE_MAXSTACK; j++) rgStack[nStack++] = i + j;
            break;
         }
         if (wJump != 0 && nStack < HE_MAXSTACK) rgStack[nStack++] = wJump;

         i++;
      }
   }

   return FALSE;
}

/* #11: an interaction the player has already tried once needs no further hint.  Per event object we remember
   the trigger script entry left behind by the last search interaction; the star stays off while the object
   still points at that entry, and comes back only if the story re-points it at a different script (e.g. the
   酒菜 examined before 李大娘 gives the order).  0 = never interacted.  Kept beside the save as <slot>.hls. */
#define PAL_HILITE_MAXOBJ 8192
static WORD g_rgwHiliteSeen[PAL_HILITE_MAXOBJ];

VOID
PAL_HiliteSeenMark(
   INT k
)
{
   if (k >= 0 && k < PAL_HILITE_MAXOBJ)
   {
      g_rgwHiliteSeen[k] = gpGlobals->g.lprgEventObject[k].wTriggerScript;
   }
}

VOID
PAL_HiliteSeenLoad(
   INT iSaveSlot
)
{
   FILE *fp;
   memset(g_rgwHiliteSeen, 0, sizeof(g_rgwHiliteSeen));
   if (iSaveSlot <= 0) return;
   fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(1, "%d.hls", iSaveSlot));
   if (fp != NULL)
   {
      if (fread(g_rgwHiliteSeen, 1, sizeof(g_rgwHiliteSeen), fp) != sizeof(g_rgwHiliteSeen))
      {
         memset(g_rgwHiliteSeen, 0, sizeof(g_rgwHiliteSeen));
      }
      fclose(fp);
   }
}

VOID
PAL_HiliteSeenSave(
   INT iSaveSlot
)
{
   FILE *fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, PAL_va(1, "%d.hls", iSaveSlot), "wb");
   if (fp != NULL)
   {
      fwrite(g_rgwHiliteSeen, 1, sizeof(g_rgwHiliteSeen), fp);
      fclose(fp);
   }
}

BOOL
PAL_IsHighlightEventObject(
   INT k
)
/*++
   TRUE when event object k (0-based index into lprgEventObject, must belong to the current scene) should
   carry the free-roam "interactable" highlight: a search-triggered prop with a live trigger script that is
   within PAL_HILITE_STEPS (3) party walking steps of the leader, in ANY direction (not just the facing search
   range), so the player is told there is something to examine before lining up on it.  One walking step moves
   the party 16 px in x and 8 px in y, so the step distance is max(|dx|/16, |dy|/8).  Inspects state only.
--*/
{
#define PAL_HILITE_STEPS 3
   LPEVENTOBJECT      p = &(gpGlobals->g.lprgEventObject[k]);
   int                dx, dy;

   if (p->sState <= 0 || p->sVanishTime != 0 ||
      p->wTriggerMode == kTriggerNone || p->wTriggerMode >= kTriggerTouchNear ||
      p->wTriggerScript == 0)   /* no script left -> pressing confirm does nothing, so don't highlight */
   {
      return FALSE;
   }

   if (k >= 0 && k < PAL_HILITE_MAXOBJ && g_rgwHiliteSeen[k] == p->wTriggerScript)
   {
      return FALSE;   /* #11: already tried this interaction once */
   }

   /* #5/#9: never highlight people -- NPCs, and people lying in a bed / sitting / kneeling -- only ITEMS.
      No field of the event object separates the two: nSpriteFramesAuto is the sprite's frame count for every
      object with a sprite (PAL_LoadResources), and nSpriteFrames == 0 also covers ~100 single-pose people
      (patients in bed, seated/kneeling/lying NPCs, bound captives).  So an object WITH a sprite is highlighted
      only when its sprite is one of the item sprites below (classified by eye from every search-triggered,
      non-directional sprite in mgo.mkf: 宝箱, 门, 酒菜, 石板, 柜, 罈, 葫芦, 剑, 骷髅, 草药, 地上小物...; people,
      people-in-bed and animals are left out).
      Sprite-less search spots (wSpriteNum == 0: 柜子/水缸 painted in the map) are always items. */
   if (p->wSpriteNum != 0)
   {
      static const WORD rgItemSprite[] = {
         10, 53, 54, 57, 62, 63, 64, 65, 68, 69, 70, 84, 210, 211, 230, 255, 268, 269, 274, 275, 299,
         308, 380, 381, 389, 395, 396, 506, 519, 524, 603, 604, 612, 615
      };
      int i;
      if (p->nSpriteFrames != 0) return FALSE;   /* directional = a character PAL_Search turns to face you */
      for (i = 0; i < (int)(sizeof(rgItemSprite) / sizeof(rgItemSprite[0])); i++)
      {
         if (rgItemSprite[i] == p->wSpriteNum) break;
      }
      if (i >= (int)(sizeof(rgItemSprite) / sizeof(rgItemSprite[0]))) return FALSE;

      /* user request: an OPEN door needs no hint. A door leaf (sprites 274 / 275 ...: frame 0 closed, frame 1 open)
         is a blocker (state 2) while closed and passable (state 1) on its open frame once opened. */
      if (p->wCurrentFrameNum != 0 && p->sState < kObjStateBlocker) return FALSE;
   }

   dx = (SHORT)p->x - (PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset));
   dy = (SHORT)p->y - (PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset));
   if (dx < 0) dx = -dx;
   if (dy < 0) dy = -dy;

   if (dx > 16 * PAL_HILITE_STEPS || dy > 8 * PAL_HILITE_STEPS)
   {
      return FALSE;
   }

   /* #10: only when the interaction would actually do something in the current story state */
   return PAL_ScriptHasEffect(p->wTriggerScript);
}

/* Saturn 0x060237a4..0x060238b4, after the search 0x06035bb8 found nothing (emu s_r61g, scene 61):
   0x060367b0 looks at checkpoints 0..6 of the same range for a touch-triggered object (state > 0, vanish
   timer 0, mode >= 4), checkpoint order first.  Found: the party faces it (0x0603358c on the signs of dx, dy),
   takes one step (0x06033654) and a frame is drawn (0x0603440c); then, if the object is still off on both
   axes and its mode is 4 (touch on the spot), once more.  Scene 61 switch obj1099 (mode 4): B from two
   steps away walks the party onto it and it fires (emu: steps 6 vsync apart). */
static VOID
PAL_SatSearchApproach(
   VOID
)
{
   TRIGGERRANGE       rgRange = PAL_GetSearchTriggerRange();
   LPEVENTOBJECT      p = NULL, q;
   int                i, k, n, dx, dy, dh, x, y, sx, sy, dir;

   for (i = 0; i <= 6 && p == NULL; i++)
   {
      dh = ((PAL_X(rgRange.rgPos[i]) % 32) ? 1 : 0);
      dx = PAL_X(rgRange.rgPos[i]) / 32;
      dy = PAL_Y(rgRange.rgPos[i]) / 16;

      for (k = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
         k < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; k++)
      {
         q = &(gpGlobals->g.lprgEventObject[k]);
         if (q->sState <= 0 || q->sVanishTime != 0 || q->wTriggerMode < kTriggerTouchNear ||
            q->x / 32 != dx || q->y / 16 != dy || ((q->x % 32) ? 1 : 0) != dh)
         {
            continue;
         }
         p = q;
         break;
      }
   }

   if (p == NULL)
   {
      return;
   }

   for (n = 0; n < 2; n++)
   {
      x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
      y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);
      sx = (p->x > x) - (p->x < x);
      sy = (p->y > y) - (p->y < y);

      if (n == 1 && (sx == 0 || sy == 0 || p->wTriggerMode != kTriggerTouchNear))
      {
         break;
      }

      if (sx != 0 || sy != 0)
      {
         dir = gpGlobals->wPartyDirection;
         if (sx <= 0 && sy > 0) dir = kDirSouth;
         if (sx < 0 && sy <= 0) dir = kDirWest;
         if (sx >= 0 && sy < 0) dir = kDirNorth;
         if (sx > 0 && sy >= 0) dir = kDirEast;
         gpGlobals->wPartyDirection = dir;
         if (!PAL_SatPartyStep(dir))
         {
            PAL_UpdatePartyGestures(FALSE);
         }
      }
      else
      {
         PAL_UpdatePartyGestures(FALSE);
      }

      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
      UTIL_Delay(SAT_SCRIPT_FRAME_TIME);
   }
}

VOID
PAL_Search(
   VOID
)
/*++
  Purpose:

    Process searching trigger events.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int                x, y, xOffset, yOffset, dx, dy, dh, ex, ey, eh, i, k, l;
   LPEVENTOBJECT      p;
   BOOL               fTurnMembers = FALSE;
   TRIGGERRANGE       rgRange;

   rgRange = PAL_GetSearchTriggerRange();

   for (i = 0; i < 13; i++)
   {
      //
      // Convert to map location
      //
      dh = ((PAL_X(rgRange.rgPos[i]) % 32) ? 1 : 0);
      dx = PAL_X(rgRange.rgPos[i]) / 32;
      dy = PAL_Y(rgRange.rgPos[i]) / 16;

      //
      // Loop through all event objects
      //
      for (k = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
         k < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; k++)
      {
         p = &(gpGlobals->g.lprgEventObject[k]);
         ex = p->x / 32;
         ey = p->y / 16;
         eh = ((p->x % 32) ? 1 : 0);

         if (p->sState <= 0 || p->wTriggerMode >= kTriggerTouchNear ||
            /* Saturn 0x0603611c: m = mode, m++ if m is 1 or 2, then needs i <= 3*m (DOS: i < mode*6-4).
               emu s_z99c: door obj102 (mode 1) at checkpoint 2 opens on B. */
            3 * (p->wTriggerMode + (p->wTriggerMode == 1 || p->wTriggerMode == 2)) < i ||
            dx != ex || dy != ey || dh != eh)
         {
            continue;
         }

         //
         // Adjust direction/gesture for party members and the event object
         //
         if (p->nSpriteFrames * 4 > p->wCurrentFrameNum)
         {
            p->wCurrentFrameNum = 0; // use standing gesture
            p->wDirection = (gpGlobals->wPartyDirection + 2) % 4; // face the party

            //
            // Saturn: only the leader takes the facing gesture before the script; the other members turn after it
            // (emu o_o360, scene 21 maid: Ling'er keeps her north frame during the text, faces west once it closes).
            //
            gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * 3;
            fTurnMembers = TRUE;

            //
            // Redraw everything
            //
            PAL_MakeScene();
            VIDEO_UpdateScreen(NULL);
         }

         //
         // Execute the script
         //
         p->wTriggerScript = PAL_RunTriggerScript(p->wTriggerScript, k + 1);
         PAL_HiliteSeenMark(k);   /* #11 */

         if (fTurnMembers)
         {
            for (l = 1; l <= gpGlobals->wMaxPartyMemberIndex; l++)
            {
               gpGlobals->rgParty[l].wFrame = gpGlobals->wPartyDirection * 3;
            }
         }

         //
         // Clear inputs and delay for a short time
         //
         UTIL_Delay(50);
         PAL_ClearKeyState();

         return; // don't go further
      }
   }

   PAL_SatSearchApproach();
}

VOID
PAL_StartFrame(
   VOID
)
/*++
  Purpose:

    Starts a video frame. Called once per video frame.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   //
   // Run the game logic of one frame
   //
   PAL_GameUpdate(TRUE);
   if (gpGlobals->fEnteringScene)
   {
      return;
   }

   //
   // Update the positions and gestures of party members
   //
   PAL_UpdateParty();

   //
   // Update the scene
   //
   g_fHighlightOK = 1;   /* #2: this is the free-roam draw -> the interactable highlight may show now */
   PAL_MakeScene();
   g_fHighlightOK = 0;   /* keep it off for every scripted/cutscene PAL_MakeScene */
   PAL_MapNameDraw();
   PAL_AutomapUpdate();     /* user request: the automap floats over this frame only (not over dialogs, menus, cutscenes) */
   VIDEO_UpdateScreen(NULL);
   PAL_AutomapFrameEnd();

   if (g_InputState.dwKeyPress & kKeyMenu)
   {
      //
      // Show the in-game menu
      //
      PAL_InGameMenu();
   }
   else if (g_InputState.dwKeyPress & kKeyUseItem)
   {
      //
      // Show the use item menu
      //
      PAL_GameUseItem();
   }
   else if (g_InputState.dwKeyPress & kKeyThrowItem)
   {
      //
      // Show the equipment menu
      //
      PAL_GameEquipItem();
   }
   else if (g_InputState.dwKeyPress & kKeyForce)
   {
      //
      // Show the magic menu
      //
      PAL_InGameMagicMenu();
   }
   else if (g_InputState.dwKeyPress & kKeyStatus)
   {
      //
      // Show the player status
      //
      PAL_PlayerStatus();
   }
   else if (g_InputState.dwKeyPress & kKeySearch)
   {
      //
      // Process search events
      //
      PAL_Search();
   }
   else if (g_InputState.dwKeyPress & kKeyFlee)
   {
      //
      // Quit Game
      //
      PAL_QuitGame();
   }
}

static inline VOID
PAL_WaitForKeyInternal(
   WORD      wTimeOut,
   BOOL      fAllowAnyKey
)
/*++
  Purpose:

    Wait for any key.

  Parameters:

    [IN]  wTimeOut - the maximum time of the waiting. 0 = wait forever.

    [IN]  fAllowAnyKey - Whether any key are allowed. If no, only KeySearch and KeyMenu allowed.

  Return value:

    None.

--*/
{
   DWORD     dwTimeOut = SDL_GetTicks() + wTimeOut;

   PAL_ClearKeyState();

   while (wTimeOut == 0 || !SDL_TICKS_PASSED(SDL_GetTicks(), dwTimeOut))
   {
      UTIL_Delay(5);

      if (g_InputState.dwKeyPress && fAllowAnyKey
         || g_InputState.dwKeyPress & (kKeySearch | kKeyMenu))
      {
         break;
      }
   }
}

VOID
PAL_WaitForKey(
   WORD      wTimeOut
)
/*++
  Purpose:

    Wait for KeySearch and KeyMenu.

  Parameters:

    [IN]  wTimeOut - the maximum time of the waiting. 0 = wait forever.

  Return value:

    None.

--*/
{
   PAL_WaitForKeyInternal(wTimeOut, FALSE);
}

VOID
PAL_WaitForAnyKey(
   WORD      wTimeOut
)
/*++
  Purpose:

    Wait for any key.

  Parameters:

    [IN]  wTimeOut - the maximum time of the waiting. 0 = wait forever.

  Return value:

    None.

--*/
{
   PAL_WaitForKeyInternal(wTimeOut, TRUE);
}
