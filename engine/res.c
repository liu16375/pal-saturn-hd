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

typedef struct tagRESOURCES
{
   BYTE             bLoadFlags;

   LPPALMAP         lpMap;                                      // current loaded map
   LPSPRITE        *lppEventObjectSprites;                      // event object sprites
   int              nEventObject;                               // number of event objects

   LPSPRITE         rglpPlayerSprite[MAX_PLAYABLE_PLAYER_ROLES]; // player sprites
} RESOURCES, *LPRESOURCES;

static LPRESOURCES gpResources = NULL;

static VOID
PAL_FreeEventObjectSprites(
   VOID
)
/*++
  Purpose:

    Free all sprites of event objects on the scene.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int i;

   if (gpResources->lppEventObjectSprites != NULL)
   {
      for (i = 0; i < gpResources->nEventObject; i++)
      {
         free(gpResources->lppEventObjectSprites[i]);
      }

      free(gpResources->lppEventObjectSprites);

      gpResources->lppEventObjectSprites = NULL;
      gpResources->nEventObject = 0;
   }
}

static VOID
PAL_FreePlayerSprites(
   VOID
)
/*++
  Purpose:

    Free all player sprites.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int i;

   for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)
   {
      free(gpResources->rglpPlayerSprite[i]);
      gpResources->rglpPlayerSprite[i] = NULL;
   }
}

VOID
PAL_InitResources(
   VOID
)
/*++
  Purpose:

    Initialze the resource manager.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   gpResources = (LPRESOURCES)UTIL_calloc(1, sizeof(RESOURCES));
}

VOID
PAL_FreeResources(
   VOID
)
/*++
  Purpose:

    Free all loaded resources.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   if (gpResources != NULL)
   {
      //
      // Free all loaded sprites
      //
      PAL_FreePlayerSprites();
      PAL_FreeEventObjectSprites();

      //
      // Free map
      //
      PAL_FreeMap(gpResources->lpMap);

      //
      // Delete the instance
      //
      free(gpResources);
   }

   gpResources = NULL;
}

VOID
PAL_SetLoadFlags(
   BYTE       bFlags
)
/*++
  Purpose:

    Set flags to load resources.

  Parameters:

    [IN]  bFlags - flags to be set.

  Return value:

    None.

--*/
{
   if (gpResources == NULL)
   {
      return;
   }

   gpResources->bLoadFlags |= bFlags;
}

/* View (viewport, party, trail, direction, layer) at the moment of the last op 0x59: see script.c. */
static struct
{
   BOOL     fValid;
   PAL_POS  viewport;
   PARTY    rgParty[MAX_PLAYABLE_PLAYER_ROLES];
   TRAIL    rgTrail[MAX_PLAYABLE_PLAYER_ROLES];
   WORD     wPartyDirection, wLayer;
   int      vpx, vpy;
} s_SatPrevView;

static VOID
PAL_SatPrevViewExchange(
   BOOL     fSaveOnly
)
{
   extern int g_iSatVpX, g_iSatVpY;
   PAL_POS  vp = gpGlobals->viewport;
   PARTY    pa[MAX_PLAYABLE_PLAYER_ROLES];
   TRAIL    tr[MAX_PLAYABLE_PLAYER_ROLES];
   WORD     dir = gpGlobals->wPartyDirection, layer = gpGlobals->wLayer;
   int      vx = g_iSatVpX, vy = g_iSatVpY;

   memcpy(pa, gpGlobals->rgParty, sizeof(pa));
   memcpy(tr, gpGlobals->rgTrail, sizeof(tr));
   if (!fSaveOnly)
   {
      if (!s_SatPrevView.fValid) return;
      gpGlobals->viewport = s_SatPrevView.viewport;
      memcpy(gpGlobals->rgParty, s_SatPrevView.rgParty, sizeof(pa));
      memcpy(gpGlobals->rgTrail, s_SatPrevView.rgTrail, sizeof(tr));
      gpGlobals->wPartyDirection = s_SatPrevView.wPartyDirection;
      gpGlobals->wLayer = s_SatPrevView.wLayer;
      g_iSatVpX = s_SatPrevView.vpx; g_iSatVpY = s_SatPrevView.vpy;
   }
   s_SatPrevView.fValid = TRUE;
   s_SatPrevView.viewport = vp;
   memcpy(s_SatPrevView.rgParty, pa, sizeof(pa));
   memcpy(s_SatPrevView.rgTrail, tr, sizeof(tr));
   s_SatPrevView.wPartyDirection = dir;
   s_SatPrevView.wLayer = layer;
   s_SatPrevView.vpx = vx; s_SatPrevView.vpy = vy;
}

/* Called by every scene draw: the view the player last SAW.  Exit scripts set the party position of the new
   scene (op 0x46) before or after op 0x59 and before the loader runs; where no 0x50 fade-out came first
   (scenes 69 <-> 70) the loader's redraw of the old scene showed the old map at the new
   position for a moment.  The redraw now uses the last drawn view. */
static BOOL s_fSatPrevViewHold = FALSE;
VOID PAL_SatPrevViewSave(VOID) { if (!s_fSatPrevViewHold) PAL_SatPrevViewExchange(TRUE); }
static VOID PAL_SatPrevViewSwap(VOID) { PAL_SatPrevViewExchange(FALSE); }

VOID
PAL_LoadResources(
   VOID
)
/*++
  Purpose:

    Load the game resources if needed.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int                i, index, l, n;
   WORD               wPlayerID, wSpriteNum;

   if (gpResources == NULL || gpResources->bLoadFlags == 0)
   {
      return;
   }

   //
   // Load global data
   //
   if (gpResources->bLoadFlags & kLoadGlobalData)
   {
      PAL_InitGameData(gpGlobals->bCurrentSaveSlot);
      AUDIO_PlayMusic(gpGlobals->wNumMusic, TRUE, 1);
   }

   //
   // Load scene
   //
   if (gpResources->bLoadFlags & kLoadScene)
   {
      FILE              *fpMAP, *fpGOP;

      extern BOOL g_fSatRngReloading;
      if (g_wSatPrevScene != 0 && (gpGlobals->fEnteringScene || g_fSatRngReloading) && !(gpResources->bLoadFlags & kLoadGlobalData)
          && gpResources->lpMap != NULL)
      {
         //
         // Saturn 0x06026fe8: scene change by op 0x59 -> the old scene is redrawn (dialog gone) and faded out
         // before loading.
         //
         WORD wNew = gpGlobals->wNumScene;
         BOOL fFade = gpGlobals->fNeedToFadeIn;
         gpGlobals->wNumScene = g_wSatPrevScene;
         gpGlobals->fNeedToFadeIn = FALSE;
         PAL_SatPrevViewSwap();   /* the old scene at the OLD party position (a 0x46 may already have moved it) */
         s_fSatPrevViewHold = TRUE;
         PAL_MakeScene();
         VIDEO_UpdateScreen(NULL);
         s_fSatPrevViewHold = FALSE;
         PAL_SatPrevViewSwap();
         gpGlobals->wNumScene = wNew;
         gpGlobals->fNeedToFadeIn = fFade;
         PAL_SatSceneLoadFade(wNew);
         gpGlobals->fNeedToFadeIn = TRUE;   /* like op 0x50: the next scene draw fades in */
         { extern BOOL g_fSatReloadFade; g_fSatReloadFade = g_fSatRngReloading; }
      }
      g_wSatPrevScene = 0;

      fpMAP = UTIL_OpenRequiredFile("map.mkf");
      fpGOP = UTIL_OpenRequiredFile("gop.mkf");

      if (gpGlobals->fEnteringScene)
      {
         gpGlobals->wScreenWave = 0;
         gpGlobals->sWaveProgression = 0;
      }

      //
      // Free previous loaded scene (sprites and map)
      //
      PAL_FreeEventObjectSprites();
      PAL_FreeMap(gpResources->lpMap);

      //
      // Load map
      //
      i = gpGlobals->wNumScene - 1;
      gpResources->lpMap = PAL_LoadMap(gpGlobals->g.rgScene[i].wMapNum,
         fpMAP, fpGOP);

      if (gpResources->lpMap == NULL)
      {
         fclose(fpMAP);
         fclose(fpGOP);

         TerminateOnError("PAL_LoadResources(): Fail to load map #%d (scene #%d) !",
            gpGlobals->g.rgScene[i].wMapNum, gpGlobals->wNumScene);
      }

      //
      // Load sprites
      //
      index = gpGlobals->g.rgScene[i].wEventObjectIndex;
      gpResources->nEventObject = gpGlobals->g.rgScene[i + 1].wEventObjectIndex;
      gpResources->nEventObject -= index;

      if (gpResources->nEventObject > 0)
      {
         gpResources->lppEventObjectSprites =
            (LPSPRITE *)UTIL_calloc(gpResources->nEventObject, sizeof(LPSPRITE));
      }

      for (i = 0; i < gpResources->nEventObject; i++, index++)
      {
         n = gpGlobals->g.lprgEventObject[index].wSpriteNum;
         if (n == 0)
         {
            //
            // this event object has no sprite
            //
            gpResources->lppEventObjectSprites[i] = NULL;
            continue;
         }

         l = PAL_MKFGetDecompressedSize(n, gpGlobals->f.fpMGO);

         gpResources->lppEventObjectSprites[i] = (LPSPRITE)UTIL_malloc(l);

         if (PAL_MKFDecompressChunk(gpResources->lppEventObjectSprites[i], l,
            n, gpGlobals->f.fpMGO) > 0)
         {
            gpGlobals->g.lprgEventObject[index].nSpriteFramesAuto =
               PAL_SpriteGetNumFrames(gpResources->lppEventObjectSprites[i]);
         }
      }

      gpGlobals->partyoffset = PAL_XY(160, 112);

      fclose(fpGOP);
      fclose(fpMAP);
   }

   //
   // Load player sprites
   //
   if (gpResources->bLoadFlags & kLoadPlayerSprite)
   {
      //
      // Free previous loaded player sprites
      //
      PAL_FreePlayerSprites();

      for (i = 0; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
      {
         wPlayerID = gpGlobals->rgParty[i].wPlayerRole;
         assert(wPlayerID < MAX_PLAYER_ROLES);

         //
         // Load player sprite
         //
         wSpriteNum = gpGlobals->g.PlayerRoles.rgwSpriteNum[wPlayerID];

         l = PAL_MKFGetDecompressedSize(wSpriteNum, gpGlobals->f.fpMGO);

         gpResources->rglpPlayerSprite[i] = (LPSPRITE)UTIL_malloc(l);

         PAL_MKFDecompressChunk(gpResources->rglpPlayerSprite[i], l, wSpriteNum,
            gpGlobals->f.fpMGO);
      }

      for (i = 1; i <= gpGlobals->nFollower; i++)
      {
         //
         // Load the follower sprite
         //
         wSpriteNum = gpGlobals->rgParty[(short)gpGlobals->wMaxPartyMemberIndex+i].wPlayerRole;

         l = PAL_MKFGetDecompressedSize(wSpriteNum, gpGlobals->f.fpMGO);

         gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i] = (LPSPRITE)UTIL_malloc(l);

         PAL_MKFDecompressChunk(gpResources->rglpPlayerSprite[(short)gpGlobals->wMaxPartyMemberIndex+i], l, wSpriteNum,
            gpGlobals->f.fpMGO);
      }
   }

   /* #6-class fix (stale/mismatched keyed-HD pointers), made GLOBAL: ANY in-scene resource reload re-mallocs
      sprite/map frame buffers, so every frame POINTER changes, yet the HD keyed-image table (g_hdPtr in
      scene.c) is rebuilt only on a SCENE-NUMBER change (PAL_HD_Load early-returns while sc==g_hdScene). After
      an in-scene reload the table holds stale/freed pointers, and malloc can re-hand a new frame the SAME
      address a freed frame had, so PAL_HD_FrameIdx matches a keyed entry built for a DIFFERENT image -> the
      wrong HD frame is blitted. This bit the 端酒菜 party sprite (#6), but the same hazard exists for TILES and
      event-object (NPC) sprites on reloads that keep the scene number: op 0x99 in-place map change
      (wMapNum changed, wNumScene unchanged -> new tile+NPC pointers), op 0x65 player-sprite swap, and follower
      swap. So fire the re-key for EVERY reload path, not just kLoadPlayerSprite. Harmless on a normal scene
      change (which re-keys anyway); affected frames fall to the crisp ×6 fallback during the brief re-stream. */
   { extern void PAL_HD_ForceReload(void); PAL_HD_ForceReload(); }

   //
   // Clear all of the load flags
   //
   gpResources->bLoadFlags = 0;
}

LPPALMAP
PAL_GetCurrentMap(
   VOID
)
/*++
  Purpose:

    Get the current loaded map.

  Parameters:

    None.

  Return value:

    Pointer to the current loaded map. NULL if no map is loaded.

--*/
{
   if (gpResources == NULL)
   {
      return NULL;
   }

   return gpResources->lpMap;
}

LPSPRITE
PAL_GetPlayerSprite(
   BYTE      bPlayerIndex
)
/*++
  Purpose:

    Get the player sprite.

  Parameters:

    [IN]  bPlayerIndex - index of player in party (starts from 0).

  Return value:

    Pointer to the player sprite.

--*/
{
   if (gpResources == NULL || bPlayerIndex > MAX_PLAYABLE_PLAYER_ROLES-1)
   {
      return NULL;
   }

   return gpResources->rglpPlayerSprite[bPlayerIndex];
}

LPSPRITE
PAL_GetEventObjectSprite(
   WORD      wEventObjectID
)
/*++
  Purpose:

    Get the sprite of the specified event object.

  Parameters:

    [IN]  wEventObjectID - the ID of event object.

  Return value:

    Pointer to the sprite.

--*/
{
   wEventObjectID -= gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
   wEventObjectID--;

   if (gpResources == NULL || wEventObjectID >= gpResources->nEventObject)
   {
      return NULL;
   }

   return gpResources->lppEventObjectSprites[wEventObjectID];
}
