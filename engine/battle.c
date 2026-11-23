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

BATTLE          g_Battle;

WORD
g_rgPlayerPos[3][3][2] = {
   {{240, 170}},                         // one player
   {{200, 176}, {256, 152}},             // two players
   {{180, 180}, {234, 170}, {270, 146}}  // three players
};

VOID
PAL_BattleDrawBackground(
   VOID
)
/*++
  Purpose:

    Generate the combat background to the specified screen buffer.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int          i;
   LPBYTE       pSrc, pDst;
   BYTE         b;

   //
   // Draw the background
   //
   pSrc = g_Battle.lpBackground->pixels;
   pDst = g_Battle.lpSceneBuf->pixels;

   for (i = 0; i < g_Battle.lpSceneBuf->pitch * g_Battle.lpSceneBuf->h; i++)
   {
      b = (*pSrc & 0x0F);
      b += g_Battle.sBackgroundColorShift;

      if (b & 0x80)
      {
         b = 0;
      }
      else if (b & 0x70)
      {
         b = 0x0F;
      }

      *pDst = (b | (*pSrc & 0xF0));

      ++pSrc;
      ++pDst;
   }

   //
   // Saturn: the battle field FBP is shown with its own palette -> carry the bank plane over too
   //
   {
      unsigned char *ps = SAT_BankPlane(g_Battle.lpBackground, FALSE);
      unsigned char *pd = SAT_BankPlane(g_Battle.lpSceneBuf, ps != NULL);
      if (pd != NULL)
      {
         size_t n = (size_t)g_Battle.lpSceneBuf->w * g_Battle.lpSceneBuf->h;
         size_t m = (size_t)g_Battle.lpBackground->w * g_Battle.lpBackground->h;
         if (ps != NULL) { memcpy(pd, ps, n < m ? n : m); if (n > m) memset(pd + m, 0, n - m); }
         else memset(pd, 0, n);
      }
   }

   PAL_ApplyWave(g_Battle.lpSceneBuf);
}

WORD g_wSatGhostMask = 0;

VOID
PAL_BattleDrawEnemySprites(
   WORD              wEnemyIndex,
   SDL_Surface      *lpDstSurface
)
/*++
  Purpose:

    Generate enemies in battle into the specified buffer.

  Parameters:

    [IN]  wEnemyIndex - The index of the enemy.

    [OUT] lpDstSurface - pointer to the destination SDL surface.

  Return value:

    None.

--*/
{
   PAL_POS       pos;

   //
   // Draw the enemies
   //
   pos = g_Battle.rgEnemy[wEnemyIndex].pos;

   if (g_Battle.rgEnemy[wEnemyIndex].rgwStatus[kStatusConfused] > 0 &&
      g_Battle.rgEnemy[wEnemyIndex].rgwStatus[kStatusSleep] == 0 &&
      g_Battle.rgEnemy[wEnemyIndex].rgwStatus[kStatusParalyzed] == 0)
   {
      //
      // Enemy is confused
      //
      pos = PAL_XY(PAL_X(pos) + RandomLong(-1, 1), PAL_Y(pos));
   }
      
   pos = PAL_XY(PAL_X(pos) + 1 - PAL_RLEGetWidth(PAL_SpriteGetFrame(g_Battle.rgEnemy[wEnemyIndex].lpSprite, g_Battle.rgEnemy[wEnemyIndex].wCurrentFrame)) / 2,
      PAL_Y(pos) - 9 - PAL_RLEGetHeight(PAL_SpriteGetFrame(g_Battle.rgEnemy[wEnemyIndex].lpSprite, g_Battle.rgEnemy[wEnemyIndex].wCurrentFrame)));   /* Saturn 0x06036e40 (emulator-measured, h 63/66/113): screen left = x+1-w/2, bottom = y+3 (gpScreen = screen-12) */

   if (g_Battle.rgEnemy[wEnemyIndex].wObjectID != 0)
   {
      if (g_Battle.rgEnemy[wEnemyIndex].iColorShift)
      {
         PAL_RLEBlitWithColorShift(PAL_SpriteGetFrame(g_Battle.rgEnemy[wEnemyIndex].lpSprite, g_Battle.rgEnemy[wEnemyIndex].wCurrentFrame),
            lpDstSurface, pos, g_Battle.rgEnemy[wEnemyIndex].iColorShift);
      }
      else
      {
         extern int g_iSatBattleTick;
         extern BYTE g_bSatBankOr;
         // Saturn 0x0603b664: the enemy being targeted is drawn as a ghost on odd ticks (blink); the UI update bumps
         // the tick after the scene is made, hence +1
         if (g_Battle.UI.state == kBattleUISelectTargetEnemy && wEnemyIndex == g_Battle.UI.iSelectedIndex &&
            ((g_iSatBattleTick + 1) & 1))
            g_bSatBankOr = 0x40;
         if (g_wSatGhostMask & (1 << wEnemyIndex))
            g_bSatBankOr = 0x40;   // Saturn: enemy hit by a physical attack is drawn as a ghost for one tick
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(g_Battle.rgEnemy[wEnemyIndex].lpSprite, g_Battle.rgEnemy[wEnemyIndex].wCurrentFrame),
            lpDstSurface, pos);
         g_bSatBankOr = 0;
      }
      {  // HD-mirror: keyed HD enemy frame into g_hdScreen at the same position
         extern int PAL_HDM_On(void); extern void PAL_HDBattle_Sprite(const void *fr, int x, int y);
         if (PAL_HDM_On()) PAL_HDBattle_Sprite(PAL_SpriteGetFrame(g_Battle.rgEnemy[wEnemyIndex].lpSprite, g_Battle.rgEnemy[wEnemyIndex].wCurrentFrame), PAL_X(pos), PAL_Y(pos));
      }
   }
}

VOID
PAL_BattleDrawPlayerSprites(
   WORD              wPlayerIndex,
   SDL_Surface      *lpDstSurface
)
/*++
  Purpose:

    Generate players in battle into the specified buffer.

  Parameters:

    [IN]  wPlayerIndex - The index of the player (-1 = summon god).

    [OUT] lpDstSurface - pointer to the destination SDL surface.

  Return value:

    None.

--*/
{
   PAL_POS      pos;

   if (wPlayerIndex == 0xFFFF)
   {
      //
      // Draw the summoned god
      //
      if (g_Battle.lpSummonSprite != NULL)
      {
         pos = PAL_XY(PAL_X(g_Battle.posSummon) + 1 - PAL_RLEGetWidth(PAL_SpriteGetFrame(g_Battle.lpSummonSprite, g_Battle.iSummonFrame)) / 2,
            PAL_Y(g_Battle.posSummon) - 9 - PAL_RLEGetHeight(PAL_SpriteGetFrame(g_Battle.lpSummonSprite, g_Battle.iSummonFrame)));   /* Saturn 0x06036e40 (emulator-measured, h 63/66/113): screen left = x+1-w/2, bottom = y+3 (gpScreen = screen-12) */

         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(g_Battle.lpSummonSprite, g_Battle.iSummonFrame),
            lpDstSurface, pos);
         {  extern int PAL_HDM_On(void); extern void PAL_HDBattle_Sprite(const void *fr, int x, int y);
            if (PAL_HDM_On()) PAL_HDBattle_Sprite(PAL_SpriteGetFrame(g_Battle.lpSummonSprite, g_Battle.iSummonFrame), PAL_X(pos), PAL_Y(pos)); }
      }
   }
   else
   {
      //
      // Draw the players
      //
      pos = g_Battle.rgPlayer[wPlayerIndex].pos;

      if (gpGlobals->rgPlayerStatus[gpGlobals->rgParty[wPlayerIndex].wPlayerRole][kStatusConfused] != 0 &&
         gpGlobals->rgPlayerStatus[gpGlobals->rgParty[wPlayerIndex].wPlayerRole][kStatusSleep] == 0 &&
         gpGlobals->rgPlayerStatus[gpGlobals->rgParty[wPlayerIndex].wPlayerRole][kStatusParalyzed] == 0 &&
         gpGlobals->g.PlayerRoles.rgwHP[gpGlobals->rgParty[wPlayerIndex].wPlayerRole] > 0 &&
         !PAL_IsPlayerDying(gpGlobals->rgParty[wPlayerIndex].wPlayerRole))
      {
         //
         // Player is confused and not dead
         //
         pos = PAL_XY(PAL_X(pos), PAL_Y(pos) + RandomLong(-1, 1));
      }

      pos = PAL_XY(PAL_X(pos) + 1 - PAL_RLEGetWidth(PAL_SpriteGetFrame(g_Battle.rgPlayer[wPlayerIndex].lpSprite, g_Battle.rgPlayer[wPlayerIndex].wCurrentFrame)) / 2,
         PAL_Y(pos) - 9 - PAL_RLEGetHeight(PAL_SpriteGetFrame(g_Battle.rgPlayer[wPlayerIndex].lpSprite, g_Battle.rgPlayer[wPlayerIndex].wCurrentFrame)));   /* Saturn 0x06036e40 (emulator-measured, h 63/66/113): screen left = x+1-w/2, bottom = y+3 (gpScreen = screen-12) */

      if (g_Battle.rgPlayer[wPlayerIndex].iColorShift != 0)
      {
         PAL_RLEBlitWithColorShift(PAL_SpriteGetFrame(g_Battle.rgPlayer[wPlayerIndex].lpSprite, g_Battle.rgPlayer[wPlayerIndex].wCurrentFrame),
            lpDstSurface, pos, g_Battle.rgPlayer[wPlayerIndex].iColorShift);
      }
      else if (g_Battle.iHidingTime == 0)
      {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(g_Battle.rgPlayer[wPlayerIndex].lpSprite, g_Battle.rgPlayer[wPlayerIndex].wCurrentFrame),
            lpDstSurface, pos);
      }
      {  // HD-mirror: keyed HD player frame into g_hdScreen at the same position
         extern int PAL_HDM_On(void); extern void PAL_HDBattle_Sprite(const void *fr, int x, int y);
         if (PAL_HDM_On() && g_Battle.iHidingTime == 0)
            PAL_HDBattle_Sprite(PAL_SpriteGetFrame(g_Battle.rgPlayer[wPlayerIndex].lpSprite, g_Battle.rgPlayer[wPlayerIndex].wCurrentFrame), PAL_X(pos), PAL_Y(pos));
      }
   }
}

VOID
PAL_BattleDrawMagicSprites(
   INT               iMagicNum,
   SDL_Surface      *lpDstSurface,
   PAL_POS           pos
)
/*++
  Purpose:

    Generate magic in battle into the specified buffer.

  Parameters:

    [IN]  wObjectID - The index of the magic.

    [OUT] lpDstSurface - pointer to the destination SDL surface.

  Return value:

    None.

--*/
{
   SHORT x, y;
   LPCBITMAPRLE lpBitmap = g_Battle.lpMagicBitmap;

   x = PAL_X(pos);
   y = PAL_Y(pos);

   PAL_RLEBlitToSurface(lpBitmap, lpDstSurface, PAL_XY(x - PAL_RLEGetWidth(lpBitmap) / 2, y - PAL_RLEGetHeight(lpBitmap)));

   {  // HD-mirror: magic FX use a rotating single bitmap (can't key) -> ×6-upscale on the fly into g_hdScreen
      extern int PAL_HDM_On(void); extern void PAL_HDM_BlitRLE(const void *bmp, int bx, int by);
      if (PAL_HDM_On() && lpBitmap)
         PAL_HDM_BlitRLE((const void *)lpBitmap, x - PAL_RLEGetWidth(lpBitmap) / 2, y - PAL_RLEGetHeight(lpBitmap));
   }
}

VOID
PAL_BattleClearSpriteObject(
   VOID
)
/*++
  Purpose:

    Empty the Sprite object drawing sequence.

  Parameters:

    [IN]  wSpriteObjectIndex - Specifies the index of the sprite object to be removed.

  Return value:

    None.

--*/
{
   memset(&g_Battle.SpriteDrawSeq, 0, sizeof(g_Battle.SpriteDrawSeq));

   g_Battle.wMaxSpriteDrawSeqIndex = 0;
}

VOID
PAL_BattleSpriteAddUnlock(
   VOID
)
{
   g_Battle.fSpriteAddLock = FALSE;

   PAL_BattleClearSpriteObject();
}

VOID
PAL_BattleAddSpriteObject(
   WORD               wType,
   WORD               wObjectIndex,
   PAL_POS            pos,
   SHORT              sLayerOffset,
   BOOL               fHaveColorShift
)
/*++
  Purpose:

    Add sprite objects to the drawing sequence.

  Parameters:

    [IN]  bType - What type is the sprite object?

    [IN]  bObjectIndex - The index of this sprite object in its type.

    [IN]  pos - The pos of sprite object.

    [IN]  sLayerOffset - The layer offset of sprite object.

    [IN]  fHaveColorShift - TRUE is the highest layer in battle.

  Return value:

    None.

--*/
{
   WORD *wMaxIndex = &g_Battle.wMaxSpriteDrawSeqIndex;
   BATTLESPRITESEQ *SpriteObject;

   if (*wMaxIndex + 1 < MAX_BATTLESPRITESEQ_ITEMS)
   {
      SpriteObject = &g_Battle.SpriteDrawSeq[*wMaxIndex];

      SpriteObject->wType = wType;
      SpriteObject->wObjectIndex = wObjectIndex;
      SpriteObject->pos = pos;
      SpriteObject->sLayerOffset = sLayerOffset;
      SpriteObject->fHaveColorShift = fHaveColorShift;

      (*wMaxIndex)++;
   }
}

VOID
PAL_BattleRemoveSpriteObject(
   WORD               wSpriteObjectIndex
)
/*++
  Purpose:

    Removes the specified sprite object from the drawn sequence by index.

  Parameters:

    [IN]  wSpriteObjectIndex - Specifies the index of the sprite object to be removed.

  Return value:

    None.

--*/
{
   BATTLESPRITESEQ *SpriteObject;

   if (wSpriteObjectIndex < MAX_BATTLESPRITESEQ_ITEMS)
   {
      SpriteObject = &g_Battle.SpriteDrawSeq[wSpriteObjectIndex];

      memset(SpriteObject, 0, sizeof(*SpriteObject));

      g_Battle.wMaxSpriteDrawSeqIndex--;
   }
}

VOID
PAL_BattleAddFighterSpriteObject(
   VOID
)
/*++
  Purpose:

    Place enemies and players in the drawing sequence.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   INT i;

   //
   // Place enemies in the drawing sequence.
   //
   for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
   {
      PAL_BattleAddSpriteObject(kBattleSpriteTypeEnemy, i, g_Battle.rgEnemy[i].pos, 0, g_Battle.rgEnemy[i].iColorShift);
   }

   if (g_Battle.lpSummonSprite != NULL)
   {
      //
      // Place summon god in the drawing sequence.
      //
      PAL_BattleAddSpriteObject(kBattleSpriteTypePlayer, -1, g_Battle.posSummon, 0, g_Battle.fSummonColorShift);
   }
   else
   {
      //
      // Place players in the drawing sequence.
      //
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         PAL_BattleAddSpriteObject(kBattleSpriteTypePlayer, i, g_Battle.rgPlayer[i].pos, 0, g_Battle.rgPlayer[i].iColorShift);
      }
   }
}

VOID
PAL_BattleSortSpriteObjecByPos(
   VOID
)
/*++
  Purpose:

    Sort all sprite according to pos.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   INT i, j;
   BATTLESPRITESEQ *this, *next, tmp;
   SHORT thisPosX, thisPosY, nextPosX, nextPosY;

   //
   // Sort the players drawing order by Y coordinate
   //
   for (i = 0; i < g_Battle.wMaxSpriteDrawSeqIndex; i++)
   {
      for (j = i + 1; j <= g_Battle.wMaxSpriteDrawSeqIndex - 1; j++)
      {
         this = &g_Battle.SpriteDrawSeq[i];
         next = &g_Battle.SpriteDrawSeq[j];

         thisPosY = PAL_Y(this->pos) + this->sLayerOffset;
         nextPosY = PAL_Y(next->pos) + next->sLayerOffset;

         if (thisPosY > nextPosY)
         {
            //
            // Pos Y compare successfully, directly swap variable values
            //
            tmp = *this;
            *this = *next;
            *next = tmp;
         }
         else if(thisPosY == nextPosY)
         {
            //
            // The pos Y of the two are equal. Compare their X coordinates
            //
            thisPosX = PAL_X(this->pos);
            nextPosX = PAL_X(next->pos);

            if (thisPosX < nextPosX)
            {
               tmp = *this;
               *this = *next;
               *next = tmp;
            }
         }
      }
   }
}

VOID
PAL_BattleDrawAllSprites(
   VOID
)
/*++
  Purpose:

    Draw all sprites to the battle screen buffer.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   //
   // Draw all sprites to the battle screen buffer.
   //
   PAL_BattleDrawAllSpritesWithColorShift(FALSE);

   //
   // Only draw sprites with color shift into the battle screen buffer.
   // Because in the original game,
   // sprites with color shift are directly overlaid on the original sprites.
   //
   PAL_BattleDrawAllSpritesWithColorShift(TRUE);
}

VOID
PAL_BattleDrawAllSpritesWithColorShift(
   BOOL               fColorShift
)
/*++
  Purpose:

    Draws all sprites with color shift to the battle screen buffer..

  Parameters:

    None.

  Return value:

    None.

--*/
{
   INT i;
   BATTLESPRITESEQ *SpriteObject;

   //
   // Sort the sprite that need to be drawn
   //
   PAL_BattleSortSpriteObjecByPos();

   for (i = 0; i <= g_Battle.wMaxSpriteDrawSeqIndex; i++)
   {
      SpriteObject = &g_Battle.SpriteDrawSeq[i];

      if (fColorShift)
      {
         //
         // Draw only sprites with color shift
         //
         if (!SpriteObject->fHaveColorShift) continue;
      }

      switch (SpriteObject->wType)
      {
      case kBattleSpriteTypeNone:
         break;

      case kBattleSpriteTypeEnemy:
         PAL_BattleDrawEnemySprites(SpriteObject->wObjectIndex, g_Battle.lpSceneBuf);
         break;

      case kBattleSpriteTypePlayer:
         PAL_BattleDrawPlayerSprites(SpriteObject->wObjectIndex, g_Battle.lpSceneBuf);
         break;

      case kBattleSpriteTypeMagic:
         PAL_BattleDrawMagicSprites(SpriteObject->wObjectIndex, g_Battle.lpSceneBuf, SpriteObject->pos);
         break;

      }
   }
}

VOID
PAL_BattleMakeScene(
   VOID
)
/*++
  Purpose:

    Generate the battle scene into the scene buffer.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   {  /* Saturn-port debug: env PAL_BTRACE=file logs one line per scene (ms, player0 x y frame, enemies x y frame) */
      static FILE *bt = (FILE *)-1; int k;
      if (bt == (FILE *)-1) { const char *p = SDL_getenv("PAL_BTRACE"); bt = p ? fopen(p, "w") : NULL; }
      if (bt)
      {
         fprintf(bt, "%u%s P %d %d %d", (unsigned)SDL_GetTicks(), g_Battle.fSpriteAddLock ? "" : "u", PAL_X(g_Battle.rgPlayer[0].pos), PAL_Y(g_Battle.rgPlayer[0].pos),
            g_Battle.rgPlayer[0].wCurrentFrame);
         for (k = 1; k <= gpGlobals->wMaxPartyMemberIndex; k++)
            fprintf(bt, " P%d %d %d %d", k, PAL_X(g_Battle.rgPlayer[k].pos), PAL_Y(g_Battle.rgPlayer[k].pos), g_Battle.rgPlayer[k].wCurrentFrame);
         fprintf(bt, " E");
         for (k = 0; k <= g_Battle.wMaxEnemyIndex; k++)
            fprintf(bt, " %d %d %d", PAL_X(g_Battle.rgEnemy[k].pos), PAL_Y(g_Battle.rgEnemy[k].pos), g_Battle.rgEnemy[k].wCurrentFrame);
         fprintf(bt, "\n"); fflush(bt);
      }
   }
   PAL_BattleDrawBackground();

   {  // HD-mirror: start the HD battle frame (clear + HD background) before sprites compose
      extern int PAL_HDM_On(void); extern void PAL_HDBattle_Begin(void); extern void PAL_HDBattle_LoadStep(void);
      if (PAL_HDM_On()) { PAL_HDBattle_Begin(); PAL_HDBattle_LoadStep(); }  /* stream a chunk of HD frames each battle frame */
   }

   if (g_Battle.fSpriteAddLock)
   {
      //
      // Initialize the drawing sequence
      //
      PAL_BattleClearSpriteObject();
   }
   else
   {
      g_Battle.fSpriteAddLock = TRUE;
   }

   //
   // Place enemies and players in the drawing sequence
   //
   PAL_BattleAddFighterSpriteObject();

   //
   // Draw all sprite
   //
   PAL_BattleDrawAllSprites();
}

VOID
PAL_BattleFadeScene(
   VOID
)
/*++
  Purpose:

    Fade in the scene of battle.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int               i, j, k;
   DWORD             time;
   BYTE              a, b;
   const int         rgIndex[6] = {0, 3, 1, 5, 2, 4};
   
   time = SDL_GetTicks();

   for (i = 0; i < 12; i++)
   {
      for (j = 0; j < 6; j++)
      {
         PAL_DelayUntil(time);
         time = SDL_GetTicks() + 16;

         //
         // Blend the pixels in the 2 buffers, and put the result into the
         // backup buffer
         //
         for (k = rgIndex[j]; k < gpScreen->pitch * gpScreen->h; k += 6)
         {
            a = ((LPBYTE)(g_Battle.lpSceneBuf->pixels))[k];
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
         }

         //
         // Draw the backup buffer to the screen
         //
		 VIDEO_RestoreScreen(gpScreen);

         PAL_BattleUIUpdate();
         VIDEO_UpdateScreen(NULL);
      }
   }

   //
   // Draw the result buffer to the screen as the final step
   //
   VIDEO_CopyEntireSurface(g_Battle.lpSceneBuf, gpScreen);
   PAL_BattleUIUpdate();

   VIDEO_UpdateScreen(NULL);
}

static DWORD s_dwSatBattleBlack = 0;   /* Saturn battle entry: time the screen went black */

static BATTLERESULT
PAL_BattleMain(
   VOID
)
/*++
  Purpose:

    The main battle routine.

  Parameters:

    None.

  Return value:

    The result of the battle.

--*/
{
   int         i;
   DWORD       dwTime;
   
   VIDEO_BackupScreen(gpScreen);

   //
   // Generate the scene and draw the scene to the screen buffer
   //
   PAL_BattleMakeScene();
   VIDEO_CopyEntireSurface(g_Battle.lpSceneBuf, gpScreen);

   //
   // Fade out the music and delay for a while
   //
   AUDIO_PlayMusic(0, FALSE, 1);

   //
   // Saturn held the screen black for 171 vsync (~2.85 s) before the battle screen appeared.  That reads as a
   // 2-3 s freeze on entry (user #8), so the remaster shortens it to a brief transition and spends the wait
   // STREAMING the HD battle sprites (so the battle is already crisp when it shows). Tunable: PAL_BATTLEBLACK_MS.
   //
   {
      static int blackms = -1;
      if (blackms < 0) { const char *e = SDL_getenv("PAL_BATTLEBLACK_MS"); blackms = e ? SDL_atoi(e) : 650; }
      while (SDL_GetTicks() < s_dwSatBattleBlack + (DWORD)blackms)
      {
         PAL_ProcessEvent();
         { extern int PAL_HDM_On(void); extern void PAL_HDBattle_LoadStep(void); if (PAL_HDM_On()) PAL_HDBattle_LoadStep(); }
         SDL_Delay(5);
      }
   }
   VIDEO_UpdateScreen(NULL);

   //
   // Play the battle music
   //
   AUDIO_PlayMusic(gpGlobals->wNumBattleMusic, TRUE, 0);

   //
   // Fade in the screen when needed
   //
   if (gpGlobals->fNeedToFadeIn)
   {
      PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
      gpGlobals->fNeedToFadeIn = FALSE;
   }

   //
   // Run the pre-battle scripts for each enemies
   //
   for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
   {
      g_Battle.rgEnemy[i].wScriptOnTurnStart =
         PAL_RunTriggerScript(g_Battle.rgEnemy[i].wScriptOnTurnStart, i);

      if (g_Battle.BattleResult != kBattleResultPreBattle)
      {
         break;
      }
   }

   if (g_Battle.BattleResult == kBattleResultPreBattle)
   {
      g_Battle.BattleResult = kBattleResultOnGoing;
   }

#ifndef PAL_CLASSIC
   PAL_UpdateTimeChargingUnit();
#endif

   dwTime = SDL_GetTicks();

   PAL_ClearKeyState();

   //
   // Run the main battle loop.
   //
   while (TRUE)
   {
      //
      // Break out if the battle ended.
      //
      if (g_Battle.BattleResult != kBattleResultOnGoing)
      {
         break;
      }

      //
      // Wait for the time of one frame. Accept input here.
      //
      PAL_DelayUntil(dwTime);

      //
      // Set the time of the next frame.
      //
      dwTime = SDL_GetTicks() + BATTLE_FRAME_TIME;

      //
      // Run the main frame routine.
      //
      PAL_BattleStartFrame();

      //
      // Update the screen.
      //
      VIDEO_UpdateScreen(NULL);
   }

   //
   // Return the battle result
   //
   return g_Battle.BattleResult;
}

static VOID
PAL_FreeBattleSprites(
   VOID
)
/*++
  Purpose:

    Free all the loaded sprites.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int         i;

   //
   // Free all the loaded sprites
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      if (g_Battle.rgPlayer[i].lpSprite != NULL)
      {
         free(g_Battle.rgPlayer[i].lpSprite);
      }
      g_Battle.rgPlayer[i].lpSprite = NULL;
   }

   for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
   {
      if (g_Battle.rgEnemy[i].lpSprite != NULL)
      {
         free(g_Battle.rgEnemy[i].lpSprite);
      }
      g_Battle.rgEnemy[i].lpSprite = NULL;
   }

   if (g_Battle.lpSummonSprite != NULL)
   {
      free(g_Battle.lpSummonSprite);
   }
   g_Battle.lpSummonSprite = NULL;
}

VOID
PAL_LoadBattleSprites(
   VOID
)
/*++
  Purpose:

    Load all the loaded sprites.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int           i, l, x, y, s;
   FILE         *fp;

   PAL_FreeBattleSprites();

   fp = UTIL_OpenRequiredFile("abc.mkf");

   //
   // Load battle sprites for players
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      s = PAL_GetPlayerBattleSprite(gpGlobals->rgParty[i].wPlayerRole);

      l = PAL_MKFGetDecompressedSize(s, gpGlobals->f.fpF);

      if (l <= 0)
      {
         continue;
      }

      g_Battle.rgPlayer[i].lpSprite = UTIL_calloc(l, 1);

      PAL_MKFDecompressChunk(g_Battle.rgPlayer[i].lpSprite, l,
         s, gpGlobals->f.fpF);

      //
      // Set the default position for this player
      //
      x = g_rgPlayerPos[gpGlobals->wMaxPartyMemberIndex][i][0];
      y = g_rgPlayerPos[gpGlobals->wMaxPartyMemberIndex][i][1];

      g_Battle.rgPlayer[i].posOriginal = PAL_XY(x, y);
      g_Battle.rgPlayer[i].pos = PAL_XY(x, y);
   }

   //
   // Load battle sprites for enemies
   //
   for (i = 0; i < MAX_ENEMIES_IN_TEAM; i++)
   {
      if (g_Battle.rgEnemy[i].wObjectID == 0)
      {
         continue;
      }

      l = PAL_MKFGetDecompressedSize(
         gpGlobals->g.rgObject[g_Battle.rgEnemy[i].wObjectID].enemy.wEnemyID, fp);

      if (l <= 0)
      {
         continue;
      }

      g_Battle.rgEnemy[i].lpSprite = UTIL_calloc(l, 1);

      PAL_MKFDecompressChunk(g_Battle.rgEnemy[i].lpSprite, l,
         gpGlobals->g.rgObject[g_Battle.rgEnemy[i].wObjectID].enemy.wEnemyID, fp);

      //
      // Set the default position for this enemy
      //
      x = gpGlobals->g.EnemyPos.pos[i][g_Battle.wMaxEnemyIndex].x;
      y = gpGlobals->g.EnemyPos.pos[i][g_Battle.wMaxEnemyIndex].y;

      y += g_Battle.rgEnemy[i].e.wYPosOffset;

      g_Battle.rgEnemy[i].posOriginal = PAL_XY(x, y);
      g_Battle.rgEnemy[i].pos = PAL_XY(x, y);
   }

   {  // HD-mirror (PAL_HDFULL): load keyed HD battle assets (bg + party/enemy frames by frame pointer)
      extern int PAL_HDM_On(void);
      extern void PAL_HDBattle_Reset(void);
      extern void PAL_HDBattle_AddFrame(const void *framePtr, const char *path);
      extern int PAL_HDBattle_LoadBg(const char *dir, int field);
      const char *hdir = SDL_getenv("PAL_HDBATTLE");
      if (PAL_HDM_On() && hdir)
      {
         int pi, ei, fr, nf; char path[512];
         PAL_HDBattle_Reset();
         PAL_HDBattle_LoadBg(hdir, gpGlobals->wNumBattleField);
         for (pi = 0; pi <= gpGlobals->wMaxPartyMemberIndex; pi++)
         {
            int chunk = PAL_GetPlayerBattleSprite(gpGlobals->rgParty[pi].wPlayerRole);
            LPCSPRITE spr = g_Battle.rgPlayer[pi].lpSprite;
            if (spr == NULL) continue;
            nf = PAL_SpriteGetNumFrames(spr);
            for (fr = 0; fr < nf; fr++)
            { SDL_snprintf(path, sizeof path, "%s/party_c%d_fr%03d.rgba", hdir, chunk, fr);
              PAL_HDBattle_AddFrame(PAL_SpriteGetFrame(spr, fr), path); }
         }
         for (ei = 0; ei < MAX_ENEMIES_IN_TEAM; ei++)
         {
            LPCSPRITE spr; int chunk;
            if (g_Battle.rgEnemy[ei].wObjectID == 0) continue;
            spr = g_Battle.rgEnemy[ei].lpSprite;
            if (spr == NULL) continue;
            chunk = gpGlobals->g.rgObject[g_Battle.rgEnemy[ei].wObjectID].enemy.wEnemyID;
            nf = PAL_SpriteGetNumFrames(spr);
            for (fr = 0; fr < nf; fr++)
            { SDL_snprintf(path, sizeof path, "%s/enemy_c%d_fr%03d.rgba", hdir, chunk, fr);
              PAL_HDBattle_AddFrame(PAL_SpriteGetFrame(spr, fr), path); }
         }
         { extern void PAL_HDBattle_BeginStream(void); PAL_HDBattle_BeginStream(); }  /* stream the queued frames over the first battle frames instead of freezing ~3s here */
      }
   }

   fclose(fp);
}

static VOID
PAL_LoadBattleBackground(
   VOID
)
/*++
  Purpose:

    Load the screen background picture of the battle.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   PAL_LARGE BYTE           buf[320 * 200];

   //
   // Create the surface
   //
   g_Battle.lpBackground = VIDEO_CreateCompatibleSurface(gpScreen);
   { extern SDL_Surface *g_pSatCCBack; g_pSatCCBack = g_Battle.lpBackground; }   /* video.c: sprite colour calculation */

   if (g_Battle.lpBackground == NULL)
   {
      TerminateOnError("PAL_LoadBattleBackground(): failed to create surface!");
   }

   //
   // Load the picture
   //
   PAL_MKFDecompressChunk(buf, 320 * 200, gpGlobals->wNumBattleField, gpGlobals->f.fpFBP);

   //
   // Draw the picture to the surface.
   //
   PAL_FBPBlitToSurface(buf, g_Battle.lpBackground);

   //
   // Saturn: the battle plane shows one line past the 200-line FBP (screen line 212). That VRAM line holds fixed
   // residue = UI bitmap line 200 (text.c g_rgSatUiRow200; emu VRAM 0x25E59000 copied at battle start).
   //
   if (g_Battle.lpBackground->h > 200)
   {
      LPBYTE p = (LPBYTE)g_Battle.lpBackground->pixels + 200 * g_Battle.lpBackground->pitch;
      unsigned char *pb = SAT_BankPlane(g_Battle.lpBackground, FALSE);
      int x;
      extern BYTE g_rgSatUiRow200[320];
      for (x = 0; x < 320; x++)
      {
         p[x] = g_rgSatUiRow200[x];
         if (pb != NULL) pb[200 * g_Battle.lpBackground->w + x] = 0;
      }
   }
}

static VOID
PAL_BattleWon(
   VOID
)
/*++
  Purpose:

    Show the "you win" message and add the experience points for players.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   const SDL_Rect   rect = {0, 60, 320, 100};
   SDL_Rect   rect1 = {80, 0, 180, 200};

   int              i, j, iTotalCount;
   DWORD            dwExp;
   WORD             w;
   BOOL             fLevelUp;
   PLAYERROLES      OrigPlayerRoles;

   //
   // Backup the initial player stats
   //
   OrigPlayerRoles = gpGlobals->g.PlayerRoles;

   VIDEO_BackupScreen(gpScreen);

   if (g_Battle.iExpGained > 0)
   {
      int w1 = PAL_WordWidth(BATTLEWIN_GETEXP_LABEL) + 3;
	  int ww1 = (w1 - 8) << 3;
      //
      // Play the "battle win" music
      //
      AUDIO_PlayMusic(g_Battle.fIsBoss ? 2 : 3, FALSE, 0);

      //
      // Show the message about the total number of exp. and cash gained
      //
      //
      // Saturn (PROG 0x060451e0..): the battle bitmap is cleared to black, then the money box
      // (0x060507bc) and the exp box (0x06040abc) are drawn on it, then wait 200.
      //   money: box1 (60,100) len 10, word 9 at (70,110), 6-digit zero-padded number (148,114), word 10 at (194,110)
      //   exp:   box1 (68,60) len 9, word 30 at (78,70), number right digit at (208,75)
      //
      (void)ww1;
      /* emulator: drawn over the battle scene (not black) */
      PAL_CreateSingleLineBox(PAL_XY(60, 100), 10, FALSE);
      PAL_DrawText(PAL_GetWord(BATTLEWIN_BEATENEMY_LABEL), PAL_XY(70, 110), 0, FALSE, FALSE, FALSE);
      {
         int k, v = g_Battle.iCashGained;
         for (k = 5; k >= 0 && (k == 5 || v > 0); k--, v /= 10)      // 0x0604be5c positions; emulator: no leading zeros
            PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, 19 + v % 10), gpScreen, PAL_XY(148 + 6 * k, 114));
      }
      PAL_DrawText(PAL_GetWord(BATTLEWIN_DOLLAR_LABEL), PAL_XY(194, 110), 0, FALSE, FALSE, FALSE);

      PAL_CreateSingleLineBox(PAL_XY(68, 60), 9, FALSE);
      PAL_DrawText(PAL_GetWord(BATTLEWIN_GETEXP_LABEL), PAL_XY(78, 70), 0, FALSE, FALSE, FALSE);
      PAL_DrawNumber(g_Battle.iExpGained, 5, PAL_XY(208 - 24, 75), kNumColorYellow, kNumAlignRight);

      VIDEO_UpdateScreen(NULL);
      PAL_WaitForAnyKey(3333);   // 200 ticks @60Hz
   }

   //
   // Add the cash value
   //
   gpGlobals->dwCash += g_Battle.iCashGained;

    
    const MENUITEM      rgFakeMenuItem[] =
    {
        // value  label                        enabled   pos
        { 1,      gpGlobals->g.PlayerRoles.rgwName[0],    TRUE,     PAL_XY(0, 0) },
        { 2,      gpGlobals->g.PlayerRoles.rgwName[1],    TRUE,     PAL_XY(0, 0) },
        { 3,      gpGlobals->g.PlayerRoles.rgwName[2],    TRUE,     PAL_XY(0, 0) },
        { 4,      gpGlobals->g.PlayerRoles.rgwName[3],    TRUE,     PAL_XY(0, 0) },
        { 5,      gpGlobals->g.PlayerRoles.rgwName[4],    TRUE,     PAL_XY(0, 0) },
        { 6,      gpGlobals->g.PlayerRoles.rgwName[5],    TRUE,     PAL_XY(0, 0) },
    };
    int maxNameWidth = PAL_MenuTextMaxWidth(rgFakeMenuItem, sizeof(rgFakeMenuItem) / sizeof(MENUITEM));
    const MENUITEM      rgFakeMenuItem2[] =
    {
        // value  label                        enabled   pos
        { 1,      STATUS_LABEL_LEVEL,          TRUE,     PAL_XY(0, 0) },
        { 2,      STATUS_LABEL_HP,             TRUE,     PAL_XY(0, 0) },
        { 3,      STATUS_LABEL_MP,             TRUE,     PAL_XY(0, 0) },
        { 4,      STATUS_LABEL_ATTACKPOWER,    TRUE,     PAL_XY(0, 0) },
        { 5,      STATUS_LABEL_MAGICPOWER,     TRUE,     PAL_XY(0, 0) },
        { 6,      STATUS_LABEL_RESISTANCE,     TRUE,     PAL_XY(0, 0) },
        { 7,      STATUS_LABEL_DEXTERITY,      TRUE,     PAL_XY(0, 0) },
        { 8,      STATUS_LABEL_FLEERATE,       TRUE,     PAL_XY(0, 0) },
    };
    int maxPropertyWidth = PAL_MenuTextMaxWidth(rgFakeMenuItem2, sizeof(rgFakeMenuItem2) / sizeof(MENUITEM)) - 1;
    int propertyLength = maxPropertyWidth - 1;
    int offsetX = -8*propertyLength;
    rect1.x += offsetX;
    rect1.w -= 2*offsetX;
   //
   // Add the experience points for each players
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      fLevelUp = FALSE;

      w = gpGlobals->rgParty[i].wPlayerRole;
      if (gpGlobals->g.PlayerRoles.rgwHP[w] == 0)
      {
         continue; // don't care about dead players
      }

      dwExp = gpGlobals->Exp.rgPrimaryExp[w].wExp;
      dwExp += g_Battle.iExpGained;

      if (gpGlobals->g.PlayerRoles.rgwLevel[w] > MAX_LEVELS)
      {
         gpGlobals->g.PlayerRoles.rgwLevel[w] = MAX_LEVELS;
      }

      while (dwExp >= gpGlobals->g.rgLevelUpExp[gpGlobals->g.PlayerRoles.rgwLevel[w]])
      {
         dwExp -= gpGlobals->g.rgLevelUpExp[gpGlobals->g.PlayerRoles.rgwLevel[w]];

         if (gpGlobals->g.PlayerRoles.rgwLevel[w] < MAX_LEVELS)
         {
            fLevelUp = TRUE;
            PAL_PlayerLevelUp(w, 1);

            gpGlobals->g.PlayerRoles.rgwHP[w] = gpGlobals->g.PlayerRoles.rgwMaxHP[w];
            gpGlobals->g.PlayerRoles.rgwMP[w] = gpGlobals->g.PlayerRoles.rgwMaxMP[w];
         }
            if (gpGlobals->g.PlayerRoles.rgwLevel[w] != OrigPlayerRoles.rgwLevel[w])   /* Saturn: one level-up window per level gained */
            {
               VIDEO_RestoreScreen(gpScreen);   /* emulator: level-up window over the battle scene */
               //
               // Player has gained a level. Show the message
               //
               /* Saturn 0x06041084..0x06041188 + column routine 0x06041640 */
               if (PAL_LangGet() == 3) PAL_CreateSingleLineBox(PAL_XY(62, 0), 12, FALSE);   /* English: longer name and words */
               else
               PAL_CreateSingleLineBox(PAL_XY(78, 0), propertyLength+10, FALSE);
               PAL_CreateBox(PAL_XY(72, 34), 7, 9, 1, FALSE); /* BOX? 0x0604905c(x,y,style*9,cols+1,rows+1); rows step 20/18 */

               if (PAL_LangGet() == 3)
               {
                  int fx = PAL_DrawTextFlow(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), 74, 10, 0);
                  fx = PAL_DrawTextFlow(PAL_GetWord(STATUS_LABEL_LEVEL), fx, 10, 0);
                  PAL_DrawTextFlow(PAL_GetWord(BATTLEWIN_LEVELUP_LABEL), fx, 10, 0);
               }
               else
               {
               PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), PAL_XY(92, 10), 0, FALSE, FALSE, FALSE);
               PAL_DrawText(PAL_GetWord(STATUS_LABEL_LEVEL), PAL_XY(146, 10), 0, FALSE, FALSE, FALSE);
               PAL_DrawText(PAL_GetWord(BATTLEWIN_LEVELUP_LABEL), PAL_XY(190, 10), 0, FALSE, FALSE, FALSE);
               }

               for (j = 0; j < 8; j++)
               {
                  PAL_DrawTextMaxW(PAL_GetWord(STATUS_LABEL_LEVEL + j), PAL_XY(88, 49 + 18 * j), BATTLEWIN_LEVELUP_LABEL_COLOR, TRUE, 40);   /* the numbers start 40 pixels to the right */
                  PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ARROW), gpScreen, PAL_XY(185, 55 + 18 * j));
               }
               {
                  /* NUM: rightmost digit at x (Saturn) == PAL_DrawNumber(.., 4, x-18, right) */
                  static const int colx[2] = { 88, 169 };
                  for (j = 0; j < 2; j++)
                  {
                     const PLAYERROLES *r = j ? &gpGlobals->g.PlayerRoles : &OrigPlayerRoles;
                     int x = colx[j] + 58 - 18, k;
                     WORD v[5];
                     PAL_DrawNumber(r->rgwLevel[w], 4, PAL_XY(x, 51), kNumColorYellow, kNumAlignRight);
                     /* user request: current/maximum on ONE line (the Saturn layout put the maximum 6 px lower,
                        which reads as a wrapped line): current ends 6 px further left, the maximum follows the slash */
                     PAL_DrawNumber(r->rgwHP[w], 4, PAL_XY(x - 6, 69), kNumColorYellow, kNumAlignRight);
                     PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen, PAL_XY(colx[j] + 59, 69));
                     PAL_DrawNumber(r->rgwMaxHP[w], 4, PAL_XY(colx[j] + 65, 69), kNumColorBlue, kNumAlignLeft);
                     PAL_DrawNumber(r->rgwMP[w], 4, PAL_XY(x - 6, 87), kNumColorYellow, kNumAlignRight);
                     PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen, PAL_XY(colx[j] + 59, 87));
                     PAL_DrawNumber(r->rgwMaxMP[w], 4, PAL_XY(colx[j] + 65, 87), kNumColorBlue, kNumAlignLeft);
                     v[0] = r->rgwAttackStrength[w] + PAL_GetPlayerAttackStrength(w) - gpGlobals->g.PlayerRoles.rgwAttackStrength[w];
                     v[1] = r->rgwMagicStrength[w] + PAL_GetPlayerMagicStrength(w) - gpGlobals->g.PlayerRoles.rgwMagicStrength[w];
                     v[2] = r->rgwDefense[w] + PAL_GetPlayerDefense(w) - gpGlobals->g.PlayerRoles.rgwDefense[w];
                     v[3] = r->rgwDexterity[w] + PAL_GetPlayerDexterity(w) - gpGlobals->g.PlayerRoles.rgwDexterity[w];
                     v[4] = r->rgwFleeRate[w] + PAL_GetPlayerFleeRate(w) - gpGlobals->g.PlayerRoles.rgwFleeRate[w];
                     for (k = 0; k < 5; k++)
                        PAL_DrawNumber(v[k], 4, PAL_XY(x, 105 + 18 * k), kNumColorYellow, kNumAlignRight);
                  }
               }

               //
               // Update the screen and wait for key
               //
               VIDEO_UpdateScreen(NULL);
               PAL_WaitForAnyKey(6667); /* Saturn 0x060220d0(400) */
               VIDEO_RestoreScreen(gpScreen);   /* emulator: window removed before the ability-up boxes */
               VIDEO_UpdateScreen(NULL);

               OrigPlayerRoles = gpGlobals->g.PlayerRoles;
            }
      }

      gpGlobals->Exp.rgPrimaryExp[w].wExp = (WORD)dwExp;


      //
      // Increasing of other hidden levels
      //
      iTotalCount = 0;

      iTotalCount += gpGlobals->Exp.rgAttackExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgDefenseExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgDexterityExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgFleeExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgHealthExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgMagicExp[w].wCount;
      iTotalCount += gpGlobals->Exp.rgMagicPowerExp[w].wCount;

      if (iTotalCount > 0)
      {
#define CHECK_HIDDEN_EXP(expname, statname, label)          \
{                                                           \
   dwExp = g_Battle.iExpGained;                             \
   dwExp *= gpGlobals->Exp.expname[w].wCount;               \
   dwExp /= iTotalCount;                                    \
   dwExp *= 2;                                              \
                                                            \
   dwExp += gpGlobals->Exp.expname[w].wExp;                 \
                                                            \
   if (gpGlobals->Exp.expname[w].wLevel > MAX_LEVELS)       \
   {                                                        \
      gpGlobals->Exp.expname[w].wLevel = MAX_LEVELS;        \
   }                                                        \
                                                            \
   while (dwExp >= gpGlobals->g.rgLevelUpExp[gpGlobals->Exp.expname[w].wLevel]) \
   {                                                        \
      dwExp -= gpGlobals->g.rgLevelUpExp[gpGlobals->Exp.expname[w].wLevel]; \
      gpGlobals->g.PlayerRoles.statname[w] += RandomLong(1, 2); \
      if (gpGlobals->Exp.expname[w].wLevel < MAX_LEVELS)    \
      {                                                     \
         gpGlobals->Exp.expname[w].wLevel++;                \
      }                                                     \
   }                                                        \
                                                            \
   gpGlobals->Exp.expname[w].wExp = (WORD)dwExp;            \
                                                            \
   if (gpGlobals->g.PlayerRoles.statname[w] != OrigPlayerRoles.statname[w]) \
   {                                                        \
      /* Saturn 0x06041af0..0x06041bbe */ \
      if (PAL_LangGet() >= 2) \
      { \
         /* English: the pieces follow one another; a wider box */ \
         int fx; \
         PAL_CreateSingleLineBox(PAL_XY(20, 60), 16, FALSE); \
         fx = PAL_DrawTextFlow(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), 30, 70, 0); \
         fx = PAL_DrawTextFlow(PAL_GetWord(label), fx, 70, 0); \
         fx = PAL_DrawTextFlow(PAL_GetWord(BATTLEWIN_LEVELUP_LABEL), fx, 70, 0); \
         PAL_DrawNumber(gpGlobals->g.PlayerRoles.statname[w] - OrigPlayerRoles.statname[w], 5, PAL_XY(fx, 74), kNumColorYellow, kNumAlignLeft); \
      } \
      else \
      { \
      PAL_CreateSingleLineBox(PAL_XY(52, 60), 11, FALSE); \
      PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), PAL_XY(62, 70), 0, FALSE, FALSE, FALSE); \
      PAL_DrawText(PAL_GetWord(label), PAL_XY(128, 70), 0, FALSE, FALSE, FALSE); \
      PAL_DrawText(PAL_GetWord(BATTLEWIN_LEVELUP_LABEL), PAL_XY(168, 70), 0, FALSE, FALSE, FALSE); \
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.statname[w] - OrigPlayerRoles.statname[w], 5, PAL_XY(218 - 24, 74), kNumColorYellow, kNumAlignRight); \
      } \
      VIDEO_UpdateScreen(NULL); \
      PAL_WaitForAnyKey(3333); \
   }                                                        \
}

         CHECK_HIDDEN_EXP(rgHealthExp, rgwMaxHP, STATUS_LABEL_HP);
         CHECK_HIDDEN_EXP(rgMagicExp, rgwMaxMP, STATUS_LABEL_MP);
         CHECK_HIDDEN_EXP(rgAttackExp, rgwAttackStrength, STATUS_LABEL_ATTACKPOWER);
         CHECK_HIDDEN_EXP(rgMagicPowerExp, rgwMagicStrength, STATUS_LABEL_MAGICPOWER);
         CHECK_HIDDEN_EXP(rgDefenseExp, rgwDefense, STATUS_LABEL_RESISTANCE);
         CHECK_HIDDEN_EXP(rgDexterityExp, rgwDexterity, STATUS_LABEL_DEXTERITY);
         CHECK_HIDDEN_EXP(rgFleeExp, rgwFleeRate, STATUS_LABEL_FLEERATE);

#undef CHECK_HIDDEN_EXP

         //
         // Avoid HP/MP out of sync with upgraded maxHP/MP
         //
         if(fLevelUp){
            gpGlobals->g.PlayerRoles.rgwHP[w] = gpGlobals->g.PlayerRoles.rgwMaxHP[w];
            gpGlobals->g.PlayerRoles.rgwMP[w] = gpGlobals->g.PlayerRoles.rgwMaxMP[w];
         }
      }

   }

   //
   // Run the post-battle scripts
   //
   for (i = 0; i <= g_Battle.wMaxEnemyIndex; i++)
   {
      PAL_RunTriggerScript(g_Battle.rgEnemy[i].wScriptOnBattleEnd, i);
   }

   //
   // Recover automatically after each battle
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      w = gpGlobals->rgParty[i].wPlayerRole;

      //
      // Learn all magics at the current level.
      // Saturn 0x06045282..0x060453aa: after the exp / level-up pass (0x06040abc) and the enemies' post-battle
      // scripts, one more pass over the party (no HP check) learns the magics (0x06042090), then recovers.
      // emu o_r66c: 李 / 趙 / 林 level-up windows first, 「趙靈兒 練成 五氣朝元」 last.
      //
      j = 0;

      while (j < gpGlobals->g.nLevelUpMagic)
      {
         if (gpGlobals->g.lprgLevelUpMagic[j].m[w].wMagic == 0 ||
            gpGlobals->g.lprgLevelUpMagic[j].m[w].wLevel > gpGlobals->g.PlayerRoles.rgwLevel[w])
         {
            j++;
            continue;
         }

         if (PAL_AddMagic(w, gpGlobals->g.lprgLevelUpMagic[j].m[w].wMagic))
         {
            /* Saturn 0x060421d6..0x0604227a */
            if (PAL_LangGet() == 3)
            {
               /* English: "Zhao Ling'er Learned Guanyin Chant" -- the pieces follow one another; a wider box */
               int fx;
               PAL_CreateSingleLineBox(PAL_XY(4, 100), 18, FALSE);
               fx = PAL_DrawTextFlow(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), 14, 110, 0);
               fx = PAL_DrawTextFlow(PAL_GetWord(BATTLEWIN_ADDMAGIC_LABEL), fx, 110, 0);
               PAL_DrawTextFlow(PAL_GetWord(gpGlobals->g.lprgLevelUpMagic[j].m[w].wMagic), fx, 110, 0x19);
            }
            else
            {
            PAL_CreateSingleLineBox(PAL_XY(40, 100), 14, FALSE);
            PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]), PAL_XY(50, 110), 0, FALSE, FALSE, FALSE);
            PAL_DrawText(PAL_GetWord(BATTLEWIN_ADDMAGIC_LABEL), PAL_XY(106, 110), 0, FALSE, FALSE, FALSE);
            PAL_DrawText(PAL_GetWord(gpGlobals->g.lprgLevelUpMagic[j].m[w].wMagic), PAL_XY(146, 110), 0x19, FALSE, FALSE, FALSE);
            }
            VIDEO_UpdateScreen(NULL);
            PAL_WaitForAnyKey(3333);
         }

         j++;
      }

#if 1//def PAL_CLASSIC
      gpGlobals->g.PlayerRoles.rgwHP[w] +=
         (gpGlobals->g.PlayerRoles.rgwMaxHP[w] - gpGlobals->g.PlayerRoles.rgwHP[w]) / 2;
      gpGlobals->g.PlayerRoles.rgwMP[w] +=
         (gpGlobals->g.PlayerRoles.rgwMaxMP[w] - gpGlobals->g.PlayerRoles.rgwMP[w]) / 2;
#else
      if (gpGlobals->g.PlayerRoles.rgwHP[w] == 0)
      {
         gpGlobals->g.PlayerRoles.rgwHP[w] = 1;
      }
      else if (g_Battle.iExpGained > 0)
      {
         FLOAT f =
            (gpGlobals->g.rgLevelUpExp[gpGlobals->g.PlayerRoles.rgwLevel[w]] / 5.0f) / g_Battle.iExpGained;

         if (f < 2)
         {
            f = 2;
         }

         gpGlobals->g.PlayerRoles.rgwHP[w] +=
            (gpGlobals->g.PlayerRoles.rgwMaxHP[w] - gpGlobals->g.PlayerRoles.rgwHP[w]) / f;
         gpGlobals->g.PlayerRoles.rgwMP[w] +=
            (gpGlobals->g.PlayerRoles.rgwMaxMP[w] - gpGlobals->g.PlayerRoles.rgwMP[w]) / f / 1.2;
      }
#endif
   }
}

VOID
PAL_BattleEnemyEscape(
   VOID
)
/*++
  Purpose:

    Enemy flee the battle.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int j, x, y, w;
   BOOL f = TRUE;

   AUDIO_PlaySound(45);

   //
   // Show the animation
   //
   while (f)
   {
   	  f = FALSE;

   	  for (j = 0; j <= g_Battle.wMaxEnemyIndex; j++)
   	  {
   	  	 if (g_Battle.rgEnemy[j].wObjectID == 0)
   	  	 {
   	  	 	continue;
   	  	 }

   	  	 x = PAL_X(g_Battle.rgEnemy[j].pos) - 5;
   	  	 y = PAL_Y(g_Battle.rgEnemy[j].pos);

   	  	 g_Battle.rgEnemy[j].pos = PAL_XY(x, y);

   	  	 w = PAL_RLEGetWidth(PAL_SpriteGetFrame(g_Battle.rgEnemy[j].lpSprite, 0));

   	  	 if (x + w > 0)
   	  	 {
   	  	 	f = TRUE;
   	  	 }
   	  }

   	  PAL_BattleMakeScene();
      VIDEO_CopyEntireSurface(g_Battle.lpSceneBuf, gpScreen);
      VIDEO_UpdateScreen(NULL);

      UTIL_Delay(10);
   }

   UTIL_Delay(500);
   g_Battle.BattleResult = kBattleResultTerminated;
}

VOID
PAL_BattlePlayerEscape(
   VOID
)
/*++
  Purpose:

    Player flee the battle.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int         i, j;
   WORD        wPlayerRole;

   AUDIO_PlaySound(45);

   PAL_BattleUpdateFighters();

   //
   // Saturn (emu o_fx3, st_bf2 + flee rate w21 = 999): every living member stands (frame 0), 3 ticks later steps
   // (+6,+4) from home, 10 steps held 4 ticks each, then 15 more ticks before the battle ends (black screen).
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      wPlayerRole = gpGlobals->rgParty[i].wPlayerRole;

      if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] > 0)
      {
         g_Battle.rgPlayer[i].wCurrentFrame = 0;
         g_Battle.rgPlayer[i].pos = g_Battle.rgPlayer[i].posOriginal;
      }
   }

   PAL_BattleDelay(3, 0, FALSE);

   for (i = 0; i < 10; i++)
   {
      for (j = 0; j <= gpGlobals->wMaxPartyMemberIndex; j++)
      {
         wPlayerRole = gpGlobals->rgParty[j].wPlayerRole;

         if (gpGlobals->g.PlayerRoles.rgwHP[wPlayerRole] > 0)
         {
            g_Battle.rgPlayer[j].pos = (i == 9) ? PAL_XY(9999, 9999) :   // Saturn: not drawn at the 10th step
               PAL_XY(PAL_X(g_Battle.rgPlayer[j].pos) + 6 + 2 * j, PAL_Y(g_Battle.rgPlayer[j].pos) + 4 - j);   // emu o_fl2b / o_fl3c: member j steps (6+2j, 4-j)
         }
      }

      PAL_BattleDelay(4, 0, FALSE);
   }

   PAL_BattleDelay(15, 0, FALSE);

   //
   // Remove all players from the screen
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      g_Battle.rgPlayer[i].pos = PAL_XY(9999, 9999);
   }

   g_Battle.BattleResult = kBattleResultFleed;
}

//
// Saturn: a script battle followed by op 0x78 (scene reload, 0x0602cb8c) keeps the screen black after the battle;
// the next 0x05 fades the scene in stepwise (emu o_r22c, 0x1617 林月如: darkness 24/16/8 ~372 vsync after the
// battle's black screen, then the 0x3C box).  Set by script op 0x07, used by op 0x78.
//
BOOL   g_fSatBattleStayBlack = FALSE;
BOOL   g_fSatBattleWinClean = FALSE;   /* op 0x07: the win path hides the monster then fades (script.c) */
Uint64 g_qwSatBattleBlack = 0;
WORD g_wBattleEventObjectID = 0;   /* event object (field monster) whose script started the current battle; 0 = none */

BATTLERESULT
PAL_StartBattle(
   WORD        wEnemyTeam,
   BOOL        fIsBoss
)
/*++
  Purpose:

    Start a battle.

  Parameters:

    [IN]  wEnemyTeam - the number of the enemy team.

    [IN]  fIsBoss - TRUE for boss fight (not allowed to flee).

  Return value:

    The result of the battle.

--*/
{
   int            i, j;
   WORD           w, wPrevWaveLevel;
   SHORT          sPrevWaveProgression;

   //
   // Saturn (emu o_btin): the field cuts to black on the next frame of the battle call; the battle screen
   // appears at once 171 vsync (~2.85 s) later (see PAL_BattleMain).
   //
   SDL_FillRect(gpScreen, NULL, 0); SAT_BankFill(gpScreen, NULL, 0);
   VIDEO_UpdateScreen(NULL);
   s_dwSatBattleBlack = SDL_GetTicks();

   //
   // Set the screen waving effects
   //
   wPrevWaveLevel = gpGlobals->wScreenWave;
   sPrevWaveProgression = gpGlobals->sWaveProgression;

   gpGlobals->sWaveProgression = 0;
   gpGlobals->wScreenWave = gpGlobals->g.lprgBattleField[gpGlobals->wNumBattleField].wScreenWave;

   //
   // Make sure everyone in the party is alive, also clear all hidden
   // EXP count records
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      w = gpGlobals->rgParty[i].wPlayerRole;

      if (gpGlobals->g.PlayerRoles.rgwHP[w] == 0)
      {
         gpGlobals->g.PlayerRoles.rgwHP[w] = 1;
         gpGlobals->rgPlayerStatus[w][kStatusPuppet] = 0;
      }

      gpGlobals->Exp.rgHealthExp[w].wCount = 0;
      gpGlobals->Exp.rgMagicExp[w].wCount = 0;
      gpGlobals->Exp.rgAttackExp[w].wCount = 0;
      gpGlobals->Exp.rgMagicPowerExp[w].wCount = 0;
      gpGlobals->Exp.rgDefenseExp[w].wCount = 0;
      gpGlobals->Exp.rgDexterityExp[w].wCount = 0;
      gpGlobals->Exp.rgFleeExp[w].wCount = 0;
   }

   //
   // Clear all item-using records
   //
   for (i = 0; i < MAX_INVENTORY; i++)
   {
      gpGlobals->rgInventory[i].nAmountInUse = 0;
   }

   //
   // Store all enemies
   //
   for (i = 0, j = 0; j < MAX_ENEMIES_IN_TEAM; j++)
   {
      memset(&(g_Battle.rgEnemy[j]), 0, sizeof(BATTLEENEMY));
      w = gpGlobals->g.lprgEnemyTeam[wEnemyTeam].rgwEnemy[j];

      if (w == 0xFFFF)
      {
         continue;
      }

      if (w != 0)
      {
         g_Battle.rgEnemy[i].e = gpGlobals->g.lprgEnemy[gpGlobals->g.rgObject[w].enemy.wEnemyID];
         g_Battle.rgEnemy[i].state = kFighterWait;
         g_Battle.rgEnemy[i].wScriptOnTurnStart = gpGlobals->g.rgObject[w].enemy.wScriptOnTurnStart;
         g_Battle.rgEnemy[i].wScriptOnBattleEnd = gpGlobals->g.rgObject[w].enemy.wScriptOnBattleEnd;
         g_Battle.rgEnemy[i].wScriptOnReady = gpGlobals->g.rgObject[w].enemy.wScriptOnReady;
         g_Battle.rgEnemy[i].iColorShift = 0;
         /* Port fix (user request, the "steal, flee, fight again" money exploit): a field monster that has been
            stolen from keeps that across battles -- bit i of its event object's (otherwise unused)
            wSpritePtrOffset, which is saved with the game.  Cleared when the monster is beaten. */
         if (g_wBattleEventObjectID != 0 && g_wBattleEventObjectID <= gpGlobals->g.nEventObject &&
            (gpGlobals->g.lprgEventObject[g_wBattleEventObjectID - 1].wSpritePtrOffset & (1 << i)))
         {
            g_Battle.rgEnemy[i].e.nStealItem = 0;
         }
         if (SDL_getenv("PAL_STEALLOG"))   /* debug: what can be stolen at battle start */
         {
            FILE *fs = fopen(SDL_getenv("PAL_STEALLOG"), "a");
            if (fs) { fprintf(fs, "start ev=%d slot=%d item=%d n=%d%c", g_wBattleEventObjectID, i, g_Battle.rgEnemy[i].e.wStealItem, g_Battle.rgEnemy[i].e.nStealItem, 10); fclose(fs); }
         }
         if (SDL_getenv("PAL_EHP"))   /* debug: env PAL_EHP=n sets every enemy's HP */
         {
            g_Battle.rgEnemy[i].e.wHealth = (WORD)SDL_atoi(SDL_getenv("PAL_EHP"));
         }

#ifndef PAL_CLASSIC
         g_Battle.rgEnemy[i].flTimeMeter = 50;

         //
         // HACK: Otherwise the black thief lady will be too hard to beat
         //
         if (g_Battle.rgEnemy[i].e.wDexterity == 164)
         {
            g_Battle.rgEnemy[i].e.wDexterity /= ((gpGlobals->wMaxPartyMemberIndex == 0) ? 6 : 3);
         }

         //
         // HACK: Heal up automatically for final boss
         //
         if (g_Battle.rgEnemy[i].e.wHealth == 32760)
         {
            for (w = 0; w < MAX_PLAYER_ROLES; w++)
            {
               gpGlobals->g.PlayerRoles.rgwHP[w] = gpGlobals->g.PlayerRoles.rgwMaxHP[w];
               gpGlobals->g.PlayerRoles.rgwMP[w] = gpGlobals->g.PlayerRoles.rgwMaxMP[w];
            }
         }

         //
         // Yet another HACKs
         //
         if ((SHORT)g_Battle.rgEnemy[i].e.wDexterity == -32)
         {
            g_Battle.rgEnemy[i].e.wDexterity = 0; // for Grandma Knife
         }
         else if (g_Battle.rgEnemy[i].e.wDexterity == 20)
         {
            //
            // for Fox Demon
            //
            if (gpGlobals->g.PlayerRoles.rgwLevel[0] < 15)
            {
               g_Battle.rgEnemy[i].e.wDexterity = 8;
            }
            else if (gpGlobals->g.PlayerRoles.rgwLevel[4] > 28 ||
               gpGlobals->Exp.rgPrimaryExp[4].wExp > 0)
            {
               g_Battle.rgEnemy[i].e.wDexterity = 60;
            }
         }
         else if (g_Battle.rgEnemy[i].e.wExp == 250 &&
            g_Battle.rgEnemy[i].e.wCash == 1100)
         {
            g_Battle.rgEnemy[i].e.wDexterity += 12; // for Snake Demon
         }
         else if ((SHORT)g_Battle.rgEnemy[i].e.wDexterity == -60)
         {
            g_Battle.rgEnemy[i].e.wDexterity = 15; // for Spider
         }
         else if ((SHORT)g_Battle.rgEnemy[i].e.wDexterity == -30)
         {
            g_Battle.rgEnemy[i].e.wDexterity = (WORD)-10; // for Stone Head
         }
         else if ((SHORT)g_Battle.rgEnemy[i].e.wDexterity == -16)
         {
            g_Battle.rgEnemy[i].e.wDexterity = 0; // for Zombie
         }
         else if ((SHORT)g_Battle.rgEnemy[i].e.wDexterity == -20)
         {
            g_Battle.rgEnemy[i].e.wDexterity = -8; // for Flower Demon
         }
         else if (g_Battle.rgEnemy[i].e.wLevel < 20 &&
            gpGlobals->wNumScene >= 0xD8 && gpGlobals->wNumScene <= 0xE2)
         {
            //
            // for low-level monsters in the Cave of Trial
            //
            g_Battle.rgEnemy[i].e.wLevel += 15;
            g_Battle.rgEnemy[i].e.wDexterity += 25;
         }
         else if (gpGlobals->wNumScene == 0x90)
         {
            g_Battle.rgEnemy[i].e.wDexterity += 25; // for Tower Dragons
         }
         else if (g_Battle.rgEnemy[i].e.wLevel == 2 &&
            g_Battle.rgEnemy[i].e.wCash == 48)
         {
            g_Battle.rgEnemy[i].e.wDexterity += 8; // for Miao Fists
         }
         else if (g_Battle.rgEnemy[i].e.wLevel == 4 &&
            g_Battle.rgEnemy[i].e.wCash == 240)
         {
            g_Battle.rgEnemy[i].e.wDexterity += 18; // for Fat Miao
         }
         else if (g_Battle.rgEnemy[i].e.wLevel == 16 &&
            g_Battle.rgEnemy[i].e.wMagicRate == 4 &&
            g_Battle.rgEnemy[i].e.wAttackEquivItemRate == 4)
         {
            g_Battle.rgEnemy[i].e.wDexterity += 50; // for Black Spider
         }
#endif
      }

      g_Battle.rgEnemy[i++].wObjectID = w;
   }

   g_Battle.wMaxEnemyIndex = i - 1;

   //
   // Store all players
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      g_Battle.rgPlayer[i].flTimeMeter = 15.0f;
#ifndef PAL_CLASSIC
      g_Battle.rgPlayer[i].flTimeSpeedModifier = 2.0f;
      g_Battle.rgPlayer[i].sTurnOrder = -1;
#endif
      g_Battle.rgPlayer[i].wHidingTime = 0;
      g_Battle.rgPlayer[i].state = kFighterWait;
      g_Battle.rgPlayer[i].fDefending = FALSE;
      g_Battle.rgPlayer[i].wCurrentFrame = 0;
      g_Battle.rgPlayer[i].iColorShift = FALSE;
   }

   //
   // Load sprites and background
   //
   PAL_LoadBattleSprites();
   PAL_LoadBattleBackground();

   //
   // Create the surface for scene buffer
   //
   g_Battle.lpSceneBuf = VIDEO_CreateCompatibleSurface(gpScreen);

   if (g_Battle.lpSceneBuf == NULL)
   {
      TerminateOnError("PAL_StartBattle(): creating surface for scene buffer failed!");
   }

   PAL_UpdateEquipments();

   g_Battle.iExpGained = 0;
   g_Battle.iCashGained = 0;
   g_iSatSynch = 2;   /* Saturn 0x06027204 */

   g_Battle.fIsBoss = fIsBoss;
   g_Battle.fEnemyCleared = FALSE;
   g_Battle.fEnemyMoving = FALSE;
   g_Battle.iHidingTime = 0;
   g_Battle.wMovingPlayerIndex = 0;

   g_Battle.UI.szMsg[0] = '\0';
   g_Battle.UI.szNextMsg[0] = '\0';
   g_Battle.UI.dwMsgShowTime = 0;
   g_Battle.UI.state = kBattleUIWait;
   { extern BOOL PAL_AutoBattlePref(VOID); g_Battle.UI.fAutoAttack = !fIsBoss && PAL_AutoBattlePref(); }   /* kept from the last battle; a story battle starts (and leaves it) off */
   g_Battle.UI.iSelectedIndex = 0;
   g_Battle.UI.iPrevEnemyTarget = -1;

   memset(g_Battle.UI.rgShowNum, 0, sizeof(g_Battle.UI.rgShowNum));

   g_Battle.lpSummonSprite = NULL;
   g_Battle.sBackgroundColorShift = 0;

   gpGlobals->fInBattle = TRUE;
   g_Battle.BattleResult = kBattleResultPreBattle;
   g_Battle.fSpriteAddLock = TRUE;

   PAL_BattleUpdateFighters();

   //
   // Load the battle effect sprite.
   //
   i = PAL_MKFGetChunkSize(10, gpGlobals->f.fpDATA);
   g_Battle.lpEffectSprite = UTIL_malloc(i);

   PAL_MKFReadChunk(g_Battle.lpEffectSprite, i, 10, gpGlobals->f.fpDATA);

#ifdef PAL_CLASSIC
   g_Battle.Phase = kBattlePhaseSelectAction;
   g_Battle.fRepeat = FALSE;
   g_Battle.fForce = FALSE;
   g_Battle.fFlee = FALSE;
   g_Battle.fPrevAutoAtk = FALSE;
   g_Battle.fThisTurnCoop = FALSE;
#endif

   //
   // Run the main battle routine.
   //
   i = PAL_BattleMain();

   if (i == kBattleResultWon)
   {
      //
      // Player won the battle. Add the Experience points.
      //
      PAL_BattleWon();
   }

   //
   // Clear all item-using records
   //
   for (w = 0; w < MAX_INVENTORY; w++)
   {
      gpGlobals->rgInventory[w].nAmountInUse = 0;
   }

   //
   // Clear all player status, poisons and temporary effects
   //
   PAL_ClearAllPlayerStatus();
   for (w = 0; w < MAX_PLAYER_ROLES; w++)
   {
      PAL_CurePoisonByLevel(w, 3);
      PAL_RemoveEquipmentEffect(w, kBodyPartExtra);
   }

   //
   // Free all the battle sprites
   //
   PAL_FreeBattleSprites();
   free(g_Battle.lpEffectSprite);

   //
   // Free the surfaces for the background picture and scene buffer
   //
   { extern SDL_Surface *g_pSatCCBack; g_pSatCCBack = NULL; }
   VIDEO_FreeSurface(g_Battle.lpBackground);
   VIDEO_FreeSurface(g_Battle.lpSceneBuf);

   g_Battle.lpBackground = NULL;
   g_Battle.lpSceneBuf = NULL;

   gpGlobals->fInBattle = FALSE;

   AUDIO_PlayMusic(gpGlobals->wNumMusic, TRUE, 1);

   //
   // Restore the screen waving effects
   //
   gpGlobals->sWaveProgression = sPrevWaveProgression;
   gpGlobals->wScreenWave = wPrevWaveLevel;

   //
   // Saturn (emu o_wout2): after the last battle message the screen is black for 76 vsync (~1.27 s), then the
   // field appears at once.
   //
   SDL_FillRect(gpScreen, NULL, 0); SAT_BankFill(gpScreen, NULL, 0);
   VIDEO_UpdateScreen(NULL);
   g_qwSatBattleBlack = SDL_GetTicks();
   UTIL_Delay((g_fSatBattleWinClean && i == kBattleResultWon) ? 300 : 1267);
   g_iSatSynch = 3;   /* Saturn 0x060272e2: back to the field */
   if (!gpGlobals->fNeedToFadeIn && !g_fSatBattleStayBlack && !(g_fSatBattleWinClean && i == kBattleResultWon))
   {
      PAL_MakeScene();
      VIDEO_UpdateScreen(NULL);
   }

   return i;
}
