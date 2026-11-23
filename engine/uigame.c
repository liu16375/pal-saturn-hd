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
#include <sys/stat.h>
#include <time.h>

static BOOL __buymenu_firsttime_render;

static WORD GetSavedTimes(int iSaveSlot)
{
	FILE *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(0, "%d.rpg", iSaveSlot));
	WORD wSavedTimes = 0;
	if (fp != NULL)
	{
		if (fread(&wSavedTimes, sizeof(WORD), 1, fp) == 1)
			wSavedTimes = SDL_SwapLE16(wSavedTimes);
		else
			wSavedTimes = 0;
		fclose(fp);
	}
	return wSavedTimes;
}

/* Most recently written save (slots 1-5 or the auto save), 0 when there is none. */
static INT
PAL_NewestSaveSlot(
   VOID
)
{
   static const int rgSlot[6] = { 1, 2, 3, 4, 5, 250 /* PAL_AUTOSAVE_SLOT */ };
   int i, iBest = 0; time_t tBest = 0;
   for (i = 0; i < 6; i++)
   {
      FILE *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(0, "%d.rpg", rgSlot[i]));
      struct stat st;
      if (fp == NULL) continue;
      if (fstat(fileno(fp), &st) == 0 && (iBest == 0 || st.st_mtime > tBest)) { iBest = rgSlot[i]; tBest = st.st_mtime; }
      fclose(fp);
   }
   return iBest;
}

VOID
PAL_DrawOpeningMenuBackground(
   VOID
)
/*++
  Purpose:

    Draw the background of the main menu.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   LPBYTE        buf;

   buf = (LPBYTE)malloc(320 * 200);
   if (buf == NULL)
   {
      return;
   }

   //
   // Read the picture from fbp.mkf.
   //
   PAL_MKFDecompressChunk(buf, 320 * 200, MAINMENU_BACKGROUND_FBPNUM, gpGlobals->f.fpFBP);

   //
   // ...and blit it to the screen buffer.
   //
   PAL_FBPBlitToSurface(buf, gpScreen);
   VIDEO_UpdateScreen(NULL);

   free(buf);
}

static VOID
PAL_OpeningSettingsMenu(
   VOID
)
/*++
  System-settings submenu reachable from the opening menu: toggle music / sound on/off.
--*/
{
   WORD          wSel = 3;   /* value of the row the cursor starts on (language) */
   MENUITEM      rgItems[4] = {
      { 3,  SYSMENU_LABEL_LANG_0,   TRUE,  PAL_XY(112, 78)  },   /* language: Enter steps to the next one (user request) */
      { 0,  SYSMENU_LABEL_MUSIC_ON, TRUE,  PAL_XY(112, 96)  },   /* 遊戲音樂：開/關 (user request: in-line toggle like 不遇敵人) */
      { 1,  SYSMENU_LABEL_SOUND_ON, TRUE,  PAL_XY(112, 114) },   /* 遊戲音效：開/關 */
      { 2,  SYSMENU_LABEL_OPENING,  TRUE,  PAL_XY(112, 132) }    /* 開場動畫 (user request: replay the opening movies) */
   };

   while (TRUE)
   {
      //
      // Redraw the title background so the submenu shows cleanly (no leftover opening items).
      //
      PAL_DrawOpeningMenuBackground();
      rgItems[1].wNumWord = AUDIO_MusicEnabled() ? SYSMENU_LABEL_MUSIC_ON : SYSMENU_LABEL_MUSIC_OFF;
      rgItems[2].wNumWord = AUDIO_SoundEnabled() ? SYSMENU_LABEL_SOUND_ON : SYSMENU_LABEL_SOUND_OFF;
      rgItems[0].wNumWord = SYSMENU_LABEL_LANG_0 + PAL_LangGet();
      {
         /* the four rows start at the same x; a long label (English) moves all of them left */
         int i, wMax = 0;
         for (i = 0; i < 4; i++) { int w = PAL_TextWidth(PAL_GetWord(rgItems[i].wNumWord)); if (w > wMax) wMax = w; }
         for (i = 0; i < 4; i++) rgItems[i].pos = PAL_XY(wMax > 96 ? 160 - wMax / 2 : 112, 78 + 18 * i);
      }
      wSel = PAL_ReadMenu(NULL, rgItems, 4, wSel == 3 ? 0 : wSel + 1, MENUITEM_COLOR);
      if (wSel == MENUITEM_VALUE_CANCELLED)
      {
         break;
      }
      else if (wSel == 3)
      {
         //
         // Next language: guo yu -> Simplified Chinese -> Japanese -> English -> guo yu
         //
         PAL_LangSet((PAL_LangGet() + 1) % 4);
      }
      else if (wSel == 0)
      {
         //
         // Toggle music; immediately resume/stop the opening BGM so the change is audible.
         //
         AUDIO_EnableMusic(!AUDIO_MusicEnabled());
         AUDIO_PlayMusic(AUDIO_MusicEnabled() ? RIX_NUM_OPENINGMENU : 0, AUDIO_MusicEnabled(), 1);
      }
      else if (wSel == 1)
      {
         //
         // Toggle sound effects.
         //
         AUDIO_EnableSound(!AUDIO_SoundEnabled());
      }
      else if (wSel == 2)
      {
         //
         // Replay the opening movies, then bring the title screen and its music back.
         //
         AUDIO_PlayMusic(0, FALSE, 1);
         PAL_FadeOut(1);
         PAL_PlayAVI("1.avi");
         PAL_PlayAVI("2.avi");
         AUDIO_PlayMusic(AUDIO_MusicEnabled() ? RIX_NUM_OPENINGMENU : 0, AUDIO_MusicEnabled(), 1);
         PAL_DrawOpeningMenuBackground();
         PAL_FadeIn(0, FALSE, 1);
      }
   }
}

BOOL
PAL_FirstRunNotice(
   VOID
)
/*++
  First start on this machine (user request 2026-11-23): show the notice with 同意 / 拒絕.  Returns TRUE when the
  player agreed (now or on an earlier start: marker file "agreed" beside the saves), FALSE when the player declined.
--*/
{
   static const WCHAR * const rgLine[4] = {
      L"\x672C\x6E38\x6232\x70BA\x4E2D\x83EF\x6C11\x570B\xFF08\x53F0\x7063\xFF09\x5927\x5B87\x8CC7\x8A0A\x7814\x767C\xFF0C",
      L"\x7D93\x7531\xFF21\xFF29\x5354\x52A9\x5B8C\x6210\x4E86\x90E8\x5206\xFF28\xFF24\x5316\xFF0C",
      L"\x7981\x6B62\x7528\x65BC\x5546\x7528\xFF0C",
      L"\x53F0\x7063\x662F\x7368\x7ACB\x570B\x5BB6\xFF0C\x8ACB\x4E2D\x570B\x7684\x670B\x53CB\x6089\x77E5\x3002" };
   MENUITEM      rgItems[2] = {
      { 1,  SYSMENU_LABEL_AGREE,   TRUE,  PAL_XY(104, 164) },
      { 2,  SYSMENU_LABEL_DECLINE, TRUE,  PAL_XY(184, 164) } };
   FILE         *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, "agreed");
   WORD          wSel;
   int           i;

   if (fp != NULL) { fclose(fp); return TRUE; }

   {
      SDL_Rect rc = { 0, 0, 320, 200 };
      SDL_FillSurfaceRect(gpScreen, &rc, 0);
   }
   VIDEO_UpdateScreen(NULL);
   PAL_SetPalette(0, FALSE);
   for (i = 0; i < 4; i++)
   {
      LPCWSTR s = PAL_Tr(rgLine[i]);
      PAL_DrawText(s, PAL_XY(160 - PAL_TextWidth(s) / 2, 58 + 22 * i), 0x4F, TRUE, FALSE, FALSE);
   }
   VIDEO_UpdateScreen(NULL);
   do
   {
      wSel = PAL_ReadMenu(NULL, rgItems, 2, 0, MENUITEM_COLOR);
   } while (wSel == MENUITEM_VALUE_CANCELLED);   /* Esc does not answer for the player */
   PAL_FadeOut(1);
   if (wSel != 1) return FALSE;
   if ((fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, "agreed", "wb")) != NULL) { fputs("1", fp); fclose(fp); }
   return TRUE;
}

INT
PAL_OpeningMenu(
   VOID
)
/*++
  Purpose:

    Show the opening menu.

  Parameters:

    None.

  Return value:

    Which saved slot to load from (1-5). 0 to start a new game.

--*/
{
   WORD          wItemSelected;
   WORD          wDefaultItem     = PAL_NewestSaveSlot() ? 1 : 0;   /* user request: with a save present the cursor starts on load game */
   INT           w[3] = { PAL_WordWidth(MAINMENU_LABEL_NEWGAME), PAL_WordWidth(MAINMENU_LABEL_LOADGAME), PAL_WordWidth(GAMEMENU_LABEL_SYSTEM) };

   MENUITEM      rgMainMenuItem[3] = {
      // value   label                     enabled   position
      /* Saturn 0x0602166a: menu core (112,84), no box -> (125, 96+18i); 3rd item = 系統設定 */
      {  0,      MAINMENU_LABEL_NEWGAME,   TRUE,     PAL_XY(125 - (w[0] > 4 ? (w[0] - 4) * 8 : 0), 96)  },
      {  1,      MAINMENU_LABEL_LOADGAME,  TRUE,     PAL_XY(125 - (w[1] > 4 ? (w[1] - 4) * 8 : 0), 114) },
      {  2,      GAMEMENU_LABEL_SYSTEM,    TRUE,     PAL_XY(125 - (w[2] > 4 ? (w[2] - 4) * 8 : 0), 132) }
   };

   //
   // Play the background music
   //
   AUDIO_PlayMusic(RIX_NUM_OPENINGMENU, TRUE, 1);

   //
   // Draw the background
   //
   PAL_DrawOpeningMenuBackground();
   PAL_FadeIn(0, FALSE, 1);

   while (TRUE)
   {
      //
      // Activate the menu
      //
      wItemSelected = PAL_ReadMenu(NULL, rgMainMenuItem, 3, wDefaultItem, MENUITEM_COLOR);

      if (wItemSelected == 0 || wItemSelected == MENUITEM_VALUE_CANCELLED)
      {
         //
         // Start a new game
         //
         wItemSelected = 0;
         break;
      }
      else if (wItemSelected == 2)
      {
         //
         // System settings (music / sound toggles)
         //
         PAL_OpeningSettingsMenu();
         //
         // The language may have changed: draw the title again from scratch (a restored copy of the screen
         // still carried the labels of the old language under the new ones) and place the labels again.
         //
         PAL_DrawOpeningMenuBackground();
         {
            static const WORD rgLabel[3] = { MAINMENU_LABEL_NEWGAME, MAINMENU_LABEL_LOADGAME, GAMEMENU_LABEL_SYSTEM };
            int k;
            for (k = 0; k < 3; k++)
            {
               int wk = PAL_WordWidth(rgLabel[k]);
               rgMainMenuItem[k].pos = PAL_XY(125 - (wk > 4 ? (wk - 4) * 8 : 0), 96 + 18 * k);
            }
         }
         wDefaultItem = 2;
      }
      else
      {
         //
         // Load game
         //
         VIDEO_BackupScreen(gpScreen);
         wItemSelected = PAL_SaveSlotMenu((WORD)(PAL_NewestSaveSlot() ? PAL_NewestSaveSlot() : 1));   /* cursor on the newest save */
         VIDEO_RestoreScreen(gpScreen);
         VIDEO_UpdateScreen(NULL);
         if (wItemSelected != MENUITEM_VALUE_CANCELLED)
         {
            break;
         }
         wDefaultItem = 0;
      }
   }

   //
   // Fade out the screen and the music
   //
   AUDIO_PlayMusic(0, FALSE, 1);
   PAL_FadeOut(1);

   if (wItemSelected == 0)
   {
      PAL_PlayAVI("3.avi");
   }

   return (INT)wItemSelected;
}

#define SLOTMENU_X 66
extern VOID PAL_DrawSmallText(LPCWSTR lpszText, int x, int y, int px, BYTE bColor, BOOL fRight);
static BOOL g_fSaveSlotMenuSaving = FALSE;   /* TRUE while the menu picks a slot to SAVE to (auto save row disabled) */
static BOOL s_fAutoSaveSkip = TRUE;          /* the next scene "change" is a load / new game, not the party walking */
static WORD s_wAutoSaveScene = 0;

VOID PAL_AutoSaveSkipNext(VOID) { s_fAutoSaveSkip = TRUE; }

VOID
PAL_AutoSaveCheck(
   VOID
)
/* Called once per field frame (game.c): when the party has arrived in another scene, write the auto save. */
{
   static int fOff = -1;
   if (gpGlobals->fEnteringScene || gpGlobals->fInBattle) return;
   if (fOff < 0) fOff = (SDL_getenv("PAL_NOAUTOSAVE") || ((SDL_getenv("PAL_KEYFILE") || SDL_getenv("PAL_AUTOKEYS")) && !SDL_getenv("PAL_AUTOSAVETEST"))) ? 1 : 0;   /* test runs must not touch the player's auto save */
   if (gpGlobals->wNumScene != s_wAutoSaveScene)
   {
      if (!s_fAutoSaveSkip && s_wAutoSaveScene != 0 && !fOff)
      {
         WORD wSavedTimes = GetSavedTimes(PAL_AUTOSAVE_SLOT), w; int i;
         for (i = 1; i <= 5; i++) if ((w = GetSavedTimes(i)) > wSavedTimes) wSavedTimes = w;
         PAL_SaveGame(SDL_getenv("PAL_AUTOSAVETEST") ? SDL_atoi(SDL_getenv("PAL_AUTOSAVETEST")) : PAL_AUTOSAVE_SLOT, wSavedTimes + 1);   /* test runs write another slot */
      }
      s_wAutoSaveScene = gpGlobals->wNumScene;
   }
   s_fAutoSaveSkip = FALSE;
}

INT
PAL_SaveSlotMenu(
   WORD        wDefaultSlot
)
/*++
  Purpose:

    Show the load game menu.

  Parameters:

    [IN]  wDefaultSlot - default save slot number (1-5).

  Return value:

    Which saved slot to load from (1-5). MENUITEM_VALUE_CANCELLED if cancelled.

--*/
{
   LPBOX           rgpBox[5];
   int             i, w = PAL_WordMaxWidth(LOADMENU_LABEL_SLOT_FIRST, 5);
   int             dx = (w > 4) ? (w - 4) * 16 : 0;
   WORD            wItemSelected;

   MENUITEM        rgMenuItem[6];

   const SDL_Rect  rect = { SLOTMENU_X, 40, 320 - SLOTMENU_X, 160 };
   LPBOX           lpBox;
   int             nItems = 5, iTop = 58;

   (void)dx;
   //
   // Saturn save-device menu 0x06051118: BOX?(80,80,style 0,8,5) = 4 rows x 6 cols;
   // words 43+i at (x+13, y+12+18i); no save-count numbers.
   //
   // Port: a row for the auto save (written on every scene change) when loading and one exists, and the
   // time each slot was last saved, small and right-aligned, so the saves are easy to tell apart.
   //
   {
      FILE *fpa = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(0, "%d.rpg", PAL_AUTOSAVE_SLOT));
      if (fpa != NULL) fclose(fpa);
      nItems = (!g_fSaveSlotMenuSaving && fpa != NULL) ? 6 : 5;
   }
   iTop = (nItems == 6) ? 48 : 58;
   lpBox = PAL_CreateBox(PAL_XY(SLOTMENU_X, iTop), nItems - 1, 9, 0, TRUE);
   for (i = 0; i < nItems; i++)
   {
      static const WCHAR wszEmpty[] = { 0x2014, 0x2014, 0 };
      int   iSlot = (i < 5) ? i + 1 : PAL_AUTOSAVE_SLOT;
      FILE *fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(0, "%d.rpg", iSlot));
      WCHAR wsz[16]; char sz[16]; int k;
      if (i < 5) rgpBox[i] = NULL;
      rgMenuItem[i].wValue = iSlot;
      rgMenuItem[i].fEnabled = TRUE;
      rgMenuItem[i].wNumWord = (i < 5) ? LOADMENU_LABEL_SLOT_FIRST + i : SYSMENU_LABEL_AUTOSAVE;
      rgMenuItem[i].pos = PAL_XY(SLOTMENU_X + 13, iTop + 12 + 18 * i);
      wsz[0] = 0;
      if (fp != NULL)
      {
         struct stat st; struct tm *tm;
         if (fstat(fileno(fp), &st) == 0 && (tm = localtime(&st.st_mtime)) != NULL)
         {
            SDL_snprintf(sz, sizeof(sz), "%02d/%02d %02d:%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
            for (k = 0; sz[k] && k < 15; k++) wsz[k] = (WCHAR)sz[k];
            wsz[k] = 0;
         }
         fclose(fp);
      }
      PAL_DrawSmallText(wsz[0] ? wsz : wszEmpty, SLOTMENU_X + 187 - 20, iTop + 12 + 18 * i + 3, 11,
         wsz[0] ? MENUITEM_COLOR_CONFIRMED : MENUITEM_COLOR, TRUE);
   }
   if (wDefaultSlot == PAL_AUTOSAVE_SLOT) wDefaultSlot = (nItems == 6) ? 6 : 1;
   if (wDefaultSlot < 1 || wDefaultSlot > nItems) wDefaultSlot = 1;

   //
   // Activate the menu
   //
   wItemSelected = PAL_ReadMenu(NULL, rgMenuItem, nItems, wDefaultSlot - 1, MENUITEM_COLOR);

   //
   // Delete the boxes
   //
   (void)rgpBox;
   PAL_DeleteBox(lpBox);

   VIDEO_UpdateScreen(&rect);

   return wItemSelected;
}

/* user request: the shop's 否/是 confirm must not sit on top of the goods list (Saturn puts every 2-way box
   at (120,70)/(210,70), right over the list rows).  A caller may set these to place the two boxes itself. */
static BOOL    s_fConfirmAt = FALSE;
static PAL_POS s_rgConfirmAt[2];

static WORD
PAL_SelectionMenu(
	int   nWords,
	int   nDefault,
	WORD  wItems[]
)
/*++
  Purpose:

    Show a common selection box.

  Parameters:

    [IN]  nWords - number of emnu items.
	[IN]  nDefault - index of default item.
	[IN]  wItems - item word array.

  Return value:

    User-selected index.

--*/
{
	LPBOX           rgpBox[4];
	MENUITEM        rgMenuItem[4];
	int             w[4] = {
		(nWords >= 1 && wItems[0]) ? PAL_WordWidth(wItems[0]) : 1,
		(nWords >= 2 && wItems[1]) ? PAL_WordWidth(wItems[1]) : 1,
		(nWords >= 3 && wItems[2]) ? PAL_WordWidth(wItems[2]) : 1,
		(nWords >= 4 && wItems[3]) ? PAL_WordWidth(wItems[3]) : 1 };
	int             dx[4] = { (w[0] - 1) * 16, (w[1] - 1) * 16, (w[2] - 1) * 16, (w[3] - 1) * 16 }, i;
	PAL_POS         pos[4] = { PAL_XY(145, 110), PAL_XY(220 + dx[0], 110), PAL_XY(145, 160), PAL_XY(220 + dx[2], 160) };
	WORD            wReturnValue;

	const SDL_Rect  rect = { 130, 100, 125 + max(dx[0] + dx[1], dx[2] + dx[3]), 100 };

	for (i = 0; i < nWords; i++)
		if (nWords > i && !wItems[i])
			return MENUITEM_VALUE_CANCELLED;

	//
	// Create menu items
	//
	if (nWords == 2)
	{
		/* Saturn 0x06050070: BOX1(120 + 90i, 70, 4); selected text at box +15,+9 */
		pos[0] = PAL_XY(120 + 15, 70 + 9); pos[1] = PAL_XY(210 + 15, 70 + 9);
		if (s_fConfirmAt) { pos[0] = PAL_XY(PAL_X(s_rgConfirmAt[0]) + 15, PAL_Y(s_rgConfirmAt[0]) + 9); pos[1] = PAL_XY(PAL_X(s_rgConfirmAt[1]) + 15, PAL_Y(s_rgConfirmAt[1]) + 9); }
	}
	for (i = 0; i < nWords; i++)
	{
		rgMenuItem[i].fEnabled = TRUE;
		rgMenuItem[i].pos = pos[i];
		rgMenuItem[i].wValue = i;
		rgMenuItem[i].wNumWord = wItems[i];
	}

	//
	// Create the boxes
	//
	{ extern void PAL_HDM_PopupSave(void); PAL_HDM_PopupSave(); }   /* HD twin before the boxes */
	dx[1] = dx[0]; dx[3] = dx[2]; dx[0] = dx[2] = 0;
	for (i = 0; i < nWords; i++)
	{
		if (nWords == 2)
			rgpBox[i] = PAL_CreateSingleLineBox(s_fConfirmAt ? s_rgConfirmAt[i] : PAL_XY(120 + 90 * i, 70), 4, TRUE);
		else
		rgpBox[i] = PAL_CreateSingleLineBox(PAL_XY(130 + 75 * (i % 2) + dx[i], 100 + 50 * (i / 2)), w[i] + 1, TRUE);
	}

	//
	// Activate the menu
	//
	wReturnValue = PAL_ReadMenu(NULL, rgMenuItem, nWords, nDefault, nWords == 2 ? 0 : MENUITEM_COLOR); /* Saturn 0x060501ea: unselected colour 0 */

	//
	// Delete the boxes
	//
	for (i = 0; i < nWords; i++)
	{
		PAL_DeleteBox(rgpBox[i]);
	}

	{ extern void PAL_HDM_PopupRestore(void); PAL_HDM_PopupRestore(); }   /* ...and wipe them from it again */
	VIDEO_UpdateScreen(nWords == 2 ? NULL : &rect);

	return wReturnValue;
}

WORD
PAL_TripleMenu(
   WORD  wThirdWord
)
/*++
  Purpose:

    Show a triple-selection box.

  Parameters:

    None.

  Return value:

    User-selected index.

--*/
{
   WORD wItems[3] = { CONFIRMMENU_LABEL_NO, CONFIRMMENU_LABEL_YES, wThirdWord };
   return PAL_SelectionMenu(3, 0, wItems);
}

BOOL
PAL_ConfirmMenu(
   VOID
)
/*++
  Purpose:

    Show a "Yes or No?" confirm box.

  Parameters:

    None.

  Return value:

    TRUE if user selected Yes, FALSE if selected No.

--*/
{
   WORD wItems[2] = { CONFIRMMENU_LABEL_NO, CONFIRMMENU_LABEL_YES };
   WORD wReturnValue = PAL_SelectionMenu(2, 0, wItems);

   return (wReturnValue == MENUITEM_VALUE_CANCELLED || wReturnValue == 0) ? FALSE : TRUE;
}

BOOL
PAL_SwitchMenu(
   BOOL      fEnabled
)
/*++
  Purpose:

    Show a "Enable/Disable" selection box.

  Parameters:

    [IN]  fEnabled - whether the option is originally enabled or not.

  Return value:

    TRUE if user selected "Enable", FALSE if selected "Disable".

--*/
{
   WORD wItems[2] = { SWITCHMENU_LABEL_DISABLE, SWITCHMENU_LABEL_ENABLE };
   WORD wReturnValue = PAL_SelectionMenu(2, fEnabled ? 1 : 0, wItems);
   return (wReturnValue == MENUITEM_VALUE_CANCELLED) ? fEnabled : ((wReturnValue == 0) ? FALSE : TRUE);
}

#ifndef PAL_CLASSIC

static VOID
PAL_BattleSpeedMenu(
   VOID
)
/*++
  Purpose:

    Show the Battle Speed selection box.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   LPBOX           lpBox;
   WORD            wReturnValue;
   const SDL_Rect  rect = {131, 100, 165, 50};

   MENUITEM        rgMenuItem[5] = {
      { 1,   BATTLESPEEDMENU_LABEL_1,       TRUE,   PAL_XY(145, 110) },
      { 2,   BATTLESPEEDMENU_LABEL_2,       TRUE,   PAL_XY(170, 110) },
      { 3,   BATTLESPEEDMENU_LABEL_3,       TRUE,   PAL_XY(195, 110) },
      { 4,   BATTLESPEEDMENU_LABEL_4,       TRUE,   PAL_XY(220, 110) },
      { 5,   BATTLESPEEDMENU_LABEL_5,       TRUE,   PAL_XY(245, 110) },
   };

   //
   // Create the boxes
   //
   lpBox = PAL_CreateSingleLineBox(PAL_XY(131, 100), 8, TRUE);

   //
   // Activate the menu
   //
   wReturnValue = PAL_ReadMenu(NULL, rgMenuItem, 5, gpGlobals->bBattleSpeed - 1,
      MENUITEM_COLOR);

   //
   // Delete the boxes
   //
   PAL_DeleteBox(lpBox);

   VIDEO_UpdateScreen(&rect);

   if (wReturnValue != MENUITEM_VALUE_CANCELLED)
   {
      gpGlobals->bBattleSpeed = wReturnValue;
   }
}

#endif

LPBOX
PAL_ShowCash(
   DWORD      dwCash
)
/*++
  Purpose:

    Show the cash amount at the top left corner of the screen.

  Parameters:

    [IN]  dwCash - amount of cash.

  Return value:

    pointer to the saved screen part.

--*/
{
   LPBOX     lpBox;

   //
   // Create the box.
   //
   lpBox = PAL_CreateSingleLineBox(PAL_XY(0, 0), 5, TRUE);
   if (lpBox == NULL)
   {
      return NULL;
   }

   //
   // Draw the text label.
   //
   PAL_DrawText(PAL_GetWord(CASH_LABEL), PAL_XY(10, 10), 0, FALSE, FALSE, FALSE);

   //
   // Draw the cash amount.
   //
   /* Saturn 0x0604bcd4: NUM2(x+48, y+15, cash, 6) -- right-aligned, no zero padding */
   PAL_DrawNumberZeroPad(dwCash, 6, PAL_XY(48, 15));

   return lpBox;
}

static VOID
PAL_SystemMenu_OnItemChange(
   WORD        wCurrentItem
)
/*++
  Purpose:

    Callback function when user selected another item in the system menu.

  Parameters:

    [IN]  wCurrentItem - current selected item.

  Return value:

    None.

--*/
{
   gpGlobals->iCurSystemMenuItem = (wCurrentItem == 11) ? 2 : (wCurrentItem == 12) ? 3 : wCurrentItem - 1;   /* item value -> row (music / sound rows have the values 11 / 12) */
}

BOOL
PAL_SystemMenu(
   VOID
)
/*++
  Purpose:

    Show the system menu.

  Parameters:

    None.

  Return value:

    TRUE if user made some operations in the menu, FALSE if user cancelled.

--*/
{
   LPBOX               lpMenuBox;
   WORD                wReturnValue;
   int                 iSlot, i;
   const SDL_Rect      rect = {40, 60, 280, 135};

   //
   // Create menu items
   //
   extern void PAL_HDM_PopupSave(void);
   extern void PAL_HDM_PopupRestore(void);
   MENUITEM            rgSystemMenuItem[] =
   {
      // value  label                        enabled   pos
      { 1,      SYSMENU_LABEL_SAVE,          TRUE,     PAL_XY(53, 72) },
      { 2,      SYSMENU_LABEL_LOAD,          TRUE,     PAL_XY(53, 72 + 18) },
      { 11,    SYSMENU_LABEL_MUSIC_ON,      TRUE,     PAL_XY(53, 72 + 36) },   /* user request: music / sound toggles */
      { 12,     SYSMENU_LABEL_SOUND_ON,      TRUE,     PAL_XY(53, 72 + 54) },
      { 7,      SYSMENU_LABEL_OPENING,       TRUE,     PAL_XY(53, 56 + 54) },   /* not shown any more (user request): the opening replay stays on the title screen only */
      { 3,      SYSMENU_LABEL_MUSIC,         TRUE,     PAL_XY(53, 56 + 36) },
      { 4,      SYSMENU_LABEL_SOUND,         TRUE,     PAL_XY(53, 56 + 54) },
      { 5,      SYSMENU_LABEL_QUIT,          TRUE,     PAL_XY(53, 56 + 72) },
#if !defined(PAL_CLASSIC)
      { 6,      SYSMENU_LABEL_BATTLEMODE,    TRUE,     PAL_XY(53, 56 + 90) },
#endif
   };
   const int           nSystemMenuItem = 4; /* Saturn 0x0604c650: system menu = words 11,12 only (save / load); + music / sound toggles (port). The test aids (one-hit kill, experience multiplier, no encounters) were removed for the final build. */

   //
   // Create the menu box.
   //
   PAL_HDM_PopupSave();   /* HD twin before this box: Esc back to the main menu must wipe it from the twin too */
   if (gpGlobals->iCurSystemMenuItem >= nSystemMenuItem) gpGlobals->iCurSystemMenuItem = 0;
again:
   rgSystemMenuItem[2].wNumWord = AUDIO_MusicEnabled() ? SYSMENU_LABEL_MUSIC_ON : SYSMENU_LABEL_MUSIC_OFF;
   rgSystemMenuItem[3].wNumWord = AUDIO_SoundEnabled() ? SYSMENU_LABEL_SOUND_ON : SYSMENU_LABEL_SOUND_OFF;
   lpMenuBox = PAL_CreateBox(PAL_XY(40, 60), nSystemMenuItem - 1, 5, 0, TRUE); /* Saturn BOX?(40,60,0,4,2); wider for the port's 6-character toggle */

   //
   // Perform the menu.
   //
   wReturnValue = PAL_ReadMenu(PAL_SystemMenu_OnItemChange, rgSystemMenuItem, nSystemMenuItem, gpGlobals->iCurSystemMenuItem, MENUITEM_COLOR);

   if (wReturnValue == MENUITEM_VALUE_CANCELLED)
   {
      //
      // User cancelled the menu
      //
      PAL_DeleteBox(lpMenuBox);
      PAL_HDM_PopupRestore();
      VIDEO_UpdateScreen(&rect);
      return FALSE;
   }

   if (wReturnValue == 11 || wReturnValue == 12)
   {
      //
      // Music / sound toggle: flip it and show the menu again with the new state
      //
      if (wReturnValue == 11)
      {
         AUDIO_EnableMusic(!AUDIO_MusicEnabled());   /* the mixer mutes / unmutes the current track at once (audio.c) */
      }
      else
      {
         AUDIO_EnableSound(!AUDIO_SoundEnabled());
      }
      gpGlobals->iCurSystemMenuItem = (wReturnValue == 11) ? 2 : 3;
      PAL_DeleteBox(lpMenuBox);
      PAL_HDM_PopupRestore();
      goto again;
   }

   switch (wReturnValue)
   {
   case 1:
      //
      // Save game
      //
      g_fSaveSlotMenuSaving = TRUE;
      iSlot = PAL_SaveSlotMenu(gpGlobals->bCurrentSaveSlot);
      g_fSaveSlotMenuSaving = FALSE;

      if (iSlot != MENUITEM_VALUE_CANCELLED)
      {
         WORD wSavedTimes = GetSavedTimes(PAL_AUTOSAVE_SLOT);
         gpGlobals->bCurrentSaveSlot = (BYTE)iSlot;

         for (i = 1; i <= 5; i++)
         {
            WORD curSavedTimes = GetSavedTimes(i);
            if (curSavedTimes > wSavedTimes)
            {
               wSavedTimes = curSavedTimes;
            }
         }
         PAL_SaveGame(iSlot, wSavedTimes + 1);
      }
      break;

   case 2:
      //
      // Load game
      //
      iSlot = PAL_SaveSlotMenu(gpGlobals->bCurrentSaveSlot);
      if (iSlot != MENUITEM_VALUE_CANCELLED)
      {
         AUDIO_PlayMusic(0, FALSE, 1);
         PAL_FadeOut(1);
         PAL_ReloadInNextTick(iSlot);
      }
      break;

   case 7:
      //
      // Replay the opening movies, then come back to the field (faded in by the next scene draw)
      //
      AUDIO_PlayMusic(0, FALSE, 1);
      PAL_FadeOut(1);
      PAL_PlayAVI("1.avi");
      PAL_PlayAVI("2.avi");
      gpGlobals->fNeedToFadeIn = TRUE;
      AUDIO_PlayMusic(gpGlobals->wNumMusic, TRUE, 1);
      break;

   case 3:
      //
      // Music
      //
      AUDIO_EnableMusic(PAL_SwitchMenu(AUDIO_MusicEnabled()));
      if (gConfig.eMIDISynth == SYNTH_NATIVE && gConfig.eMusicType == MUSIC_MIDI)
      {
         AUDIO_PlayMusic(AUDIO_MusicEnabled() ? gpGlobals->wNumMusic : 0, AUDIO_MusicEnabled(), 0);
      }
      break;

   case 4:
      //
      // Sound
      //
      AUDIO_EnableSound(PAL_SwitchMenu(AUDIO_SoundEnabled()));
      break;

   case 5:
      //
      // Quit
      //
      PAL_QuitGame();
      break;

#if !defined(PAL_CLASSIC)
   case 6:
      //
      // Battle Mode
      //
      PAL_BattleSpeedMenu();
      break;
#endif
   }

   PAL_DeleteBox(lpMenuBox);
   return TRUE;
}
/* user request: clear feedback for a spell cast from the field menu -- who cast what on whom and what it did
   ("沒有效果" when nothing changed, e.g. healing someone whose HP is already full; no MP is spent then).
   A two-line box in the middle of the screen; Enter / Esc / a direction key closes it. */
typedef struct tagFIELDMAGICSNAP
{
   PLAYERROLES   roles;
   WORD          rgStatus[MAX_PLAYER_ROLES][kStatusAll];
   POISONSTATUS  rgPoison[MAX_POISONS][MAX_PLAYABLE_PLAYER_ROLES];
   INVENTORY     rgInventory[MAX_INVENTORY];
   DWORD         dwCash;
   WORD          wNumScene;
   PAL_POS       viewport;
} FIELDMAGICSNAP;

static VOID
PAL_FieldMagicSnap(
   FIELDMAGICSNAP *p
)
{
   memset(p, 0, sizeof(*p));
   p->roles = gpGlobals->g.PlayerRoles;
   memcpy(p->rgStatus, gpGlobals->rgPlayerStatus, sizeof(p->rgStatus));
   memcpy(p->rgPoison, gpGlobals->rgPoisonStatus, sizeof(p->rgPoison));
   memcpy(p->rgInventory, gpGlobals->rgInventory, sizeof(p->rgInventory));
   p->dwCash = gpGlobals->dwCash;
   p->wNumScene = gpGlobals->wNumScene;
   p->viewport = gpGlobals->viewport;
}

static VOID
PAL_FieldMagicBox(
   LPCWSTR  lpszLine1,
   LPCWSTR  lpszLine2
)
{
   extern void PAL_HDM_PopupSave(void);
   extern void PAL_HDM_PopupRestore(void);
   int      w1 = PAL_TextWidth(lpszLine1), w2 = PAL_TextWidth(lpszLine2);
   int      w = (w1 > w2) ? w1 : w2;
   int      nCol = (w + 15) / 16;
   int      x, y = 64;
   LPBOX    lpBox;
   DWORD    dwBegin;

   if (nCol < 4) nCol = 4;
   x = (320 - (nCol * 16 + 26)) / 2;
   if (x < 0) x = 0;

   PAL_HDM_PopupSave();
   lpBox = PAL_CreateBox(PAL_XY(x, y), 1, nCol - 1, 0, TRUE);
   PAL_DrawText(lpszLine1, PAL_XY(x + 13 + (nCol * 16 - w1) / 2, y + 12), MENUITEM_COLOR, TRUE, FALSE, FALSE);
   PAL_DrawText(lpszLine2, PAL_XY(x + 13 + (nCol * 16 - w2) / 2, y + 30), MENUITEM_COLOR_SELECTED, TRUE, FALSE, FALSE);
   VIDEO_UpdateScreen(NULL);

   PAL_ClearKeyState();
   dwBegin = SDL_GetTicks();
   while (TRUE)
   {
      UTIL_Delay(5);
      if (g_InputState.dwKeyPress & (kKeySearch | kKeyMenu | kKeyDialogNext)) break;
      if (SDL_GetTicks() - dwBegin > 250 &&
          (g_InputState.dwKeyPress & (kKeyUp | kKeyDown | kKeyLeft | kKeyRight))) break;
   }
   PAL_ClearKeyState();

   PAL_DeleteBox(lpBox);
   PAL_HDM_PopupRestore();
   VIDEO_UpdateScreen(NULL);
}

static BOOL
PAL_CastFieldMagic(
   WORD     wCasterRole,
   WORD     wMagic,
   BOOL     fAll,
   WORD     wTargetRole
)
/* Runs the spell's field scripts, charges the MP only if it did something, reports. TRUE = MP was spent. */
{
   static FIELDMAGICSNAP before, after;
   WCHAR    s1[96], s2[96];
   WORD     wCost = gpGlobals->g.lprgMagic[gpGlobals->g.rgObject[wMagic].magic.wMagicNumber].wCostMP;
   BOOL     fOK, fChanged, fPaid = FALSE;
   int      i, dHP = 0, dMP = 0;

   PAL_FieldMagicSnap(&before);

   gpGlobals->g.rgObject[wMagic].magic.wScriptOnUse =
      PAL_RunTriggerScript(gpGlobals->g.rgObject[wMagic].magic.wScriptOnUse, fAll ? 0 : wTargetRole);
   fOK = g_fScriptSuccess;
   if (fOK)
   {
      gpGlobals->g.rgObject[wMagic].magic.wScriptOnSuccess =
         PAL_RunTriggerScript(gpGlobals->g.rgObject[wMagic].magic.wScriptOnSuccess, fAll ? 0 : wTargetRole);
      fOK = g_fScriptSuccess;
   }

   PAL_FieldMagicSnap(&after);
   fChanged = (memcmp(&before, &after, sizeof(before)) != 0) || gpGlobals->fEnteringScene;

   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      WORD r = gpGlobals->rgParty[i].wPlayerRole;
      dHP += (int)after.roles.rgwHP[r] - (int)before.roles.rgwHP[r];
      dMP += (int)after.roles.rgwMP[r] - (int)before.roles.rgwMP[r];
   }

   if (fOK && fChanged)
   {
      gpGlobals->g.PlayerRoles.rgwMP[wCasterRole] -= wCost;
      fPaid = TRUE;
   }

   //
   // A spell that leaves the scene (or faded the screen out) reports nothing: the scene change is the feedback.
   //
   if (gpGlobals->fEnteringScene || gpGlobals->fNeedToFadeIn)
   {
      return fPaid;
   }

   if (fAll)
      PAL_swprintf(s1, sizeof(s1) / sizeof(WCHAR), PAL_Tr(L"%ls 對 全體 施展 %ls"),
         PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[wCasterRole]), PAL_GetWord(wMagic));
   else if (wTargetRole == wCasterRole)
      PAL_swprintf(s1, sizeof(s1) / sizeof(WCHAR), PAL_Tr(L"%ls 對 自己 施展 %ls"),
         PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[wCasterRole]), PAL_GetWord(wMagic));
   else
      PAL_swprintf(s1, sizeof(s1) / sizeof(WCHAR), PAL_Tr(L"%ls 對 %ls 施展 %ls"),
         PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[wCasterRole]),
         PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[wTargetRole]), PAL_GetWord(wMagic));

   /* the heal scripts report failure when there is nothing to heal (HP / MP already full), so a failed
      script and an unchanged party both read as "no effect" */
   if (!fOK || !fChanged)
      PAL_swprintf(s2, sizeof(s2) / sizeof(WCHAR), PAL_Tr(L"沒有效果"));
   else if (dHP > 0 && dMP > 0)
      PAL_swprintf(s2, sizeof(s2) / sizeof(WCHAR), L"%ls +%d  %ls +%d",
         PAL_GetWord(STATUS_LABEL_HP), dHP, PAL_GetWord(STATUS_LABEL_MP), dMP);
   else if (dHP > 0)
      PAL_swprintf(s2, sizeof(s2) / sizeof(WCHAR), L"%ls +%d", PAL_GetWord(STATUS_LABEL_HP), dHP);
   else if (dMP > 0)
      PAL_swprintf(s2, sizeof(s2) / sizeof(WCHAR), L"%ls +%d", PAL_GetWord(STATUS_LABEL_MP), dMP);
   else
      PAL_swprintf(s2, sizeof(s2) / sizeof(WCHAR), PAL_Tr(L"施展成功"));

   if (!fAll)
   {
      //
      // show the new HP / MP in the party boxes under the report
      //
      int yy = 42;
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         PAL_PlayerInfoBox(PAL_XY(yy, 165), gpGlobals->rgParty[i].wPlayerRole, 100, TIMEMETER_COLOR_DEFAULT, TRUE);
         yy += 78;
      }
   }

   PAL_FieldMagicBox(s1, s2);
   return fPaid;
}


VOID
PAL_InGameMagicMenu(
   VOID
)
/*++
  Purpose:

    Show the magic menu.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   MENUITEM         rgMenuItem[MAX_PLAYERS_IN_PARTY];
   int              i, y;
   static WORD      w;
   WORD             wMagic;

   if (gpGlobals->wMaxPartyMemberIndex == 0)
   {
      w = 0;
      goto start_magicmenu;
   }

   //
   // Draw the player info boxes
   //
   y = 42; /* Saturn 0x0604c912 */

   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      PAL_PlayerInfoBox(PAL_XY(y, 165), gpGlobals->rgParty[i].wPlayerRole, 100,
         TIMEMETER_COLOR_DEFAULT, TRUE);
      y += 78;
   }

   y = 75;

   //
   // Generate one menu items for each player in the party
   //
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      assert(i <= MAX_PLAYERS_IN_PARTY);

      rgMenuItem[i].wValue = i;
      rgMenuItem[i].wNumWord =
         gpGlobals->g.PlayerRoles.rgwName[gpGlobals->rgParty[i].wPlayerRole];
      rgMenuItem[i].fEnabled =
         (gpGlobals->g.PlayerRoles.rgwHP[gpGlobals->rgParty[i].wPlayerRole] > 0);
      rgMenuItem[i].pos = PAL_XY(48, y);

      y += 18;
   }

   //
   // Draw the box
   //
   PAL_CreateBox(PAL_XY(35, 62), gpGlobals->wMaxPartyMemberIndex, PAL_MenuTextMaxWidth(rgMenuItem, sizeof(rgMenuItem)/sizeof(MENUITEM)) - 1, 0, FALSE);

   w = PAL_ReadMenu(NULL, rgMenuItem, gpGlobals->wMaxPartyMemberIndex + 1, w, MENUITEM_COLOR);

   if (w == MENUITEM_VALUE_CANCELLED)
   {
      return;
   }

start_magicmenu:

   wMagic = 0;

   while (TRUE)
   {
      wMagic = PAL_MagicSelectionMenu(gpGlobals->rgParty[w].wPlayerRole, FALSE, wMagic);
      if (wMagic == 0)
      {
         break;
      }

      VIDEO_BackupScreen(gpScreen);

      if (gpGlobals->g.rgObject[wMagic].magic.wFlags & kMagicFlagApplyToAll)
      {
         PAL_CastFieldMagic(gpGlobals->rgParty[w].wPlayerRole, wMagic, TRUE, 0);

         if (gpGlobals->fNeedToFadeIn)
         {
            PAL_FadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette, 1);
            gpGlobals->fNeedToFadeIn = FALSE;
         }
      }
      else
      {
         //
         // Need to select which player to use the magic on.
         //
         WORD       wPlayer = 0;
         SDL_Rect   rect;

         while (wPlayer != MENUITEM_VALUE_CANCELLED)
         {
            //
            // Redraw the player info boxes first
            //
            y = 42; /* Saturn 0x0604c912 */

            for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
            {
               PAL_PlayerInfoBox(PAL_XY(y, 165), gpGlobals->rgParty[i].wPlayerRole, 100,
                  TIMEMETER_COLOR_DEFAULT, TRUE);
               y += 78;
            }

            //
            // Draw the cursor on the selected item
            //
            rect.x = 0;
            rect.y = 158;
            rect.w = 320;
            rect.h = 6;

            VIDEO_RestoreScreen(gpScreen);

            PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_CURSOR_UP),
               gpScreen, PAL_XY(75 + 78 * wPlayer, rect.y));

            VIDEO_UpdateScreen(&rect);

            while (TRUE)
            {
               PAL_ClearKeyState();
               PAL_ProcessEvent();

               if (g_InputState.dwKeyPress & kKeyMenu)
               {
                  wPlayer = MENUITEM_VALUE_CANCELLED;
                  break;
               }
               else if (g_InputState.dwKeyPress & kKeySearch)
               {
                  if (PAL_CastFieldMagic(gpGlobals->rgParty[w].wPlayerRole, wMagic, FALSE,
                         gpGlobals->rgParty[wPlayer].wPlayerRole))
                  {
                     //
                     // Don't go further if run out of MP
                     //
                     if (gpGlobals->g.PlayerRoles.rgwMP[gpGlobals->rgParty[w].wPlayerRole] <
                        gpGlobals->g.lprgMagic[gpGlobals->g.rgObject[wMagic].magic.wMagicNumber].wCostMP)
                     {
                        wPlayer = MENUITEM_VALUE_CANCELLED;
                     }
                  }

                  //
                  // Show the effect (e.g. 气疗术 healing) IMMEDIATELY. The target-select loop below
                  // restores the pre-cast backup via VIDEO_RestoreScreen and only pushes the cursor row,
                  // so without this the healed HP/MP never appeared until the magic menu was left -- it
                  // looked like the spell "无法治愈自己". Redraw the info boxes with the new values, push
                  // them to the screen, and refresh the backup so the restore keeps showing them. [user #4]
                  //
                  {
                     int yy = 42, ii;
                     for (ii = 0; ii <= gpGlobals->wMaxPartyMemberIndex; ii++)
                     {
                        PAL_PlayerInfoBox(PAL_XY(yy, 165), gpGlobals->rgParty[ii].wPlayerRole, 100,
                           TIMEMETER_COLOR_DEFAULT, TRUE);
                        yy += 78;
                     }
                     VIDEO_UpdateScreen(NULL);
                     VIDEO_BackupScreen(gpScreen);
                  }

                  break;
               }
               else if (g_InputState.dwKeyPress & (kKeyLeft | kKeyUp))
               {
                  if (wPlayer > 0)
                  {
                     wPlayer--;
                     break;
                  }
               }
               else if (g_InputState.dwKeyPress & (kKeyRight | kKeyDown))
               {
                  if (wPlayer < gpGlobals->wMaxPartyMemberIndex)
                  {
                     wPlayer++;
                     break;
                  }
               }

               SDL_Delay(1);
            }
         }
      }

      //
      // Redraw the player info boxes
      //
      y = 42; /* Saturn 0x0604c912 */

      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         PAL_PlayerInfoBox(PAL_XY(y, 165), gpGlobals->rgParty[i].wPlayerRole, 100,
            TIMEMETER_COLOR_DEFAULT, TRUE);
         y += 78;
      }
   }
}

static VOID
PAL_InventoryMenu(
   VOID
)
/*++
  Purpose:

    Show the inventory menu.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   static WORD      w = 0;

   MENUITEM        rgMenuItem[2] =
   {
      // value  label                     enabled   pos
      /* Saturn 0x0604d618: generic menu (30,60), words 22/23, A=2 B=2 -> items (43, 72+18i) */
      { 1,      INVMENU_LABEL_EQUIP,      TRUE,     PAL_XY(43, 72) },
      { 2,      INVMENU_LABEL_USE,        TRUE,     PAL_XY(43, 72 + 18) },
   };

   PAL_CreateBox(PAL_XY(30, 60), 1, PAL_MenuTextMaxWidth(rgMenuItem, sizeof(rgMenuItem)/sizeof(MENUITEM)) - 1, 0, FALSE);

   w = PAL_ReadMenu(NULL, rgMenuItem, 2, w - 1, MENUITEM_COLOR);

   switch (w)
   {
   case 1:
      PAL_GameEquipItem();
      break;

   case 2:
      PAL_GameUseItem();
      break;
   }
}


/* ===== Dialogue log (user request) =====================================================================
   Every dialog line is recorded with its speaker; the in-game menu (ESC) shows the record as a small-font
   panel on the right half of the screen.  RIGHT moves into the panel (UP/DOWN scroll, PgUp/PgDn page,
   LEFT/ESC back); PgUp/PgDn/Home/End also scroll it straight from the menu.  Kept beside the save as
   <slot>.dlg.  Buffer: lines "<kind><text>\n", kind N = speaker name, P = first text line of a dialog
   without a name, T = further text line.  The text keeps the dialog markup (colour toggles) for display. */
#define DLGLOG_MAX   300000
static WCHAR *s_dlgBuf = NULL;
static int    s_dlgLen = 0, s_dlgCap = 0;
static BOOL   s_dlgNew = TRUE;
static INT    s_dlgFace = 0;

/* A dialog that shows only a face (no "name:" line, e.g. Caiyi's plea inside her battle) is filed under the
   name that face carries elsewhere in the scripts: 0x3C/0x3D face, then a text line ending in a colon. */
#define DLG_MAXFACE 1024
static WORD s_dlgFaceMsg[DLG_MAXFACE];   /* message number of the name, 0 = none */

static LPCWSTR
PAL_DlgLogFaceName(
   INT iFace
)
{
   static BOOL fScanned = FALSE;
   if (!fScanned)
   {
      static short cnt[DLG_MAXFACE];
      int i, n = gpGlobals->g.nScriptEntry;
      fScanned = TRUE;
      for (i = 0; i + 1 < n; i++)
      {
         LPSCRIPTENTRY p = &gpGlobals->g.lprgScriptEntry[i];
         int f = p->rgwOperand[0]; LPCWSTR t; size_t l;
         if ((p->wOperation != 0x003C && p->wOperation != 0x003D) || f <= 0 || f >= DLG_MAXFACE) continue;
         if (p[1].wOperation != 0xFFFF) continue;
         t = PAL_GetMsg(p[1].rgwOperand[0]);
         if (t == NULL || (l = wcslen(t)) == 0 || !(t[l - 1] == 0xff1a || t[l - 1] == 0x2236 || t[l - 1] == L':')) continue;
         /* majority vote: a face can appear under an early alias */
         if (cnt[f] == 0) { s_dlgFaceMsg[f] = p[1].rgwOperand[0]; cnt[f] = 1; }
         else
         {
            LPCWSTR c = PAL_GetMsg(s_dlgFaceMsg[f]);
            /* PAL_GetMsg may reuse one buffer: compare through a copy */
            WCHAR a[64]; wcsncpy(a, c ? c : L"", 63); a[63] = 0;
            t = PAL_GetMsg(p[1].rgwOperand[0]);
            if (t != NULL && wcscmp(a, t) == 0) cnt[f]++; else cnt[f]--;
         }
      }
   }
   if (iFace <= 0 || iFace >= DLG_MAXFACE || s_dlgFaceMsg[iFace] == 0) return NULL;
   return PAL_GetMsg(s_dlgFaceMsg[iFace]);
}

VOID
PAL_DlgLogStart(
   INT iFace
)
{
   s_dlgNew = TRUE; s_dlgFace = iFace;
}

VOID
PAL_DlgLogAdd(
   INT        iKind,      /* 0 = a dialog starts, 1 = speaker name, 2 = text line */
   LPCWSTR    lpszText
)
{
   int n; WCHAR kind;
   if (iKind == 0) { s_dlgNew = TRUE; s_dlgFace = 0; return; }
   if (lpszText == NULL || wcslen(lpszText) == 0) return;
   if (iKind == 2 && s_dlgNew && s_dlgFace > 0)
   {
      LPCWSTR name = PAL_DlgLogFaceName(s_dlgFace);
      s_dlgFace = 0;
      if (name != NULL && name != lpszText)
      {
         WCHAR a[64]; wcsncpy(a, name, 63); a[63] = 0;
         PAL_DlgLogAdd(1, a);
      }
   }
   n = (int)wcslen(lpszText);
   kind = (iKind == 1) ? L'N' : (s_dlgNew ? L'P' : L'T');
   s_dlgNew = FALSE;
   if (s_dlgLen + n + 2 > s_dlgCap)
   {
      int cap = s_dlgLen + n + 2 + 32768;
      WCHAR *p = (WCHAR *)realloc(s_dlgBuf, cap * sizeof(WCHAR));
      if (p == NULL) return;
      s_dlgBuf = p; s_dlgCap = cap;
   }
   s_dlgBuf[s_dlgLen++] = kind;
   memcpy(s_dlgBuf + s_dlgLen, lpszText, n * sizeof(WCHAR)); s_dlgLen += n;
   s_dlgBuf[s_dlgLen++] = L'\n';
   if (s_dlgLen > DLGLOG_MAX)
   {  /* drop the oldest quarter, up to the start of an entry */
      int k = s_dlgLen / 4;
      while (k < s_dlgLen && !(s_dlgBuf[k - 1] == L'\n' && (s_dlgBuf[k] == L'N' || s_dlgBuf[k] == L'P'))) k++;
      memmove(s_dlgBuf, s_dlgBuf + k, (s_dlgLen - k) * sizeof(WCHAR)); s_dlgLen -= k;
   }
}

VOID
PAL_DlgLogLoad(
   INT iSaveSlot
)
{
   FILE *fp; Uint32 hdr[2];
   s_dlgLen = 0; s_dlgNew = TRUE;
   if (iSaveSlot <= 0) return;
   fp = UTIL_OpenFileAtPath(gConfig.pszSavePath, PAL_va(1, "%d.dlg", iSaveSlot));
   if (fp == NULL) return;
   if (fread(hdr, 4, 2, fp) == 2 && hdr[0] == 0x31474C44 && hdr[1] <= DLGLOG_MAX + 4096)
   {
      if ((int)hdr[1] + 2 > s_dlgCap)
      {
         WCHAR *p = (WCHAR *)realloc(s_dlgBuf, (hdr[1] + 32768) * sizeof(WCHAR));
         if (p != NULL) { s_dlgBuf = p; s_dlgCap = (int)hdr[1] + 32768; }
      }
      if ((int)hdr[1] + 2 <= s_dlgCap && fread(s_dlgBuf, sizeof(WCHAR), hdr[1], fp) == hdr[1] &&
          (hdr[1] == 0 || s_dlgBuf[hdr[1] - 1] == L'\n'))
         s_dlgLen = (int)hdr[1];
   }
   fclose(fp);
}

VOID
PAL_DlgLogSave(
   INT iSaveSlot
)
{
   FILE *fp = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, PAL_va(1, "%d.dlg", iSaveSlot), "wb");
   Uint32 hdr[2];
   if (fp == NULL) return;
   hdr[0] = 0x31474C44; hdr[1] = (Uint32)s_dlgLen;   /* "DLG1" */
   fwrite(hdr, 4, 2, fp);
   if (s_dlgLen > 0) fwrite(s_dlgBuf, sizeof(WCHAR), s_dlgLen, fp);
   fclose(fp);
}

/* ---- panel ---- */
extern int  PAL_HDM_On(void);
extern int  PAL_HDM_TTFText(const uint16_t *wsz, int hx, int hy, int hpx, uint8_t bColor, int fDraw);
extern VOID PAL_HDM_BlitRGBA(const Uint32 *px, int w, int h, int hx, int hy);
extern void PAL_HDM_SnapSave(int i);
extern void PAL_HDM_SnapRestore(int i, int x, int y, int w, int h);
extern Uint32 g_dwHdTtfRGB;
extern VOID (*g_pfnMenuKeyHook)(VOID);

#define DLG_PX      40                /* body glyph height, HD pixels (game font = 96) */
#define DLG_PITCH   52                /* line pitch */
#define DLG_GAP     16                /* extra space above a speaker name */
#define DLG_TITLE   46

typedef struct { int start, len; BYTE name; } DLGROW;
static WCHAR  *s_dlgCh = NULL;  static BYTE *s_dlgCol = NULL;  static int s_dlgNCell = 0, s_dlgCellCap = 0;
static DLGROW *s_dlgRow = NULL; static int s_dlgNRow = 0, s_dlgRowCap = 0;
static int     s_dlgTop = 0, s_dlgMaxTop = 0;
static SDL_Rect s_dlgRect;                         /* panel, screen pixels (shadow included) */
static int     s_dlgTx, s_dlgTy, s_dlgTw, s_dlgTh; /* text area, HD pixels */
static SDL_Surface *s_dlgSnapA = NULL, *s_dlgSnapB = NULL;
static const Uint32 s_dlgRGB[5] = { 0xE6E2D6, 0xFFDC3C, 0xFF6A5A, 0x6ADCF0, 0xF2B84A };   /* text, yellow, red, cyan, name */
static const BYTE   s_dlgIdx[5] = { 0x4F, 0x2D, 0x1A, 0x8D, 0x2D };                       /* low-res fallback */

static int
PAL_DlgLogChW(
   WCHAR c
)
{
   static int cjk = -1; static short asc[128];
   WCHAR s[2]; s[0] = c; s[1] = 0;
   if (!PAL_HDM_On()) return PAL_CharWidth(c) * 6;
   if (c < 128) { if (asc[c] == 0) asc[c] = (short)(PAL_HDM_TTFText((const uint16_t *)s, 0, 0, DLG_PX, 0, 0) + 1); return asc[c] - 1; }
   if (c >= 0x2E80) { if (cjk < 0) { s[0] = 0x4E2D; cjk = PAL_HDM_TTFText((const uint16_t *)s, 0, 0, DLG_PX, 0, 0); } return cjk; }
   return PAL_HDM_TTFText((const uint16_t *)s, 0, 0, DLG_PX, 0, 0);
}

static VOID
PAL_DlgLogRun(
   const WCHAR *s, int n, int hx, int hy, int px, int col
)
{
   WCHAR buf[128];
   if (n <= 0) return;
   if (n > 127) n = 127;
   memcpy(buf, s, n * sizeof(WCHAR)); buf[n] = 0;
   if (PAL_HDM_On())
   {
      g_dwHdTtfRGB = 0xFF000000u | s_dlgRGB[col];
      PAL_HDM_TTFText((const uint16_t *)buf, hx, hy, px, 0, 1);
      g_dwHdTtfRGB = 0;
   }
   else
   {
      PAL_DrawText(buf, PAL_XY(hx / 6, hy / 6), s_dlgIdx[col], TRUE, FALSE, FALSE);
   }
}

static VOID
PAL_DlgLogAddRow(
   int start, int len, BOOL name
)
{
   if (s_dlgNRow >= s_dlgRowCap)
   {
      DLGROW *p = (DLGROW *)realloc(s_dlgRow, (s_dlgRowCap + 1024) * sizeof(DLGROW));
      if (p == NULL) return;
      s_dlgRow = p; s_dlgRowCap += 1024;
   }
   s_dlgRow[s_dlgNRow].start = start; s_dlgRow[s_dlgNRow].len = len; s_dlgRow[s_dlgNRow].name = (BYTE)name; s_dlgNRow++;
}

static VOID
PAL_DlgLogWrap(
   int from, int maxw
)
/* wrap the cells [from, s_dlgNCell) into text rows */
{
   int i = from, st = from, w = 0;
   for (; i < s_dlgNCell; i++)
   {
      int cw = PAL_DlgLogChW(s_dlgCh[i]);
      if (w + cw > maxw && i > st)
      {
         /* closing punctuation stays on its line (the scroll-bar margin has room for it) */
         WCHAR c = s_dlgCh[i];
         BOOL fClose = (c == 0xFF0C || c == 0x3002 || c == 0xFF01 || c == 0xFF1F || c == 0x3001 || c == 0xFF1A ||
                        c == 0xFF1B || c == 0x300D || c == 0x300F || c == 0xFF09 || c == 0xFF0E || c == L'.' ||
                        c == L'!' || c == L'?' || c == L',');
         if (!(fClose && w <= maxw)) { PAL_DlgLogAddRow(st, i - st, FALSE); st = i; w = 0; }
      }
      w += cw;
   }
   if (i > st) PAL_DlgLogAddRow(st, i - st, FALSE);
}

static int
PAL_DlgLogRowH(
   int r, int first
)
{
   if (s_dlgRow[r].name && s_dlgRow[r].len == 0) return (r != first) ? DLG_GAP : 0;   /* nameless entry: only the gap */
   return DLG_PITCH + ((s_dlgRow[r].name && r != first) ? DLG_GAP : 0);
}

static BOOL
PAL_DlgLogFits(
   int top
)
/* TRUE when the rows from top to the end fit into the text area */
{
   int y = 0, q = top;
   while (q < s_dlgNRow && y + PAL_DlgLogRowH(q, top) <= s_dlgTh) { y += PAL_DlgLogRowH(q, top); q++; }
   return q >= s_dlgNRow;
}

static VOID
PAL_DlgLogBuild(
   VOID
)
{
   int i = 0, entry = -1, lastName = -1, lastNameLen = 0, col = 0;
   int indent = PAL_DlgLogChW(0x4E2D);
   s_dlgNCell = 0; s_dlgNRow = 0; s_dlgMaxTop = 0;
   if (s_dlgCellCap < s_dlgLen + 16)
   {
      free(s_dlgCh); free(s_dlgCol);
      s_dlgCellCap = s_dlgLen + 16;
      s_dlgCh = (WCHAR *)malloc(s_dlgCellCap * sizeof(WCHAR)); s_dlgCol = (BYTE *)malloc(s_dlgCellCap);
      if (s_dlgCh == NULL || s_dlgCol == NULL) { free(s_dlgCh); free(s_dlgCol); s_dlgCh = NULL; s_dlgCol = NULL; s_dlgCellCap = 0; return; }
   }
   while (i < s_dlgLen)
   {
      WCHAR kind = s_dlgBuf[i++]; int st = i, len;
      while (i < s_dlgLen && s_dlgBuf[i] != L'\n') i++;
      len = i - st; i++;
      if (kind == L'N' || kind == L'P')
      {
         if (entry >= 0) PAL_DlgLogWrap(entry, s_dlgTw - indent);   /* flush the previous entry's text */
         entry = -1; col = 0;
      }
      if (kind == L'N')
      {
         /* the same speaker again: carry on under the name already shown */
         if (!(lastName >= 0 && lastNameLen == len && memcmp(s_dlgBuf + lastName, s_dlgBuf + st, len * sizeof(WCHAR)) == 0))
         {
            int c0 = s_dlgNCell, k;
            for (k = 0; k < len && k < 60; k++) { s_dlgCh[s_dlgNCell] = s_dlgBuf[st + k]; s_dlgCol[s_dlgNCell++] = 4; }
            PAL_DlgLogAddRow(c0, s_dlgNCell - c0, TRUE);
         }
         lastName = st; lastNameLen = len;
         entry = s_dlgNCell;
         continue;
      }
      if (kind == L'P') { lastName = -1; PAL_DlgLogAddRow(s_dlgNCell, 0, TRUE); }   /* nameless: just the gap */
      if (entry < 0) entry = s_dlgNCell;
      {
         int k; BOOL lead = (entry == s_dlgNCell);
         for (k = 0; k < len; k++)
         {
            WCHAR c = s_dlgBuf[st + k];
            if (c == L'-') { col = (col == 3) ? 0 : 3; continue; }
            if (c == 0x27 || c == L'@') { col = (col == 2) ? 0 : 2; continue; }
            if (c == 0x22) { col = (col == 1) ? 0 : 1; continue; }
            if (c == L'$') { k += 2; continue; }
            if (c == L'~') break;
            if (c == L'(' || c == L')') continue;
            if (c == 0x5C && k + 1 < len) c = s_dlgBuf[st + ++k];
            if (lead && (c == L' ' || c == 0x3000)) continue;
            lead = FALSE;
            s_dlgCh[s_dlgNCell] = c; s_dlgCol[s_dlgNCell++] = (BYTE)col;
         }
      }
   }
   if (entry >= 0) PAL_DlgLogWrap(entry, s_dlgTw - indent);

   /* last page: the smallest top row from which everything down to the end fits */
   s_dlgMaxTop = s_dlgNRow;
   while (s_dlgMaxTop > 0 && PAL_DlgLogFits(s_dlgMaxTop - 1)) s_dlgMaxTop--;
}

static VOID
PAL_DlgLogDraw(
   BOOL fFocus
)
{
   static const WCHAR szTitle[] = L"對話記錄";
   static const WCHAR szEmpty[] = L"（尚無對話記錄）";
   static const WCHAR szHint0[] = L"左右鍵／滾輪：翻頁";
   static const WCHAR szHint1[] = L"上下：滾動　翻頁鍵：翻頁　左：返回";
   int y, r, shown = 0;
   int indent = PAL_DlgLogChW(0x4E2D);

   VIDEO_CopySurface(s_dlgSnapB, &s_dlgRect, gpScreen, &s_dlgRect);
   PAL_HDM_SnapRestore(1, s_dlgRect.x, s_dlgRect.y, s_dlgRect.w, s_dlgRect.h);

   /* title, hint */
   PAL_DlgLogRun(PAL_Tr(szTitle), (int)wcslen(PAL_Tr(szTitle)), s_dlgTx, s_dlgTy - DLG_TITLE - 22, DLG_TITLE, fFocus ? 1 : 4);
   if (PAL_HDM_On())
   {
      const WCHAR *h = PAL_Tr(fFocus ? szHint1 : szHint0);
      int w = PAL_HDM_TTFText((const uint16_t *)h, 0, 0, 28, 0, 0);
      static Uint32 line[1920 * 3]; int k;
      g_dwHdTtfRGB = 0xFF000000u | (fFocus ? 0xC8C4B8 : 0x9A968C);
      PAL_HDM_TTFText((const uint16_t *)h, s_dlgTx + s_dlgTw - w, s_dlgTy - 22 - 34, 28, 0, 1);
      g_dwHdTtfRGB = 0;
      /* rule under the title */
      for (k = 0; k < s_dlgTw * 3 && k < 1920 * 3; k++) line[k] = (k < s_dlgTw || k >= s_dlgTw * 2) ? 0x60F2B84Au : 0xC0F2B84Au;
      PAL_HDM_BlitRGBA(line, s_dlgTw, 3, s_dlgTx, s_dlgTy - 14);
   }

   if (s_dlgNRow == 0)
   {
      PAL_DlgLogRun(PAL_Tr(szEmpty), (int)wcslen(PAL_Tr(szEmpty)), s_dlgTx + indent, s_dlgTy + 20, DLG_PX, 0);
   }
   y = 0;
   for (r = s_dlgTop; r < s_dlgNRow; r++)
   {
      int h = PAL_DlgLogRowH(r, s_dlgTop), x, k, st;
      if (y + h > s_dlgTh) break;
      y += h; shown++;
      x = s_dlgTx + (s_dlgRow[r].name ? 0 : indent);
      for (st = 0; st < s_dlgRow[r].len; st = k)
      {
         int c0 = s_dlgRow[r].start + st, w = 0;
         for (k = st; k < s_dlgRow[r].len && s_dlgCol[s_dlgRow[r].start + k] == s_dlgCol[c0]; k++) w += PAL_DlgLogChW(s_dlgCh[s_dlgRow[r].start + k]);
         PAL_DlgLogRun(s_dlgCh + c0, k - st, x, s_dlgTy + y - DLG_PITCH + (DLG_PITCH - DLG_PX) / 2, DLG_PX, s_dlgCol[c0]);
         x += w;
      }
   }
   if (PAL_HDM_On() && s_dlgNRow > shown && s_dlgNRow > 0)
   {  /* scroll bar */
      static Uint32 bar[8 * 1344]; int k, j, th = s_dlgTh < 1344 ? s_dlgTh : 1344;
      int t0 = th * s_dlgTop / s_dlgNRow, t1 = th * (s_dlgTop + shown) / s_dlgNRow;
      if (t1 - t0 < 24) t1 = t0 + 24;
      if (t1 > th) { t0 -= t1 - th; t1 = th; }
      for (j = 0; j < th; j++) for (k = 0; k < 8; k++)
         bar[j * 8 + k] = (j >= t0 && j < t1) ? (fFocus ? 0xFFF2B84Au : 0xB0F2B84Au) : 0x50000000u;
      PAL_HDM_BlitRGBA(bar, 8, th, s_dlgTx + s_dlgTw + 14, s_dlgTy);
   }
   VIDEO_UpdateScreen(NULL);
}

static VOID
PAL_DlgLogShow(
   VOID
)
{
   LPCBITMAPRLE b0 = PAL_SpriteGetFrame(gpSpriteUI, 0), b1 = PAL_SpriteGetFrame(gpSpriteUI, 4), b2 = PAL_SpriteGetFrame(gpSpriteUI, 2);
   int w0 = PAL_RLEGetWidth(b0), w1 = PAL_RLEGetWidth(b1), w2 = PAL_RLEGetWidth(b2);
   int h0 = PAL_RLEGetHeight(b0), h1 = PAL_RLEGetHeight(b1), h2 = PAL_RLEGetHeight(b2);
   /* UI tiles: 22x20 / 16x20 / 33x20 corners (the right ones carry a 10 px ornament), 22 / 16 / 23 x 18 sides */
   int wr = PAL_RLEGetWidth(PAL_SpriteGetFrame(gpSpriteUI, 5));
   const int X = 148, Y = 6, W = 172, H = 194;
   int cols = (W - 3 - w0 - w2) / (w1 > 0 ? w1 : 1), rows = (H - 3 - h0 - h2) / (h1 > 0 ? h1 : 1);

   if (s_dlgSnapA == NULL) s_dlgSnapA = VIDEO_CreateCompatibleSurface(gpScreen);
   if (s_dlgSnapB == NULL) s_dlgSnapB = VIDEO_CreateCompatibleSurface(gpScreen);
   if (s_dlgSnapA == NULL || s_dlgSnapB == NULL) return;

   s_dlgRect.x = X; s_dlgRect.y = Y;
   s_dlgRect.w = w0 + w2 + w1 * cols + 3; s_dlgRect.h = h0 + h2 + h1 * rows + 3;

   VIDEO_CopySurface(gpScreen, NULL, s_dlgSnapA, NULL);
   PAL_HDM_SnapSave(0);
   PAL_CreateBox(PAL_XY(X, Y), rows, cols, 0, FALSE);
   VIDEO_CopySurface(gpScreen, NULL, s_dlgSnapB, NULL);
   PAL_HDM_SnapSave(1);

   s_dlgTx = (X + 10) * 6; s_dlgTw = (w0 + w1 * cols + wr - 10 - 9) * 6 - 26;
   s_dlgTy = (Y + 9) * 6 + DLG_TITLE + 30; s_dlgTh = (Y + s_dlgRect.h - 3 - 9) * 6 - s_dlgTy;

   PAL_DlgLogBuild();
   s_dlgTop = s_dlgMaxTop;
   { extern int g_iMouseWheel; g_iMouseWheel = 0; }
   PAL_DlgLogDraw(FALSE);
}

static VOID
PAL_DlgLogHide(
   VOID
)
{
   if (s_dlgSnapA == NULL) return;
   VIDEO_CopySurface(s_dlgSnapA, &s_dlgRect, gpScreen, &s_dlgRect);
   PAL_HDM_SnapRestore(0, s_dlgRect.x, s_dlgRect.y, s_dlgRect.w, s_dlgRect.h);
   VIDEO_UpdateScreen(NULL);
}

static BOOL
PAL_DlgLogScroll(
   int d
)
{
   int t = s_dlgTop + d;
   if (t > s_dlgMaxTop) t = s_dlgMaxTop;
   if (t < 0) t = 0;
   if (t == s_dlgTop) return FALSE;
   s_dlgTop = t;
   return TRUE;
}

static VOID
PAL_DlgLogMenuHook(
   VOID
)
/* Keys of the in-game menu that belong to the log: LEFT / RIGHT and PgUp / PgDn page, Home / End jump,
   the mouse wheel scrolls three lines a notch.  (UP / DOWN stay with the menu.) */
{
   extern int g_iMouseWheel;
   DWORD k = g_InputState.dwKeyPress;
   const int page = 10;
   int d = 0;
   if (g_iMouseWheel != 0) { d = -3 * g_iMouseWheel; g_iMouseWheel = 0; }
   if (k & (kKeyPgUp | kKeyLeft)) d = -page;
   else if (k & (kKeyPgDn | kKeyRight)) d = page;
   else if (k & kKeyHome) d = -0x3FFFFFF;
   else if (k & kKeyEnd) d = 0x3FFFFFF;
   g_InputState.dwKeyPress &= ~(kKeyPgUp | kKeyPgDn | kKeyHome | kKeyEnd | kKeyLeft | kKeyRight);
   if (d != 0 && PAL_DlgLogScroll(d)) PAL_DlgLogDraw(FALSE);
}

static VOID
PAL_InGameMenu_OnItemChange(
   WORD        wCurrentItem
)
/*++
  Purpose:

    Callback function when user selected another item in the in-game menu.

  Parameters:

    [IN]  wCurrentItem - current selected item.

  Return value:

    None.

--*/
{
   gpGlobals->iCurMainMenuItem = wCurrentItem - 1;
}

VOID
PAL_InGameMenu(
   VOID
)
/*++
  Purpose:

    Show the in-game main menu.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   LPBOX                lpCashBox, lpMenuBox;
   WORD                 wReturnValue;
   
   // Fix render problem with shadow
   VIDEO_BackupScreen(gpScreen);

   //
   // Create menu items
   //
   MENUITEM        rgMainMenuItem[4] =
   {
      // value  label                      enabled   pos
      { 1,      GAMEMENU_LABEL_STATUS,     TRUE,     PAL_XY(16, 49) },
      { 2,      GAMEMENU_LABEL_MAGIC,      TRUE,     PAL_XY(16, 49 + 18) },
      { 3,      GAMEMENU_LABEL_INVENTORY,  TRUE,     PAL_XY(16, 49 + 36) },
      { 4,      GAMEMENU_LABEL_SYSTEM,     TRUE,     PAL_XY(16, 49 + 54) },
   };

   //
   // Display the cash amount.
   //
   lpCashBox = PAL_ShowCash(gpGlobals->dwCash);

   //
   // Create the menu box.
   //
   // Fix render problem with shadow
   lpMenuBox = PAL_CreateBox(PAL_XY(3, 37), 3, 3, 0, FALSE); /* Saturn BOX?(3,37,0,4,4) */

   PAL_DlgLogShow();   /* dialogue log on the right half */

   //
   // Process the menu
   //
   while (TRUE)
   {
      g_pfnMenuKeyHook = PAL_DlgLogMenuHook;
      wReturnValue = PAL_ReadMenu(PAL_InGameMenu_OnItemChange, rgMainMenuItem, 4,
         gpGlobals->iCurMainMenuItem, MENUITEM_COLOR);
      g_pfnMenuKeyHook = NULL;
      PAL_DlgLogHide();   /* the sub-menus use that part of the screen */

      if (wReturnValue == MENUITEM_VALUE_CANCELLED)
      {
         break;
      }

      switch (wReturnValue)
      {
      case 1:
         //
         // Status
         //
         PAL_PlayerStatus();
         goto out;

      case 2:
         //
         // Magic
         //
         PAL_InGameMagicMenu();
         goto out;

      case 3:
         //
         // Inventory
         //
         PAL_InventoryMenu();
         goto out;

      case 4:
         //
         // System
         //
         if (PAL_SystemMenu())
         {
            goto out;
         }
         PAL_DlgLogShow();
         break;
      }
   }

out:
   //
   // Remove the boxes.
   //
   PAL_DeleteBox(lpCashBox);
   PAL_DeleteBox(lpMenuBox);

   // Fix render problem with shadow
   VIDEO_RestoreScreen(gpScreen);
}

VOID
PAL_PlayerStatus(
   VOID
)
/*++
  Purpose:

    Show the player status.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   PAL_LARGE BYTE   bufBackground[320 * 200];
   PAL_LARGE BYTE   bufImage[PAL_RLEBUFSIZE];
   PAL_LARGE BYTE   bufImageBox[50 * 49];
   int              labels0[] = {
      STATUS_LABEL_EXP, STATUS_LABEL_LEVEL, STATUS_LABEL_HP,
      STATUS_LABEL_MP
   };
   int              labels1[] = {
      STATUS_LABEL_EXP_LAYOUT, STATUS_LABEL_LEVEL_LAYOUT, STATUS_LABEL_HP_LAYOUT,
      STATUS_LABEL_MP_LAYOUT
   };
   int              labels[] = {
      STATUS_LABEL_ATTACKPOWER, STATUS_LABEL_MAGICPOWER, STATUS_LABEL_RESISTANCE,
      STATUS_LABEL_DEXTERITY, STATUS_LABEL_FLEERATE
   };
   int              iCurrent;
   int              iPlayerRole;
   int              i, j;
   WORD             w;

   PAL_MKFDecompressChunk(bufBackground, 320 * 200, STATUS_BACKGROUND_FBPNUM, gpGlobals->f.fpFBP);
   iCurrent = 0;

   if (gConfig.fUseCustomScreenLayout)
   {
      for (i = 0; i < 49; i++)
      {
         memcpy(&bufImageBox[i * 50], &bufBackground[(i + 39) * 320 + 247], 50);
      }
      for (i = 0; i < 49; i++)
      {
         memcpy(&bufBackground[(i + 125) * 320 + 81], &bufBackground[(i + 125) * 320 + 81 - 50], 50);
         memcpy(&bufBackground[(i + 141) * 320 + 141], &bufBackground[(i + 141) * 320 + 81 - 50], 50);
         memcpy(&bufBackground[(i + 133) * 320 + 201], &bufBackground[(i + 133) * 320 + 81 - 50], 50);
         memcpy(&bufBackground[(i + 101) * 320 + 251], &bufBackground[(i + 101) * 320 + 81 - 50], 50);
         memcpy(&bufBackground[(i + 39) * 320 + 247], &bufBackground[(i + 39) * 320 + 189 - 50], 50);
         if (i > 0) memcpy(&bufBackground[(i - 1) * 320 + 189], &bufBackground[(i - 1) * 320 + 189 - 50], 50);
      }
      for(i = 0; i < MAX_PLAYER_EQUIPMENTS; i++)
      {
         short x = PAL_X(gConfig.ScreenLayout.RoleEquipImageBoxes[i]);
         short y = PAL_Y(gConfig.ScreenLayout.RoleEquipImageBoxes[i]);
         short sx = (x < 0) ? -x : 0, sy = (y < 0) ? -y : 0, d = (x > 270) ? x - 270 : 0;
         if (sx >= 50 || sy >= 49 || x >= 320 || y >= 200) continue;
         for (; sy < 49 && y + sy < 200; sy++)
         {
            memcpy(&bufBackground[(y + sy) * 320 + x + sx], &bufImageBox[sy * 50 + sx], 50 - sx - d);
         }
      }
   }

   while (iCurrent >= 0 && iCurrent <= gpGlobals->wMaxPartyMemberIndex)
   {
      iPlayerRole = gpGlobals->rgParty[iCurrent].wPlayerRole;

      //
      // Draw the background image
      //
      PAL_FBPBlitToSurface(bufBackground, gpScreen);

      //
      // Draw the image of player role
      //
      if (PAL_MKFReadChunk(bufImage, PAL_RLEBUFSIZE, gpGlobals->g.PlayerRoles.rgwAvatar[iPlayerRole], gpGlobals->f.fpRGM) > 0)
      {
         /* Saturn 0x0604b690: portrait centred */
         PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY(PAL_X(gConfig.ScreenLayout.RoleImage) - PAL_RLEGetWidth(bufImage) / 2,
            PAL_Y(gConfig.ScreenLayout.RoleImage) - PAL_RLEGetHeight(bufImage) / 2));
      }

      //
      // Draw the equipments
      //
      for (i = 0; i < MAX_PLAYER_EQUIPMENTS; i++)
      {
         int offset;

         w = gpGlobals->g.PlayerRoles.rgwEquipment[i][iPlayerRole];

         if (w == 0)
         {
            continue;
         }

         //
         // Draw the image
         //
         if (PAL_MKFReadChunk(bufImage, PAL_RLEBUFSIZE,
            gpGlobals->g.rgObject[w].item.wBitmap, gpGlobals->f.fpBALL) > 0)
         {
            PAL_RLEBlitToSurface(bufImage, gpScreen,
               PAL_XY_OFFSET(gConfig.ScreenLayout.RoleEquipImageBoxes[i], 1, 1));
         }

         //
         // Draw the text label
         //
         offset = PAL_WordWidth(w) * 16;
         if (PAL_X(gConfig.ScreenLayout.RoleEquipNames[i]) + offset > 320)
         {
            offset = 320 - PAL_X(gConfig.ScreenLayout.RoleEquipNames[i]) - offset;
         }
         else
         {
            offset = 0;
         }
         int index = &gConfig.ScreenLayout.RoleEquipNames[i] - gConfig.ScreenLayoutArray;
         BOOL fShadow = (gConfig.ScreenLayoutFlag[index] & DISABLE_SHADOW) ? FALSE : TRUE;
         BOOL fUse8x8Font = (gConfig.ScreenLayoutFlag[index] & USE_8x8_FONT) ? TRUE : FALSE;
         (void)offset;
         /* Saturn 0x0604b872: name centred on image box x+25, y = box y + 43 */
         if (PAL_LangGet() == 3)
         {
            /* English: names are up to 14 letters. At most 72 pixels wide (squeezed when longer), kept inside the
               screen; the names of the two neighbouring bottom boxes are moved apart so they cannot touch. */
            extern void PAL_DrawTextFit(const wchar_t *text, PAL_POS pos, unsigned char color, int shadow, int maxw);
            int tw = PAL_TextWidth(PAL_GetWord(w));
            int bx = PAL_X(gConfig.ScreenLayout.RoleEquipImageBoxes[i]), by = PAL_Y(gConfig.ScreenLayout.RoleEquipImageBoxes[i]);
            int tx;
            if (tw > 72) tw = 72;
            tx = bx + 25 - tw / 2;
            if (by > 120 && bx > 120 && bx < 180) tx -= 6;        /* feet */
            else if (by > 120 && bx >= 180) tx += 6;              /* hand */
            if (tx + tw > 318) tx = 318 - tw;
            if (tx < 2) tx = 2;
            PAL_DrawTextFit(PAL_GetWord(w), PAL_XY(tx, by + 43), STATUS_COLOR_EQUIPMENT, fShadow, 72);
         }
         else
         PAL_DrawText(PAL_GetWord(w), PAL_XY(PAL_X(gConfig.ScreenLayout.RoleEquipImageBoxes[i]) + 25 - PAL_TextWidth(PAL_GetWord(w)) / 2,
            PAL_Y(gConfig.ScreenLayout.RoleEquipImageBoxes[i]) + 43), STATUS_COLOR_EQUIPMENT, fShadow, FALSE, fUse8x8Font);
      }

      //
      // Draw the text labels
      //
      for (i = 0; i < sizeof(labels0) / sizeof(int); i++)
      {
         int index = labels1[i];
         BOOL fShadow = (gConfig.ScreenLayoutFlag[index] & DISABLE_SHADOW) ? FALSE : TRUE;
         BOOL fUse8x8Font = (gConfig.ScreenLayoutFlag[index] & USE_8x8_FONT) ? TRUE : FALSE;
         PAL_DrawText(PAL_GetWord(labels0[i]), *(&gConfig.ScreenLayout.RoleExpLabel+i), MENUITEM_COLOR, fShadow, FALSE, fUse8x8Font);
      }
      for (i = 0; i < sizeof(labels) / sizeof(int); i++)
      {
         int index = &gConfig.ScreenLayout.RoleStatusLabels[i] - gConfig.ScreenLayoutArray;
         BOOL fShadow = (gConfig.ScreenLayoutFlag[index] & DISABLE_SHADOW) ? FALSE : TRUE;
         BOOL fUse8x8Font = (gConfig.ScreenLayoutFlag[index] & USE_8x8_FONT) ? TRUE : FALSE;
         PAL_DrawText(PAL_GetWord(labels[i]), gConfig.ScreenLayout.RoleStatusLabels[i], MENUITEM_COLOR, fShadow, FALSE, fUse8x8Font);
      }

      PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[iPlayerRole]),
         gConfig.ScreenLayout.RoleName, MENUITEM_COLOR, TRUE, FALSE, FALSE);   /* Saturn: same colour as the labels (emulator) */

      //
      // Draw the stats
      //
      if (gConfig.ScreenLayout.RoleExpSlash != 0)
	  {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen, gConfig.ScreenLayout.RoleExpSlash);
      }
      if (gConfig.ScreenLayout.RoleHPSlash != 0)
	  {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen, gConfig.ScreenLayout.RoleHPSlash);
      }
      if (gConfig.ScreenLayout.RoleMPSlash != 0)
	  {
         PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen, gConfig.ScreenLayout.RoleMPSlash);
      }

      PAL_DrawNumber(gpGlobals->Exp.rgPrimaryExp[iPlayerRole].wExp, 5,
         gConfig.ScreenLayout.RoleCurrExp, kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.rgLevelUpExp[gpGlobals->g.PlayerRoles.rgwLevel[iPlayerRole]], 5,
         gConfig.ScreenLayout.RoleNextExp, kNumColorBlue, kNumAlignRight); /* Saturn colour 1 */
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwLevel[iPlayerRole], 2,
         gConfig.ScreenLayout.RoleLevel, kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwHP[iPlayerRole], 4,
         gConfig.ScreenLayout.RoleCurHP, kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMaxHP[iPlayerRole], 4,
         gConfig.ScreenLayout.RoleMaxHP, kNumColorBlue, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMP[iPlayerRole], 4,
         gConfig.ScreenLayout.RoleCurMP, kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMaxMP[iPlayerRole], 4,
         gConfig.ScreenLayout.RoleMaxMP, kNumColorBlue, kNumAlignRight);

      PAL_DrawNumber(PAL_GetPlayerAttackStrength(iPlayerRole), 4,
         gConfig.ScreenLayout.RoleStatusValues[0], kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerMagicStrength(iPlayerRole), 4,
         gConfig.ScreenLayout.RoleStatusValues[1], kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDefense(iPlayerRole), 4,
         gConfig.ScreenLayout.RoleStatusValues[2], kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDexterity(iPlayerRole), 4,
         gConfig.ScreenLayout.RoleStatusValues[3], kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerFleeRate(iPlayerRole), 4,
         gConfig.ScreenLayout.RoleStatusValues[4], kNumColorYellow, kNumAlignRight);

      //
      // Draw all poisons
      //
      for (i = j = 0; i < MAX_POISONS; i++)
      {
         w = gpGlobals->rgPoisonStatus[i][iCurrent].wPoisonID;

         if (w != 0 && gpGlobals->g.rgObject[w].poison.wPoisonLevel <= 3)
         {
            /* Saturn 0x0604b99a: poison colour code -> palette index */
            static const WORD code[] = { 0x80, 0xa0, 0x50, 0x60, 0xc0, 0xb0, 0x10, 0x40, 0x21, 0xe0 };
            static const BYTE col[]  = { 181,  172,  203,  151,  177,  193,  188,  5,    42,   47 };
            BYTE c = (BYTE)(gpGlobals->g.rgObject[w].poison.wColor + 10);
            int k;
            for (k = 0; k < 10; k++) if (gpGlobals->g.rgObject[w].poison.wColor == code[k]) c = col[k];
            PAL_DrawText(PAL_GetWord(w), gConfig.ScreenLayout.RolePoisonNames[j++], c, TRUE, FALSE, FALSE);
         }
      }

      //
      // Update the screen
      //
      VIDEO_UpdateScreen(NULL);

      //
      // Wait for input
      //
      PAL_ClearKeyState();

      while (TRUE)
      {
         UTIL_Delay(1);

         if (g_InputState.dwKeyPress & kKeyMenu)
         {
            iCurrent = -1;
            break;
         }
         else if (g_InputState.dwKeyPress & (kKeyLeft | kKeyUp))
         {
            iCurrent--;
            break;
         }
         else if (g_InputState.dwKeyPress & (kKeyRight | kKeyDown | kKeySearch))
         {
            iCurrent++;
            break;
         }
      }
   }
}

WORD
PAL_ItemUseMenu(
   WORD           wItemToUse
)
/*++
  Purpose:

    Show the use item menu.

  Parameters:

    [IN]  wItemToUse - the object ID of the item to use.

  Return value:

    The selected player to use the item onto.
    MENUITEM_VALUE_CANCELLED if user cancelled.

--*/
{
   BYTE           bColor, bSelectedColor;
   PAL_LARGE BYTE bufImage[2048];
   DWORD          dwColorChangeTime;
   static SHORT   sSelectedPlayer = 0;
   SDL_Rect       rect = {110, 2, 200, 180};
   int            i;

   bSelectedColor = MENUITEM_COLOR_SELECTED_FIRST;
   dwColorChangeTime = 0;

   while (TRUE)
   {
      if (sSelectedPlayer > gpGlobals->wMaxPartyMemberIndex)
      {
         sSelectedPlayer = 0;
      }

      //
      // Draw the box
      //
      PAL_CreateBox(PAL_XY(110, 2), 7, 9, 0, FALSE);

      //
      // Draw the stats of the selected player
      //
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_LEVEL), PAL_XY(200, 16),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_HP), PAL_XY(200, 34),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_MP), PAL_XY(200, 52),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_ATTACKPOWER), PAL_XY(200, 70),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_MAGICPOWER), PAL_XY(200, 88),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_RESISTANCE), PAL_XY(200, 106),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_DEXTERITY), PAL_XY(200, 124),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);
      PAL_DrawText(PAL_GetWord(STATUS_LABEL_FLEERATE), PAL_XY(200, 142),
         ITEMUSEMENU_COLOR_STATLABEL, TRUE, FALSE, FALSE);

      i = gpGlobals->rgParty[sSelectedPlayer].wPlayerRole;

      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwLevel[i], 4, PAL_XY(240, 20),
         kNumColorYellow, kNumAlignRight);

      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen,
         PAL_XY(263, 38));
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMaxHP[i], 4,
         PAL_XY(261, 40), kNumColorBlue, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwHP[i], 4,
         PAL_XY(240, 37), kNumColorYellow, kNumAlignRight);

      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_SLASH), gpScreen,
         PAL_XY(263, 56));
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMaxMP[i], 4,
         PAL_XY(261, 58), kNumColorBlue, kNumAlignRight);
      PAL_DrawNumber(gpGlobals->g.PlayerRoles.rgwMP[i], 4,
         PAL_XY(240, 55), kNumColorYellow, kNumAlignRight);

      PAL_DrawNumber(PAL_GetPlayerAttackStrength(i), 4, PAL_XY(240, 74),
         kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerMagicStrength(i), 4, PAL_XY(240, 92),
         kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDefense(i), 4, PAL_XY(240, 110),
         kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDexterity(i), 4, PAL_XY(240, 128),
         kNumColorYellow, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerFleeRate(i), 4, PAL_XY(240, 146),
         kNumColorYellow, kNumAlignRight);

      //
      // Draw the names of the players in the party
      //
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         if (i == sSelectedPlayer)
         {
            bColor = bSelectedColor;
         }
         else
         {
            bColor = MENUITEM_COLOR;
         }

         PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[gpGlobals->rgParty[i].wPlayerRole]),
            PAL_XY(125, 16 + 20 * i), bColor, TRUE, FALSE, FALSE);
      }

      PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX), gpScreen,
         PAL_XY(120, 80));

      i = PAL_GetItemAmount(wItemToUse);

      if (i > 0)
      {
         //
         // Draw the picture of the item
         //
         if (PAL_MKFReadChunk(bufImage, 2048,
            gpGlobals->g.rgObject[wItemToUse].item.wBitmap, gpGlobals->f.fpBALL) > 0)
         {
            PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY(127, 88));
         }

         //
         // Draw the amount and label of the item
         //
         PAL_DrawText(PAL_GetWord(wItemToUse), PAL_XY(116, 143), STATUS_COLOR_EQUIPMENT, TRUE, FALSE, FALSE);
         PAL_DrawNumber(i, 2, PAL_XY(170, 133), kNumColorCyan, kNumAlignRight);
      }

      //
      // Update the screen area
      //
      VIDEO_UpdateScreen(&rect);

      //
      // Wait for key
      //
      PAL_ClearKeyState();

      while (TRUE)
      {
         //
         // See if we should change the highlight color
         //
         if (SDL_TICKS_PASSED(SDL_GetTicks(), dwColorChangeTime))
         {
            if ((WORD)bSelectedColor + 1 >=
               (WORD)MENUITEM_COLOR_SELECTED_FIRST + MENUITEM_COLOR_SELECTED_TOTALNUM)
            {
               bSelectedColor = MENUITEM_COLOR_SELECTED_FIRST;
            }
            else
            {
               bSelectedColor++;
            }

            dwColorChangeTime = SDL_GetTicks() + (600 / MENUITEM_COLOR_SELECTED_TOTALNUM);

            //
            // Redraw the selected item.
            //
            PAL_DrawText(
               PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[gpGlobals->rgParty[sSelectedPlayer].wPlayerRole]),
               PAL_XY(125, 16 + 20 * sSelectedPlayer), bSelectedColor, FALSE, TRUE, FALSE);
         }

         PAL_ProcessEvent();

         if (g_InputState.dwKeyPress != 0)
         {
            break;
         }

         SDL_Delay(1);
      }

      if (i <= 0)
      {
         return MENUITEM_VALUE_CANCELLED;
      }

      if (g_InputState.dwKeyPress & (kKeyUp | kKeyLeft))
      {
         sSelectedPlayer--;
         if (sSelectedPlayer < 0)
         {
            sSelectedPlayer = gpGlobals->wMaxPartyMemberIndex;
         }
      }
      else if (g_InputState.dwKeyPress & (kKeyDown | kKeyRight))
      {
         sSelectedPlayer++;
         if (sSelectedPlayer > gpGlobals->wMaxPartyMemberIndex)
         {
            sSelectedPlayer = 0;
         }
      }
      else if (g_InputState.dwKeyPress & kKeyMenu)
      {
         break;
      }
      else if (g_InputState.dwKeyPress & kKeySearch)
      {
         return gpGlobals->rgParty[sSelectedPlayer].wPlayerRole;
      }
   }

   return MENUITEM_VALUE_CANCELLED;
}

/* Port (user request): floating description of the highlighted shop item. The Saturn buy menu shows none;
   the text is the item's own description script (the lines the inventory menu shows), drawn in the small
   HD font on a panel between the item picture and the amount box. */
static VOID
PAL_BuyMenu_DrawDesc(
   WORD           wItem
)
{
   extern int PAL_HDM_On(void);
   extern VOID PAL_HDM_BlitRGBA(const Uint32 *px, int w, int h, int hx, int hy);
   enum { PX = 3, PY = 77, PW = 142, PH = 46, S = 6 };
   static Uint32 *s_panel = NULL;
   WORD  e = gpGlobals->g.rgObject[wItem].item.wScriptDesc;
   int   line = 0, n, x, y;

   if (!PAL_HDM_On()) return;

   if (s_panel == NULL)
   {
      s_panel = (Uint32 *)malloc((size_t)PW * S * PH * S * 4);
      if (s_panel == NULL) return;
      for (y = 0; y < PH * S; y++)
      {
         for (x = 0; x < PW * S; x++)
         {
            int d = x; if (y < d) d = y; if (PW * S - 1 - x < d) d = PW * S - 1 - x; if (PH * S - 1 - y < d) d = PH * S - 1 - y;
            s_panel[y * PW * S + x] = (d < 3) ? 0xFF2A1808u : (d < 7) ? 0xFFC8A060u : (d < 9) ? 0xFF2A1808u : 0xFF1C1410u;
         }
      }
   }
   PAL_HDM_BlitRGBA(s_panel, PW * S, PH * S, PX * S, PY * S);

   for (n = 0; e != 0 && n < 16 && gpGlobals->g.lprgScriptEntry[e].wOperation != 0; n++, e++)
   {
      if (gpGlobals->g.lprgScriptEntry[e].wOperation == 0xFFFF && line < 5)
      {
         LPCWSTR p = PAL_GetMsg(gpGlobals->g.lprgScriptEntry[e].rgwOperand[0]);
         while (p != NULL && *p && *p != 1 && line < 5)   /* translated text: several lines in one message */
         {
            WCHAR part[128]; LPCWSTR q = wcschr(p, '\n');
            size_t l = q ? (size_t)(q - p) : wcslen(p);
            if (l > 127) l = 127;
            memcpy(part, p, l * sizeof(WCHAR)); part[l] = 0;
            PAL_DrawSmallText(part, PX + 5, PY + 2 + line * (PAL_LangGet() >= 2 ? 9 : 10), (PAL_LangGet() >= 2) ? 8 : 9, MENUITEM_COLOR, FALSE);   /* Japanese / English: up to five lines */
            line++;
            if (q == NULL) break;
            p = q + 1;
         }
      }
   }
}

static VOID
PAL_BuyMenu_OnItemChange(
   WORD           wCurrentItem
)
/*++
  Purpose:

    Callback function which is called when player selected another item
    in the buy menu.

  Parameters:

    [IN]  wCurrentItem - current item on the menu, indicates the object ID of
                         the currently selected item.

  Return value:

    None.

--*/
{
   const SDL_Rect      rect = {0, 0, 320, 200};
   int                 i, j, n, iPlayerID, x, y;
   PAL_LARGE BYTE      bufImage[2048];

   //
   // Prepare item bakcground box pos
   //
   x = 40, y = 8;

   if( __buymenu_firsttime_render )
      PAL_RLEBlitToSurfaceWithShadow(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX), gpScreen, PAL_XY(x + 3, y + 3), TRUE);
   //
   // Draw the picture of current selected item
   //
   PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX), gpScreen,
      PAL_XY(x, y));

   //
   // Prepare item pos
   //
   x = 48, y = 15;

   if (PAL_MKFReadChunk(bufImage, 2048,
      gpGlobals->g.rgObject[wCurrentItem].item.wBitmap, gpGlobals->f.fpBALL) > 0)
   {
      PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY(x, y));
   }

   //
   // See how many of this item we have in the inventory
   //
   n = 0;

   for (i = 0; i < MAX_INVENTORY; i++)
   {
      if (gpGlobals->rgInventory[i].wItem == 0)
      {
         break;
      }
      else if (gpGlobals->rgInventory[i].wItem == wCurrentItem)
      {
         n = gpGlobals->rgInventory[i].nAmount;
         break;
      }
   }

   for (i = 0; i < MAX_PLAYER_EQUIPMENTS; i++)
   {
      for (j = 0; j <= gpGlobals->wMaxPartyMemberIndex; j++)
      {
         iPlayerID = gpGlobals->rgParty[j].wPlayerRole;

         if (gpGlobals->g.PlayerRoles.rgwEquipment[i][iPlayerID] == wCurrentItem) n++;
      }
   }

   //
   // Prepare inventory quantities pos
   //
   x = 20, y = 124;   /* Saturn: 100; moved down for the description panel */

   if( __buymenu_firsttime_render )
      PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 6);
   else
   //
   // Draw the amount of this item in the inventory
   //
   PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 0);
   PAL_DrawText(PAL_GetWord(BUYMENU_LABEL_CURRENT), PAL_XY(x + 10, y + 10), 0, FALSE, FALSE, FALSE);
   PAL_DrawNumberZeroPad(n, 6, PAL_XY(x + 48, y + 15)); /* Saturn 0x0604bcd4 */

   //
   // Prepare inventory quantities pos
   //
   x = 20, y = 162;   /* Saturn: 141 */

   if( __buymenu_firsttime_render )
      PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 6);
   else
   //
   // Draw the cash amount
   //
   PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 0);
   PAL_DrawText(PAL_GetWord(CASH_LABEL), PAL_XY(x + 10, y + 10), 0, FALSE, FALSE, FALSE);
   PAL_DrawNumberZeroPad(gpGlobals->dwCash, 6, PAL_XY(x + 48, y + 15));

   PAL_BuyMenu_DrawDesc(wCurrentItem);

   VIDEO_UpdateScreen(&rect);
   
   __buymenu_firsttime_render = FALSE;
}

VOID
PAL_BuyMenu(
   WORD           wStoreNum
)
/*++
  Purpose:

    Show the buy item menu.

  Parameters:

    [IN]  wStoreNum - number of the store to buy items from.

  Return value:

    None.

--*/
{
   MENUITEM        rgMenuItem[MAX_STORE_ITEM];
   int             i, y;
   WORD            w;

   //
   // create the menu items
   //
   y = 21;

   for (i = 0; i < MAX_STORE_ITEM; i++)
   {
      if (gpGlobals->g.lprgStore[wStoreNum].rgwItems[i] == 0)
      {
         break;
      }

      rgMenuItem[i].wValue = gpGlobals->g.lprgStore[wStoreNum].rgwItems[i];
      rgMenuItem[i].wNumWord = gpGlobals->g.lprgStore[wStoreNum].rgwItems[i];
      rgMenuItem[i].fEnabled = TRUE;
      rgMenuItem[i].pos = PAL_XY(176, y);

      y += 18;
   }

   //
   // Draw the box
   //
   PAL_CreateBox(PAL_XY(148, 8), 8, 8, 1, FALSE); /* Saturn BOX?(122,8,9,9,9); 26 to the right: room for the description panel */

   //
   // Draw the number of prices
   //
   for (y = 0; y < i; y++)
   {
      w = gpGlobals->g.rgObject[rgMenuItem[y].wValue].item.wPrice;
      PAL_DrawNumber(w, 6, PAL_XY(258, 26 + y * 18), kNumColorYellow, kNumAlignRight);
   }

   w = 0;
   __buymenu_firsttime_render = TRUE;

   while (TRUE)
   {
      { extern int g_iMenuItemMaxW; g_iMenuItemMaxW = 84; }   /* names end before the price column */
      w = PAL_ReadMenu(PAL_BuyMenu_OnItemChange, rgMenuItem, i, w, MENUITEM_COLOR);
      { extern int g_iMenuItemMaxW; g_iMenuItemMaxW = 0; }

      if (w == MENUITEM_VALUE_CANCELLED)
      {
         break;
      }

      if (gpGlobals->g.rgObject[w].item.wPrice <= gpGlobals->dwCash &&
         PAL_GetItemAmount(w) < 99)   /* port fix: a 100th item was paid for and then dropped by the 99 cap */
      {
         BOOL fBuy;
         /* user request: lower centre of the screen (Saturn: y = 70, across the middle list rows) */
         s_rgConfirmAt[0] = PAL_XY(120, 140); s_rgConfirmAt[1] = PAL_XY(210, 140); s_fConfirmAt = TRUE;
         fBuy = PAL_ConfirmMenu();
         s_fConfirmAt = FALSE;
         if (fBuy)
         {
            //
            // Player bought an item
            //
            gpGlobals->dwCash -= gpGlobals->g.rgObject[w].item.wPrice;
            PAL_AddItemToInventory(w, 1);
         }
      }

      //
      // Place the cursor to the current item on next loop
      //
      for (y = 0; y < i; y++)
      {
         if (w == rgMenuItem[y].wValue)
         {
            w = y;
            break;
         }
      }
   }
}

static VOID
PAL_SellMenu_OnItemChange(
   WORD         wCurrentItem
)
/*++
  Purpose:

    Callback function which is called when player selected another item
    in the sell item menu.

  Parameters:

    [IN]  wCurrentItem - current item on the menu, indicates the object ID of
                         the currently selected item.

  Return value:

    None.

--*/
{
   /* Saturn 0x060506d8 / 0x0604ead2: cash box (220,135), price box (220,170), zero-padded digits */
   WORD x = 220, y = 135;
   PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 0);
   PAL_DrawText(PAL_GetWord(CASH_LABEL), PAL_XY(x + 10, y + 10), 0, FALSE, FALSE, FALSE);
   PAL_DrawNumberZeroPad(gpGlobals->dwCash, 6, PAL_XY(x + 48, y + 15));

   y = 170;
   PAL_CreateSingleLineBoxWithShadow(PAL_XY(x, y), 5, FALSE, 0);
   if (gpGlobals->g.rgObject[wCurrentItem].item.wFlags & kItemFlagSellable)
   {
      PAL_DrawText(PAL_GetWord(SELLMENU_LABEL_PRICE), PAL_XY(x + 10, y + 10), 0, FALSE, FALSE, FALSE);
      PAL_DrawNumberZeroPad(gpGlobals->g.rgObject[wCurrentItem].item.wPrice / 2, 6, PAL_XY(x + 48, y + 15));
   }
}

VOID
PAL_SellMenu(
   VOID
)
/*++
  Purpose:

    Show the sell item menu.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   WORD      w;

   while (TRUE)
   {
      w = PAL_ItemSelectMenu(PAL_SellMenu_OnItemChange, kItemFlagSellable);
      if (w == 0)
      {
         break;
      }

      if (PAL_ConfirmMenu())
      {
         if (PAL_AddItemToInventory(w, -1))
         {
            gpGlobals->dwCash += gpGlobals->g.rgObject[w].item.wPrice / 2;
         }
      }
   }
}

VOID
PAL_EquipItemMenu(
   WORD        wItem
)
/*++
  Purpose:

    Show the menu which allow players to equip the specified item.

  Parameters:

    [IN]  wItem - the object ID of the item.

  Return value:

    None.

--*/
{
   PAL_LARGE BYTE   bufBackground[320 * 200];
   PAL_LARGE BYTE   bufImageBox[72 * 72];
   PAL_LARGE BYTE   bufImage[2048];
   WORD             w;
   int              iCurrentPlayer, i;
   BYTE             bColor, bSelectedColor;
   DWORD            dwColorChangeTime;

   gpGlobals->wLastUnequippedItem = wItem;

   PAL_MKFDecompressChunk(bufBackground, 320 * 200, EQUIPMENU_BACKGROUND_FBPNUM,
      gpGlobals->f.fpFBP);

   if (gConfig.fUseCustomScreenLayout)
   {
      int x = PAL_X(gConfig.ScreenLayout.EquipImageBox);
      int y = PAL_Y(gConfig.ScreenLayout.EquipImageBox);
      for (i = 8; i < 72; i++)
      {
         memcpy(&bufBackground[i * 320 + 92], &bufBackground[(i + 128) * 320 + 92], 32);
         memcpy(&bufBackground[(i + 64) * 320 + 92], &bufBackground[(i + 128) * 320 + 92], 32);
      }
      for (i = 9; i < 90; i++)
      {
         memcpy(&bufBackground[i * 320 + 226], &bufBackground[(i + 104) * 320 + 226], 32);
      }
      for (i = 99; i < 113; i++)
      {
         memcpy(&bufBackground[i * 320 + 226], &bufBackground[(i + 16) * 320 + 226], 32);
      }
      for (i = 8; i < 80; i++)
      {
         memcpy(&bufImageBox[(i - 8) * 72], &bufBackground[i * 320 + 8], 72);
         memcpy(&bufBackground[i * 320 + 8], &bufBackground[(i + 72) * 320 + 8], 72);
      }
      for (i = 0; i < 72; i++)
      {
         memcpy(&bufBackground[(i + y) * 320 + x], &bufImageBox[i * 72], 72);
      }
   }

   iCurrentPlayer = 0;
   bSelectedColor = MENUITEM_COLOR_SELECTED_FIRST;
   dwColorChangeTime = SDL_GetTicks() + (600 / MENUITEM_COLOR_SELECTED_TOTALNUM);

   while (TRUE)
   {
      wItem = gpGlobals->wLastUnequippedItem;

      //
      // Draw the background
      //
      PAL_FBPBlitToSurface(bufBackground, gpScreen);

      //
      // Draw the item picture
      //
      if (PAL_MKFReadChunk(bufImage, 2048,
         gpGlobals->g.rgObject[wItem].item.wBitmap, gpGlobals->f.fpBALL) > 0)
      {
         PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY_OFFSET(gConfig.ScreenLayout.EquipImageBox, 8, 8));
      }

      if (gConfig.fUseCustomScreenLayout)
      {
         int labels1[] = { STATUS_LABEL_ATTACKPOWER, STATUS_LABEL_MAGICPOWER, STATUS_LABEL_RESISTANCE, STATUS_LABEL_DEXTERITY, STATUS_LABEL_FLEERATE };
         int labels2[] = { EQUIP_LABEL_HEAD, EQUIP_LABEL_SHOULDER, EQUIP_LABEL_BODY, EQUIP_LABEL_HAND, EQUIP_LABEL_FOOT, EQUIP_LABEL_NECK };
		 for (i = 0; i < sizeof(labels1) / sizeof(int); i++)
         {
            int index = &gConfig.ScreenLayout.EquipStatusLabels[i] - gConfig.ScreenLayoutArray;
            BOOL fShadow = (gConfig.ScreenLayoutFlag[index] & DISABLE_SHADOW) ? FALSE : TRUE;
            BOOL fUse8x8Font = (gConfig.ScreenLayoutFlag[index] & USE_8x8_FONT) ? TRUE : FALSE;
            PAL_DrawText(PAL_GetWord(labels1[i]), gConfig.ScreenLayoutArray[index], MENUITEM_COLOR, fShadow, FALSE, fUse8x8Font);
         }
		 for (i = 0; i < sizeof(labels2) / sizeof(int); i++)
         {
            int index = &gConfig.ScreenLayout.EquipLabels[i] - gConfig.ScreenLayoutArray;
            BOOL fShadow = (gConfig.ScreenLayoutFlag[index] & DISABLE_SHADOW) ? FALSE : TRUE;
            BOOL fUse8x8Font = (gConfig.ScreenLayoutFlag[index] & USE_8x8_FONT) ? TRUE : FALSE;
            PAL_DrawText(PAL_GetWord(labels2[i]), gConfig.ScreenLayoutArray[index], MENUITEM_COLOR, fShadow, FALSE, fUse8x8Font);
         }
      }

      //
      // Draw the current equipment of the selected player
      //
      w = gpGlobals->rgParty[iCurrentPlayer].wPlayerRole;
      for (i = 0; i < MAX_PLAYER_EQUIPMENTS; i++)
      {
         if (gpGlobals->g.PlayerRoles.rgwEquipment[i][w] != 0)
         {
            {
               extern void PAL_DrawTextFit(const wchar_t *text, PAL_POS pos, unsigned char color, int shadow, int maxw);
               PAL_DrawTextFit(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwEquipment[i][w]),
                  gConfig.ScreenLayout.EquipNames[i], MENUITEM_COLOR, TRUE, 92);   /* the stat labels start 95 pixels to the right */
            }
         }
      }

      //
      // Draw the stats of the currently selected player
      //
      PAL_DrawNumber(PAL_GetPlayerAttackStrength(w), 4, gConfig.ScreenLayout.EquipStatusValues[0], kNumColorCyan, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerMagicStrength(w), 4, gConfig.ScreenLayout.EquipStatusValues[1], kNumColorCyan, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDefense(w), 4, gConfig.ScreenLayout.EquipStatusValues[2], kNumColorCyan, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerDexterity(w), 4, gConfig.ScreenLayout.EquipStatusValues[3], kNumColorCyan, kNumAlignRight);
      PAL_DrawNumber(PAL_GetPlayerFleeRate(w), 4, gConfig.ScreenLayout.EquipStatusValues[4], kNumColorCyan, kNumAlignRight);

      //
      // Draw a box for player selection
      //
      PAL_CreateBox(gConfig.ScreenLayout.EquipRoleListBox, gpGlobals->wMaxPartyMemberIndex, PAL_WordMaxWidth(36, 4) - 1, 0, FALSE);

      //
      // Draw the label of players
      //
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         w = gpGlobals->rgParty[i].wPlayerRole;

         if (iCurrentPlayer == i)
         {
            if (gpGlobals->g.rgObject[wItem].item.wFlags & (kItemFlagEquipableByPlayerRole_First << w))
            {
               bColor = bSelectedColor;
            }
            else
            {
               bColor = MENUITEM_COLOR_SELECTED_INACTIVE;
            }
         }
         else
         {
            if (gpGlobals->g.rgObject[wItem].item.wFlags & (kItemFlagEquipableByPlayerRole_First << w))
            {
               bColor = MENUITEM_COLOR;
            }
            else
            {
               bColor = MENUITEM_COLOR_INACTIVE;
            }
         }

         PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]),
            PAL_XY_OFFSET(gConfig.ScreenLayout.EquipRoleListBox, 13, 12 + 18 * i), bColor, TRUE, FALSE, FALSE); /* Saturn (16,110+18i) */
      }

      //
      // Draw the text label and amount of the item
      //
      if (wItem != 0)
      {
         /* Saturn 0x0604d8b4: name centred, x = 44 - 8 * chars, y = 69 */
         PAL_DrawText(PAL_GetWord(wItem), PAL_XY(44 - PAL_TextWidth(PAL_GetWord(wItem)) / 2, 69), MENUITEM_COLOR_CONFIRMED, TRUE, FALSE, FALSE);
         PAL_DrawNumber(PAL_GetItemAmount(wItem), 2, gConfig.ScreenLayout.EquipItemAmount, kNumColorCyan, kNumAlignRight);
      }

      //
      // Update the screen
      //
      VIDEO_UpdateScreen(NULL);

      //
      // Accept input
      //
      PAL_ClearKeyState();

      while (TRUE)
      {
         PAL_ProcessEvent();

         //
         // See if we should change the highlight color
         //
         if (SDL_TICKS_PASSED(SDL_GetTicks(), dwColorChangeTime))
         {
            if ((WORD)bSelectedColor + 1 >=
               (WORD)MENUITEM_COLOR_SELECTED_FIRST + MENUITEM_COLOR_SELECTED_TOTALNUM)
            {
               bSelectedColor = MENUITEM_COLOR_SELECTED_FIRST;
            }
            else
            {
               bSelectedColor++;
            }

            dwColorChangeTime = SDL_GetTicks() + (600 / MENUITEM_COLOR_SELECTED_TOTALNUM);

            //
            // Redraw the selected item if needed.
            //
            w = gpGlobals->rgParty[iCurrentPlayer].wPlayerRole;

            if (gpGlobals->g.rgObject[wItem].item.wFlags & (kItemFlagEquipableByPlayerRole_First << w))
            {
               PAL_DrawText(PAL_GetWord(gpGlobals->g.PlayerRoles.rgwName[w]),
                  PAL_XY_OFFSET(gConfig.ScreenLayout.EquipRoleListBox, 13, 12 + 18 * iCurrentPlayer), bSelectedColor, TRUE, TRUE, FALSE);
            }
         }

         if (g_InputState.dwKeyPress != 0)
         {
            break;
         }

         SDL_Delay(1);
      }

      if (wItem == 0)
      {
         return;
      }

      if (g_InputState.dwKeyPress & (kKeyUp | kKeyLeft))
      {
         iCurrentPlayer--;
         if (iCurrentPlayer < 0)
         {
            iCurrentPlayer = gpGlobals->wMaxPartyMemberIndex;
         }
      }
      else if (g_InputState.dwKeyPress & (kKeyDown | kKeyRight))
      {
         iCurrentPlayer++;
         if (iCurrentPlayer > gpGlobals->wMaxPartyMemberIndex)
         {
            iCurrentPlayer = 0;
         }
      }
      else if (g_InputState.dwKeyPress & kKeyMenu)
      {
         return;
      }
      else if (g_InputState.dwKeyPress & kKeySearch)
      {
         w = gpGlobals->rgParty[iCurrentPlayer].wPlayerRole;

         if (gpGlobals->g.rgObject[wItem].item.wFlags & (kItemFlagEquipableByPlayerRole_First << w))
         {
            //
            // Run the equip script
            //
            gpGlobals->g.rgObject[wItem].item.wScriptOnEquip =
               PAL_RunTriggerScript(gpGlobals->g.rgObject[wItem].item.wScriptOnEquip,
                  gpGlobals->rgParty[iCurrentPlayer].wPlayerRole);
         }
      }
   }
}

VOID
PAL_QuitGame(
   VOID
)
{
#if 0 /* Saturn quit uses the plain yes/no confirm (0x0604fbe0) */
	WORD wReturnValue = PAL_TripleMenu(SYSMENU_LABEL_LAUNCHSETTING);
#else
	WORD wReturnValue = PAL_ConfirmMenu(); // No config menu available
#endif
	if (wReturnValue == 1 || wReturnValue == 2)
	{
		if (wReturnValue == 2) gConfig.fLaunchSetting = TRUE;
		PAL_SaveConfig();		// Keep the fullscreen state
		AUDIO_PlayMusic(0, FALSE, 2);
		PAL_FadeOut(2);
		PAL_Shutdown(0);
	}
}
