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

#ifndef GAME_H
#define GAME_H

#include "common.h"

/* Saturn: the field loop sets the SGL frame count (byte 0x060ffc12, PROG 0x06026d9c etc.) to 3 vsyncs
   -> 20 frames/s; measured in an emulator: one 16x8 walking step every 3 vsyncs. (DOS: 10) */
#define    FPS             20
#define    FRAME_TIME      (1000 / FPS)
// Saturn: one scene iteration inside scripts (0x06021fd8 / op 0x09 / 0x80 loops, 0x06035410 + 0x060232a4) = one slSynch
// = SGL frame count (byte 0x060ffc12) vsyncs.  The field loop (0x06026e5e) sets it every frame: 6 when the party does
// not move, 3 when it walks; 3 at field start (0x06026d9c) and after a battle (0x060272e2), 2 in battle (0x06027204);
// ride ops 0x3F / 0x44 / 0x97 use 3 / 2 / 3 and leave 3 (0x0602a4b0).  emu trace of 0x060ffc12 / 0x06076b36:
// idle field 6, walking 3, new-game opening 3.
extern int g_iSatSynch;
extern WORD g_wSatPrevScene;
/* Saturn exact viewport 0x06076948/4c: reset to the viewport every field frame, ops 0x6E / 0x7F add to it and
   the shown viewport is its 16x8-lattice cell (0x06033dd8) */
extern int g_iSatVpX, g_iSatVpY;
VOID PAL_SatGridSnap(int *px, int *py);
#define    SAT_SCRIPT_FRAME_TIME   (g_iSatSynch * 50 / 3)

PAL_C_LINKAGE
VOID
PAL_GameMain(
   VOID
);

#endif
