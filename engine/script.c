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
// Based on PALx Project by palxex.
// Copyright (c) 2006-2008, Pal Lockheart <palxex@gmail.com>.
//

#include "main.h"
extern BOOL g_fSatRNGBackdrop;
extern SDL_Surface *g_pSatRNGBackdrop, *g_pSatRNGShown;

BOOL            g_fScriptSuccess = TRUE;
static int      g_iCurEquipPart = -1;

static BOOL
PAL_NPCWalkTo(
   WORD           wEventObjectID,
   INT            x,
   INT            y,
   INT            h,
   INT            iSpeed
)
/*++
  Purpose:

    Make the specified event object walk to the map position specified by (x, y, h)
    at the speed of iSpeed.

  Parameters:

    [IN]  wEventObjectID - the event object to move.

    [IN]  x - Column number of the tile.

    [IN]  y - Line number in the map.

    [IN]  h - Each line in the map has two lines of tiles, 0 and 1.
              (See map.h for details.)

    [IN]  iSpeed - the speed to move.

  Return value:

    TRUE if the event object has successfully moved to the specified position,
    FALSE if still need more moving.

--*/
{
   LPEVENTOBJECT    pEvtObj;
   int              xOffset, yOffset;

   pEvtObj = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);

   xOffset = (x * 32 + h * 16) - pEvtObj->x;
   yOffset = (y * 16 + h * 8) - pEvtObj->y;

   //
   // Saturn 0x060277bc (ops 0x7C / 0x82 / ...): step while |dx| >= 2*speed or |dy| >= speed; the direction comes
   // from 0x0603358c (later rules win, (0,0) keeps it -- same as the party walk) and 0x060351f8 moves
   // (2*speed, speed) diagonally.  DOS snapped as soon as either axis was close.
   //
   if (abs(xOffset) < iSpeed * 2 && abs(yOffset) < iSpeed)
   {
      pEvtObj->x = x * 32 + h * 16;
      pEvtObj->y = y * 16 + h * 8;
   }
   else
   {
      int d = pEvtObj->wDirection;
      if (xOffset <= 0 && yOffset > 0) d = kDirSouth;
      if (xOffset < 0 && yOffset <= 0) d = kDirWest;
      if (xOffset >= 0 && yOffset < 0) d = kDirNorth;
      if (xOffset > 0 && yOffset >= 0) d = kDirEast;
      pEvtObj->wDirection = d;
      PAL_NPCWalkOneStep(wEventObjectID, iSpeed);
   }

   if (pEvtObj->x == x * 32 + h * 16 && pEvtObj->y == y * 16 + h * 8)
   {
      pEvtObj->wCurrentFrameNum = 0;
      return TRUE;
   }

   return FALSE;
}

static VOID
PAL_PartyWalkTo(
   INT            x,
   INT            y,
   INT            h,
   INT            iSpeed
)
/*++
  Purpose:

    Make the party walk to the map position specified by (x, y, h)
    at the speed of iSpeed.

  Parameters:

    [IN]  x - Column number of the tile.

    [IN]  y - Line number in the map.

    [IN]  h - Each line in the map has two lines of tiles, 0 and 1.
              (See map.h for details.)

    [IN]  iSpeed - the speed to move.

  Return value:

    None.

--*/
{
   int           xOffset, yOffset, i, dx, dy, iSatSteps = 0;
   DWORD         t;

   xOffset = x * 32 + h * 16 - PAL_X(gpGlobals->viewport) - PAL_X(gpGlobals->partyoffset);
   yOffset = y * 16 + h * 8 - PAL_Y(gpGlobals->viewport) - PAL_Y(gpGlobals->partyoffset);

   t = 0;

   while (xOffset != 0 || yOffset != 0)
   {
      PAL_DelayUntil(t);

      t = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;   /* Saturn: one step every 6 vsync */
      {  /* debug: env PAL_VPLOG=file logs the viewport per step (ms x y xOffset yOffset) */
         static FILE *fvp = (FILE *)-1;
         if (fvp == (FILE *)-1) { const char *p = SDL_getenv("PAL_VPLOG"); fvp = p ? fopen(p, "w") : NULL; }
         if (fvp) { fprintf(fvp, "W %u %d %d %d %d p1 %d %d f%d t1 %d %d d%d\n", SDL_GetTicks(), PAL_X(gpGlobals->viewport), PAL_Y(gpGlobals->viewport), xOffset, yOffset, gpGlobals->rgParty[1].x, gpGlobals->rgParty[1].y, gpGlobals->rgParty[1].wFrame, gpGlobals->rgTrail[1].x, gpGlobals->rgTrail[1].y, gpGlobals->rgTrail[1].wDirection); fflush(fvp); }
      }

      //
      // Store trail
      //
      for (i = 3; i >= 0; i--)
      {
         gpGlobals->rgTrail[i + 1] = gpGlobals->rgTrail[i];
      }
      gpGlobals->rgTrail[0].x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
      gpGlobals->rgTrail[0].y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);

      //
      // Saturn 0x0602c488: direction from the remaining offset (0x0603358c, later rules win; (0,0) keeps it),
      // then one diagonal step (tables 0x06089c40 / 0x0609cde0: x -1,-1,1,1  y 1,-1,-1,1) of (2*speed, speed).
      // emu entry 0x1dc (op 0x70): viewport (-4,+2) every 6 vsync, zigzag in x once x is reached.
      //
      if (++iSatSteps > 4000)
      {
         // safety: the target is off the 16x8 lattice -- finish like the DOS version
         dx = PAL_X(gpGlobals->viewport) + xOffset;
         dy = PAL_Y(gpGlobals->viewport) + yOffset;
      }
      else
      {
         static const int rgSatDX[4] = { -1, -1, 1, 1 }, rgSatDY[4] = { 1, -1, -1, 1 };
         int d = gpGlobals->wPartyDirection;
         if (xOffset <= 0 && yOffset > 0) d = kDirSouth;
         if (xOffset < 0 && yOffset <= 0) d = kDirWest;
         if (xOffset >= 0 && yOffset < 0) d = kDirNorth;
         if (xOffset > 0 && yOffset >= 0) d = kDirEast;
         gpGlobals->wPartyDirection = d;
         dx = PAL_X(gpGlobals->viewport) + rgSatDX[d] * iSpeed * 2;
         dy = PAL_Y(gpGlobals->viewport) + rgSatDY[d] * iSpeed;
      }
      // Saturn (emu ram 0x0609ce60 during op 0x70): the stored point carries the direction of the step
      // being taken now, not the previous one (members are placed from trail[1] by this direction).
      gpGlobals->rgTrail[0].wDirection = gpGlobals->wPartyDirection;

      //
      // Move the viewport
      //
      gpGlobals->viewport = PAL_XY(dx, dy);

      PAL_UpdatePartyGestures(TRUE);
      PAL_GameUpdate(FALSE);
      //
      // Saturn: while the party walks by script (0x70/0x7A/0x7B) the map tiles are drawn 1 px up-left of the
      // viewport, the sprites are not (emu s_z144 0x70, o_s17b 0x7B: walk frames match with the map at (+1,+1)).
      //
      g_iSatMapShift = 1;
      PAL_MakeScene();
      g_iSatMapShift = 0;
      VIDEO_UpdateScreen(NULL);

      xOffset = x * 32 + h * 16 - PAL_X(gpGlobals->viewport) - PAL_X(gpGlobals->partyoffset);
      yOffset = y * 16 + h * 8 - PAL_Y(gpGlobals->viewport) - PAL_Y(gpGlobals->partyoffset);
   }

   //
   // Saturn: a scripted walk sets only the exact viewport's y (0x0607694c) to the target's; x (0x06076948) keeps its
   // value (emu s_s17x, scene 17 entries 0x14ec..0x1500: exact (390,508) -> 0x7A to vp (3d0,4d8) -> (390,4d8), the
   // next 0x6E (-16,+8) shows (380,4e0); every 0x7A/0x7B there leaves x at 0x380 and y at the target's).
   //
   g_iSatVpY = PAL_Y(gpGlobals->viewport);

   PAL_UpdatePartyGestures(FALSE);
}

static VOID
PAL_PartyRideEventObject(
   WORD           wEventObjectID,
   INT            x,
   INT            y,
   INT            h,
   INT            iSpeed
)
/*++
  Purpose:

    Move the party to the specified position, riding the specified event object.

  Parameters:

    [IN]  wEventObjectID - the event object to be ridden.

    [IN]  x - Column number of the tile.

    [IN]  y - Line number in the map.

    [IN]  h - Each line in the map has two lines of tiles, 0 and 1.
              (See map.h for details.)

    [IN]  iSpeed - the speed to move.

  Return value:

    TRUE if the party and event object has successfully moved to the specified
    position, FALSE if still need more moving.

--*/
{
   int              xOffset, yOffset, dx, dy, i;
   DWORD            t;
   LPEVENTOBJECT    p;

   p = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);

   xOffset = x * 32 + h * 16 - PAL_X(gpGlobals->viewport) - PAL_X(gpGlobals->partyoffset);
   yOffset = y * 16 + h * 8 - PAL_Y(gpGlobals->viewport) - PAL_Y(gpGlobals->partyoffset);

   t = 0;

   while (xOffset != 0 || yOffset != 0)
   {
      PAL_DelayUntil(t);

      t = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;

      //
      // Saturn 0x0602a1ac: direction from 0x0603358c (later rules win, (0,0) keeps it); the ridden object
      // takes the same direction (byte +16).  Each axis then moves by a full step until it is 0.
      //
      {
         int d = gpGlobals->wPartyDirection;
         if (xOffset <= 0 && yOffset > 0) d = kDirSouth;
         if (xOffset < 0 && yOffset <= 0) d = kDirWest;
         if (xOffset >= 0 && yOffset < 0) d = kDirNorth;
         if (xOffset > 0 && yOffset >= 0) d = kDirEast;
         // Only the ridden object turns: emu 0x06076970 keeps its value during the ride (boat 0x5e5 rides north
         // with direction 0; scene 18 stones keep the pre-ride direction, members later face it via the trail).
         p->wDirection = d;
      }

      if (abs(xOffset) > iSpeed * 2)
      {
         dx = iSpeed * (xOffset < 0 ? -2 : 2);
      }
      else
      {
         dx = xOffset;
      }

      if (abs(yOffset) > iSpeed)
      {
         dy = iSpeed * (yOffset < 0 ? -1 : 1);
      }
      else
      {
         dy = yOffset;
      }

      //
      // Store trail
      //
      for (i = 3; i >= 0; i--)
      {
         gpGlobals->rgTrail[i + 1] = gpGlobals->rgTrail[i];
      }

      gpGlobals->rgTrail[0].wDirection = gpGlobals->wPartyDirection;
      gpGlobals->rgTrail[0].x = PAL_X(gpGlobals->viewport) + dx + PAL_X(gpGlobals->partyoffset);
      gpGlobals->rgTrail[0].y = PAL_Y(gpGlobals->viewport) + dy + PAL_Y(gpGlobals->partyoffset);

      //
      // Move the viewport
      //
      gpGlobals->viewport =
         PAL_XY(PAL_X(gpGlobals->viewport) + dx, PAL_Y(gpGlobals->viewport) + dy);

      p->x += dx;
      p->y += dy;

      PAL_GameUpdate(FALSE);
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);

      xOffset = x * 32 + h * 16 - PAL_X(gpGlobals->viewport) - PAL_X(gpGlobals->partyoffset);
      yOffset = y * 16 + h * 8 - PAL_Y(gpGlobals->viewport) - PAL_Y(gpGlobals->partyoffset);
   }

   g_iSatVpX = PAL_X(gpGlobals->viewport);   /* 0x0602a66e: exact viewport = viewport */
   g_iSatVpY = PAL_Y(gpGlobals->viewport);
}

static VOID
PAL_MonsterChasePlayer(
   WORD         wEventObjectID,
   WORD         wSpeed,
   WORD         wChaseRange,
   BOOL         fFloating
)
/*++
  Purpose:

    Make the specified event object chase the players.

  Parameters:

    [IN]  wEventObjectID - the event object ID of the monster.

    [IN]  wSpeed - the speed of chasing.

    [IN]  wChaseRange - sensitive range of the monster.

    [IN]  fFloating - TRUE if monster is floating (i.e., ignore the obstacles)

  Return value:

    None.

--*/
{
   //
   // Saturn op 0x4C handler 0x0602a99c:
   //  - range test uses |dx| + 2*|dx| (the original reads dx twice), before the zero tie-break;
   //  - direction from 0x0603358c starting from the current direction (later rules win);
   //  - target = grid-aligned position + (+-16, +-8) checked with objects (0x06033fa0);
   //  - blocked -> snap to the grid; then the four +-4/+-2 probes (tile only, 0x06033ee0) are taken
   //    from the current position (not cumulative) and each blocked one snaps to the grid;
   //  - after the step (0x060351f8) a non-floating monster that did not move and stands on a blocked
   //    tile jumps to one random neighbour (+-16, +-8) if that one is free;
   //  - chase range 0 (exorcism fragrance): direction += frame-parity byte 0x060768e7.
   //
   static const int   rgX4[4] = { -4, -4, 4, 4 }, rgY2[4] = { 2, -2, -2, 2 };
   static const int   rgX16[4] = { -16, -16, 16, 16 }, rgY8[4] = { 8, -8, -8, 8 };
   LPEVENTOBJECT    pEvtObj = &gpGlobals->g.lprgEventObject[wEventObjectID - 1];
   WORD             wMonsterSpeed = 0, prevx, prevy;
   int              x, y, i, j, l, d;

   if (gpGlobals->wChaseRange != 0)
   {
      x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset) - pEvtObj->x;
      y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset) - pEvtObj->y;

      /* The Saturn test is |dx| * 3 (it reads dx twice and never looks at dy), so every monster in the
         party's column chased from anywhere on the map. Use the intended |dx| + 2 * |dy| distance (the DOS
         rule): a monster only starts chasing once the party is within its range. PAL_SATCHASE=1 restores
         the Saturn behaviour. */
      if ((SDL_getenv("PAL_SATCHASE") ? abs(x) * 3 : abs(x) + abs(y) * 2) < (int)wChaseRange * 32 * gpGlobals->wChaseRange)
      {
         if (x == 0) x = RandomLong(0, 1) ? 1 : -1;
         if (y == 0) y = RandomLong(0, 1) ? 1 : -1;

         d = pEvtObj->wDirection;
         if (x <= 0 && y > 0) d = kDirSouth;
         if (x < 0 && y <= 0) d = kDirWest;
         if (x >= 0 && y < 0) d = kDirNorth;
         if (x > 0 && y >= 0) d = kDirEast;

         prevx = pEvtObj->x;
         prevy = pEvtObj->y;

         i = prevx % 32;
         j = prevy % 16;

         prevx /= 32;
         prevy /= 16;
         l = 0;

         if (i + j * 2 >= 16)
         {
            if (i + j * 2 >= 48)
            {
               prevx++;
               prevy++;
            }
            else if (32 - i + j * 2 < 16)
            {
               prevx++;
            }
            else if (32 - i + j * 2 < 48)
            {
               l = 1;
            }
            else
            {
               prevy++;
            }
         }

         prevx = prevx * 32 + l * 16;
         prevy = prevy * 16 + l * 8;

         x = prevx + rgX16[d];
         y = prevy + rgY8[d];

         if (fFloating || !PAL_CheckObstacle(PAL_XY(x, y), TRUE, wEventObjectID))
         {
            pEvtObj->wDirection = d;
            wMonsterSpeed = wSpeed;
         }
         else
         {
            pEvtObj->x = prevx;
            pEvtObj->y = prevy;
         }

         if (!fFloating)
         {
            for (l = 0; l < 4; l++)
            {
               if (PAL_CheckObstacle(PAL_XY(pEvtObj->x + rgX4[l], pEvtObj->y + rgY2[l]), FALSE, 0))
               {
                  pEvtObj->x = prevx;
                  pEvtObj->y = prevy;
               }
            }
         }
      }
   }
   else
   {
      pEvtObj->wDirection = (pEvtObj->wDirection + (gpGlobals->dwFrameNum & 1)) & 3;
   }

   PAL_NPCWalkOneStep(wEventObjectID, wMonsterSpeed);

   if (!fFloating && wMonsterSpeed == 0 &&
      PAL_CheckObstacle(PAL_XY(pEvtObj->x, pEvtObj->y), FALSE, 0))
   {
      l = RandomLong(0, 3);
      x = pEvtObj->x + rgX16[l];
      y = pEvtObj->y + rgY8[l];
      if (!PAL_CheckObstacle(PAL_XY(x, y), FALSE, 0))
      {
         pEvtObj->x = x;
         pEvtObj->y = y;
      }
   }
}

VOID
PAL_AdditionalCredits(
   VOID
)
/*++
  Purpose:

    Show the additional credits.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   LPCWSTR rgszcps[][CP_MAX] = {
	   // Traditional Chinese, Simplified Chinese
	   { L"", L"", /*L""*/ },
	   { L"         經典特別篇   ",
	     L"         经典特别篇   ",
	     //L"   \x30AF\x30E9\x30B7\x30C3\x30AF\x7279\x5225\x7DE8  "
	   },
	   { L"", L"", /*L""*/ },
	   { L"", L"", /*L""*/ },
	   { L"", L"", /*L""*/ },
	   { L"", L"", /*L""*/ },
	   { L"", L"", /*L""*/ },
	   { L"", L"", /*L""*/ },
	   { L"   本程式是自由軟體，按照 GNU General",
	     L"   本程序是自由软件，按照 GNU General",
		 //L" \x3053\x306E\x30D7\x30ED\x30B0\x30E9\x30E0\x306F\x81EA\x7531\x30BD\x30D5\x30C8\x30A6\x30A7\x30A2\x3067\x3059\x3001"
	   },
	   { L"   Public License v3 或更高版本發佈",
	     L"   Public License v3 或更高版本发布",
	     //L" GNU General Public License v3 \x306E\x4E0B\x3067"
	   },
	   { L"", L"", /*L" \x914D\x5E03\x3055\x308C\x3066\x3044\x307E\x3059\x3002"*/ },
	   { L"                    ...按 Enter 結束",
	     L"                    ...按 Enter 结束",
	     //L"      ...Enter\x30AD\x30FC\x3092\x62BC\x3057\x3066\x7D42\x4E86\x3057\x307E\x3059"
	   },
   };

   LPCWSTR rgszStrings[] = {
      L"  SDLPAL (http://sdlpal.github.io/)",
#ifdef PAL_CLASSIC
	  L"%ls(" WIDETEXT(__DATE__) L")",
#else
	  L"                        (" WIDETEXT(__DATE__) L")",
#endif
      L" ",
	  L"    (c) 2009-2011, Wei Mingzhi",
	  L"        <whistler_wmz@users.sf.net>.",
      L"    (c) 2011-2026, SDLPAL Team",
	  L"%ls",  // Porting information line 1
	  L"%ls",  // Porting information line 2
	  L"%ls",  // GNU line 1
	  L"%ls",  // GNU line 2
	  L"%ls",  // GNU line 3
      L"%ls",  // Press Enter to continue
   };

   int        i = 0;

   PAL_DrawOpeningMenuBackground();

   for (i = 0; i < 12; i++)
   {
      WCHAR buffer[48];
      PAL_swprintf(buffer, sizeof(buffer) / sizeof(WCHAR), rgszStrings[i], gConfig.pszMsgFile ? g_rcCredits[i] : rgszcps[i][PAL_GetCodePage()]);
      PAL_DrawText(buffer, PAL_XY(0, 2 + i * 16), DESCTEXT_COLOR, TRUE, FALSE, FALSE);
   }

   PAL_SetPalette(0, FALSE);
   VIDEO_UpdateScreen(NULL);

   PAL_WaitForKey(0);
}

int g_iDbgScriptEntry = 0;   /* debug: last executed script entry (PAL_HDMCHECK) */
static void
SAT_OpLog(WORD wScriptEntry, LPSCRIPTENTRY pScript)
{  /* debug: env PAL_OPLOG=file logs every executed op (ms entry op operands) */
   g_iDbgScriptEntry = wScriptEntry;
   static FILE *fop = (FILE *)-1;
   if (fop == (FILE *)-1) { const char *p = SDL_getenv("PAL_OPLOG"); fop = p ? fopen(p, "w") : NULL; }
   if (fop) { fprintf(fop, "%u %d %04x %04x %04x %04x\n", SDL_GetTicks(), wScriptEntry, pScript->wOperation,
                       pScript->rgwOperand[0], pScript->rgwOperand[1], pScript->rgwOperand[2]); fflush(fop); }
}

BOOL g_fSatSkipFadeOut = FALSE;   /* a won field battle left the screen black: the next 0x50 has nothing to fade */

VOID
PAL_OpeningPanHit(
   VOID
)
/*++
  Enhancement (user-authorized 2026-11-17 "按照windows版本的来"): the Saturn opening shows 婶婶's
  pan-swing sprite (event object 12 of the inn room = 4 frames: ready / raise / IMPACT+stars / follow)
  STATICALLY at frame 0, so the 砸头 never animates. Play it like the DOS/Windows version did: cycle
  the object's own 4 frames with an impact beat, so 婶婶 actually swings the 锅 and hits 李逍遥. Uses
  the game's original Saturn art, so it stays visually faithful; only the motion is restored.
--*/
{
   LPEVENTOBJECT   p;
   /* Mirror the game's OWN scripted pan-hit (the natural one at script 3564-3576, "来客人前最后一下"):
      ev12 frame 1 raise -> 2 IMPACT -> 3 follow -> 0 settle, each held for N frame-units (same cadence as
      the script's op 0x09 waits), and 李逍遥 (rgParty[0]) recoils into the bent pose (frame 9, exactly what
      that scripted hit leaves him in) across the impact+follow, then straightens as 婶婶 lowers the pan. */
   struct { int ev; int pf; int hold; } seq[] = {
      /* ev12 frame, party0 frame (-1 = leave as is), hold in frame-units */
      { 1, -1, 3 },    /* raise  : 婶婶 lifts the 锅, 李逍遥 still as he was        */
      { 2,  9, 2 },    /* IMPACT : stars + 李逍遥 doubles over                        */
      { 3,  9, 3 },    /* follow : 婶婶 follows through, 李逍遥 still reeling          */
      { 0, -2, 3 },    /* settle : 婶婶 lowers the 锅, 李逍遥 straightens (restore)    */
   };
   int              n = (int)(sizeof(seq) / sizeof(seq[0])), i, h;
   int              unit = SAT_SCRIPT_FRAME_TIME;    /* same per-frame time the scripted hit uses */
   WORD             savedDir, savedFrame, savedPFrame;

   if (gpGlobals->g.nEventObject < 12) return;
   p = &(gpGlobals->g.lprgEventObject[11]);          /* event object 12 (1-based) = 婶婶's swing sprite (spr 628,
                                                        4 frames, nSpriteFrames==0 -> shown static at frame 0) */
   if (p->sState != kObjStateNormal) return;         /* only if the swing sprite is actually on screen */

   /* nSpriteFrames==0 means the draw maps frameIndex = wCurrentFrameNum directly, so 0..3 hit the raw frames. */
   savedDir = p->wDirection; savedFrame = p->wCurrentFrameNum;
   savedPFrame = gpGlobals->rgParty[0].wFrame;
   if (unit < 33) unit = 33;                          /* guard against a 0 synch */

   for (i = 0; i < n; i++)
   {
      p->wCurrentFrameNum = (WORD)seq[i].ev;
      if (seq[i].pf == -2)      gpGlobals->rgParty[0].wFrame = savedPFrame;   /* restore on settle */
      else if (seq[i].pf >= 0)  gpGlobals->rgParty[0].wFrame = (WORD)seq[i].pf;
      if (seq[i].ev == 2)
         VIDEO_ShakeScreen(2, 3);                     /* impact: shakes the low-res path; HD punch is the fr2 stars */
      for (h = 0; h < seq[i].hold; h++)               /* hold the frame across several redraws = smooth, like op 0x09 */
      {
         DWORD t = SDL_GetTicks() + (DWORD)unit;
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);
         PAL_ProcessEvent();
         while (!SDL_TICKS_PASSED(SDL_GetTicks(), t)) { PAL_ProcessEvent(); SDL_Delay(2); }
      }
   }
   VIDEO_ShakeScreen(0, 0);

   p->wDirection = savedDir; p->wCurrentFrameNum = savedFrame;
   gpGlobals->rgParty[0].wFrame = savedPFrame;
   PAL_MakeScene();
   VIDEO_UpdateScreen(NULL);
}

static WORD
PAL_InterpretInstruction(
   WORD           wScriptEntry,
   WORD           wEventObjectID
)
/*++
  Purpose:

    Interpret and execute one instruction in the script.

  Parameters:

    [IN]  wScriptEntry - The script entry to execute.

    [IN]  wEventObjectID - The event object ID which invoked the script.

  Return value:

    The address of the next script instruction to execute.

--*/
{
   LPEVENTOBJECT          pEvtObj, pCurrent;
   LPSCRIPTENTRY          pScript;
   int                    iPlayerRole, i, j, x, y;
   WORD                   w, wCurEventObjectID;

   pScript = &(gpGlobals->g.lprgScriptEntry[wScriptEntry]);

   if (wEventObjectID != 0)
   {
      pEvtObj = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);
   }
   else
   {
      pEvtObj = NULL;
   }

   if (pScript->rgwOperand[0] == 0 || pScript->rgwOperand[0] == 0xFFFF)
   {
      pCurrent = pEvtObj;
      wCurEventObjectID = wEventObjectID;
   }
   else
   {
      i = pScript->rgwOperand[0] - 1;
      if (i > 0x9000)
      {
         // HACK for Dream 2.11 to avoid crash
         i -= 0x9000;
      }
      pCurrent = &(gpGlobals->g.lprgEventObject[i]);
      wCurEventObjectID = pScript->rgwOperand[0];
   }

   if (pScript->rgwOperand[0] < MAX_PLAYABLE_PLAYER_ROLES)
   {
      iPlayerRole = gpGlobals->rgParty[pScript->rgwOperand[0]].wPlayerRole;
   }
   else
   {
      iPlayerRole = gpGlobals->rgParty[0].wPlayerRole;
   }

   SAT_OpLog(wScriptEntry, pScript);

   switch (pScript->wOperation)
   {
   case 0x000B:
   case 0x000C:
   case 0x000D:
   case 0x000E:
      //
      // walk one step
      //
      pEvtObj->wDirection = pScript->wOperation - 0x000B;
      PAL_NPCWalkOneStep(wEventObjectID, 2);
      break;

   case 0x000F:
      //
      // Set the direction and/or gesture for event object
      //
      if (pScript->rgwOperand[0] != 0xFFFF)
      {
         pEvtObj->wDirection = pScript->rgwOperand[0];
      }
      if (pScript->rgwOperand[1] != 0xFFFF)
      {
         pEvtObj->wCurrentFrameNum = pScript->rgwOperand[1];
      }
      break;

   case 0x0010:
      //
      // Walk straight to the specified position
      //
      if (!PAL_NPCWalkTo(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
         pScript->rgwOperand[2], 3))
      {
         wScriptEntry--;
      }
      break;

   case 0x0011:
      //
      // Walk straight to the specified position, at a lower speed
      //
      if ((wEventObjectID & 1) ^ (gpGlobals->dwFrameNum & 1))
      {
         if (!PAL_NPCWalkTo(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
            pScript->rgwOperand[2], 2))
         {
            wScriptEntry--;
         }
      }
      else
      {
         wScriptEntry--;
      }
      break;

   case 0x0012:
      //
      // Set the position of the event object, relative to the party
      //
      pCurrent->x =
         pScript->rgwOperand[1] + PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
      pCurrent->y =
         pScript->rgwOperand[2] + PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);
      break;

   case 0x0013:
      //
      // Set the position of the event object
      //
      pCurrent->x = pScript->rgwOperand[1];
      pCurrent->y = pScript->rgwOperand[2];
      break;

   case 0x0014:
      //
      // Set the gesture of the event object
      //
      pEvtObj->wCurrentFrameNum = pScript->rgwOperand[0];
      pEvtObj->wDirection = kDirSouth;
      break;

   case 0x0015:
      //
      // Set the direction and gesture for a party member
      //
      gpGlobals->wPartyDirection = pScript->rgwOperand[0];
      gpGlobals->rgParty[pScript->rgwOperand[2]].wFrame =
         gpGlobals->wPartyDirection * 3 + pScript->rgwOperand[1];
      break;

   case 0x0016:
      //
      // Set the direction and gesture for an event object
      //
      if (pScript->rgwOperand[0] != 0)
      {
         pCurrent->wDirection = pScript->rgwOperand[1];
         pCurrent->wCurrentFrameNum = pScript->rgwOperand[2];
      }
      break;

   case 0x0017:
      //
      // set the player's extra attribute
      //
      {
         WORD *p;

         i = pScript->rgwOperand[0] - 0xB;

         p = (WORD *)(&gpGlobals->rgEquipmentEffect[i]); // HACKHACK

         p[pScript->rgwOperand[1] * MAX_PLAYER_ROLES + wEventObjectID] =
            (SHORT)pScript->rgwOperand[2];
      }
      break;

   case 0x0018:
      //
      // Equip the selected item
      //
      i = pScript->rgwOperand[0] - 0x0B;
      g_iCurEquipPart = i;

      //
      // The wEventObjectID parameter here should indicate the player role
      //
      PAL_RemoveEquipmentEffect(wEventObjectID, i);

      if (gpGlobals->g.PlayerRoles.rgwEquipment[i][wEventObjectID] != pScript->rgwOperand[1])
      {
         w = gpGlobals->g.PlayerRoles.rgwEquipment[i][wEventObjectID];
         gpGlobals->g.PlayerRoles.rgwEquipment[i][wEventObjectID] = pScript->rgwOperand[1];

         if (PAL_GetItemIndexToInventory(pScript->rgwOperand[1], &i)
            && i < MAX_INVENTORY
            && gpGlobals->rgInventory[i].nAmount == 1
            && w != 0
            && !PAL_GetItemIndexToInventory(w, &j))
         {
            //
            // When the number of items you want to wear is 1 
            // and the number of items you are wearing is also 1, 
            // replace them directly, instead of removing items 
            // and adding them at the end of the item menu
            //
            gpGlobals->rgInventory[i].wItem = w;
         }
         else
         {
            PAL_AddItemToInventory(pScript->rgwOperand[1], -1);

            if (w != 0)
            {
               PAL_AddItemToInventory(w, 1);
            }
         }

         gpGlobals->wLastUnequippedItem = w;
      }
      break;

   case 0x0019:
      //
      // Increase/decrease the player's attribute
      //
      {
         WORD *p = (WORD *)(&gpGlobals->g.PlayerRoles); // HACKHACK

         if (pScript->rgwOperand[2] == 0)
         {
            iPlayerRole = wEventObjectID;
         }
         else
         {
            iPlayerRole = pScript->rgwOperand[2] - 1;
         }

         p[pScript->rgwOperand[0] * MAX_PLAYER_ROLES + iPlayerRole] +=
            (SHORT)pScript->rgwOperand[1];
      }
      break;

   case 0x001A:
      //
      // Set player's stat
      //
      {
         WORD *p = (WORD *)(&gpGlobals->g.PlayerRoles); // HACKHACK

         if (g_iCurEquipPart != -1)
         {
            //
            // In the progress of equipping items
            //
            p = (WORD *)&(gpGlobals->rgEquipmentEffect[g_iCurEquipPart]);
         }

         if (pScript->rgwOperand[2] == 0)
         {
            //
            // Apply to the current player. The wEventObjectID should
            // indicate the player role.
            //
            iPlayerRole = wEventObjectID;
         }
         else
         {
            iPlayerRole = pScript->rgwOperand[2] - 1;
         }

         p[pScript->rgwOperand[0] * MAX_PLAYER_ROLES + iPlayerRole] =
            (SHORT)pScript->rgwOperand[1];
      }
      break;

   case 0x001B:
      //
      // Increase/decrease player's HP
      //
      if (pScript->rgwOperand[0])
      {
         g_fScriptSuccess = FALSE;
         //
         // Apply to everyone
         //
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            if (PAL_IncreaseHPMP(w, (SHORT)(pScript->rgwOperand[1]), 0))
               g_fScriptSuccess = TRUE;
         }
      }
      else
      {
         //
         // Apply to one player. The wEventObjectID parameter should indicate the player role.
         //
         if (!PAL_IncreaseHPMP(wEventObjectID, (SHORT)(pScript->rgwOperand[1]), 0))
         {
            g_fScriptSuccess = FALSE;
         }
      }
      break;

   case 0x001C:
      //
      // Increase/decrease player's MP
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to everyone
         //
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            PAL_IncreaseHPMP(w, 0, (SHORT)(pScript->rgwOperand[1]));
         }
      }
      else
      {
         //
         // Apply to one player. The wEventObjectID parameter should indicate the player role.
         //
         if (!PAL_IncreaseHPMP(wEventObjectID, 0, (SHORT)(pScript->rgwOperand[1])))
         {
            g_fScriptSuccess = FALSE;
         }
      }
      break;

   case 0x001D:
      //
      // Increase/decrease player's HP and MP
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to everyone
         //
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            PAL_IncreaseHPMP(w,
               (SHORT)(pScript->rgwOperand[1]), (SHORT)(pScript->rgwOperand[1]));
         }
      }
      else
      {
         //
         // Apply to one player. The wEventObjectID parameter should indicate the player role.
         //
         if (!PAL_IncreaseHPMP(wEventObjectID,
            (SHORT)(pScript->rgwOperand[1]), (SHORT)(pScript->rgwOperand[1])))
         {
            g_fScriptSuccess = FALSE;
         }
      }
      break;

   case 0x001E:
      //
      // Increase or decrease cash by the specified amount
      //
      if ((SHORT)(pScript->rgwOperand[0]) < 0 &&
         gpGlobals->dwCash < (WORD)(-(SHORT)(pScript->rgwOperand[0])))
      {
         //
         // not enough cash
         //
         wScriptEntry = pScript->rgwOperand[1] - 1;
      }
      else
      {
         gpGlobals->dwCash += (SHORT)(pScript->rgwOperand[0]);
      }
      break;

   case 0x001F:
      //
      // Add item to inventory
      //
      PAL_AddItemToInventory(pScript->rgwOperand[0], (SHORT)(pScript->rgwOperand[1]));
      break;

   case 0x0020:
      //
      // Remove item from inventory
      //
      x = pScript->rgwOperand[1];
      if (x == 0)
      {
         x = 1;
      }
      if (x <= PAL_CountItem(pScript->rgwOperand[0]) || pScript->rgwOperand[2] == 0)
      {
      y = PAL_AddItemToInventory(pScript->rgwOperand[0], -x);
      if (y <= 0)
      {
         //
         // Check if it's a partial removal and set the remaining amount
         //
         if (y < 0)
         {
            x = -y;
         }
         //
         // Try removing equipped item
         //
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;

            for (j = 0; j < MAX_PLAYER_EQUIPMENTS; j++)
            {
               if (gpGlobals->g.PlayerRoles.rgwEquipment[j][w] == pScript->rgwOperand[0])
               {
                  PAL_RemoveEquipmentEffect(w, j);
                  gpGlobals->g.PlayerRoles.rgwEquipment[j][w] = 0;

                  if (--x == 0)
                  {
                     i = 9999;
                     break;
                  }
               }
            }
         }
      }
      }
      else
          wScriptEntry = pScript->rgwOperand[2] - 1;
      break;

   case 0x0021:
      //
      // Inflict damage to the enemy
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Inflict damage to all enemies
         //
         for (i = 0;i <= g_Battle.wMaxEnemyIndex; i++)
         {
            if (g_Battle.rgEnemy[i].wObjectID != 0)
            {
               g_Battle.rgEnemy[i].e.wHealth -= pScript->rgwOperand[1];
            }
         }
      }
      else
      {
         //
         // Inflict damage to one enemy
         //
         g_Battle.rgEnemy[wEventObjectID].e.wHealth -= pScript->rgwOperand[1];
      }
      break;

   case 0x0022:
      //
      // Revive player
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to everyone
         //
         g_fScriptSuccess = FALSE;

         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            if (gpGlobals->g.PlayerRoles.rgwHP[w] == 0)
            {
               gpGlobals->g.PlayerRoles.rgwHP[w] =
                  gpGlobals->g.PlayerRoles.rgwMaxHP[w] * pScript->rgwOperand[1] / 10;

               PAL_CurePoisonByLevel(w, 3);
               for (x = 0; x < kStatusAll; x++)
               {
                  PAL_RemovePlayerStatus(w, x);
               }

               g_fScriptSuccess = TRUE;
            }
         }
      }
      else
      {
         //
         // Apply to one player
         //
         if (gpGlobals->g.PlayerRoles.rgwHP[wEventObjectID] == 0)
         {
            gpGlobals->g.PlayerRoles.rgwHP[wEventObjectID] =
               gpGlobals->g.PlayerRoles.rgwMaxHP[wEventObjectID] * pScript->rgwOperand[1] / 10;

            PAL_CurePoisonByLevel(wEventObjectID, 3);
            for (x = 0; x < kStatusAll; x++)
            {
               PAL_RemovePlayerStatus(wEventObjectID, x);
            }
         }
         else
         {
            g_fScriptSuccess = FALSE;
         }
      }
      break;

   case 0x0023:
      //
      // Remove equipment from the specified player
      //
      iPlayerRole = pScript->rgwOperand[0];
      if (pScript->rgwOperand[1] == 0)
      {
         //
         // Remove all equipments
         //
         for (i = 0; i < MAX_PLAYER_EQUIPMENTS; i++)
         {
            w = gpGlobals->g.PlayerRoles.rgwEquipment[i][iPlayerRole];
            if (w != 0)
            {
               PAL_AddItemToInventory(w, 1);
               gpGlobals->g.PlayerRoles.rgwEquipment[i][iPlayerRole] = 0;
            }
            PAL_RemoveEquipmentEffect(iPlayerRole, i);
         }
      }
      else
      {
         w = gpGlobals->g.PlayerRoles.rgwEquipment[pScript->rgwOperand[1] - 1][iPlayerRole];
         if (w != 0)
         {
            PAL_RemoveEquipmentEffect(iPlayerRole, pScript->rgwOperand[1] - 1);
            PAL_AddItemToInventory(w, 1);
            gpGlobals->g.PlayerRoles.rgwEquipment[pScript->rgwOperand[1] - 1][iPlayerRole] = 0;
         }
      }
      break;

   case 0x0024:
      //
      // Set the autoscript entry address for an event object
      //
      if (pScript->rgwOperand[0] != 0)
      {
         pCurrent->wAutoScript = pScript->rgwOperand[1];
         /* Start the new auto script with a clean loop/wait counter. The counter used to keep whatever the
            object's previous auto script had left in it, so two objects given the same "move N frames then
            vanish" script ran out of step: the Suzhou ferry (counter 5 left over) vanished 5 frames before the
            boatman riding it, who hung in the air for half a second. */
         pCurrent->wScriptIdleFrameCountAuto = 0;
      }
      break;

   case 0x0025:
      //
      // Set the trigger script entry address for an event object
      //
      if (pScript->rgwOperand[0] != 0)
      {
         pCurrent->wTriggerScript = pScript->rgwOperand[1];
      }
      break;

   case 0x0026:
      //
      // Show the buy item menu
      //
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
      PAL_BuyMenu(pScript->rgwOperand[0]);
      break;

   case 0x0027:
      //
      // Show the sell item menu
      //
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
      PAL_SellMenu();
      break;

   case 0x0028:
      //
      // Apply poison to enemy
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to everyone
         //
         for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
         {
            w = g_Battle.rgEnemy[i].wObjectID;

            if (w == 0)
            {
               continue;
            }

            if (RandomLong(0, 9) >=
               gpGlobals->g.rgObject[w].enemy.wResistanceToSorcery)
            {
               for (j = 0; j < MAX_POISONS; j++)
               {
                  if (g_Battle.rgEnemy[i].rgPoisons[j].wPoisonID ==
                     pScript->rgwOperand[1])
                  {
                     break;
                  }
               }

               if (j >= MAX_POISONS)
               {
                  for (j = 0; j < MAX_POISONS; j++)
                  {
                     if (g_Battle.rgEnemy[i].rgPoisons[j].wPoisonID == 0)
                     {
                        g_Battle.rgEnemy[i].rgPoisons[j].wPoisonID = pScript->rgwOperand[1];
                        g_Battle.rgEnemy[i].rgPoisons[j].wPoisonScript =
                           PAL_RunTriggerScript(gpGlobals->g.rgObject[pScript->rgwOperand[1]].poison.wEnemyScript, wEventObjectID);
                        break;
                     }
                  }
               }
            }
         }
      }
      else
      {
         //
         // Apply to one enemy
         //
         w = g_Battle.rgEnemy[wEventObjectID].wObjectID;

         if (RandomLong(0, 9) >=
            gpGlobals->g.rgObject[w].enemy.wResistanceToSorcery)
         {
            for (j = 0; j < MAX_POISONS; j++)
            {
               if (g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonID ==
                  pScript->rgwOperand[1])
               {
                  break;
               }
            }

            if (j >= MAX_POISONS)
            {
               for (j = 0; j < MAX_POISONS; j++)
               {
                  if (g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonID == 0)
                  {
                     g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonID = pScript->rgwOperand[1];
                     g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonScript =
                        PAL_RunTriggerScript(gpGlobals->g.rgObject[pScript->rgwOperand[1]].poison.wEnemyScript, wEventObjectID);
                     break;
                  }
               }
            }
         }
      }
      break;

   case 0x0029:
      //
      // Apply poison to player
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to everyone
         //
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            if (RandomLong(1, 100) > PAL_GetPlayerPoisonResistance(w))
            {
               PAL_AddPoisonForPlayer(w, pScript->rgwOperand[1]);
            }
         }
      }
      else
      {
         //
         // Apply to one player
         //
         if (RandomLong(1, 100) > PAL_GetPlayerPoisonResistance(wEventObjectID))
         {
            PAL_AddPoisonForPlayer(wEventObjectID, pScript->rgwOperand[1]);
         }
      }
      break;

   case 0x002A:
      //
      // Cure poison by object ID for enemy
      //
      if (pScript->rgwOperand[0])
      {
         //
         // Apply to all enemies
         //
         for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
         {
            if (g_Battle.rgEnemy[i].wObjectID == 0)
            {
               continue;
            }

            for (j = 0; j < MAX_POISONS; j++)
            {
               if (g_Battle.rgEnemy[i].rgPoisons[j].wPoisonID == pScript->rgwOperand[1])
               {
                  g_Battle.rgEnemy[i].rgPoisons[j].wPoisonID = 0;
                  g_Battle.rgEnemy[i].rgPoisons[j].wPoisonScript = 0;
                  break;
               }
            }
         }
      }
      else
      {
         //
         // Apply to one enemy
         //
         for (j = 0; j < MAX_POISONS; j++)
         {
            if (g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonID == pScript->rgwOperand[1])
            {
               g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonID = 0;
               g_Battle.rgEnemy[wEventObjectID].rgPoisons[j].wPoisonScript = 0;
               break;
            }
         }
      }
      break;

   case 0x002B:
      //
      // Cure poison by object ID for player
      //
      if (pScript->rgwOperand[0])
      {
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            PAL_CurePoisonByKind(w, pScript->rgwOperand[1]);
         }
      }
      else
      {
         PAL_CurePoisonByKind(wEventObjectID, pScript->rgwOperand[1]);
      }
      break;

   case 0x002C:
      //
      // Cure poisons by level
      //
      if (pScript->rgwOperand[0])
      {
         for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
         {
            w = gpGlobals->rgParty[i].wPlayerRole;
            PAL_CurePoisonByLevel(w, pScript->rgwOperand[1]);
         }
      }
      else
      {
         PAL_CurePoisonByLevel(wEventObjectID, pScript->rgwOperand[1]);
      }
      break;

   case 0x002D:
      //
      // Set the status for player
      //
      if (!PAL_SetPlayerStatus(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1]))
      {
         g_fScriptSuccess = FALSE;
      }
      break;

   case 0x002E:
      //
      // Set the status for enemy
      //
      w = g_Battle.rgEnemy[wEventObjectID].wObjectID;

#ifdef PAL_CLASSIC
      i = 9;
#else
      i = ((pScript->rgwOperand[0] == kStatusSlow) ? 14 : 9);
#endif

      if (RandomLong(0, i) > gpGlobals->g.rgObject[w].enemy.wResistanceToSorcery)
      {
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[pScript->rgwOperand[0]] = pScript->rgwOperand[1];
      }
      else
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
      }
      break;

   case 0x002F:
      //
      // Remove player's status
      //
      PAL_RemovePlayerStatus(wEventObjectID, pScript->rgwOperand[0]);
      break;

   case 0x0030:
      //
      // Increase player's stat temporarily by percent
      //
      {
         WORD *p = (WORD *)(&gpGlobals->rgEquipmentEffect[kBodyPartExtra]); // HACKHACK
         WORD *p1 = (WORD *)(&gpGlobals->g.PlayerRoles);

         if (pScript->rgwOperand[2] == 0)
         {
            iPlayerRole = wEventObjectID;
         }
         else
         {
            iPlayerRole = pScript->rgwOperand[2] - 1;
         }

         p[pScript->rgwOperand[0] * MAX_PLAYER_ROLES + iPlayerRole] =
            p1[pScript->rgwOperand[0] * MAX_PLAYER_ROLES + iPlayerRole] *
               (SHORT)pScript->rgwOperand[1] / 100;
      }
      break;

   case 0x0031:
      //
      // Change battle sprite temporarily for player
      //
      gpGlobals->rgEquipmentEffect[kBodyPartExtra].rgwSpriteNumInBattle[wEventObjectID] =
         pScript->rgwOperand[0];
      break;

   case 0x0033:
      //
      // collect the enemy for items
      //
      if (g_Battle.rgEnemy[wEventObjectID].e.wCollectValue != 0)
      {
         gpGlobals->wCollectValue +=
            g_Battle.rgEnemy[wEventObjectID].e.wCollectValue;
      }
      else
      {
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0034:
      //
      // Transform collected enemies into items
      //
      if (gpGlobals->wCollectValue > 0)
      {
         WCHAR s[256];

#ifdef PAL_CLASSIC
         i = RandomLong(1, gpGlobals->wCollectValue);
         if (i > 9)
         {
            i = 9;
         }
#else
         i = RandomLong(1, 9);
         if (i > gpGlobals->wCollectValue)
         {
            i = gpGlobals->wCollectValue;
         }
#endif

         gpGlobals->wCollectValue -= i;
         i--;

         PAL_AddItemToInventory(gpGlobals->g.lprgStore[0].rgwItems[i], 1);

         /* Saturn 0x060298f8: get box 0x0602c008 with word 42, item frame, wait 100 frames */
         PAL_ShowGetItemBox(42, gpGlobals->g.lprgStore[0].rgwItems[i]);
         LPCBITMAPRLE pBG = PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX);
         INT iBGWidth = PAL_RLEGetWidth(pBG), iBGHeight = PAL_RLEGetHeight(pBG);
         INT iBG_X = 128, iBG_Y = 73; /* Saturn 0x06029932 */
         PAL_POS pos = PAL_XY(iBG_X, iBG_Y);
         SDL_Rect rect = {iBG_X, iBG_Y, iBGWidth, iBGHeight};
         PAL_RLEBlitToSurface(pBG, gpScreen, pos);
         
         WORD wObject = gpGlobals->g.lprgStore[0].rgwItems[i];
         static WORD wPrevImageIndex = 0xFFFF;
         static BYTE bufImage[2048];
         if (gpGlobals->g.rgObject[wObject].item.wBitmap != wPrevImageIndex)
         {
            if (PAL_MKFReadChunk(bufImage, 2048,
                                 gpGlobals->g.rgObject[wObject].item.wBitmap, gpGlobals->f.fpBALL) > 0)
            {
               wPrevImageIndex = gpGlobals->g.rgObject[wObject].item.wBitmap;
            }
            else
            {
               wPrevImageIndex = 0xFFFF;
            }
         }
         if (wPrevImageIndex != 0xFFFF)
         {
            PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY(PAL_X(pos)+7, PAL_Y(pos)+7)); /* Saturn (135,80) */
         }
         
         VIDEO_UpdateScreen(&rect);
         PAL_WaitForAnyKey(1667);
      }
      else
      {
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0035:
      //
      // Shake the screen
      //
      // Saturn op 0x35 (table 0x06076b44) is an empty function: no screen shake.
      break;

   case 0x0036:
      //
      // Set the current playing RNG animation
      //
      gpGlobals->iCurPlayingRNG = pScript->rgwOperand[0];
      { extern BOOL g_fSatRngReload; g_fSatRngReload = TRUE; }   /* Saturn: load flag 0x10 -> scene reload after the script */
      break;

   case 0x0037:
      //
      // Play RNG animation
      //
      { extern VOID PAL_SatReloadCancel(VOID); PAL_SatReloadCancel(); }   /* 0xa98 / 0x1e79: movie right after 0x78 */
      PAL_RNGPlay(gpGlobals->iCurPlayingRNG,
         pScript->rgwOperand[0],
         pScript->rgwOperand[1] > 0 ? pScript->rgwOperand[1] : -1,
         pScript->rgwOperand[2] > 0 ? pScript->rgwOperand[2] : 16);
      break;

   case 0x0038:
      //
      // Teleport the party out of the scene
      //
      if (!gpGlobals->fInBattle &&
         gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wScriptOnTeleport != 0)
      {
         PAL_RunTriggerScript(gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wScriptOnTeleport, 0xFFFF);
      }
      else
      {
         //
         // failed
         //
         g_fScriptSuccess = FALSE;
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0039:
      //
      // Drain HP from enemy
      //
      w = gpGlobals->rgParty[g_Battle.wMovingPlayerIndex].wPlayerRole;

      g_Battle.rgEnemy[wEventObjectID].e.wHealth -= pScript->rgwOperand[0];
      gpGlobals->g.PlayerRoles.rgwHP[w] += pScript->rgwOperand[0];

      if (gpGlobals->g.PlayerRoles.rgwHP[w] > gpGlobals->g.PlayerRoles.rgwMaxHP[w])
      {
         gpGlobals->g.PlayerRoles.rgwHP[w] = gpGlobals->g.PlayerRoles.rgwMaxHP[w];
      }
      break;

   case 0x003A:
      //
      // Player flee from the battle
      //
      if (g_Battle.fIsBoss)
      {
         //
         // Cannot flee from bosses
         //
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      else
      {
         PAL_BattlePlayerEscape();
      }
      break;

   case 0x003F:
      //
      // Ride the event object to the specified position, at a low speed
      //
      g_iSatSynch = 3;   /* Saturn: SGL frame count while riding; 3 afterwards (0x0602a4b0) */
      PAL_PartyRideEventObject(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
         pScript->rgwOperand[2], 2);
      g_iSatSynch = 3;
      break;

   case 0x0040:
      //
      // set the trigger method for a event object
      //
      if (pScript->rgwOperand[0] != 0)
      {
         pCurrent->wTriggerMode = pScript->rgwOperand[1];
      }
      break;

   case 0x0041:
      //
      // Mark the script as failed
      //
      g_fScriptSuccess = FALSE;
      break;

   case 0x0042:
      //
      // Simulate a magic for player
      //
      i = (SHORT)(pScript->rgwOperand[2]) - 1;
      if (i < 0)
      {
         i = wEventObjectID;
      }
      PAL_BattleSimulateMagic(i, pScript->rgwOperand[0], pScript->rgwOperand[1]);
      break;

   case 0x0043:
      //
      // Set background music
      //
      gpGlobals->wNumMusic = pScript->rgwOperand[0];
      AUDIO_PlayMusic(pScript->rgwOperand[0], pScript->rgwOperand[1] != 1, (pScript->rgwOperand[1] == 3 && pScript->rgwOperand[0] != 9) ? 3.0f : 0.0f);
      break;

   case 0x0044:
      //
      // Ride the event object to the specified position, at the normal speed
      //
      g_iSatSynch = 2;   /* Saturn: SGL frame count while riding; 3 afterwards (0x0602a4b0) */
      PAL_PartyRideEventObject(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
         pScript->rgwOperand[2], 4);
      g_iSatSynch = 3;
      break;

   case 0x0045:
      //
      // Set battle music
      //
      gpGlobals->wNumBattleMusic = pScript->rgwOperand[0];
      break;

   case 0x0046:
      //
      // Set the party position on the map
      //
      {
         int xOffset, yOffset, x, y;

         xOffset =
            ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirSouth)
               ? 16 : -16);
         yOffset =
            ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirNorth)
               ? 8 : -8);

         x = pScript->rgwOperand[0] * 32 + pScript->rgwOperand[2] * 16;
         y = pScript->rgwOperand[1] * 16 + pScript->rgwOperand[2] * 8;

         x -= PAL_X(gpGlobals->partyoffset);
         y -= PAL_Y(gpGlobals->partyoffset);

         //
         // Saturn 0x0602a584: viewport clamped to 0..0x6A0 / 0..0x730 (0x06033538), exact viewport = viewport,
         // held direction ignored until released (0x06076a09 = -1)
         //
         // Only with the normal party offset: after a camera pan (0x7F dx dy n) the offset holds the panned
         // distance and viewport + offset is what counts; clamping the viewport alone there put the party
         // hundreds of pixels off (tree trunk 0x622c: pan up 368, 0x46 -> y < 0 clamped -> party outside the
         // tree, then a 3.7 s walk back through the walls).
         if (PAL_X(gpGlobals->partyoffset) == 160 && PAL_Y(gpGlobals->partyoffset) == 112)
         {
            if (x > 0x6A0) x = 0x6A0;
            if (y > 0x730) y = 0x730;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
         }

         gpGlobals->viewport = PAL_XY(x, y);
         g_iSatVpX = x; g_iSatVpY = y;
         PAL_ClearDirState();

         x = PAL_X(gpGlobals->partyoffset);
         y = PAL_Y(gpGlobals->partyoffset);

         for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)
         {
            gpGlobals->rgParty[i].x = x;
            gpGlobals->rgParty[i].y = y;
            gpGlobals->rgTrail[i].x = x + PAL_X(gpGlobals->viewport);
            gpGlobals->rgTrail[i].y = y + PAL_Y(gpGlobals->viewport);
            gpGlobals->rgTrail[i].wDirection = gpGlobals->wPartyDirection;

            x += xOffset;
            y += yOffset;
         }
      }
      break;

   case 0x0047:
      //
      // Play sound effect
      //
      AUDIO_PlaySound(pScript->rgwOperand[0]);
      break;

   case 0x0049:
      //
      // Set the state of event object
      //
      if (pScript->rgwOperand[0] != 0)
         pCurrent->sState = pScript->rgwOperand[1];
      break;

   case 0x004A:
      //
      // Set the current battlefield
      //
      gpGlobals->wNumBattleField = pScript->rgwOperand[0];
      break;

   case 0x004B:
      //
      // Nullify the event object for a short while
      //
      {  /* Saturn 0x0602a954: vanish byte = -(operand), default 10 (units of 20 frames) */
         int v = pScript->rgwOperand[0] ? pScript->rgwOperand[0] : 10;
         pEvtObj->sVanishTime = (SHORT)(signed char)(-(v & 0xFF));
      }
      break;

   case 0x004C:
      //
      // chase the player
      //
      i = pScript->rgwOperand[0]; // max. distance
      j = pScript->rgwOperand[1]; // speed

      if (i == 0)
      {
         i = 8;
      }

      if (j == 0)
      {
         j = 4;
      }

      PAL_MonsterChasePlayer(wEventObjectID, j, i, pScript->rgwOperand[2]);
      break;

   case 0x004D:
      //
      // wait for any key
      //
      PAL_WaitForKey(0);
      break;

   case 0x004E:
      //
      // Load the last saved game
      //
      PAL_FadeOut(1);
      PAL_ReloadInNextTick(gpGlobals->bCurrentSaveSlot);
      return 0; // don't go further

   case 0x004F:
      //
      // Fade the screen to red color (game over)
      //
      // Saturn op 0x4F is an empty function: no fade to red.
      break;

   case 0x0050:
      //
      // screen fade out
      //
      if (g_fSatSkipFadeOut)
      {  /* the battle already left the screen black (see op 0x07): nothing to fade out, no black wait */
         SDL_Color black[256];
         g_fSatSkipFadeOut = FALSE;
         memset(black, 0, sizeof(black));
         VIDEO_SetPalette(black);
         g_fSatRNGBackdrop = FALSE;
         gpGlobals->fNeedToFadeIn = TRUE;
         break;
      }
      VIDEO_UpdateScreen(NULL);
      PAL_SatFadeOut(pScript->rgwOperand[0] ? pScript->rgwOperand[0] : 1);   /* Saturn 0x06023914 */
      g_fSatRNGBackdrop = FALSE;   /* emu o_r4d k23: after the fade the map is drawn again, not the movie frame */
      gpGlobals->fNeedToFadeIn = TRUE;
      break;

   case 0x0051:
      //
      // screen fade in
      //
      VIDEO_UpdateScreen(NULL);
      PAL_SatFadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette);   /* Saturn 0x06023c64: instant */
      gpGlobals->fNeedToFadeIn = FALSE;
      break;

   case 0x0052:
      //
      // hide the event object for a while, default 800 frames
      //
      pEvtObj->sState *= -1;
      {  /* Saturn 0x0602b0a8: operand <= 0 -> 127, > 254 -> 255, stored as a signed byte (units of 20 frames) */
         int v = (SHORT)pScript->rgwOperand[0];
         if (v <= 0) v = 127; else if (v > 254) v = 255;
         pEvtObj->sVanishTime = (SHORT)(signed char)v;
      }
      break;

   case 0x0053:
      //
      // use the day palette
      //
      gpGlobals->fNightPalette = FALSE;
      break;

   case 0x0054:
      //
      // use the night palette
      //
      gpGlobals->fNightPalette = TRUE;
      break;

   case 0x0055:
      //
      // Add magic to a player
      //
      i = pScript->rgwOperand[1];
      if (i == 0)
      {
         i = wEventObjectID;
      }
      else
      {
         i--;
      }
      PAL_AddMagic(i, pScript->rgwOperand[0]);
      break;

   case 0x0056:
      //
      // Remove magic from a player
      //
      i = pScript->rgwOperand[1];
      if (i == 0)
      {
         i = wEventObjectID;
      }
      else
      {
         i--;
      }
      PAL_RemoveMagic(i, pScript->rgwOperand[0]);
      break;

   case 0x0057:
      //
      // Set the base damage of magic according to MP value
      //
      i = ((pScript->rgwOperand[1] == 0) ? 8 : pScript->rgwOperand[1]);
      j = gpGlobals->g.rgObject[pScript->rgwOperand[0]].magic.wMagicNumber;
      gpGlobals->g.lprgMagic[j].wBaseDamage =
         gpGlobals->g.PlayerRoles.rgwMP[wEventObjectID] * i;
      gpGlobals->g.PlayerRoles.rgwMP[wEventObjectID] = 0;
      break;

   case 0x0058:
      //
      // Jump if there is less than the specified number of the specified items
      // in the inventory
      //
      if (PAL_GetItemAmount(pScript->rgwOperand[0]) < (SHORT)(pScript->rgwOperand[1]))
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
      }
      break;

   case 0x0059:
      //
      // Change to the specified scene
      //
      if (pScript->rgwOperand[0] > 0 && pScript->rgwOperand[0] <= MAX_SCENES &&
         gpGlobals->wNumScene != pScript->rgwOperand[0])
      {
         //
         // Set data to load the scene in the next frame
         //
         g_wSatPrevScene = gpGlobals->wNumScene;   /* for the loader's fade-out redraw (res.c) */
         g_fSatRNGBackdrop = FALSE;
         gpGlobals->wNumScene = pScript->rgwOperand[0];
         PAL_SetLoadFlags(kLoadScene);
         gpGlobals->fEnteringScene = TRUE;
         gpGlobals->wLayer = 0;
      }
      break;

   case 0x005A:
      //
      // Halve the player's HP
      // The wEventObjectID parameter here should indicate the player role
      //
      gpGlobals->g.PlayerRoles.rgwHP[wEventObjectID] /= 2;
      break;

   case 0x005B:
      //
      // Halve the enemy's HP
      //
      w = g_Battle.rgEnemy[wEventObjectID].e.wHealth / 2 + 1;
      if (w > pScript->rgwOperand[0])
      {
         w = pScript->rgwOperand[0];
      }
      g_Battle.rgEnemy[wEventObjectID].e.wHealth -= w;
      break;

   case 0x005C:
      //
      // Hide for a while
      //
      g_Battle.iHidingTime = -(INT)(pScript->rgwOperand[0]);
      break;

   case 0x005D:
      //
      // Jump if player doesn't have the specified poison
      //
      if (!PAL_IsPlayerPoisonedByKind(wEventObjectID, pScript->rgwOperand[0]))
      {
         wScriptEntry = pScript->rgwOperand[1] - 1;
      }
      break;

   case 0x005E:
      //
      // Jump if enemy doesn't have the specified poison
      //
      for (i = 0; i < MAX_POISONS; i++)
      {
         if (g_Battle.rgEnemy[wEventObjectID].rgPoisons[i].wPoisonID == pScript->rgwOperand[0])
         {
            break;
         }
      }

      if (i >= MAX_POISONS)
      {
         wScriptEntry = pScript->rgwOperand[1] - 1;
      }
      break;

   case 0x005F:
      //
      // Kill the player immediately
      // The wEventObjectID parameter here should indicate the player role
      //
      gpGlobals->g.PlayerRoles.rgwHP[wEventObjectID] = 0;
      break;

   case 0x0060:
      //
      // Immediate KO of the enemy
      //
      g_Battle.rgEnemy[wEventObjectID].e.wHealth = 0;
      break;

   case 0x0061:
      //
      // Jump if player is not poisoned
      //
      if (!PAL_IsPlayerPoisonedByLevel(wEventObjectID, 0))
      {
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0062:
      //
      // Pause enemy chasing for a while
      //
      gpGlobals->wChasespeedChangeCycles = pScript->rgwOperand[0];
      gpGlobals->wChaseRange = 0;
      break;

   case 0x0063:
      //
      // Speed up enemy chasing for a while
      //
      gpGlobals->wChasespeedChangeCycles = pScript->rgwOperand[0];
      gpGlobals->wChaseRange = 3;
      break;

   case 0x0064:
      //
      // Jump if enemy's HP is more than the specified percentage
      //
      i = gpGlobals->g.rgObject[g_Battle.rgEnemy[wEventObjectID].wObjectID].enemy.wEnemyID;
      if ((INT)(g_Battle.rgEnemy[wEventObjectID].e.wHealth) * 100 >
         (INT)(gpGlobals->g.lprgEnemy[i].wHealth) * pScript->rgwOperand[0])
      {
         wScriptEntry = pScript->rgwOperand[1] - 1;
      }
      break;

   case 0x0065:
      //
      // Set the player's sprite
      //
      gpGlobals->g.PlayerRoles.rgwSpriteNum[pScript->rgwOperand[0]] = pScript->rgwOperand[1];
      if (!gpGlobals->fInBattle && pScript->rgwOperand[2])
      {
         PAL_SetLoadFlags(kLoadPlayerSprite);
         PAL_LoadResources();
      }
      break;

   case 0x0066:
      //
      // Throw weapon to enemy
      //
      w = pScript->rgwOperand[1] * 5;
      w += gpGlobals->g.PlayerRoles.rgwAttackStrength[gpGlobals->rgParty[g_Battle.wMovingPlayerIndex].wPlayerRole] * RandomLong(0, 3);
      PAL_BattleSimulateMagic((SHORT)wEventObjectID, pScript->rgwOperand[0], w);
      break;

   case 0x0067:
      //
      // Enemy use magic
      //
      g_Battle.rgEnemy[wEventObjectID].e.wMagic = pScript->rgwOperand[0];
      g_Battle.rgEnemy[wEventObjectID].e.wMagicRate =
         ((pScript->rgwOperand[1] == 0) ? 10 : pScript->rgwOperand[1]);
      break;

   case 0x0068:
      //
      // Jump if it's enemy's turn
      //
      if (g_Battle.fEnemyMoving)
      {
         wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0069:
      //
      // Enemy escape in battle
      //
      PAL_BattleEnemyEscape();
      break;

   case 0x006A:
      //
      // Steal from the enemy
      //
      PAL_BattleStealFromEnemy(wEventObjectID, pScript->rgwOperand[0]);
      break;

   case 0x006B:
      //
      // Blow away enemies
      //
      g_Battle.iBlow = (SHORT)(pScript->rgwOperand[0]);
      break;

   case 0x006C:
      //
      // Walk the NPC in one step
      //
      pCurrent->x += (SHORT)(pScript->rgwOperand[1]);
      pCurrent->y += (SHORT)(pScript->rgwOperand[2]);
      PAL_NPCWalkOneStep(wCurEventObjectID, 0);
      break;

   case 0x006D:
      //
      // Set the enter script and teleport script for a scene
      //
      if (pScript->rgwOperand[0])
      {
         if (pScript->rgwOperand[1])
         {
            gpGlobals->g.rgScene[pScript->rgwOperand[0] - 1].wScriptOnEnter =
               pScript->rgwOperand[1];
         }

         if (pScript->rgwOperand[2])
         {
            gpGlobals->g.rgScene[pScript->rgwOperand[0] - 1].wScriptOnTeleport =
               pScript->rgwOperand[2];
         }

         if (pScript->rgwOperand[1] == 0 && pScript->rgwOperand[2] == 0)
         {
            gpGlobals->g.rgScene[pScript->rgwOperand[0] - 1].wScriptOnEnter = 0;
            gpGlobals->g.rgScene[pScript->rgwOperand[0] - 1].wScriptOnTeleport = 0;
         }
      }
      break;

   case 0x006E:
      //
      // Move the player to the specified position in one step
      // Saturn 0x0602c288: exact viewport += operands, shown viewport = its lattice cell (0x06033dd8);
      // with a non-zero operand 0x06033480: walk gesture + trail shift only if the party really moved,
      // otherwise the standing gesture; trail[0] = old position (0x06032ec4).
      //
      {
         int ox = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
         int oy = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);
         int vx, vy;

         g_iSatVpX += (SHORT)(pScript->rgwOperand[0]);
         g_iSatVpY += (SHORT)(pScript->rgwOperand[1]);
         vx = g_iSatVpX; vy = g_iSatVpY;
         PAL_SatGridSnap(&vx, &vy);
         gpGlobals->viewport = PAL_XY(vx, vy);

         gpGlobals->wLayer = pScript->rgwOperand[2] * 8;

         if (pScript->rgwOperand[0] != 0 || pScript->rgwOperand[1] != 0)
         {
            BOOL fMoved = (vx + PAL_X(gpGlobals->partyoffset) != ox || vy + PAL_Y(gpGlobals->partyoffset) != oy);
            if (fMoved)
            {
               for (i = 3; i >= 0; i--)
               {
                  gpGlobals->rgTrail[i + 1] = gpGlobals->rgTrail[i];
               }
            }
            gpGlobals->rgTrail[0].wDirection = gpGlobals->wPartyDirection;
            gpGlobals->rgTrail[0].x = ox;
            gpGlobals->rgTrail[0].y = oy;
            PAL_UpdatePartyGestures(fMoved);
         }
         PAL_ClearDirState();   /* 0x06076a09 = -1: held direction ignored until released */
      }
      break;

   case 0x006F:
      //
      // Sync the state of current event object with another event object
      //
      if (pCurrent->sState == (SHORT)(pScript->rgwOperand[1]))
      {
         pEvtObj->sState = (SHORT)(pScript->rgwOperand[1]);
      }
      break;

   case 0x0070:
      //
      // Walk the party to the specified position
      //
      PAL_PartyWalkTo(pScript->rgwOperand[0], pScript->rgwOperand[1], pScript->rgwOperand[2], 2);
      break;

   case 0x0071:
      //
      // Wave the screen
      //
      // Saturn op 0x71 is an empty function: no screen wave.
      break;

   case 0x0073:
      //
      // Fade the screen to scene
      //
      // Saturn op 0x73 (0x0602c914): clears VDP2 VRAM 0x25E20000 (0x20000 bytes), then the op 0x51 routine
      // 0x06023c64 -> the scene appears at once, no cross-fade.
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
      PAL_SatFadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
      gpGlobals->fNeedToFadeIn = FALSE;
      break;

   case 0x0074:
      //
      // Jump if not all players are full HP
      //
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         w = gpGlobals->rgParty[i].wPlayerRole;
         if (gpGlobals->g.PlayerRoles.rgwHP[w] < gpGlobals->g.PlayerRoles.rgwMaxHP[w])
         {
            wScriptEntry = pScript->rgwOperand[0] - 1;
            break;
         }
      }
      break;

   case 0x0075:
      //
      // Set the player party
      //
      gpGlobals->wMaxPartyMemberIndex = 0;
      for (i = 0; i < 3; i++)
      {
         if (pScript->rgwOperand[i] != 0)
         {
            gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex].wPlayerRole =
               pScript->rgwOperand[i] - 1;

            gpGlobals->wMaxPartyMemberIndex++;
         }
      }

      if (gpGlobals->wMaxPartyMemberIndex == 0)
      {
         // HACK for Dream 2.11
         gpGlobals->rgParty[0].wPlayerRole = 0;
         gpGlobals->wMaxPartyMemberIndex = 1;
      }

      gpGlobals->wMaxPartyMemberIndex--;

      //
      // Reload the player sprites
      //
      PAL_SetLoadFlags(kLoadPlayerSprite);
      PAL_LoadResources();

      memset(gpGlobals->rgPoisonStatus, 0, sizeof(gpGlobals->rgPoisonStatus));
      PAL_UpdateEquipments();
      {  // Saturn 0x0602ca2c ends with 0x06032ec4: members placed from the trail
         extern VOID PAL_SatMemberPos(int i);
         extern VOID PAL_SatPartyGesturesFromSteps(VOID);
         PAL_UpdatePartyGestures(FALSE);
         for (i = 1; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++) PAL_SatMemberPos(i);
         PAL_SatPartyGesturesFromSteps();   // gestures from the step counters, not the standing ones
      }
      break;

   case 0x0076:
      //
      // Show FBP picture
      //
      if (gConfig.fIsWIN95)
	  {
         SDL_FillRect(gpScreen, NULL, 0); SAT_BankFill(gpScreen, NULL, 0);
         VIDEO_UpdateScreen(NULL);
      }
      else
	  {
         PAL_EndingSetEffectSprite(0);
         PAL_ShowFBP(pScript->rgwOperand[0], pScript->rgwOperand[1]);
      }
      //
      // Saturn: the picture (0xffff = black screen) is shown at full brightness even right after a 0x50 fade-out;
      // emu o_r4f k05 (0xb1d: 0x50, 0x76 ffff, 0x3b "當夜"): black, then the text visible 20 vsync later.
      //
      VIDEO_SetPalette(PAL_GetPalette(gpGlobals->wNumPalette, gpGlobals->fNightPalette));
      break;

   case 0x0077:
      //
      // Stop current playing music
      //
      AUDIO_PlayMusic(0, FALSE,
         (pScript->rgwOperand[0] == 0) ? 2.0f : (FLOAT)(pScript->rgwOperand[0]) * 3);
      gpGlobals->wNumMusic = 0;
      break;

   case 0x0078:
      //
      // Saturn 0x0602cb8c: reload the scene (loader).  After a script battle the screen stays black and the next
      // 0x05 fades in stepwise (see battle.c g_fSatBattleStayBlack).
      //
      {
         extern BOOL g_fSatBattleStayBlack;
         extern Uint64 g_qwSatBattleBlack;
         extern VOID PAL_SatReloadFirstStepAt(Uint64);
         if (g_fSatBattleStayBlack)
         {
            g_fSatBattleStayBlack = FALSE;
            PAL_SatReloadFirstStepAt(g_qwSatBattleBlack + 6200);
         }
         else if (!gpGlobals->fInBattle)
         {
            //
            // Not right after a battle (0x2402 area: 0x07, 0x45, 0x1F, 0x3E text, 0x78, 0x05): the loader fades the
            // map out (box gone, black ~10 vsync later) and the next 0x05 fades in stepwise ~110 vsync after the black
            // (emu o_r48b k20: 54.5 -> 49.9 -> black x11 samples -> 20.2 -> 57.1).  Port: like op 0x50, then the stepped redraw.
            //
            VIDEO_UpdateScreen(NULL);
            PAL_SatFadeOut(1);
            PAL_SatReloadFirstStepAt(SDL_GetTicks() + 1833);
         }
      }
      break;

   case 0x0079:
      //
      // Jump if the specified player is in the party
      //
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         if (gpGlobals->g.PlayerRoles.rgwName[gpGlobals->rgParty[i].wPlayerRole] ==
            pScript->rgwOperand[0])
         {
            wScriptEntry = pScript->rgwOperand[1] - 1;
            break;
         }
      }
      break;

   case 0x007A:
      //
      // Walk the party to the specified position, at a higher speed
      //
      PAL_PartyWalkTo(pScript->rgwOperand[0], pScript->rgwOperand[1], pScript->rgwOperand[2], 4);
      break;

   case 0x007B:
      //
      // Walk the party to the specified position, at the highest speed
      //
      PAL_PartyWalkTo(pScript->rgwOperand[0], pScript->rgwOperand[1], pScript->rgwOperand[2], 8);
      break;

   case 0x007C:
      //
      // Walk straight to the specified position
      //
      if (gpGlobals->dwFrameNum & 1)   /* Saturn 0x0602cc88: global flag 0x060768e6 toggled every field frame, no object parity */
      {
         if (!PAL_NPCWalkTo(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
            pScript->rgwOperand[2], 4))
         {
            wScriptEntry--;
         }
      }
      else
      {
         wScriptEntry--;
      }
      break;

   case 0x007D:
      //
      // Move the event object
      //
      pCurrent->x += (SHORT)(pScript->rgwOperand[1]);
      pCurrent->y += (SHORT)(pScript->rgwOperand[2]);
      break;

   case 0x007E:
      //
      // Set the layer of event object
      //
      pCurrent->sLayer = (SHORT)(pScript->rgwOperand[1]);
      break;

   case 0x007F:
      //
      // Move the viewport
      //
      // Saturn 0x0602ce38: (4, 0, n) -> (32, 0, 6) and (-16, 0, n) -> (-32, 0, 6) (same 192 px, faster).
      {
      WORD w7f[3] = { pScript->rgwOperand[0], pScript->rgwOperand[1], pScript->rgwOperand[2] };
      if (w7f[1] == 0 && (SHORT)w7f[0] == 4)   { w7f[0] = 32;          w7f[2] = 6; }
      if (w7f[1] == 0 && (SHORT)w7f[0] == -16) { w7f[0] = (WORD)-32;   w7f[2] = 6; }
      if (w7f[0] == 0 && w7f[1] == 0)
      {
         //
         // Move the viewport back to normal state
         //
         x = gpGlobals->rgParty[0].x - 160;
         y = gpGlobals->rgParty[0].y - 112;

         gpGlobals->viewport =
            PAL_XY(PAL_X(gpGlobals->viewport) + x, PAL_Y(gpGlobals->viewport) + y);
         gpGlobals->partyoffset = PAL_XY(160, 112);

         for (i = 0; i <= (short)gpGlobals->wMaxPartyMemberIndex + gpGlobals->nFollower; i++)
         {
            gpGlobals->rgParty[i].x -= x;
            gpGlobals->rgParty[i].y -= y;
         }

         /* The exact viewport follows (it still held the end of the pan): the next party step (0x6E) adds to it,
            so the camera jumped back to the panned spot with the party drawn at the screen centre -- Li Xiaoyao
            hung in mid-air above the pillar (0x5bb6: pan up to Zhao Linger, 0x7F 0 0 0, 0x6E steps). */
         g_iSatVpX = PAL_X(gpGlobals->viewport); g_iSatVpY = PAL_Y(gpGlobals->viewport);

         if (w7f[2] != 0xFFFF)
         {
            PAL_MakeScene();
            VIDEO_UpdateScreen(NULL);
         }
      }
      else
      {
         DWORD time;
         //
         // Saturn 0x0602ce38: the viewport goes through 0x06033dd8 (pixel -> cell x, y, half h) every step, so
         // the shown viewport snaps to the 16x8 lattice (emu entry 0x1436, (4,2) x 48: a (16,8) jump every
         // 4 steps); one step = 0x06035410 + 0x060232a4 = 6 vsync.
         //
         int vx0 = PAL_X(gpGlobals->viewport), vy0 = PAL_Y(gpGlobals->viewport), step;

         i = 0;

         time = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;

         do
         {
            if (w7f[2] == 0xFFFF)
            {
               x = PAL_X(gpGlobals->viewport);
               y = PAL_Y(gpGlobals->viewport);

               gpGlobals->viewport =
                  PAL_XY(w7f[0] * 32 - 160, w7f[1] * 16 - 112);
               g_iSatVpX = PAL_X(gpGlobals->viewport); g_iSatVpY = PAL_Y(gpGlobals->viewport);

               x -= PAL_X(gpGlobals->viewport);
               y -= PAL_Y(gpGlobals->viewport);

               for (j = 0; j <= (short)gpGlobals->wMaxPartyMemberIndex + gpGlobals->nFollower; j++)
               {
                  gpGlobals->rgParty[j].x += x;
                  gpGlobals->rgParty[j].y += y;
               }
            }
            else
            {
               int ex = vx0 + (i + 1) * (SHORT)w7f[0], ey = vy0 + (i + 1) * (SHORT)w7f[1];
               int qx = ex, qy = ey;
               PAL_SatGridSnap(&qx, &qy);
               g_iSatVpX = ex; g_iSatVpY = ey;
               x = qx - PAL_X(gpGlobals->viewport);
               y = qy - PAL_Y(gpGlobals->viewport);
               gpGlobals->viewport =
                  PAL_XY(PAL_X(gpGlobals->viewport) + x, PAL_Y(gpGlobals->viewport) + y);
               gpGlobals->partyoffset =
                  PAL_XY(PAL_X(gpGlobals->partyoffset) - x, PAL_Y(gpGlobals->partyoffset) - y);

               for (j = 0; j <= (short)gpGlobals->wMaxPartyMemberIndex + gpGlobals->nFollower; j++)
               {
                  gpGlobals->rgParty[j].x -= x;
                  gpGlobals->rgParty[j].y -= y;
               }
            }

            if (w7f[2] != 0xFFFF)
            {
               PAL_GameUpdate(FALSE);
            }

            PAL_MakeScene();
            VIDEO_UpdateScreen(NULL);

            //
            // Delay for one frame
            //
			PAL_DelayUntil(time);
            time = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;
         } while (++i < (SHORT)(w7f[2]));
         (void)step;
      }
      }
      break;

   case 0x0080:
      //
      // Toggle day/night palette
      //
      // Saturn op 0x80 (0x0602d2ec): no palette change, just 32 frames -- operand <= 0: scene updated
      // (0x06035410 + 0x060232a4), else 0x06021fd8(operand) per iteration (operand 1 = one frame).
      {
         int k;
         for (k = 0; k < 32; k++)
         {
            // emu (entry 0x1236 = 0x80 0 + 0x50 vs. 0x1237): 192 vsync for the 32 iterations = 6 vsync each.
            SHORT op = (SHORT)pScript->rgwOperand[0];
            DWORD t = SDL_GetTicks() + ((op <= 0) ? 1 : ((op > 3) ? op / 4 : op)) * SAT_SCRIPT_FRAME_TIME;
            if ((SHORT)pScript->rgwOperand[0] <= 0)
            {
               PAL_ClearKeyState();
               g_InputState.dir = kDirUnknown;
               g_InputState.prevdir = kDirUnknown;
               PAL_GameUpdate(FALSE);
               PAL_MakeScene();
               VIDEO_UpdateScreen(NULL);
            }
            PAL_ProcessEvent();
            while (SDL_GetTicks() < t) { PAL_ProcessEvent(); SDL_Delay(1); }
         }
      }
      break;

   case 0x0081:
      //
      // Jump if the player is not facing the specified event object
      //
      {
         if (pScript->rgwOperand[0] <= gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex ||
            pScript->rgwOperand[0] > gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex)
         {
            //
            // The event object is not in the current scene
            //
            wScriptEntry = pScript->rgwOperand[2] - 1;
            g_fScriptSuccess = FALSE;
            break;
         }

         x = pCurrent->x;
         y = pCurrent->y;

         x +=
            ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirSouth)
               ? 16 : -16);
         y +=
            ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirNorth)
               ? 8 : -8);

         x -= PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
         y -= PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);

         if (abs(x) + abs(y * 2) < pScript->rgwOperand[1] * 32 + 16 && gpGlobals->g.lprgEventObject[pScript->rgwOperand[0] - 1].sState > 0)
         {
            if (pScript->rgwOperand[1] > 0)
            {
               //
               // Change the trigger mode so that the object can be triggered in next frame
               //
               pCurrent->wTriggerMode = kTriggerTouchNormal + pScript->rgwOperand[1];
            }
         }
         else
         {
            wScriptEntry = pScript->rgwOperand[2] - 1;
            g_fScriptSuccess = FALSE;
         }
      }
      break;

   case 0x0082:
      //
      // Walk straight to the specified position, at a high speed
      //
      if (!PAL_NPCWalkTo(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
         pScript->rgwOperand[2], 8))
      {
         wScriptEntry--;
      }
      break;

   case 0x0083:
      //
      // Jump if event object is not in the specified zone of the current event object
      //
      if (pScript->rgwOperand[0] <= gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex ||
         pScript->rgwOperand[0] > gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex)
      {
         //
         // The event object is not in the current scene
         //
         wScriptEntry = pScript->rgwOperand[2] - 1;
         g_fScriptSuccess = FALSE;
         break;
      }

      x = pEvtObj->x - pCurrent->x;
      y = pEvtObj->y - pCurrent->y;

      if (abs(x) + abs(y * 2) >= pScript->rgwOperand[1] * 32 + 16)
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
         g_fScriptSuccess = FALSE;
      }
      break;

   case 0x0084:
      //
      // Place the item which player used as an event object to the scene
      //
      if (pScript->rgwOperand[0] <= gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex ||
         pScript->rgwOperand[0] > gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex)
      {
         //
         // The event object is not in the current scene
         //
         wScriptEntry = pScript->rgwOperand[2] - 1;
         g_fScriptSuccess = FALSE;
         break;
      }

      x = PAL_X(gpGlobals->viewport) + PAL_X(gpGlobals->partyoffset);
      y = PAL_Y(gpGlobals->viewport) + PAL_Y(gpGlobals->partyoffset);

      x +=
         ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirSouth)
            ? -16 : 16);
      y +=
         ((gpGlobals->wPartyDirection == kDirWest || gpGlobals->wPartyDirection == kDirNorth)
            ? -8 : 8);

      if (PAL_CheckObstacle(PAL_XY(x, y), FALSE, 0))
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
         g_fScriptSuccess = FALSE;
      }
      else
      {
         pCurrent->x = x;
         pCurrent->y = y;
         pCurrent->sState = (SHORT)(pScript->rgwOperand[1]);
      }
      break;

   case 0x0085:
      //
      // Delay for a period
      //
      // Saturn op 0x85 (0x0602d7ac): 0x06021fd8(n * 5) = (5n > 3 ? 5n / 4 : 5n) iterations of the scene
      // routine 0x060231e8. Measured (emu, entry 4680 = 0x85 30 vs. 4681, next visible event 0x3c dialog):
      // 222 vsync for 37 iterations = 6 vsync each. (Measurements ending in a 0x3d dialog are unreliable:
      // its ~57 vsync start-up absorbs a preceding wait.)
      {
         int n = (SHORT)pScript->rgwOperand[0] * 5;
         if (n > 3) n /= 4;
         if (n > 0) UTIL_Delay(n * SAT_SCRIPT_FRAME_TIME);
      }
      break;

   case 0x0086:
      //
      // Jump if the specified item is not equipped
      //
      if (pScript->rgwOperand[1] == 0) 
           pScript->rgwOperand[1] = 1;
      y = 0;
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         w = gpGlobals->rgParty[i].wPlayerRole;
         for (x = 0; x < MAX_PLAYER_EQUIPMENTS; x++)
         {
            if (gpGlobals->g.PlayerRoles.rgwEquipment[x][w] == pScript->rgwOperand[0])
            {
               y++;
            }
         }
      }
      if (y < pScript->rgwOperand[1])
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
      }
      break;

   case 0x0087:
      //
      // Animate the event object
      //
      PAL_NPCWalkOneStep(wCurEventObjectID, 0);
      break;

   case 0x0088:
      //
      // Set the base damage of magic according to amount of money
      //
      i = ((gpGlobals->dwCash > 5000) ? 5000 : gpGlobals->dwCash);
      gpGlobals->dwCash -= i;
      j = gpGlobals->g.rgObject[pScript->rgwOperand[0]].magic.wMagicNumber;
      gpGlobals->g.lprgMagic[j].wBaseDamage = i * 2 / 5;
      break;

   case 0x0089:
      //
      // Set the battle result
      //
      g_Battle.BattleResult = pScript->rgwOperand[0];
      break;

   case 0x008A:
      //
      // Enable Auto-Battle for next battle
      //
      gpGlobals->fAutoBattle = TRUE;
      break;

   case 0x008B:
      //
      // change the current palette
      //
      // Saturn op 0x8B (0x0602d978) is an empty function: the scene palette never changes
      // (port: pat.mkf #2 / #5 are the enemy / title palettes and would recolour the map).
      break;

   case 0x008C:
      //
      // Fade from/to color
      //
      // Saturn op 0x8C (0x0602d984) is an empty function: no colour fade.
      break;

   case 0x008D:
      //
      // Increase player's level
      //
      PAL_PlayerLevelUp(wEventObjectID, pScript->rgwOperand[0]);
      break;

   case 0x008F:
      //
      // Halve the cash amount
      //
      gpGlobals->dwCash /= 2;
      break;

   case 0x0090:
      //
      // Set the object script
      //
      gpGlobals->g.rgObject[pScript->rgwOperand[0]].rgwData[2 + pScript->rgwOperand[2]] =
         pScript->rgwOperand[1];
      break;

   case 0x0091:
      //
      // Jump if the enemy is not first of same kind
      //
      {
         int self_pos=0;
         int count=0;
         if (gpGlobals->fInBattle)
         {
            for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
            {
               if (g_Battle.rgEnemy[i].wObjectID == g_Battle.rgEnemy[wEventObjectID].wObjectID)
               {
                  count++;
                  if(i==wEventObjectID)
                     self_pos=count;
               }
            }
         }
         if(self_pos>1)
            wScriptEntry = pScript->rgwOperand[0] - 1;
      }
      break;

   case 0x0092:
      //
      // Show a magic-casting animation for a player in battle
      //
      //
      // Saturn 0x0602dba4: only in battle with operand > 0: frame 6, one battle tick (0x0603e818(1)), then the
      // pre-magic animation 0x0603bbdc(index, 0).  No colour-shift flash and no scene fade (DOS had both).
      //
      if (gpGlobals->fInBattle && (SHORT)pScript->rgwOperand[0] > 0)
      {
         g_Battle.rgPlayer[pScript->rgwOperand[0] - 1].wCurrentFrame = 6;
         PAL_BattleDelay(1, 0, TRUE);
         PAL_BattleShowPlayerPreMagicAnim(pScript->rgwOperand[0] - 1, FALSE);
      }
      break;

   case 0x0093:
      //
      // Fade the screen. Update scene in the process.
      //
      PAL_SceneFade(gpGlobals->wNumPalette, gpGlobals->fNightPalette,
         (SHORT)(pScript->rgwOperand[0]));
      gpGlobals->fNeedToFadeIn = ((SHORT)(pScript->rgwOperand[0]) < 0);
      break;

   case 0x0094:
      //
      // Jump if the state of event object is the specified one
      //
      if (pCurrent->sState == (SHORT)(pScript->rgwOperand[1]))
      {
         wScriptEntry = pScript->rgwOperand[2] - 1;
      }
      break;

   case 0x0095:
      //
      // Jump if the current scene is the specified one
      //
      if (gpGlobals->wNumScene == pScript->rgwOperand[0])
      {
         wScriptEntry = pScript->rgwOperand[1] - 1;
      }
      break;

   case 0x0096:
      //
      // Show the ending animation
      //
      if (!gConfig.fIsWIN95)
         PAL_EndingAnimation();
      break;

   case 0x0097:
      //
      // Ride the event object to the specified position, at a higher speed
      //
      g_iSatSynch = 3;   /* Saturn: SGL frame count while riding; 3 afterwards (0x0602a4b0) */
      PAL_PartyRideEventObject(wEventObjectID, pScript->rgwOperand[0], pScript->rgwOperand[1],
         pScript->rgwOperand[2], 8);
      g_iSatSynch = 3;
      break;

   case 0x0098:
      //
      // Set follower of the party
      //
      j=0;
      for(i=0;i<2;i++)
         if (pScript->rgwOperand[i] > 0)
         {
            int curFollower = j = i+1;
            gpGlobals->nFollower = curFollower;
            gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + curFollower].wPlayerRole = pScript->rgwOperand[i];
            
            PAL_SetLoadFlags(kLoadPlayerSprite);
            PAL_LoadResources();
            
            //
            // Update the position and gesture for the follower
            //
            gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + curFollower].x =
            gpGlobals->rgTrail[3+i].x - PAL_X(gpGlobals->viewport);
            gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + curFollower].y =
            gpGlobals->rgTrail[3+i].y - PAL_Y(gpGlobals->viewport);
            gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + curFollower].wFrame =
            gpGlobals->rgTrail[3+i].wDirection * 3;
         }
      if( j == 0 )
      {
         gpGlobals->nFollower = 0;
      }
      break;

   case 0x0099:
      //
      // Change the map for the specified scene
      //
      if (pScript->rgwOperand[0] == 0xFFFF)
      {
         gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wMapNum = pScript->rgwOperand[1];
         PAL_SetLoadFlags(kLoadScene);
         PAL_LoadResources();
      }
      else
      {
         gpGlobals->g.rgScene[pScript->rgwOperand[0] - 1].wMapNum = pScript->rgwOperand[1];
      }
      break;

   case 0x009A:
      //
      // Set the state for multiple event objects
      //
      for (i = pScript->rgwOperand[0]; i <= pScript->rgwOperand[1]; i++)
      {
         gpGlobals->g.lprgEventObject[i - 1].sState = pScript->rgwOperand[2];
      }
      break;

   case 0x009B:
      //
      // Fade to the current scene
      // FIXME: This is obviously wrong
      //
      VIDEO_BackupScreen(gpScreen);
      PAL_MakeScene();
      VIDEO_FadeScreen(2);
      break;

   case 0x009C:
      //
      // Enemy division itself
      //
      w = 0;

      for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
      {
         if (g_Battle.rgEnemy[i].wObjectID != 0)
         {
            w++;
         }
      }

      if (w != 1 || g_Battle.rgEnemy[wCurEventObjectID].e.wHealth <= 1)
      {
         //
         // Division is only possible when only 1 enemy left
         // health too low also cannot division
         //
         if (pScript->rgwOperand[1] != 0)
         {
            wScriptEntry = pScript->rgwOperand[1] - 1;
         }
         break;
      }

      w = pScript->rgwOperand[0];
      if (w == 0)
      {
         w = 1;
      }
      x = w + 1;
      y = w;

      //division does not limited by original team layout
      for (i = 0; i < MAX_ENEMIES_IN_TEAM; i++)
      {
         if (w > 0 && g_Battle.rgEnemy[i].wObjectID == 0)
         {
            w--;

            //notice: MAX MAY VARYING IN DIVISION!
            memset(&(g_Battle.rgEnemy[i]), 0, sizeof(BATTLEENEMY));

            g_Battle.rgEnemy[i].wObjectID = g_Battle.rgEnemy[wEventObjectID].wObjectID;
            g_Battle.rgEnemy[i].e = g_Battle.rgEnemy[wEventObjectID].e;
            g_Battle.rgEnemy[i].e.wHealth = (g_Battle.rgEnemy[wEventObjectID].e.wHealth+y)/x;
            g_Battle.rgEnemy[i].wScriptOnTurnStart = g_Battle.rgEnemy[wEventObjectID].wScriptOnTurnStart;
            g_Battle.rgEnemy[i].wScriptOnBattleEnd = g_Battle.rgEnemy[wEventObjectID].wScriptOnBattleEnd;
            g_Battle.rgEnemy[i].wScriptOnReady = g_Battle.rgEnemy[wEventObjectID].wScriptOnReady;

            g_Battle.rgEnemy[i].state = kFighterWait;
            g_Battle.rgEnemy[i].flTimeMeter = 50;
            g_Battle.rgEnemy[i].iColorShift = 0;

         }
      }
      g_Battle.rgEnemy[wCurEventObjectID].e.wHealth = (g_Battle.rgEnemy[wEventObjectID].e.wHealth+y)/x;

      w = 0;
      for (i = 0; i < MAX_ENEMIES_IN_TEAM; i++)
         if (g_Battle.rgEnemy[i].wObjectID != 0 )
            w=i;
      g_Battle.wMaxEnemyIndex = w;

      PAL_LoadBattleSprites();

      for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
      {
         if (g_Battle.rgEnemy[i].wObjectID == 0)
         {
            continue;
         }
         g_Battle.rgEnemy[i].pos = g_Battle.rgEnemy[wEventObjectID].pos;
      }

      for (i = 0; i < 10; i++)
      {
         for (j = 0; j <= g_Battle.wMaxEnemyIndex; j++)
         {
            x = (PAL_X(g_Battle.rgEnemy[j].pos) + PAL_X(g_Battle.rgEnemy[j].posOriginal)) / 2;
            y = (PAL_Y(g_Battle.rgEnemy[j].pos) + PAL_Y(g_Battle.rgEnemy[j].posOriginal)) / 2;

            g_Battle.rgEnemy[j].pos = PAL_XY(x, y);
         }

         PAL_BattleDelay(1, 0, TRUE);
      }

      PAL_BattleUpdateFighters();
      PAL_BattleDelay(1, 0, TRUE);
      break;

   case 0x009E:
      //
      // Enemy summons another monster
      //
      // Saturn 0x0602e6c4 (emu o_sum3, team 64): 0x06021fd8(60) = 45 vsync idle, then the summoner's
      // first magic frame; the new enemy appears 7 ticks later with no fade / colour shift, pose ends a tick later.
      PAL_BattleDelay(22, 0, TRUE);
      g_Battle.rgEnemy[wEventObjectID].wCurrentFrame = g_Battle.rgEnemy[wEventObjectID].e.wIdleFrames;
      PAL_BattleDelay(7, 0, FALSE);

      x = 0;
      w = pScript->rgwOperand[0];
      y = (((SHORT)(pScript->rgwOperand[1]) <= 0) ? 1 : (SHORT)pScript->rgwOperand[1]);

      if (w == 0 || w == 0xFFFF)
      {
         w = g_Battle.rgEnemy[wEventObjectID].wObjectID;
      }

      for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
      {
         if (g_Battle.rgEnemy[i].wObjectID == 0)
         {
            x++;
         }
      }

      if (x < y || g_Battle.iHidingTime > 0 ||
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusSleep] != 0 ||
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusParalyzed] != 0 ||
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusConfused] != 0)
      {
         if (pScript->rgwOperand[2] != 0)
         {
            wScriptEntry = pScript->rgwOperand[2] - 1;
         }
      }
      else
      {
         for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
         {
            if (g_Battle.rgEnemy[i].wObjectID == 0)
            {
               memset(&(g_Battle.rgEnemy[i]), 0, sizeof(BATTLEENEMY));

               g_Battle.rgEnemy[i].wObjectID = w;
               g_Battle.rgEnemy[i].e = gpGlobals->g.lprgEnemy[gpGlobals->g.rgObject[w].enemy.wEnemyID];

               g_Battle.rgEnemy[i].state = kFighterWait;
               g_Battle.rgEnemy[i].wScriptOnTurnStart = gpGlobals->g.rgObject[w].enemy.wScriptOnTurnStart;
               g_Battle.rgEnemy[i].wScriptOnBattleEnd = gpGlobals->g.rgObject[w].enemy.wScriptOnBattleEnd;
               g_Battle.rgEnemy[i].wScriptOnReady = gpGlobals->g.rgObject[w].enemy.wScriptOnReady;
               g_Battle.rgEnemy[i].flTimeMeter = 50;
               g_Battle.rgEnemy[i].iColorShift = 8;

               y--;
               if (y <= 0)
               {
                  break;
               }
            }
         }

         for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
         {
            g_Battle.rgEnemy[i].iColorShift = 0;
         }
         PAL_LoadBattleSprites();
         AUDIO_PlaySound(212);
         PAL_BattleDelay(1, 0, FALSE);
      }
      g_Battle.rgEnemy[wEventObjectID].wCurrentFrame = 0;
      PAL_BattleDelay(1, 0, FALSE);
      break;

   case 0x009F:
      //
      // Enemy transforms into something else
      //
      if (g_Battle.iHidingTime <= 0 &&
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusSleep] == 0 &&
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusParalyzed] == 0 &&
         g_Battle.rgEnemy[wEventObjectID].rgwStatus[kStatusConfused] == 0)
      {
         w = g_Battle.rgEnemy[wEventObjectID].e.wHealth;

         g_Battle.rgEnemy[wEventObjectID].wObjectID = pScript->rgwOperand[0];
         g_Battle.rgEnemy[wEventObjectID].e =
            gpGlobals->g.lprgEnemy[gpGlobals->g.rgObject[pScript->rgwOperand[0]].enemy.wEnemyID];

         g_Battle.rgEnemy[wEventObjectID].e.wHealth = w;
         g_Battle.rgEnemy[wEventObjectID].wCurrentFrame = 0;

         // Saturn (emu o_tr, team 217): the new enemy replaces the old one at once (6 vsync load stall),
         // no colour shift and no scene fade.
         g_Battle.rgEnemy[wEventObjectID].iColorShift = 0;
         AUDIO_PlaySound(47);
         PAL_LoadBattleSprites();
         PAL_BattleDelay(1, 0, FALSE);
      }
      break;

   case 0x00A0:
      //
      // Quit game
      //
      if (gConfig.fIsWIN95)
         PAL_EndingScreen();
      PAL_AdditionalCredits();
      //
      // user request: after the ending and the credits, back to the title screen (the port used to quit here).
      // Same steps as the start of PAL_GameMain: the title menu picks new game / a save, loaded in the next tick.
      //
      AUDIO_PlayMusic(0, FALSE, 1);
      PAL_FadeOut(1);
      g_fSatRNGBackdrop = FALSE;
      gpGlobals->fInBattle = FALSE;
      PAL_ReloadInNextTick(PAL_OpeningMenu());
      break;

   case 0x00A1:
      //
      // Set the positions of all party members to the same as the first one
      //
      for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)
      {
         gpGlobals->rgTrail[i].wDirection = gpGlobals->wPartyDirection;
         gpGlobals->rgTrail[i].x = gpGlobals->rgParty[0].x + PAL_X(gpGlobals->viewport);
         gpGlobals->rgTrail[i].y = gpGlobals->rgParty[0].y + PAL_Y(gpGlobals->viewport);
      }
      for (i = 1; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         gpGlobals->rgParty[i].x = gpGlobals->rgParty[0].x;
         gpGlobals->rgParty[i].y = gpGlobals->rgParty[0].y - 1;
      }
      PAL_UpdatePartyGestures(FALSE);
      break;

   case 0x00A2:
      //
      // Jump to one of the following instructions randomly
      //
      wScriptEntry += RandomLong(0, pScript->rgwOperand[0] - 1);
      break;

   case 0x00A3:
      //
      // Play CD music. Use the RIX music for fallback.
      //
      gpGlobals->wNumMusic = pScript->rgwOperand[1];
      if (AUDIO_CD_Available())
      {
         int numTrack = (SHORT)pScript->rgwOperand[0];
         if (!AUDIO_PlayCDTrack(numTrack == -1 ? -2 : numTrack))
            AUDIO_PlayMusic(pScript->rgwOperand[1], TRUE, 0);
      }
      else
         AUDIO_PlayMusic(pScript->rgwOperand[1], TRUE, 0);
      break;

   case 0x00A4:
      //
      // Scroll FBP to the screen
      //
      if (!gConfig.fIsWIN95)
	  {
         if (pScript->rgwOperand[0] == 68)
         {
            //
            // HACKHACK: to make the ending picture show correctly
            //
            PAL_ShowFBP(69, 0);
         }
         PAL_ScrollFBP(pScript->rgwOperand[0], pScript->rgwOperand[2], TRUE);
      }
      break;

   case 0x00A5:
      //
      // Show FBP picture with sprite effects
      //
      if (!gConfig.fIsWIN95)
	  {
         if (pScript->rgwOperand[1] != 0xFFFF)
         {
            PAL_EndingSetEffectSprite(pScript->rgwOperand[1]);
         }
         PAL_ShowFBP(pScript->rgwOperand[0], pScript->rgwOperand[2]);
      }
      break;

   case 0x00A6:
      //
      // backup screen
      //
      VIDEO_BackupScreen(gpScreen);
      break;

   case 0x00A8:
      //
      // Saturn 0x0602ed84: clears the VDP2 bitmap layers (0x25E00000 / 0x25E20000, where an RNG movie stays as the
      // backdrop), resets 0x06076b40 and the window 0x0605db54(0, 0, 319, 223): the map layer under it shows again.
      // emu o_r60d (script 0x2ed4, after RNG 0x23 + 0x05): the dialogs that follow are over the map, not the last
      // movie frame.
      //
      if (!gpGlobals->fInBattle && g_fSatRNGBackdrop)
      {
         g_fSatRNGBackdrop = FALSE;
         { extern VOID PAL_SatBoxKeptReset(VOID); PAL_SatBoxKeptReset(); }
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);
      }
      break;

   case 0x00A9:
      //
      // Saturn-port only instruction (no DOS equivalent); treated as no-op.
      //
      break;

   default:
      TerminateOnError("SCRIPT: Invalid Instruction at %4x: (%4x - %4x, %4x, %4x)",
         wScriptEntry, pScript->wOperation, pScript->rgwOperand[0],
         pScript->rgwOperand[1], pScript->rgwOperand[2]);
      break;
   }

   return wScriptEntry + 1;
}

PAL_FORCE_INLINE
INT
MESSAGE_GetSpan(
    WORD *pwScriptEntry
)
/*++
 Purpose:

 Get the final span of a message block which started from message index of wScriptEntry
 
 Parameters:
 
 [IN]  pwScriptEntry - The pointer of script entry which starts the message block, must be a 0xffff command.
 
 Return value:
 
 The final span of the message block.
 
 --*/
{
    int currentScriptEntry = *pwScriptEntry;
    int result=0;
    int beginning = 1;
    int firstMsgIndex, lastMsgIndex;

    // ensure the command is 0xFFFF
    assert(gpGlobals->g.lprgScriptEntry[currentScriptEntry].wOperation == 0xFFFF);

    firstMsgIndex = lastMsgIndex = gpGlobals->g.lprgScriptEntry[currentScriptEntry].rgwOperand[0];

    //
    // If the NEXT command is 0xFFFF, but the message index is not continuous or not incremental,
    // this MESSAGE block shoud end at THIS command.
    //
    if( gpGlobals->g.lprgScriptEntry[currentScriptEntry+1].wOperation == 0xFFFF && gpGlobals->g.lprgScriptEntry[currentScriptEntry+1].rgwOperand[0] != lastMsgIndex + 1)
        currentScriptEntry++;
    else
        while ((gpGlobals->g.lprgScriptEntry[currentScriptEntry].wOperation == 0xFFFF &&
                (!beginning ? gpGlobals->g.lprgScriptEntry[currentScriptEntry].rgwOperand[0] == lastMsgIndex + 1 : 1))
               || gpGlobals->g.lprgScriptEntry[currentScriptEntry].wOperation == 0x008E)
        {
            if(gpGlobals->g.lprgScriptEntry[currentScriptEntry].wOperation == 0xFFFF)
                lastMsgIndex = gpGlobals->g.lprgScriptEntry[currentScriptEntry].rgwOperand[0];
            currentScriptEntry++;
            beginning = 0;
        }

    result = lastMsgIndex - firstMsgIndex;
    assert(result >= 0);
    *pwScriptEntry = currentScriptEntry;
    return result;
}

WORD
PAL_RunTriggerScript(
   WORD           wScriptEntry,
   WORD           wEventObjectID
)
/*++
  Purpose:

    Runs a trigger script.

  Parameters:

    [IN]  wScriptEntry - The script entry to execute.

    [IN]  wEventObjectID - The event object ID which invoked the script.

  Return value:

    The entry point of the script.

--*/
{
   static WORD       wLastEventObject = 0;

   WORD              wNextScriptEntry;
   BOOL              fEnded;
   LPSCRIPTENTRY     pScript;
   LPEVENTOBJECT     pEvtObj = NULL;
   int               i;

   extern BOOL       g_fUpdatedInBattle; // HACKHACK

   wNextScriptEntry = wScriptEntry;
   fEnded = FALSE;
   g_fUpdatedInBattle = FALSE;

   if (wEventObjectID == 0xFFFF)
   {
      wEventObjectID = wLastEventObject;
   }

   wLastEventObject = wEventObjectID;

   if (wEventObjectID != 0)
   {
      pEvtObj = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);
   }

   g_fScriptSuccess = TRUE;

   //
   // Set the default dialog speed.
   //
   PAL_DialogSetDelayTime(3);

   while (wScriptEntry != 0 && !fEnded)
   {
      pScript = &(gpGlobals->g.lprgScriptEntry[wScriptEntry]);
      if (pScript->wOperation <= 0x0A || pScript->wOperation == 0xFFFF || (pScript->wOperation >= 0x3B && pScript->wOperation <= 0x3E)) SAT_OpLog(wScriptEntry, pScript);

      {  // Enhancement: play 婶婶's pan-swing (ev12's 4 frames) right before 李逍遥's "哎呦疼啊" in the opening
         static int s_panHitDone = 0;
         if (!s_panHitDone && gpGlobals->wNumScene == 2 && wScriptEntry == 3491)
         { s_panHitDone = 1; PAL_OpeningPanHit(); }
      }

      //
      // user request: two hints from 趙靈兒 for the rain ritual (after 「走吧∼到祭壇去！」, script 33537).
      // 1) Leaving the temple hall (scene 271, exit script 33597 to the forecourt) once that cutscene has run
      //    (it switches off its own trigger, object 4584 by 巫后's statue): the altar is inside.
      // 2) Reading the stele beside the altar (scene 278, object 4642 = the stele before the stone is set,
      //    script 33986 ends at 33995): this is where the stone goes.
      // Same dialog form as her lines in that chapter (0x3D face 88).
      //
      if (wScriptEntry == 33597 && gpGlobals->wNumScene == 271 && gpGlobals->g.lprgEventObject[4584 - 1].sState == 0)
      {
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogLower, 0, 88, FALSE);
         PAL_ShowDialogText(PAL_Tr(L"趙靈兒："));
         PAL_ShowDialogText(PAL_Tr(L"祭壇就在神殿內"));
         PAL_ShowDialogText(PAL_Tr(L"逍遙哥哥，我們不要出去太久"));
      }
      else if (wScriptEntry == 33995 && gpGlobals->wNumScene == 278 && wEventObjectID == 4642)
      {
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogLower, 0, 88, FALSE);
         PAL_ShowDialogText(PAL_Tr(L"趙靈兒："));
         PAL_ShowDialogText(PAL_Tr(L"這應該就是要放入"));
         PAL_ShowDialogText(PAL_Tr(L"聖靈珠的石碑了。"));
      }

      UTIL_LogOutput(LOGLEVEL_DEBUG, "[SCRIPT] %.4x: %.4x %.4x %.4x %.4x\n", wScriptEntry,
         pScript->wOperation, pScript->rgwOperand[0],
         pScript->rgwOperand[1], pScript->rgwOperand[2]);

      switch (pScript->wOperation)
      {
      case 0x0000:
         //
         // Stop running
         //
         fEnded = TRUE;
         break;

      case 0x0001:
         //
         // Stop running and replace the entry with the next line
         //
         fEnded = TRUE;
         wNextScriptEntry = wScriptEntry + 1;
         break;

      case 0x0002:
         //
         // Stop running and replace the entry with the specified one
         //
         if (pScript->rgwOperand[1] == 0 ||
            ++(pEvtObj->nScriptIdleFrame) < pScript->rgwOperand[1])
         {
            fEnded = TRUE;
            wNextScriptEntry = pScript->rgwOperand[0];
         }
         else
         {
            //
            // failed
            //
            pEvtObj->nScriptIdleFrame = 0;
            wScriptEntry++;
         }
         break;

      case 0x0003:
         //
         // unconditional jump
         //
         if (pScript->rgwOperand[1] == 0 ||
            ++(pEvtObj->nScriptIdleFrame) < pScript->rgwOperand[1])
         {
            wScriptEntry = pScript->rgwOperand[0];
         }
         else
         {
            //
            // failed
            //
            pEvtObj->nScriptIdleFrame = 0;
            wScriptEntry++;
         }
         break;

      case 0x0004:
         //
         // Call script
         //
         PAL_RunTriggerScript(pScript->rgwOperand[0],
            ((pScript->rgwOperand[1] == 0) ? wEventObjectID : pScript->rgwOperand[1]));
         wScriptEntry++;
         break;

      case 0x0005:
         //
         // Redraw screen
         //
         PAL_ClearDialog(TRUE);

         if (PAL_DialogIsPlayingRNG())
         {
            VIDEO_RestoreScreen(gpScreen);
         }
         else if (!gpGlobals->fInBattle && g_fSatRNGBackdrop && g_pSatRNGShown != NULL)
         {
            VIDEO_CopySurface(g_pSatRNGShown, NULL, gpScreen, NULL);   /* Saturn: the movie frame stays (see rngplay.c) */
            { extern VOID PAL_SatBoxKeptReset(VOID); PAL_SatBoxKeptReset(); }
            /* HD twin: back to the bare movie frame as well, or the old box (face and name) stays under the next
               speaker's when no new movie frame is shown in between (0x6450: 李逍遙 -> 0x05 -> 0x37 44 44 -> 阿奴,
               the two names overlapped). */
            { extern VOID PAL_SatRNGShownToHD(VOID); PAL_SatRNGShownToHD(); }
            VIDEO_UpdateScreen(NULL);
            UTIL_Delay(SAT_SCRIPT_FRAME_TIME);
         }
         else if (gpGlobals->fInBattle)
         {
            PAL_BattleMakeScene();
            VIDEO_CopyEntireSurface(g_Battle.lpSceneBuf, gpScreen);
            VIDEO_UpdateScreen(NULL);
         }
         else
         {
            if (pScript->rgwOperand[2])
            {
               PAL_UpdatePartyGestures(FALSE);
            }

            //
            // Saturn 0x06025740: no use of operand 1; scene drawn + presented (emu entry 0x11d1 vs 0x11d2: 6 vsync).
            // While the screen is faded out it waits and fades the scene back in (PAL_SatRedrawFaded).
            //
            if (!PAL_SatRedrawFaded(gpGlobals->wNumPalette, gpGlobals->fNightPalette))
            {
               PAL_MakeScene();
               VIDEO_UpdateScreen(NULL);
               UTIL_Delay(SAT_SCRIPT_FRAME_TIME);
            }
         }

         wScriptEntry++;
         break;

      case 0x0006:
         //
         // Jump to the specified address by the specified rate
         //
         if (RandomLong(1, 100) >= pScript->rgwOperand[0])
         {
            wScriptEntry = pScript->rgwOperand[1];
            continue;
         }
         else
         {
             wScriptEntry++;
         }
         break;

      case 0x0007:
         //
         // Start battle
         //
         {
            extern BOOL g_fSatBattleStayBlack;
            WORD w = wScriptEntry + 1;
            int k;
            for (k = 0; k < 3; k++)   /* music / party (0x9f5: 0x43, 0x75, 0x78; 0x1e77: 0x43, 0x78) */
            {
               WORD op = gpGlobals->g.lprgScriptEntry[w].wOperation;
               if (op != 0x0043 && op != 0x0075) break;
               w++;
            }
            g_fSatBattleStayBlack = (gpGlobals->g.lprgScriptEntry[w].wOperation == 0x0078);
         }
         {  /* Field monsters: the win path is "0x52 hide this monster, 0x50 fade out". The Saturn showed the
               field again WITH the beaten monster, then faded out and back in without it. When the win path
               has that shape, leave the battle on black instead and let the 0x52 run first, so the field
               comes back once, already without the monster. */
            extern BOOL g_fSatBattleWinClean;
            WORD e = wScriptEntry + 1; int k; BOOL f52 = FALSE;
            g_fSatBattleWinClean = FALSE;
            for (k = 0; k < 8; k++)
            {
               WORD op = gpGlobals->g.lprgScriptEntry[e].wOperation;
               if (op == 0x0003) e = gpGlobals->g.lprgScriptEntry[e].rgwOperand[0];
               else if (op == 0x0052) { f52 = TRUE; e++; }
               else { g_fSatBattleWinClean = (op == 0x0050 && f52); break; }
            }
         }
         {
            extern WORD g_wBattleEventObjectID;
            g_wBattleEventObjectID = wEventObjectID;
            if (SDL_getenv("PAL_FAKEWIN"))   /* debug: skip the fight, leave the screen as a won 0x78 battle does (black) */
            { SDL_FillRect(gpScreen, NULL, 0); VIDEO_UpdateScreen(NULL); i = kBattleResultWon; }
            else
            i = PAL_StartBattle(SDL_getenv("PAL_BATTLETEAM") ? (WORD)SDL_atoi(SDL_getenv("PAL_BATTLETEAM")) : pScript->rgwOperand[0], !pScript->rgwOperand[2]);   /* debug: env PAL_BATTLETEAM = fight this enemy team instead */
            if (i == kBattleResultWon && wEventObjectID != 0 && wEventObjectID <= gpGlobals->g.nEventObject)
               gpGlobals->g.lprgEventObject[wEventObjectID - 1].wSpritePtrOffset = 0;   /* beaten: a respawn is a new monster */
            g_wBattleEventObjectID = 0;
         }
         if (g_fSatRNGBackdrop && !SDL_getenv("PAL_OLDBACKDROP"))
         {  /* A movie frame was the backdrop before the battle (0x37 ... 0x07). When the script then redraws
               (0x05) without playing a movie first, the map is meant: 34696 拜月教主's fake 巫王 -> 0x78, party
               set up on the map -> 0x05 -> 「靈兒！振作點！」 stayed black behind the dialog, because 0x05 put the
               stale movie frame back. The other such scripts (7798, 11734, 25679) play a movie first: unchanged. */
            WORD e = wScriptEntry + 1; int k;
            for (k = 0; k < 24; k++, e++)
            {
               WORD op = gpGlobals->g.lprgScriptEntry[e].wOperation;
               if (op == 0x0005) { g_fSatRNGBackdrop = FALSE; break; }
               if (op == 0x0037 || op == 0x0036 || op == 0x0059 || op == 0x0000 || op == 0x0002 || op == 0x0003) break;
            }
         }
         { extern BOOL g_fSatBattleWinClean, g_fSatSkipFadeOut; g_fSatSkipFadeOut =(g_fSatBattleWinClean && i == kBattleResultWon); g_fSatBattleWinClean = FALSE; }
         if (i == kBattleResultLost || i == kBattleResultFleed)
         {
            extern BOOL g_fSatBattleStayBlack;
            if (g_fSatBattleStayBlack) { g_fSatBattleStayBlack = FALSE; PAL_MakeScene(); VIDEO_UpdateScreen(NULL); }
         }

         if (i == kBattleResultLost && pScript->rgwOperand[1] != 0)
         {
            wScriptEntry = pScript->rgwOperand[1];
         }
         else if (i == kBattleResultFleed && pScript->rgwOperand[2] != 0)
         {
            wScriptEntry = pScript->rgwOperand[2];
         }
         else
         {
            wScriptEntry++;
         }
         gpGlobals->fAutoBattle = FALSE;
         break;

      case 0x0008:
         //
         // Replace the entry with the next instruction
         //
         wScriptEntry++;
         wNextScriptEntry = wScriptEntry;
         break;

      case 0x0009:
         //
         // wait for the specified number of frames
         //
         {
            DWORD        time;

            PAL_ClearDialog(TRUE);

            // Saturn (0x060258f0): scene draw 0x06035410 + 0x060232a4 per frame; emu entry 0x124f vs 0x1250:
            // 0x09 6 = 35 vsync, 0x124c vs 0x124d: 0x09 10 = 60 vsync -> 6 vsync per frame.
            time = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;

            for (i = 0; i < (pScript->rgwOperand[0] ? pScript->rgwOperand[0] : 1); i++)
            {
               PAL_DelayUntil(time);

               time = SDL_GetTicks() + SAT_SCRIPT_FRAME_TIME;

               if (pScript->rgwOperand[2])
               {
                  PAL_UpdatePartyGestures(FALSE);
               }

               //
               // Saturn: a pending fade-in (scene just changed) is shown before this frame's game update -- emu o_r53e,
               // scene 54 enter 0x2802 (0x16 obj869 dir 3, 0x09 6, auto 0x8878 = 0x0F dir 2): darkness 16/8/full
               // all with dir 3, the object's first auto-script step at the full-brightness vsync.
               //
               if (gpGlobals->fNeedToFadeIn)
               {
                  PAL_MakeScene();
                  VIDEO_UpdateScreen(NULL);
               }

               //
               // A wait between two ranges of the same movie (0x33c6, 0x69f8, 0x69fc: 0x37 .. 0x09 .. 0x37) keeps the
               // movie frame: drawing the map here made the next range apply its delta frames to the map picture
               // (user report: the room turned red as Lin Yueru walked, inn scene at 0x33ca).
               //
               if (!gpGlobals->fInBattle && g_fSatRNGBackdrop && g_pSatRNGShown != NULL && !gpGlobals->fNeedToFadeIn)
               {
                  PAL_GameUpdate(pScript->rgwOperand[1] ? TRUE : FALSE);
                  if (i == 0)
                  {
                     extern VOID PAL_SatBoxKeptReset(VOID);
                     extern void PAL_HDM_MovieBaseRestore(void);
                     VIDEO_CopySurface(g_pSatRNGShown, NULL, gpScreen, NULL);
                     PAL_SatBoxKeptReset();
                     { extern VOID PAL_SatRNGShownToHD(VOID); PAL_SatRNGShownToHD(); }   /* HD twin: wipe the box kept over the movie too (face and name
                                                      included), or the next text (0x33c7, no 0x3C before it) is
                                                      drawn over the old lines */
                  }
                  VIDEO_UpdateScreen(NULL);
                  continue;
               }

               PAL_GameUpdate(pScript->rgwOperand[1] ? TRUE : FALSE);
               PAL_MakeScene();
               VIDEO_UpdateScreen(NULL);
            }
         }
         wScriptEntry++;
         break;

      case 0x000A:
         //
         // Goto the specified address if player selected no
         //
         PAL_ClearDialog(FALSE);

         if (!PAL_ConfirmMenu())
         {
            wScriptEntry = pScript->rgwOperand[0];
         }
         else
         {
            wScriptEntry++;
         }
         break;

      case 0x003B:
         //
         // Show dialog in the middle part of the screen
         //
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogCenter, (BYTE)pScript->rgwOperand[0], 0,
            pScript->rgwOperand[2] ? TRUE : FALSE);
         wScriptEntry++;
         break;

      case 0x003C:
         //
         // Show dialog in the upper part of the screen
         //
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogUpper, (BYTE)pScript->rgwOperand[1],
            pScript->rgwOperand[0], pScript->rgwOperand[2] ? TRUE : FALSE);
         wScriptEntry++;
         break;

      case 0x003D:
         //
         // Show dialog in the lower part of the screen
         //
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogLower, (BYTE)pScript->rgwOperand[1],
            pScript->rgwOperand[0], pScript->rgwOperand[2] ? TRUE : FALSE);
         wScriptEntry++;
         break;

      case 0x003E:
         //
         // Show text in a window at the center of the screen
         //
         PAL_ClearDialog(TRUE);
         PAL_StartDialog(kDialogCenterWindow, (BYTE)pScript->rgwOperand[0], 0, FALSE);
         wScriptEntry++;
         break;

      case 0x008E:
         //
         // Restore the screen
         //
         PAL_SatDialogPageBreak();
         VIDEO_RestoreScreen(gpScreen);
         VIDEO_UpdateScreen(NULL);
         wScriptEntry++;
         break;

      case 0xFFFF:
         //
         // Print dialog text
         //
         if (gConfig.pszMsgFile)
         {
            int msgSpan = MESSAGE_GetSpan(&wScriptEntry);
            int idx = 0, iMsg;
            while ((iMsg = PAL_GetMsgNum(pScript->rgwOperand[0], msgSpan, idx++)) >= 0)
			   {
               if (iMsg == 0)
               {
                  //
                  // Restore the screen
                  //
                  PAL_ClearDialog(TRUE);
                  VIDEO_RestoreScreen(gpScreen);
                  VIDEO_UpdateScreen(NULL);
               }
			   else
                  PAL_ShowDialogText(PAL_GetMsg(iMsg));
            }
         }
		 else
         {
            PAL_ShowDialogText(PAL_GetMsg(pScript->rgwOperand[0]));
            //
            // user request: the fog notice (0x6923 「樹林內瀰漫著濃霧，無法前進」) also hints at the way through.
            // A second line: the whole sentence in one line is wider than the screen.
            //
            if (pScript->rgwOperand[0] == 0x263A && (PAL_LangGet() != 0 || wcsstr(PAL_GetMsg(0x263A), L"濃霧") != NULL))
               PAL_ShowDialogText(PAL_Tr(L"或許可以使用風靈珠吹散"));
            wScriptEntry++;
         }
         break;

      default:
         PAL_ClearDialog(TRUE);
         wScriptEntry = PAL_InterpretInstruction(wScriptEntry, wEventObjectID);
         break;
      }
   }

   PAL_EndDialog();
   g_iCurEquipPart = -1;

   //
   // Saturn: no step from a direction pressed before the script (emu st_corr: the party stays at 470,4e8 after the
   // corridor cutscene; the port replayed the stale 2-frame direction history and stepped west).
   //
   if (wScriptEntry != 0 || wNextScriptEntry != 0) PAL_ResetPartyWalkHeld();

   return wNextScriptEntry;
}

WORD
PAL_RunAutoScript(
   WORD           wScriptEntry,
   WORD           wEventObjectID
)
/*++
  Purpose:

    Runs the autoscript of the specified event object.

  Parameters:

    [IN]  wScriptEntry - The script entry to execute.

    [IN]  wEventObjectID - The event object ID which invoked the script.

  Return value:

    The address of the next script instruction to execute.

--*/
{
   LPSCRIPTENTRY          pScript;
   LPEVENTOBJECT          pEvtObj;

begin:
   pScript = &(gpGlobals->g.lprgScriptEntry[wScriptEntry]);
   pEvtObj = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);

   UTIL_LogOutput(LOGLEVEL_DEBUG, "[AUTOSCRIPT] %04x %.4x: %.4x %.4x %.4x %.4x\n", wEventObjectID, wScriptEntry,
       pScript->wOperation, pScript->rgwOperand[0],
       pScript->rgwOperand[1], pScript->rgwOperand[2]);

   //
   // For autoscript, we should interpret one instruction per frame (except
   // jumping) and save the address of next instruction.
   //
   switch (pScript->wOperation)
   {
   case 0x0000:
      //
      // Stop running
      //
      break;

   case 0x0001:
      //
      // Stop running and replace the entry with the next line
      //
      wScriptEntry++;
      break;

   case 0x0002:
      //
      // Stop running and replace the entry with the specified one
      //
      if (pScript->rgwOperand[1] == 0 ||
         ++(pEvtObj->wScriptIdleFrameCountAuto) < pScript->rgwOperand[1])
      {
         wScriptEntry = pScript->rgwOperand[0];
      }
      else
      {
         pEvtObj->wScriptIdleFrameCountAuto = 0;
         wScriptEntry++;
      }
      break;

   case 0x0003:
      //
      // unconditional jump
      //
      if (pScript->rgwOperand[1] == 0 ||
         ++(pEvtObj->wScriptIdleFrameCountAuto) < pScript->rgwOperand[1])
      {
         wScriptEntry = pScript->rgwOperand[0];
         goto begin;
      }
      else
      {
         pEvtObj->wScriptIdleFrameCountAuto = 0;
         wScriptEntry++;
      }
      break;

   case 0x0004:
      //
      // Call subroutine
      //
      PAL_RunTriggerScript(pScript->rgwOperand[0],
         pScript->rgwOperand[1] ? pScript->rgwOperand[1] : wEventObjectID);
      wScriptEntry++;
      break;

   case 0x0006:
      //
      // jump to the specified address by the specified rate
      //
      if (RandomLong(1, 100) >= pScript->rgwOperand[0])
      {
         if (pScript->rgwOperand[1] != 0)
		 {
            wScriptEntry = pScript->rgwOperand[1];
            goto begin;
		 }
      }
      else
      {
         wScriptEntry++;
      }
      break;

   case 0x0009:
      //
      // Wait for a certain number of frames
      //
      if (++(pEvtObj->wScriptIdleFrameCountAuto) >= pScript->rgwOperand[0])
      {
         //
         // waiting ended; go further
         //
         pEvtObj->wScriptIdleFrameCountAuto = 0;
         wScriptEntry++;
      }
      break;

   case 0xFFFF:
	   if (gConfig.fIsWIN95)
	   {
		   /* Saturn op 0xA7 (0x0602ed1c): dialog type 9, x = 72, y = caller arg (item menu 151 -> 119,
		      magic menu 2); renderer 0x060253c6 draws colour 60 and steps 17 per line */
		   int XBase = 72;
		   int YBase = (wEventObjectID & PAL_ITEM_DESC_BOTTOM) ? 119 : 2;
		   int iDescLine = (wEventObjectID & ~PAL_ITEM_DESC_BOTTOM);

		   if (gConfig.pszMsgFile)
		   {
            int msgSpan = MESSAGE_GetSpan(&wScriptEntry);
			   int idx = 0, iMsg;
			   while ((iMsg = PAL_GetMsgNum(pScript->rgwOperand[0], msgSpan, idx++)) >= 0)
			   {
				   if (iMsg > 0)
				   {
					   PAL_DrawText(PAL_GetMsg(iMsg), PAL_XY(XBase, iDescLine * 17 + YBase), DESCTEXT_COLOR, FALSE, FALSE, FALSE);   /* Saturn 0x060253c6: no shadow */
					   iDescLine++;
				   }
			   }
		   }
		   else
		   {
			   {
			      /* translated text: one message may carry several lines ('\n'); a lone 0x01 = nothing */
			      LPCWSTR p = PAL_GetMsg(pScript->rgwOperand[0]);
			      while (p != NULL && *p && *p != 1)
			      {
			         WCHAR part[128]; LPCWSTR e = wcschr(p, '\n');
			         size_t l = e ? (size_t)(e - p) : wcslen(p);
			         if (l > 127) l = 127;
			         memcpy(part, p, l * sizeof(WCHAR)); part[l] = 0;
			         PAL_DrawText(part, PAL_XY(XBase, iDescLine * ((PAL_LangGet() >= 2 && YBase > 100) ? 16 : 17) + YBase), DESCTEXT_COLOR, FALSE, FALSE, FALSE);   /* Saturn 0x060253c6: no shadow */
			         iDescLine++;   /* Japanese / English: 16 per line, five lines fit under the item list */
			         if (e == NULL) break;
			         p = e + 1;
			      }
			   }
			   wScriptEntry++;
		   }
	   }
	   else
	   {
		   wScriptEntry++;
	   }
	  break;

   case 0x00A7:
	  wScriptEntry++;
      break;

   default:
      //
      // Other operations
      //
      wScriptEntry = PAL_InterpretInstruction(wScriptEntry, wEventObjectID);
      break;
   }

   return wScriptEntry;
}
