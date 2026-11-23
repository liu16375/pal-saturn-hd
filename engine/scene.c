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
// Portions based on PALx Project by palxex.
// Copyright (c) 2006, Pal Lockheart <palxex@gmail.com>.
//

#include "main.h"

#define MAX_SPRITE_TO_DRAW         2048

typedef struct tagSPRITE_TO_DRAW
{
   LPCBITMAPRLE     lpSpriteFrame; // pointer to the frame bitmap
   PAL_POS          pos;           // position on the scene
   int              iLayer;        // logical layer
} SPRITE_TO_DRAW;

static SPRITE_TO_DRAW    g_rgSpriteToDraw[MAX_SPRITE_TO_DRAW];
static int               g_nSpriteToDraw;

static VOID
PAL_AddSpriteToDraw(
   LPCBITMAPRLE     lpSpriteFrame,
   int              x,
   int              y,
   int              iLayer
)
/*++
   Purpose:

     Add a sprite to our list of drawing.

   Parameters:

     [IN]  lpSpriteFrame - the bitmap of the sprite frame.

     [IN]  x - the X coordinate on the screen.

     [IN]  y - the Y coordinate on the screen.

     [IN]  iLayer - the layer of the sprite.

   Return value:

     None.

--*/
{
   assert(g_nSpriteToDraw < MAX_SPRITE_TO_DRAW);

   g_rgSpriteToDraw[g_nSpriteToDraw].lpSpriteFrame = lpSpriteFrame;
   g_rgSpriteToDraw[g_nSpriteToDraw].pos = PAL_XY(x, y);
   g_rgSpriteToDraw[g_nSpriteToDraw].iLayer = iLayer;

   g_nSpriteToDraw++;
}

//
// Saturn-faithful map scene (PROG.BIN 0x0603440c and callees).
//
// The Saturn version does not use the DOS "cover tile" rule.  Every map tile and
// every sprite is an SGL sprite (slDispSprite 0x0605e398) drawn CENTRED on its
// position, and VDP1 draws them back to front by Z:
//
//   * map tiles   (0x060344a0): grid of 15 rows x 11 columns x 2 half-rows starting
//     at the view tile (pixel->tile 0x06033dd8), centre (-168 + 32c + 16h, -108 + 16r + 8h),
//     lo tile then hi tile per cell;  Z = 300               if tile height == 0
//                                     Z = 300 - (16r + 8h + 8*height) / 2  otherwise
//   * party       (0x06032c64 -> 0x06034028): centre (x - 168, y - 112), Z = 300 - (y + layer + 6) / 2
//   * event objs  (0x06034fa0 -> 0x06034028): x' = x - viewX, y' = y - viewY, centre (x' - 168, y' - 108)
//                 (double constants 168.0 / 108.0 at 0x06034018), Z = 300 - (y' + sLayer*8 + 6) / 2
//
// The Saturn screen is 320x224 (slInitSystem(TV_320x224)); SGL's origin is its centre (160,112).
// This port now runs a 320x224 logical screen (PAL_SCREEN_H), so SAT_CROP_Y = 0: Saturn screen
// coordinates are used 1:1.  Everything is 8 px left of the DOS layout, exactly as on the Saturn.
//
// Below, "key" is (300 - Z) * 2: larger key = nearer = drawn later.  Entries with equal Z are
// drawn in registration order (tiles, then party, then event objects), which is what makes the
// hi layer of a flat (height 0) cell appear above its lo layer, as on the real machine.
//
#define SAT_ORG_X        (160 - 168 + 1)   /* +1: measured on the original (emulator) */
#define SAT_TILE_ORG_Y   (112 - 108)
#define SAT_CROP_Y       19   /* VDP1 screen line -> gpScreen row: work area starts at line 12 and the
                                 map appears 7 px higher than SGL centre 112 suggests (measured) */
#define SAT_ROWS         15
#define SAT_COLS         11

int g_iSatMapShift = 0;

static VOID
PAL_SatViewTile(
   int vx, int vy, int *tx, int *ty, int *th
)
// PROG 0x06033dd8: view pixel -> tile (x, y, half)
{
   int a = vx - 2 * vy - 16, b = vx + 2 * vy + 16, c, s;
   b = b / 32;                                   // C division truncates like the original
   if (a > 0) { c = a / 32 + 1; if ((a & 31) == 0) c--; }
   else c = a / 32;
   s = b + c;
   *th = s & 1;
   if (*th) s--;
   *tx = s / 2;
   *ty = *tx - c;
}

static VOID
PAL_SatAdd(
   LPCBITMAPRLE lpFrame, int cx, int cy, int key
)
// cx, cy = Saturn screen centre (320x224 coordinates)
{
   if (lpFrame == NULL || g_nSpriteToDraw >= MAX_SPRITE_TO_DRAW) return;
   PAL_AddSpriteToDraw(lpFrame,
      cx - (int)PAL_RLEGetWidth(lpFrame) / 2,
      cy - SAT_CROP_Y - (int)PAL_RLEGetHeight(lpFrame) / 2,
      key);
}

static VOID
PAL_DumpPaletteOnce(const char *dir)
{
   extern SDL_Color *VIDEO_GetPalette(VOID);
   extern SDL_Surface *gpScreen;
   SDL_Palette *sp = (gpScreen ? SDL_GetSurfacePalette(gpScreen) : NULL);
   SDL_Color *pal = (sp ? sp->colors : VIDEO_GetPalette());  /* gpScreen's palette = the colors used to composite sprites */
   char pp[512]; FILE *f; int i;
   SDL_snprintf(pp, sizeof pp, "%s/palette.bin", dir);
   f = fopen(pp, "wb");
   if (f) { for (i = 0; i < 256; i++) { unsigned char rgb[3];
            rgb[0] = pal[i].r; rgb[1] = pal[i].g; rgb[2] = pal[i].b;
            fwrite(rgb, 1, 3, f); } fclose(f); }
   {  // dump the 8 ALLDAT sprite palette banks (final display colors) -> banks.bin (8 x 256 RGB)
      extern const Uint32 *SAT_GetBankLUT(int bank);
      char bp[512]; FILE *bf; int b;
      SDL_snprintf(bp, sizeof bp, "%s/banks.bin", dir);
      bf = fopen(bp, "wb");
      if (bf) { for (b = 0; b < 8; b++) { const Uint32 *lut = SAT_GetBankLUT(b);
                 for (i = 0; i < 256; i++) { Uint32 v = lut ? lut[i] : 0; unsigned char rgb[3];
                    rgb[0] = (v >> 16) & 0xFF; rgb[1] = (v >> 8) & 0xFF; rgb[2] = v & 0xFF;
                    fwrite(rgb, 1, 3, bf); } } fclose(bf); }
   }
}

// dump all frames of a sprite as raw index bins: <dir>/<prefix><III>.bin
// (header u16 width=2*w, u16 h; left half blitted on bg index 0, right half on bg 255)
static VOID
PAL_DumpSpriteFrames(LPCSPRITE spr, const char *dir, const char *prefix)
{
   int i, n;
   if (spr == NULL) return;
   n = PAL_SpriteGetNumFrames(spr);
   for (i = 0; i < n; i++)
   {
      LPCBITMAPRLE bmp = PAL_SpriteGetFrame(spr, i);
      int w, h; SDL_Surface *s; SDL_Rect rr; char p[512];
      const unsigned char *rb; unsigned short bank;
      if (bmp == NULL) continue;
      w = PAL_RLEGetWidth(bmp); h = PAL_RLEGetHeight(bmp);
      if (w <= 0 || h <= 0 || w > 512 || h > 512) continue;
      rb = (const unsigned char *)bmp;  // detect ALLDAT palette bank from the RLE header (02 00 <bank> 00)
      bank = (rb[0] == 0x02 && rb[1] == 0x00 && rb[2] < 8 && rb[3] == 0x00) ? rb[2] : 0;
      s = SDL_CreateRGBSurface(0, w * 2, h, 8, 0, 0, 0, 0);
      if (s == NULL) continue;
      SDL_FillRect(s, NULL, 0);
      rr.x = w; rr.y = 0; rr.w = w; rr.h = h; SDL_FillRect(s, &rr, 255);
      PAL_RLEBlitToSurface(bmp, s, PAL_XY(0, 0));
      PAL_RLEBlitToSurface(bmp, s, PAL_XY(w, 0));
      SDL_snprintf(p, sizeof p, "%s/%s%03d.bin", dir, prefix, i);
      { FILE *f = fopen(p, "wb"); int y;
        if (f) { unsigned short hdr[3]; hdr[0] = (unsigned short)(w * 2); hdr[1] = (unsigned short)h; hdr[2] = bank;
                 SDL_LockSurface(s); fwrite(hdr, 2, 3, f);
                 for (y = 0; y < h; y++) fwrite((unsigned char *)s->pixels + y * s->pitch, 1, w * 2, f);
                 SDL_UnlockSurface(s); fclose(f); } }
      SDL_FreeSurface(s);
   }
}

static VOID
PAL_DumpCurrentMapTiles(const char *dir)
{
   LPCPALMAP map = PAL_GetCurrentMap();
   char pfx[64];
   if (map == NULL || map->pTileSprite == NULL) return;
   PAL_DumpPaletteOnce(dir);
   SDL_snprintf(pfx, sizeof pfx, "map%d_tile", map->iMapNum);
   PAL_DumpSpriteFrames(map->pTileSprite, dir, pfx);
}

static VOID
PAL_DumpPlayerSprites(const char *dir)
{
   int i; char pfx[64];
   PAL_DumpPaletteOnce(dir);
   for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)
   {
      LPCSPRITE spr = PAL_GetPlayerSprite((BYTE)i);
      if (spr == NULL) continue;
      SDL_snprintf(pfx, sizeof pfx, "party%d_fr", i);
      PAL_DumpSpriteFrames(spr, dir, pfx);
   }
}

static VOID
PAL_DumpEventObjectSprites(const char *dir)
{
   int i, lo, hi, sc = gpGlobals->wNumScene; char pfx[64];
   PAL_DumpPaletteOnce(dir);
   lo = gpGlobals->g.rgScene[sc - 1].wEventObjectIndex;
   hi = gpGlobals->g.rgScene[sc].wEventObjectIndex;
   for (i = lo; i < hi; i++)
   {
      LPCSPRITE spr = PAL_GetEventObjectSprite((WORD)(i + 1));
      if (spr == NULL) continue;                       // exits/triggers have no sprite
      SDL_snprintf(pfx, sizeof pfx, "npc_sc%d_ev%d_fr", sc, i + 1);
      PAL_DumpSpriteFrames(spr, dir, pfx);
   }
}

/* Dump every battle background (fbp.mkf) as <dir>/fbp<III>.bin:
   u16 w(320), u16 h(200), then w*h index bytes, then 256*3 RGB (the chunk's own FBP palette,
   resolved via its SAT bank). Call once from a settled field scene so the fade ratio is ~1.0. */
static VOID
PAL_DumpBattleBackgrounds(const char *dir)
{
   extern int SAT_FBPBank(int iChunk);
   extern const Uint32 *SAT_GetBankLUT(int bank);
   FILE *fp = gpGlobals->f.fpFBP;
   int count, c;
   PAL_LARGE BYTE buf[320 * 200];
   if (fp == NULL) return;
   count = PAL_MKFGetChunkCount(fp);
   for (c = 0; c < count; c++)
   {
      int sz = PAL_MKFDecompressChunk(buf, sizeof buf, c, fp);
      int bank, i; const Uint32 *lut; char p[512]; FILE *f;
      if (sz < 320 * 200) continue;            /* skip non-320x200 chunks */
      bank = SAT_FBPBank(c);
      lut = SAT_GetBankLUT(bank);
      SDL_snprintf(p, sizeof p, "%s/fbp%03d.bin", dir, c);
      f = fopen(p, "wb");
      if (f)
      {
         unsigned short hdr[2]; hdr[0] = 320; hdr[1] = 200;
         fwrite(hdr, 2, 2, f);
         fwrite(buf, 1, 320 * 200, f);
         for (i = 0; i < 256; i++)
         {
            Uint32 v = lut ? lut[i] : 0; unsigned char rgb[3];
            rgb[0] = (v >> 16) & 0xFF; rgb[1] = (v >> 8) & 0xFF; rgb[2] = v & 0xFF;
            fwrite(rgb, 1, 3, f);
         }
         fclose(f);
      }
   }
}

/* Dump one RLE bitmap (bank-aware) as <dir>/<name>.bin — same format as PAL_DumpSpriteFrames:
   u16 w*2, u16 h, u16 bank; then the index rows (left half on bg 0, right half on bg 255). */
static VOID
PAL_DumpOneRLE(LPCBITMAPRLE bmp, const char *dir, const char *name)
{
   int w, h; SDL_Surface *s; SDL_Rect rr; char p[512];
   const unsigned char *rb; unsigned short bank;
   if (bmp == NULL) return;
   w = PAL_RLEGetWidth(bmp); h = PAL_RLEGetHeight(bmp);
   if (w <= 0 || h <= 0 || w > 512 || h > 512) return;
   rb = (const unsigned char *)bmp;
   bank = (rb[0] == 0x02 && rb[1] == 0x00 && rb[2] < 8 && rb[3] == 0x00) ? rb[2] : 0;
   s = SDL_CreateRGBSurface(0, w * 2, h, 8, 0, 0, 0, 0);
   if (s == NULL) return;
   SDL_FillRect(s, NULL, 0);
   rr.x = w; rr.y = 0; rr.w = w; rr.h = h; SDL_FillRect(s, &rr, 255);
   PAL_RLEBlitToSurface(bmp, s, PAL_XY(0, 0));
   PAL_RLEBlitToSurface(bmp, s, PAL_XY(w, 0));
   SDL_snprintf(p, sizeof p, "%s/%s.bin", dir, name);
   { FILE *f = fopen(p, "wb"); int y;
     if (f) { unsigned short hdr[3]; hdr[0] = (unsigned short)(w * 2); hdr[1] = (unsigned short)h; hdr[2] = bank;
              SDL_LockSurface(s); fwrite(hdr, 2, 3, f);
              for (y = 0; y < h; y++) fwrite((unsigned char *)s->pixels + y * s->pitch, 1, w * 2, f);
              SDL_UnlockSurface(s); fclose(f); } }
   SDL_FreeSurface(s);
}

/* Dump all frames of all chunks of a battle-sprite MKF (f.mkf party, abc.mkf enemy). */
static VOID
PAL_DumpSpriteMKF(FILE *fp, const char *dir, const char *prefix)
{
   int count, c;
   if (fp == NULL) return;
   count = PAL_MKFGetChunkCount(fp);
   for (c = 0; c < count; c++)
   {
      int l = PAL_MKFGetDecompressedSize(c, fp), n, i;
      LPSPRITE spr;
      if (l <= 0) continue;
      spr = (LPSPRITE)UTIL_calloc(l, 1);
      if (PAL_MKFDecompressChunk(spr, l, c, fp) <= 0) { free(spr); continue; }
      n = PAL_SpriteGetNumFrames(spr);
      for (i = 0; i < n; i++)
      {
         char name[96];
         SDL_snprintf(name, sizeof name, "%sc%d_fr%03d", prefix, c, i);
         PAL_DumpOneRLE(PAL_SpriteGetFrame(spr, i), dir, name);
      }
      free(spr);
   }
}

/* Dump all face portraits (rgm.mkf): each chunk is a single RLE bitmap -> face<III>.bin */
static VOID
PAL_DumpFaces(const char *dir)
{
   FILE *fp = gpGlobals->f.fpRGM;
   int count, c;
   PAL_LARGE BYTE buf[PAL_RLEBUFSIZE];
   if (fp == NULL) return;
   count = PAL_MKFGetChunkCount(fp);
   for (c = 0; c < count; c++)
   {
      char name[64];
      if (PAL_MKFReadChunk(buf, sizeof buf, c, fp) <= 0) continue;
      SDL_snprintf(name, sizeof name, "face%03d", c);
      PAL_DumpOneRLE((LPCBITMAPRLE)buf, dir, name);
   }
}

/* ===== HD vertical-slice: compose terrain from ×6 HD tiles into a 1920x1344 surface ===== */
#define HD_W 1920
#define HD_H 1344
#define HD_S 6
typedef struct { Uint32 *px; int w, h; int isTile; } HDIMG;
static HDIMG       *g_hd = NULL;        /* HD images (tiles + party + NPC sprites), parallel to g_hdPtr */
static const void **g_hdPtr = NULL;     /* source RLE frame pointer for each HD image */
static int          g_hdN = 0, g_hdCap = 0, g_hdScene = -1;
static SDL_Surface *g_hdScreen = NULL;

/* Incremental HD asset load: reading a scene's hundreds of HD .rgba files all at once froze the game ~3s on
   every scene switch. Instead build a work queue on scene change and load a small chunk per frame, rendering
   the (already-composed) low-res scene meanwhile; when the queue drains, HD compositing resumes. */
typedef struct { const void *fr; char path[256]; int isTile; } HDQITEM;
static HDQITEM     *g_hdQ = NULL;
static int          g_hdQN = 0, g_hdQCap = 0, g_hdQPos = 0;
int                 g_hdLoading = 0;   /* 1 while a scene's HD assets are still streaming in -> use low-res */
static HDQITEM     *g_hbQ = NULL;       /* battle HD stream queue (see PAL_HDBattle_AddFrame) */
static int          g_hbQN = 0, g_hbQCap = 0, g_hbQPos = 0;
int                 g_hbLoading = 0;
int                 g_hdmDirty = 0;    /* 1 = the HD surface was written since the last present (present gate) */
int                 g_hdmStale = 1;    /* 1 = HD twin does not reflect gpScreenReal (movie/startup) -> low-res */

#define PAL_HD_MATCH_MIN 85   /* percent; see PAL_HD_SetMatches */
static VOID PAL_HD_AddImg(const void *framePtr, const char *path, int isTile)
{  // load one raw RGBA file (u16 w, u16 h, RGBA bytes) as an ARGB image mapped to framePtr
   FILE *f; unsigned short wh[2];
   if (framePtr == NULL) return;
   f = fopen(path, "rb"); if (f == NULL) return;
   if (fread(wh, 2, 2, f) == 2)
   {
      int w = wh[0], h = wh[1], k; size_t bytes = (size_t)w * h * 4;
      unsigned char *buf = (unsigned char *)malloc(bytes);
      if (buf && fread(buf, 1, bytes, f) == bytes)
      {
         if (g_hdN >= g_hdCap) { g_hdCap = g_hdCap ? g_hdCap * 2 : 512;
            g_hd = (HDIMG *)realloc(g_hd, g_hdCap * sizeof(HDIMG));
            g_hdPtr = (const void **)realloc((void *)g_hdPtr, g_hdCap * sizeof(void *)); }
         g_hd[g_hdN].w = w; g_hd[g_hdN].h = h; g_hd[g_hdN].isTile = isTile; g_hd[g_hdN].px = (Uint32 *)malloc(bytes);
         for (k = 0; k < w * h; k++) { unsigned char *q = buf + k * 4;
            g_hd[g_hdN].px[k] = ((Uint32)q[3] << 24) | ((Uint32)q[0] << 16) | ((Uint32)q[1] << 8) | q[2]; }
         g_hdPtr[g_hdN] = framePtr; g_hdN++;
      }
      free(buf);
   }
   fclose(f);
}

/* Does the prebuilt HD image in `path` show the same picture as the low-res frame `fr`?  The per-scene sprite files
   are named by party slot / event object, not by sprite, so they are only right while that slot holds the sprite
   they were rendered from: with A Nu in slot 1 the files rendered from Zhao Linger (same frame count and size)
   were keyed onto her frames and she turned into Zhao Linger whenever she faced the camera.  Compare the
   silhouettes (HD alpha sampled at the x6 cell centres against the opaque pixels of the RLE frame): returns the
   overlap in percent (intersection / union), -1 if the file is missing or of another size. */
static int PAL_HD_MatchPct(LPCBITMAPRLE fr, const char *path)
{
   const BYTE *p = (const BYTE *)fr; unsigned short wh[2]; FILE *f; unsigned char *buf, *mask;
   int w, h, x, y, len, i, inter = 0, uni = 0, pct = -1;
   if (p == NULL) return -1;
   if (p[0] == 0x02 && p[1] == 0x00 && p[2] < 8 && p[3] == 0x00) p += 4;   /* byte 2 = Saturn colour bank */
   w = p[0] | (p[1] << 8); h = p[2] | (p[3] << 8); p += 4; len = w * h;
   if (w <= 0 || h <= 0 || len > 512 * 512) return -1;
   f = fopen(path, "rb"); if (f == NULL) return -1;
   if (fread(wh, 2, 2, f) != 2 || wh[0] != w * HD_S || wh[1] != h * HD_S) { fclose(f); return -1; }
   buf = (unsigned char *)malloc((size_t)wh[0] * wh[1] * 4); mask = (unsigned char *)calloc(1, (size_t)len);
   if (buf && mask && fread(buf, 1, (size_t)wh[0] * wh[1] * 4, f) == (size_t)wh[0] * wh[1] * 4)
   {
      for (i = 0; i < len; )
      {
         BYTE T = *p++;
         if ((T & 0x80) && T <= 0x80 + w) i += T - 0x80;
         else { int j; for (j = 0; j < T && i < len; j++) mask[i++] = 1; p += T; }
      }
      for (y = 0; y < h; y++)
         for (x = 0; x < w; x++)
         {
            int a = buf[((size_t)(y * HD_S + HD_S / 2) * wh[0] + (x * HD_S + HD_S / 2)) * 4 + 3] >= 128, b = mask[y * w + x];
            inter += (a && b); uni += (a || b);
         }
      pct = uni ? inter * 100 / uni : 100;
   }
   free(buf); free(mask); fclose(f);
   return pct;
}

/* A prebuilt sprite set (party slot or event object) is used only if its frames are the live sprite's: check up
   to 4 frames spread over the set, all must overlap well. */
static int PAL_HD_SetMatches(LPCSPRITE spr, int n, const char *fmt, const char *dir, int sc, int id)
{
   int k, worst = 100; char p[512];
   for (k = 0; k < 4 && k < n; k++)
   {
      int j = (n <= 4) ? k : k * (n - 1) / 3, m;
      SDL_snprintf(p, sizeof p, fmt, dir, sc, id, j);
      m = PAL_HD_MatchPct(PAL_SpriteGetFrame(spr, j), p);
      if (m >= 0 && m < worst) worst = m;
   }
   {  /* debug: env PAL_HDMATCHLOG=file logs the worst overlap of every set */
      const char *lg = SDL_getenv("PAL_HDMATCHLOG");
      if (lg) { FILE *fl = fopen(lg, "a"); if (fl) { fprintf(fl, fmt, "", sc, id, n); fprintf(fl, " worst %d\n", worst); fclose(fl); } }
   }
   return worst >= PAL_HD_MATCH_MIN;
}

static VOID PAL_HD_QAdd(const void *fr, const char *path, int isTile)
{
   if (fr == NULL) return;
   if (g_hdQN >= g_hdQCap) { g_hdQCap = g_hdQCap ? g_hdQCap * 2 : 512;
      g_hdQ = (HDQITEM *)realloc(g_hdQ, g_hdQCap * sizeof(HDQITEM)); }
   if (g_hdQ == NULL) return;
   g_hdQ[g_hdQN].fr = fr;
   SDL_snprintf(g_hdQ[g_hdQN].path, sizeof g_hdQ[g_hdQN].path, "%s", path);
   g_hdQ[g_hdQN].isTile = isTile;
   g_hdQN++;
}

/* debug: force the next PAL_HD_Load to re-stream the current scene's HD assets (replicates the HD re-stream
   that a room exit+re-enter does, for reproducing #6 without finding the door). */
void PAL_HD_ForceReload(void) { g_hdScene = -1; }

/* Build (fast) the per-scene list of HD files to stream in; the actual file reads happen in PAL_HD_LoadStep. */
static VOID PAL_HD_Load(LPCPALMAP map, const char *dir)
{
   int i, j, n, lo, hi, sc = gpGlobals->wNumScene; char p[512];
   if (map == NULL || map->pTileSprite == NULL || sc == g_hdScene) return;
   for (i = 0; i < g_hdN; i++) free(g_hd[i].px);
   free(g_hd); g_hd = NULL; free((void *)g_hdPtr); g_hdPtr = NULL; g_hdN = g_hdCap = 0;
   free(g_hdQ); g_hdQ = NULL; g_hdQN = g_hdQCap = g_hdQPos = 0;
   // Load-on-demand layout: tiles per-map in <dir>/map<M>/, sprites per-scene in <dir>/sc<N>/.
   // Missing files are simply skipped (scene not prebuilt -> that frame falls through to the
   // normal smooth-upscaled present). So any prebuilt scene is HD; others degrade gracefully.
   n = PAL_SpriteGetNumFrames(map->pTileSprite);                                  /* map tiles (per map) */
   for (i = 0; i < n; i++) { SDL_snprintf(p, sizeof p, "%s/map%d/tile%03d.rgba", dir, map->iMapNum, i);
      PAL_HD_QAdd(PAL_SpriteGetFrame(map->pTileSprite, i), p, 1); }   /* isTile=1: terrain tiles */
   for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++)                                /* party sprites (per scene) */
   {
      LPCSPRITE spr = PAL_GetPlayerSprite((BYTE)i);
      if (spr == NULL) continue;
      n = PAL_SpriteGetNumFrames(spr);
      /* #6 fix (端酒菜消失, part 2): the prebuilt party HD was rendered for ONE specific sprite per scene
         (here the 12-frame 端酒菜 sprite 2, tray-at-waist). But the story can swap role i to a DIFFERENT
         sprite in the same scene — after 苗人头领 rejects the offer, role 0 becomes the 16-frame sprite 208
         (holding the 酒菜 out). Keying the prebuilt files to that sprite by pointer paints sprite 2's frames
         onto sprite 208's poses, so some frames show a tray-less pose and the 酒菜 appears to vanish (SW,
         persistently). Detect the mismatch by frame count: if the file for this sprite's HIGHEST frame is
         absent, the prebuilt set does not cover this sprite — skip ALL its keyed HD so it draws via the crisp
         ×6 fallback of its own correct low-res art (which holds the 酒菜 right). */
      { char chk[512]; FILE *cf; SDL_snprintf(chk, sizeof chk, "%s/sc%d/party%d_fr%03d.rgba", dir, sc, i, n - 1);
        cf = fopen(chk, "rb"); if (cf) fclose(cf); else continue; }
      if (!PAL_HD_SetMatches(spr, n, "%s/sc%d/party%d_fr%03d.rgba", dir, sc, i)) continue;   /* files of another character */
      for (j = 0; j < n; j++) { SDL_snprintf(p, sizeof p, "%s/sc%d/party%d_fr%03d.rgba", dir, sc, i, j);
         PAL_HD_QAdd(PAL_SpriteGetFrame(spr, j), p, 0); }
   }
   lo = gpGlobals->g.rgScene[sc - 1].wEventObjectIndex;                           /* NPC sprites (per scene) */
   hi = gpGlobals->g.rgScene[sc].wEventObjectIndex;
   for (i = lo; i < hi; i++)
   {
      LPCSPRITE spr = PAL_GetEventObjectSprite((WORD)(i + 1));
      if (spr == NULL) continue;
      n = PAL_SpriteGetNumFrames(spr);
      /* #6-class guard (same as the party loop): the prebuilt npc_ev files were rendered for ONE sprite, but a
         script can swap this event object to a DIFFERENT sprite in-scene. If the file for its HIGHEST live
         frame is absent the prebuilt set does not cover this sprite -> skip its keyed HD so it draws via the
         crisp ×6 fallback of its own correct art instead of mis-keyed (wrong-pose/same-size) frames. */
      { char chk[512]; FILE *cf; SDL_snprintf(chk, sizeof chk, "%s/sc%d/npc_ev%d_fr%03d.rgba", dir, sc, i + 1, n - 1);
        cf = fopen(chk, "rb"); if (cf) fclose(cf); else continue; }
      if (!PAL_HD_SetMatches(spr, n, "%s/sc%d/npc_ev%d_fr%03d.rgba", dir, sc, i + 1)) continue;   /* files of another sprite */
      for (j = 0; j < n; j++) { SDL_snprintf(p, sizeof p, "%s/sc%d/npc_ev%d_fr%03d.rgba", dir, sc, i + 1, j);
         PAL_HD_QAdd(PAL_SpriteGetFrame(spr, j), p, 0); }
   }
   g_hdScene = sc;
   if (getenv("PAL_SCLOG")) {                                                    /* debug (#6): log scene + player sprite nums on each scene change */
      FILE *fl = fopen(getenv("PAL_SCLOG"), "a");
      if (fl) { int k; LPCSPRITE s0 = PAL_GetPlayerSprite(0);
         fprintf(fl, "sc=%d map=%d spriteNum[", sc, map->iMapNum);
         for (k = 0; k < MAX_PLAYABLE_PLAYER_ROLES; k++) fprintf(fl, "%d ", gpGlobals->g.PlayerRoles.rgwSpriteNum[k]);
         fprintf(fl, "] p0frames=%d\n", s0 ? PAL_SpriteGetNumFrames(s0) : -1); fclose(fl); } }
   g_hdLoading = (g_hdQN > 0);   /* stream the files in over the next frames */
}

/* Stream the queued HD files with a per-frame TIME budget (not a fixed count), so a slow disk can't stall
   one frame for long: the scene stays responsive in low-res and fills in to HD over the next frames. */
static VOID PAL_HD_LoadStep(void)
{
   Uint32 start;
   if (!g_hdLoading) return;
   start = SDL_GetTicks();
   while (g_hdQPos < g_hdQN)
   {
      PAL_HD_AddImg(g_hdQ[g_hdQPos].fr, g_hdQ[g_hdQPos].path, g_hdQ[g_hdQPos].isTile); g_hdQPos++;
      if ((g_hdQPos & 3) == 0 && SDL_GetTicks() - start >= 16u) break;   /* ~16ms/frame cap (check every 4 files) */
   }
   if (g_hdQPos >= g_hdQN)
   { g_hdLoading = 0; free(g_hdQ); g_hdQ = NULL; g_hdQN = g_hdQCap = g_hdQPos = 0; g_hdmStale = 0; g_hdmDirty = 1; }
}

static int PAL_HD_FrameIdx(const void *fr)
{
   int i; if (g_hdPtr == NULL) return -1;
   for (i = 0; i < g_hdN; i++) if (g_hdPtr[i] == fr) return i;
   return -1;
}

static VOID PAL_HD_BlitImg(HDIMG *t, int x, int y)
{
   int sx, sy, dx, dy; Uint32 *dst = (Uint32 *)g_hdScreen->pixels; int dp = g_hdScreen->pitch / 4;
   g_hdmDirty = 1; g_hdmStale = 0;
   for (sy = 0; sy < t->h; sy++) { dy = y + sy; if (dy < 0 || dy >= HD_H) continue;
      for (sx = 0; sx < t->w; sx++) { dx = x + sx; if (dx < 0 || dx >= HD_W) continue;
         { Uint32 pv = t->px[sy * t->w + sx]; if ((pv >> 24) < 40) continue; dst[dy * dp + dx] = pv | 0xFF000000u; } } }
}

/* Accessors for video.c to present the HD frame to the window. */
static int g_hdFresh = 0;   /* 1 = this frame composed an HD scene (vs a menu/fade frame) */
SDL_Surface *PAL_HD_GetScreen(void) { return g_hdScreen; }
int PAL_HD_ConsumeFresh(void) { int v = g_hdFresh; g_hdFresh = 0; return v; }
int PAL_HDM_ConsumeDirty(void) { int v = g_hdmDirty; g_hdmDirty = 0; return v; }

/* HD fade (PAL_HDFULL): a whole-screen fade to/from black only scales the low-res *palette*; the HD
   twin is RGBA, not palette-indexed, so a fade frame writes nothing to it and the present falls back
   to the low-res picture -> the crisp TTF turns into the old bitmap font for the length of the fade
   (the "新的故事 font turns old on confirm" bug). Fix: keep presenting the HD twin during a fade,
   dimmed by the same 0..64 factor the palette uses, so the TTF stays on screen and fades smoothly. */
int g_hdmFade = -1;   /* 0..64 = brightness while a fade runs; -1 = inactive (normal dirty gating) */
void PAL_HDM_SetFade(int level) { g_hdmFade = level; }
int  PAL_HDM_GetFade(void) { return g_hdmFade; }

/* HD twin freshness: 1 = the HD twin does NOT reflect what's on gpScreenReal right now, so the present
   must use the low-res fallback (set when a movie/AVI blits a raw frame straight to gpScreenReal, and
   at startup before any scene is composed). 0 = the HD twin is current with the scene, so the present
   shows it on EVERY frame (not only the ones that redrew) — this kills the crisp↔blurry flicker that
   used to appear on each dialogue advance / between walk steps, where non-redraw frames fell through to
   the blurry low-res present. Set back to 0 whenever anything writes the HD twin (scene compose / HD
   overlay draw). See the g_hdmStale declaration up by g_hdmDirty. */
void PAL_HDM_SetStale(int s) { g_hdmStale = s; }
int  PAL_HDM_IsStale(void) { return g_hdmStale; }

/* fade-in finished: the content on screen is the current frame, so resume full-brightness HD present */
void PAL_HDM_FadeInEnd(void) { g_hdmFade = -1; g_hdmDirty = 1; g_hdmStale = 0; }
/* fade-out finished: the screen is now black and the HD twin still holds the old (pre-fade) frame.
   Blank the twin so no stale HD frame can flash before the next scene/movie draws. */
void PAL_HDM_FadeOutEnd(void)
{
   if (g_hdScreen != NULL)
   {
      Uint32 *p = (Uint32 *)g_hdScreen->pixels;
      int i, n = (g_hdScreen->pitch / 4) * g_hdScreen->h;
      for (i = 0; i < n; i++) p[i] = 0xFF000000u;
   }
   g_hdmFade = -1; g_hdmDirty = 0; g_hdmStale = 1;   /* twin blanked; use fallback until next compose */
}

/* ============ HD-mirror (PAL_HDFULL): an HD ×6 twin of gpScreen for overlays (UI/text/portraits/menus).
   Hooks on the drawing primitives (RLE blit, char draw, fills) mirror what the game paints on gpScreen
   into g_hdScreen at ×6. Present then uploads g_hdScreen. Default OFF -> normal play untouched. ============ */
int g_hdmSceneBusy = 0;   /* 1 while the field scene paints gpScreen (its keyed-HD base owns those blits) */

int PAL_HDM_On(void)
{
   static int v = -1;
   if (v < 0) v = SDL_getenv("PAL_HDFULL") ? 1 : 0;
   return v;
}

static VOID PAL_HDM_EnsureScreen(void)
{
   if (g_hdScreen == NULL)
      g_hdScreen = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
}

/* Alpha-composite a w×h ARGB buffer into g_hdScreen at HD pixel (hx,hy). */
VOID PAL_HDM_BlitRGBA(const Uint32 *px, int w, int h, int hx, int hy)
{
   int sx, sy, dx, dy, dp; Uint32 *dst;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL || px == NULL) return;
   g_hdmDirty = 1; g_hdmStale = 0;
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (sy = 0; sy < h; sy++)
   {
      dy = hy + sy; if (dy < 0 || dy >= HD_H) continue;
      for (sx = 0; sx < w; sx++)
      {
         Uint32 s = px[sy * w + sx]; unsigned a = (s >> 24) & 0xFF;
         if (a == 0) continue;
         dx = hx + sx; if (dx < 0 || dx >= HD_W) continue;
         if (a == 0xFF) { dst[dy * dp + dx] = 0xFF000000u | (s & 0xFFFFFF); }
         else
         {
            Uint32 d = dst[dy * dp + dx];
            unsigned sr=(s>>16)&0xFF, sg=(s>>8)&0xFF, sb=s&0xFF;
            unsigned dr=(d>>16)&0xFF, dg=(d>>8)&0xFF, db=d&0xFF;
            unsigned rr=(sr*a+dr*(255-a))/255, rg=(sg*a+dg*(255-a))/255, rb=(sb*a+db*(255-a))/255;
            dst[dy*dp+dx] = 0xFF000000u | (rr<<16)|(rg<<8)|rb;
         }
      }
   }
}

/* Mirror one RLE bitmap the game just blitted to gpScreen: rasterize, resolve its SAT bank, nearest ×6. */
VOID PAL_HDM_BlitRLE(const void *bmp, int x, int y)
{
   extern const Uint32 *SAT_GetBankLUTFull(int bank);
   const unsigned char *rb = (const unsigned char *)bmp;
   int w, h, bank, S = HD_S, px, py, ox, oy;
   SDL_Surface *s; const Uint32 *lut; Uint32 *src, *out;
   if (bmp == NULL) return;
   w = PAL_RLEGetWidth((LPCBITMAPRLE)bmp); h = PAL_RLEGetHeight((LPCBITMAPRLE)bmp);
   if (w <= 0 || h <= 0 || w > 512 || h > 512) return;
   bank = (rb[0]==0x02 && rb[1]==0x00 && rb[2]<8 && rb[3]==0x00) ? rb[2] : 0;
   lut = SAT_GetBankLUTFull(bank);   /* full-bright: HD present dims via colormod, so no black-on-load */
   s = SDL_CreateRGBSurface(0, w*2, h, 8, 0,0,0,0);
   if (s == NULL) return;
   SDL_FillRect(s, NULL, 0);
   { SDL_Rect rr; rr.x=w; rr.y=0; rr.w=w; rr.h=h; SDL_FillRect(s, &rr, 255); }
   PAL_RLEBlitToSurface((LPCBITMAPRLE)bmp, s, PAL_XY(0,0));
   PAL_RLEBlitToSurface((LPCBITMAPRLE)bmp, s, PAL_XY(w,0));
   src = (Uint32 *)malloc((size_t)w*h*4);
   if (src)
   {
      SDL_LockSurface(s);
      for (py=0; py<h; py++)
      { unsigned char *row=(unsigned char*)s->pixels+py*s->pitch;
        for (px=0; px<w; px++)
        { int L=row[px], R=row[w+px];
          if (L==0 && R==255) src[py*w+px]=0;
          else { Uint32 c = lut?lut[L]:0; src[py*w+px]=0xFF000000u|(c&0xFFFFFF); } } }
      SDL_UnlockSurface(s);
      out = (Uint32 *)malloc((size_t)w*S*h*S*4);
      if (out)
      {
         for (oy=0; oy<h*S; oy++) { int syy=oy/S; for (ox=0; ox<w*S; ox++) out[oy*w*S+ox]=src[syy*w+ox/S]; }
         PAL_HDM_BlitRGBA(out, w*S, h*S, x*S, y*S);
         free(out);
      }
      free(src);
   }
   SDL_FreeSurface(s);
}

/* Mono/grey mirror of a Saturn battle command icon: same ×6 pipeline as PAL_HDM_BlitRLE, but the icon
   pixels map to the UI mono palette (iBase + ((src&0x0F)>>1)) exactly like PAL_RLEBlitSatMono -- so the
   UNSELECTED (grey, iBase=2) and DISABLED (blue-grey, iBase=100) command icons are mirrored to HD too.
   Without this only the SELECTED icon (drawn via PAL_RLEBlitToSurface, which already mirrors) appeared in
   HD and the other 2-3 action icons were missing. */
VOID PAL_HDM_BlitRLEMono(const void *bmp, int x, int y, int iBase)
{
   extern const Uint32 *SAT_GetBankLUTFull(int bank);
   extern INT PAL_RLEBlitSatMono(LPCBITMAPRLE lpBitmapRLE, SDL_Surface *lpDstSurface, PAL_POS pos, INT iBase);
   const unsigned char *rb = (const unsigned char *)bmp;
   int w, h, bank, S = HD_S, px, py, ox, oy;
   SDL_Surface *s; const Uint32 *lut; Uint32 *src, *out;
   if (bmp == NULL) return;
   w = PAL_RLEGetWidth((LPCBITMAPRLE)bmp); h = PAL_RLEGetHeight((LPCBITMAPRLE)bmp);
   if (w <= 0 || h <= 0 || w > 512 || h > 512) return;
   bank = (rb[0]==0x02 && rb[1]==0x00 && rb[2]<8 && rb[3]==0x00) ? rb[2] : 0;
   lut = SAT_GetBankLUTFull(bank);
   s = SDL_CreateRGBSurface(0, w*2, h, 8, 0,0,0,0);
   if (s == NULL) return;
   SDL_FillRect(s, NULL, 0);
   { SDL_Rect rr; rr.x=w; rr.y=0; rr.w=w; rr.h=h; SDL_FillRect(s, &rr, 255); }
   /* rasterize with the SAME mono mapping used on gpScreen (opaque pixel -> iBase+(n>>1), always >=2, so
      the (L==0 && R==255) transparency test below is unambiguous). Blits to `s`, not gpScreen -> no mirror. */
   PAL_RLEBlitSatMono((LPCBITMAPRLE)bmp, s, PAL_XY(0,0), iBase);
   PAL_RLEBlitSatMono((LPCBITMAPRLE)bmp, s, PAL_XY(w,0), iBase);
   src = (Uint32 *)malloc((size_t)w*h*4);
   if (src)
   {
      SDL_LockSurface(s);
      for (py=0; py<h; py++)
      { unsigned char *row=(unsigned char*)s->pixels+py*s->pitch;
        for (px=0; px<w; px++)
        { int L=row[px], R=row[w+px];
          if (L==0 && R==255) src[py*w+px]=0;
          else { Uint32 c = lut?lut[L]:0; src[py*w+px]=0xFF000000u|(c&0xFFFFFF); } } }
      SDL_UnlockSurface(s);
      out = (Uint32 *)malloc((size_t)w*S*h*S*4);
      if (out)
      {
         for (oy=0; oy<h*S; oy++) { int syy=oy/S; for (ox=0; ox<w*S; ox++) out[oy*w*S+ox]=src[syy*w+ox/S]; }
         PAL_HDM_BlitRGBA(out, w*S, h*S, x*S, y*S);
         free(out);
      }
      free(src);
   }
   SDL_FreeSurface(s);
}

/* Mirror a gpScreen fill (idx through SAT bank; idx 0 = transparent black backdrop). */
VOID PAL_HDM_Fill(int x, int y, int w, int h, int idx, int bank)
{
   extern const Uint32 *SAT_GetBankLUTFull(int bank);
   const Uint32 *lut; Uint32 c; int sx, sy, dp; Uint32 *dst;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL) return;
   g_hdmDirty = 1; g_hdmStale = 0;
   lut = SAT_GetBankLUTFull(bank);   /* full-bright (fade handled by colormod) */
   c = (idx == 0) ? 0xFF000000u : (0xFF000000u | ((lut ? lut[idx] : 0) & 0xFFFFFF));
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (sy = y*HD_S; sy < (y+h)*HD_S; sy++) { if (sy<0||sy>=HD_H) continue;
      for (sx = x*HD_S; sx < (x+w)*HD_S; sx++) { if (sx<0||sx>=HD_W) continue; dst[sy*dp+sx]=c; } }
}

/* ===== HD battle scene compositor (keyed HD bg + party/enemy/FX frames into g_hdScreen) ===== */
static HDIMG g_hdBg = { NULL, 0, 0 };   /* HD battle background (1920×1200) */

void PAL_HDBattle_Reset(void)
{  /* free the HD image store + bg; force field HD to reload after the battle */
   int i;
   for (i = 0; i < g_hdN; i++) free(g_hd[i].px);
   free(g_hd); g_hd = NULL; free((void *)g_hdPtr); g_hdPtr = NULL; g_hdN = g_hdCap = 0;
   if (g_hdBg.px) { free(g_hdBg.px); g_hdBg.px = NULL; }
   /* drop any leftover battle stream queue (e.g. a previous battle that ended mid-stream) */
   free(g_hbQ); g_hbQ = NULL; g_hbQN = g_hbQCap = g_hbQPos = 0; g_hbLoading = 0;
   g_hdScene = -1;
}

/* Battle HD streaming — decoding every party+enemy HD frame synchronously at battle start froze the game
   ~3s (same as the old scene freeze). Enqueue them here, stream a time-budgeted chunk per battle frame in
   PAL_HDBattle_LoadStep, and ×6-fallback any frame not yet decoded (PAL_HDBattle_Sprite) so the battle shows
   crisp sprites immediately and upgrades to the refined AI-HD art as frames arrive. */
void PAL_HDBattle_AddFrame(const void *framePtr, const char *path)
{
   if (g_hbQN >= g_hbQCap) { g_hbQCap = g_hbQCap ? g_hbQCap * 2 : 256;
      g_hbQ = (HDQITEM *)realloc(g_hbQ, (size_t)g_hbQCap * sizeof(HDQITEM)); }
   if (g_hbQ == NULL) { g_hbQN = g_hbQCap = 0; return; }
   g_hbQ[g_hbQN].fr = framePtr;
   SDL_snprintf(g_hbQ[g_hbQN].path, sizeof g_hbQ[g_hbQN].path, "%s", path);
   g_hbQN++;
}

/* Called once after PAL_LoadBattleSprites queued all frames: start streaming them in. */
void PAL_HDBattle_BeginStream(void) { g_hbLoading = (g_hbQN > 0); }

/* Called each battle frame (after PAL_HDBattle_Begin): decode a ~10ms chunk of queued HD frames. */
void PAL_HDBattle_LoadStep(void)
{
   Uint32 start;
   if (!g_hbLoading) return;
   start = SDL_GetTicks();
   while (g_hbQPos < g_hbQN)
   {
      PAL_HD_AddImg(g_hbQ[g_hbQPos].fr, g_hbQ[g_hbQPos].path, 0); g_hbQPos++;   /* battle frames: not terrain tiles */
      if ((g_hbQPos & 3) == 0 && SDL_GetTicks() - start >= 10u) break;
   }
   if (g_hbQPos >= g_hbQN)
   { g_hbLoading = 0; free(g_hbQ); g_hbQ = NULL; g_hbQN = g_hbQCap = g_hbQPos = 0; }
}

int PAL_HDBattle_LoadBg(const char *dir, int field)
{
   char path[512]; FILE *f; unsigned short wh[2];
   if (g_hdBg.px) { free(g_hdBg.px); g_hdBg.px = NULL; }
   SDL_snprintf(path, sizeof path, "%s/fbp%03d.rgba", dir, field);
   f = fopen(path, "rb"); if (f == NULL) return 0;
   if (fread(wh, 2, 2, f) == 2)
   {
      int w = wh[0], h = wh[1], k; size_t bytes = (size_t)w * h * 4;
      unsigned char *buf = (unsigned char *)malloc(bytes);
      if (buf && fread(buf, 1, bytes, f) == bytes)
      {
         g_hdBg.w = w; g_hdBg.h = h; g_hdBg.px = (Uint32 *)malloc(bytes);
         for (k = 0; k < w * h; k++) { unsigned char *q = buf + k * 4;
            g_hdBg.px[k] = ((Uint32)q[3] << 24) | ((Uint32)q[0] << 16) | ((Uint32)q[1] << 8) | q[2]; }
      }
      free(buf);
   }
   fclose(f);
   return g_hdBg.px != NULL;
}

void PAL_HDBattle_Begin(void)
{  /* clear the HD surface and lay down the HD background (opaque copy at top) */
   int y, x, dp; Uint32 *dst;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL) return;
   g_hdmDirty = 1; g_hdmStale = 0;
   SDL_FillRect(g_hdScreen, NULL, 0xFF000000u);
   if (g_hdBg.px == NULL) return;
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (y = 0; y < g_hdBg.h && y < HD_H; y++)
      for (x = 0; x < g_hdBg.w && x < HD_W; x++)
         dst[y * dp + x] = 0xFF000000u | (g_hdBg.px[y * g_hdBg.w + x] & 0xFFFFFF);
}

void PAL_HDBattle_Sprite(const void *framePtr, int x, int y)
{  /* alpha-blit the keyed HD frame at battle (x,y)×6 into g_hdScreen; while that frame's AI-HD image is
      still streaming in (or has none), ×6-nearest the live sprite so the fighter is crisp, not missing. */
   int fi = PAL_HD_FrameIdx(framePtr);
   if (fi >= 0 && g_hd[fi].px && g_hdScreen) PAL_HD_BlitImg(&g_hd[fi], x * HD_S, y * HD_S);
   else if (g_hdScreen) PAL_HDM_BlitRLE(framePtr, x, y);
}

/* Mirror a fullscreen FBP picture (title/cutscenes): 320×200 indices + bank -> nearest ×6 into g_hdScreen. */
void PAL_HDM_BlitFBP(const unsigned char *buf, int bank)
{
   extern const Uint32 *SAT_GetBankLUTFull(int bank);
   const Uint32 *lut; int x, y, dp; Uint32 *dst;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL || buf == NULL) return;
   g_hdmDirty = 1; g_hdmStale = 0;
   lut = SAT_GetBankLUTFull(bank);   /* full brightness: a fade dims the whole twin (colour mod); the live table is black right after a fade-out, which left the title picture black when a fade-out came before it */
   SDL_FillRect(g_hdScreen, NULL, 0xFF000000u);
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (y = 0; y < 200 * HD_S && y < HD_H; y++)
   {
      int sy = y / HD_S;
      for (x = 0; x < 320 * HD_S && x < HD_W; x++)
      { int idx = buf[sy * 320 + x / HD_S]; Uint32 c = lut ? lut[idx] : 0; dst[y * dp + x] = 0xFF000000u | (c & 0xFFFFFF); }
   }
}

/* Blit a REFINED HD portrait (rgm.mkf face<idx>.rgba, from PAL_HDBATTLE dir) at native (x,y)×6,
   overriding the generic nearest-×6 of the face. Small ring cache (dialogue shows 1-2 faces). */
void PAL_HDM_Face(int idx, int x, int y, int lw, int lh)
{
   static struct { int idx, w, h; Uint32 *px; } cache[6];
   static int cinit = 0, cnext = 0;
   const char *dir; int i, ci = -1;
   if (!PAL_HDM_On() || idx <= 0) return;
   dir = SDL_getenv("PAL_HDBATTLE"); if (!dir) return;
   if (!cinit) { for (i = 0; i < 6; i++) { cache[i].idx = -1; cache[i].px = NULL; } cinit = 1; }
   for (i = 0; i < 6; i++) if (cache[i].idx == idx && cache[i].px) { ci = i; break; }
   if (ci < 0)
   {
      char p[512]; FILE *f; unsigned short wh[2];
      SDL_snprintf(p, sizeof p, "%s/face%03d.rgba", dir, idx);
      f = fopen(p, "rb"); if (!f) return;
      if (fread(wh, 2, 2, f) == 2)
      {
         int w = wh[0], h = wh[1], k; size_t bytes = (size_t)w * h * 4;
         unsigned char *buf = (unsigned char *)malloc(bytes);
         if (buf && fread(buf, 1, bytes, f) == bytes)
         {
            ci = cnext; cnext = (cnext + 1) % 6;
            if (cache[ci].px) free(cache[ci].px);
            cache[ci].idx = idx; cache[ci].w = w; cache[ci].h = h; cache[ci].px = (Uint32 *)malloc(bytes);
            for (k = 0; k < w * h; k++) { unsigned char *q = buf + k * 4;
               cache[ci].px[k] = ((Uint32)q[3] << 24) | ((Uint32)q[0] << 16) | ((Uint32)q[1] << 8) | q[2]; }
         }
         free(buf);
      }
      fclose(f);
      if (ci < 0 || !cache[ci].px) return;
   }
   //
   // Scale the HD portrait to EXACTLY the low-res face footprint (lw x lh, in game pixels),
   // i.e. (lw*HD_S) x (lh*HD_S) anchored at the same spot. The HD .rgba files were authored at
   // assorted native sizes (420..798 wide); blitting them 1:1 let a portrait wider than lw*HD_S
   // spill to the right, over the dialogue text (upper-dialog faces like 苗人头领 start text at
   // x=88 -> HD x=528). Scaling to the original footprint keeps the portrait inside the box the
   // Saturn drew, so it can never cover the text.   [user: 苗人头领的头像把文字盖住了]
   //
   if (lw > 0 && lh > 0 && (cache[ci].w != lw * HD_S || cache[ci].h != lh * HD_S))
   {
      int tw = lw * HD_S, th = lh * HD_S, tx, ty, sw = cache[ci].w, sh = cache[ci].h;
      Uint32 *tmp = (Uint32 *)malloc((size_t)tw * th * 4);
      if (tmp)
      {
         for (ty = 0; ty < th; ty++)
         {
            int syy = ty * sh / th; const Uint32 *srow = cache[ci].px + (size_t)syy * sw; Uint32 *drow = tmp + (size_t)ty * tw;
            for (tx = 0; tx < tw; tx++) drow[tx] = srow[tx * sw / tw];
         }
         PAL_HDM_BlitRGBA(tmp, tw, th, x * HD_S, y * HD_S);
         free(tmp);
         return;
      }
   }
   PAL_HDM_BlitRGBA(cache[ci].px, cache[ci].w, cache[ci].h, x * HD_S, y * HD_S);
}

/* HD-mirror backup/restore — parallels VIDEO_BackupScreen/RestoreScreen on gpScreen so that the
   dialogue's "restore to erase the previous text" also erases the previous HD text (fixes the
   overlap of consecutive dialogues drawn at the same position). */
static SDL_Surface *g_hdBackup = NULL;
void PAL_HDM_Backup(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL) return;
   if (g_hdBackup == NULL)
      g_hdBackup = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdBackup) SDL_BlitSurface(g_hdScreen, NULL, g_hdBackup, NULL);
}
/* Lay one 320x200 indexed movie frame into the HD twin at nearest x6 (pal = its 256 colours) and make it the
   dialog-restore base. RNG movie frames never reached the twin, so in HD the screen stayed on the pre-movie
   scene for the whole movie. The caller keeps a dialog box that is up on top with PAL_HDM_DlgSave/Reapply
   (the HD box, not a copy of the low-res one: copying the presented low-res picture doubled the text). */
/* 1 while a dialog box stays up over the movie: the backup then keeps the box's face and name, as the low-res
   backup does, for a page break (0x8E) after the movie range (0x1ddd: Li's portrait and name vanished). */
int g_hdmKeepBackup = 0;
void PAL_HDM_FromIndexed(SDL_Surface *s8, const SDL_Color *pal)
{
   int x, y, dp; Uint32 *dst; Uint32 lut[256];
   if (!PAL_HDM_On() || s8 == NULL || pal == NULL) return;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL) return;
   for (x = 0; x < 256; x++) lut[x] = 0xFF000000u | ((Uint32)pal[x].r << 16) | ((Uint32)pal[x].g << 8) | pal[x].b;
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (y = 0; y < HD_H; y++)
   {
      int sy = y / HD_S; const Uint8 *sr;
      if (sy >= 200 || sy >= s8->h) { for (x = 0; x < HD_W; x++) dst[y * dp + x] = 0xFF000000u; continue; }
      sr = (const Uint8 *)s8->pixels + sy * s8->pitch;
      for (x = 0; x < HD_W; x++) dst[y * dp + x] = lut[sr[x / HD_S]];
   }
   g_hdmDirty = 1; g_hdmStale = 0;
   if (!g_hdmKeepBackup) PAL_HDM_Backup();
}
/* The bare movie frame last laid into the twin (no dialog box, face or name on it). A wait between two ranges of
   a movie (script 0x09) puts it back: the dialog-restore base above may hold the face and name of a page break
   (0x8E), which left "Lin Yueru:" over the next, unrelated line. */
static SDL_Surface *g_hdMovieBase = NULL;
void PAL_HDM_MovieBaseSave(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL) return;
   if (g_hdMovieBase == NULL)
      g_hdMovieBase = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdMovieBase) SDL_BlitSurface(g_hdScreen, NULL, g_hdMovieBase, NULL);
}
void PAL_HDM_MovieBaseRestore(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL || g_hdMovieBase == NULL) return;
   SDL_BlitSurface(g_hdMovieBase, NULL, g_hdScreen, NULL);
   g_hdmDirty = 1; g_hdmStale = 0;
   PAL_HDM_Backup();
}
void PAL_HDM_Restore(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL || g_hdBackup == NULL) return;
   SDL_BlitSurface(g_hdBackup, NULL, g_hdScreen, NULL);
   g_hdmDirty = 1; g_hdmStale = 0;
}

/* HD twin copy of a menu's "boxes without text" state, so a menu that re-draws its labels in place (the
   Saturn 否/是 confirm box: selected = yellow + shadow, unselected = black 1 px off) can wipe the old HD
   glyphs first.  Without it the low-res text was erased but the HD glyphs piled up -> ghosted 否/是.
   Rect is in gpScreen pixels. */
static SDL_Surface *g_hdMenuBase = NULL;
void PAL_HDM_MenuBaseSave(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL) return;
   if (g_hdMenuBase == NULL)
      g_hdMenuBase = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdMenuBase) SDL_BlitSurface(g_hdScreen, NULL, g_hdMenuBase, NULL);
}
void PAL_HDM_MenuBaseRestore(int x, int y, int w, int h)
{
   SDL_Rect r;
   if (!PAL_HDM_On() || g_hdScreen == NULL || g_hdMenuBase == NULL) return;
   r.x = x * HD_S; r.y = y * HD_S; r.w = w * HD_S; r.h = h * HD_S;
   SDL_BlitSurface(g_hdMenuBase, &r, g_hdScreen, &r);
   g_hdmDirty = 1; g_hdmStale = 0;
}

/* HD twin as it was before a pop-up selection box (否/是) opened; put back when the box closes.  The low-res
   boxes restore their saved area, but nothing erased them from the HD twin, so after a purchase the 否/是
   boxes stayed on screen until something else redrew that spot. */
/* Two more whole-twin snapshots for the dialogue log panel (0 = before the panel, 1 = empty panel). */
static SDL_Surface *g_hdSnap[3] = { NULL, NULL, NULL };   /* 2 = before a centre-window text box (script 0x3E) */
void PAL_HDM_SnapSave(int i)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL || i < 0 || i > 2) return;
   if (g_hdSnap[i] == NULL)
      g_hdSnap[i] = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdSnap[i]) SDL_BlitSurface(g_hdScreen, NULL, g_hdSnap[i], NULL);
}
void PAL_HDM_SnapRestore(int i, int x, int y, int w, int h)
{
   SDL_Rect r;
   if (!PAL_HDM_On() || g_hdScreen == NULL || i < 0 || i > 2 || g_hdSnap[i] == NULL) return;
   r.x = x * HD_S; r.y = y * HD_S; r.w = w * HD_S; r.h = h * HD_S;
   SDL_BlitSurface(g_hdSnap[i], &r, g_hdScreen, &r);
   g_hdmDirty = 1; g_hdmStale = 0;
}
static SDL_Surface *g_hdPopBase = NULL;
void PAL_HDM_PopupSave(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL) return;
   if (g_hdPopBase == NULL)
      g_hdPopBase = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdPopBase) SDL_BlitSurface(g_hdScreen, NULL, g_hdPopBase, NULL);
}
void PAL_HDM_PopupRestore(void)
{
   if (!PAL_HDM_On() || g_hdScreen == NULL || g_hdPopBase == NULL) return;
   SDL_BlitSurface(g_hdPopBase, NULL, g_hdScreen, NULL);
   g_hdmDirty = 1; g_hdmStale = 0;
}

/* Dialog pixels of the HD twin = whatever differs from the dialog-free snapshot (g_hdBackup).  The Saturn
   dialog wait redraws the map under an open box when the scene changed (SAT_DialogMapRefresh -> PAL_MakeScene);
   that recomposes the twin WITHOUT the box, so the dialog vanished from the HD window while the game waited
   for Enter (looked frozen: scene 2 enter script 469, "兩位大爺！酒菜送來了").  Save the dialog pixels before
   that redraw and lay them back over the new scene after it. */
static Uint32        *g_hdDlgPx = NULL;
static unsigned char *g_hdDlgMask = NULL;
static int            g_hdDlgSaved = 0;
/* The HD twin as it was before the dialog that is up now (no face, name or text): the twin of text.c's
   s_pSatDlgBase.  g_hdBackup cannot serve for that -- the first text line of a dialog backs the screen up WITH
   the face and name on it (for the page breaks), so restoring it at the next 0x3C/0x3D kept the previous
   speaker's face and name in the HD picture (two names over each other, a second portrait). */
static SDL_Surface *g_hdDlgBase = NULL;
static int          g_hdDlgBaseOk = 0;
void PAL_HDM_DlgBaseSave(void)
{
   g_hdDlgBaseOk = 0;
   if (!PAL_HDM_On() || g_hdScreen == NULL) return;
   if (g_hdDlgBase == NULL)
      g_hdDlgBase = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
   if (g_hdDlgBase) { SDL_BlitSurface(g_hdScreen, NULL, g_hdDlgBase, NULL); g_hdDlgBaseOk = 1; }
}
void PAL_HDM_DlgBaseRestore(void)
{
   if (!g_hdDlgBaseOk || !PAL_HDM_On() || g_hdScreen == NULL || g_hdDlgBase == NULL) return;
   SDL_BlitSurface(g_hdDlgBase, NULL, g_hdScreen, NULL);
   g_hdmDirty = 1; g_hdmStale = 0;
}
static int g_hdDlgSaveVsBase = 0;
void PAL_HDM_DlgSave(void)
{
   int x, y;
   SDL_Surface *g_hdBackup_ = g_hdBackup;
   g_hdDlgSaved = 0;
   if (g_hdDlgSaveVsBase && g_hdDlgBaseOk && g_hdDlgBase != NULL) g_hdBackup_ = g_hdDlgBase;
   if (!PAL_HDM_On() || g_hdScreen == NULL || g_hdBackup_ == NULL) return;
   if (g_hdDlgPx == NULL) g_hdDlgPx = (Uint32 *)malloc((size_t)HD_W * HD_H * sizeof(Uint32));
   if (g_hdDlgMask == NULL) g_hdDlgMask = (unsigned char *)malloc((size_t)HD_W * HD_H);
   if (g_hdDlgPx == NULL || g_hdDlgMask == NULL) return;
   for (y = 0; y < HD_H; y++)
   {
      const Uint32 *t = (const Uint32 *)((const Uint8 *)g_hdScreen->pixels + y * g_hdScreen->pitch);
      const Uint32 *b = (const Uint32 *)((const Uint8 *)g_hdBackup_->pixels + y * g_hdBackup_->pitch);
      Uint32 *o = g_hdDlgPx + (size_t)y * HD_W;
      unsigned char *m = g_hdDlgMask + (size_t)y * HD_W;
      for (x = 0; x < HD_W; x++) { m[x] = (unsigned char)(t[x] != b[x]); o[x] = t[x]; }
   }
   g_hdDlgSaved = 1;
}
/* Lay the saved dialog pixels back and keep them for the next movie frame (PAL_RNGPlay: saved once per range). */
void PAL_HDM_DlgReapplyKeep(void)
{
   extern void PAL_HDM_DlgReapply(void);
   int s = g_hdDlgSaved; PAL_HDM_DlgReapply(); g_hdDlgSaved = s;
}
/* Same, with the dialog pixels taken against the dialog-free base (face and name included). */
void PAL_HDM_DlgSaveAll(void)
{
   g_hdDlgSaveVsBase = 1; PAL_HDM_DlgSave(); g_hdDlgSaveVsBase = 0;
}
void PAL_HDM_DlgReapply(void)
{
   int x, y;
   if (!g_hdDlgSaved || !PAL_HDM_On() || g_hdScreen == NULL) return;
   for (y = 0; y < HD_H; y++)
   {
      Uint32 *t = (Uint32 *)((Uint8 *)g_hdScreen->pixels + y * g_hdScreen->pitch);
      const Uint32 *o = g_hdDlgPx + (size_t)y * HD_W;
      const unsigned char *m = g_hdDlgMask + (size_t)y * HD_W;
      for (x = 0; x < HD_W; x++) if (m[x]) t[x] = o[x];
   }
   g_hdDlgSaved = 0;
   g_hdmDirty = 1; g_hdmStale = 0;
}

/* #5: a soft, slowly-pulsing golden border drawn into the HD twin tightly AROUND an interactable object's
   sprite image (its outer frame), NOT a ground ring.  (gx,gy,gw,gh) is the sprite's rect in gpScreen pixels;
   it is scaled ×HD_S here.  A band `t` HD-pixels wide hugs the rectangle edge with a soft alpha falloff and a
   time triangle-wave brightness pulse (no sqrt/sin -> no new math dependency). */
void PAL_HDM_HighlightBox(int gx, int gy, int gw, int gh)
{
   int S = HD_S, x0 = gx * S, y0 = gy * S, w = gw * S, h = gh * S;
   int t = 3 * S;                     /* border half-thickness in HD px (sits on the sprite edge) */
   int pad = 1 * S;                   /* push the band 1 gpScreen-px outside the sprite silhouette */
   int bx, by, dp;
   Uint32 *dst;
   unsigned tms; float ph, amp;
   int ox0 = x0 - pad, oy0 = y0 - pad, ox1 = x0 + w + pad, oy1 = y0 + h + pad;   /* outer rect */
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL || w <= 0 || h <= 0) return;
   tms = SDL_GetTicks() % 1100u;
   ph  = (tms < 550u) ? (tms / 550.0f) : ((1100u - tms) / 550.0f);   /* 0..1..0 */
   amp = 0.45f + 0.55f * ph;
   g_hdmDirty = 1; g_hdmStale = 0;
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (by = oy0 - t; by <= oy1 + t; by++)
   {
      int py = by; if (py < 0 || py >= HD_H) continue;
      for (bx = ox0 - t; bx <= ox1 + t; bx++)
      {
         int px = bx; int ddx, ddy, d; float a; Uint32 bg; unsigned A, br, bgc, bb, rr, rg, rb;
         if (px < 0 || px >= HD_W) continue;
         /* signed distance to the rectangle outline: 0 on the edge, grows inward and outward */
         ddx = (px < ox0) ? (ox0 - px) : (px > ox1) ? (px - ox1) : 0;
         ddy = (py < oy0) ? (oy0 - py) : (py > oy1) ? (py - oy1) : 0;
         if (ddx && ddy) { d = (ddx > ddy) ? ddx : ddy; }        /* corner: Chebyshev */
         else if (ddx || ddy) { d = ddx + ddy; }                 /* outside one edge */
         else {                                                  /* inside: distance to nearest edge */
            int din = px - ox0; int dr = ox1 - px; int du = py - oy0; int dd2 = oy1 - py;
            d = din; if (dr < d) d = dr; if (du < d) d = du; if (dd2 < d) d = dd2;
            d = -d;                                              /* negative inside */
         }
         if (d > t || d < -t) continue;                          /* only the band hugging the edge */
         a = (1.0f - (float)(d < 0 ? -d : d) / (float)t) * amp;
         if (a <= 0.02f) continue;
         bg = dst[py * dp + px];
         br = (bg >> 16) & 0xFF; bgc = (bg >> 8) & 0xFF; bb = bg & 0xFF;
         A = (unsigned)(a * 230.0f); if (A > 255) A = 255;
         rr = (255 * A + br * (255 - A)) / 255;      /* golden 0xFFD740 */
         rg = (215 * A + bgc * (255 - A)) / 255;
         rb = (64  * A + bb * (255 - A)) / 255;
         dst[py * dp + px] = 0xFF000000u | (rr << 16) | (rg << 8) | rb;
      }
   }
}

/* #9: a subtle, translucent four-point sparkle "star" drawn into the HD twin, centred on (cx,cy) in gpScreen
   pixels -- the hint that an item can be examined.  About one map cell big (PAL_STAR_R game px radius), soft
   pale-gold, slowly breathing; procedural (no asset): a concave astroid (sqrt|x|+sqrt|y| <= 1) with a soft
   falloff toward the tips plus a small round core glow. */
#define PAL_STAR_R 10
void PAL_HDM_HighlightStar(int cx, int cy)
{
   int R = PAL_STAR_R * HD_S, X = cx * HD_S, Y = cy * HD_S, bx, by, dp;
   Uint32 *dst;
   unsigned tms; float ph, amp;
   PAL_HDM_EnsureScreen();
   if (g_hdScreen == NULL) return;
   tms = SDL_GetTicks() % 1600u;
   ph  = (tms < 800u) ? (tms / 800.0f) : ((1600u - tms) / 800.0f);   /* 0..1..0 */
   amp = 0.45f + 0.40f * ph;                                          /* never fully opaque */
   g_hdmDirty = 1; g_hdmStale = 0;
   dst = (Uint32 *)g_hdScreen->pixels; dp = g_hdScreen->pitch / 4;
   for (by = -R; by <= R; by++)
   {
      int py = Y + by; if (py < 0 || py >= HD_H) continue;
      for (bx = -R; bx <= R; bx++)
      {
         int px = X + bx; float fx, fy, q, a, core; Uint32 bg; unsigned A, br, bgc, bb;
         if (px < 0 || px >= HD_W) continue;
         fx = (float)(bx < 0 ? -bx : bx) / (float)R;
         fy = (float)(by < 0 ? -by : by) / (float)R;
         q = (float)SDL_sqrt(fx) + (float)SDL_sqrt(fy);          /* astroid: 1 on the star outline */
         a = (q < 1.0f) ? (1.0f - q) * 1.6f : 0.0f;              /* rays: bright spine, soft edge */
         core = 1.0f - (fx * fx + fy * fy) * 9.0f;               /* round glow, 1/3 of the radius */
         if (core > 0.0f) a += core * 0.6f;
         if (a > 1.0f) a = 1.0f;
         a *= amp;
         if (a <= 0.02f) continue;
         bg = dst[py * dp + px];
         br = (bg >> 16) & 0xFF; bgc = (bg >> 8) & 0xFF; bb = bg & 0xFF;
         A = (unsigned)(a * 255.0f);
         br  = (255 * A + br  * (255 - A)) / 255;                /* pale gold 0xFFF4C8 */
         bgc = (244 * A + bgc * (255 - A)) / 255;
         bb  = (200 * A + bb  * (255 - A)) / 255;
         dst[py * dp + px] = 0xFF000000u | (br << 16) | (bgc << 8) | bb;
      }
   }
}

static VOID
PAL_SceneDrawSprites(
   VOID
)
{
   int i, j, r, c, hf, l;
   const int vx = PAL_X(gpGlobals->viewport), vy = PAL_Y(gpGlobals->viewport);
   int tx, ty, th, ax, ay;
   LPCPALMAP map = PAL_GetCurrentMap();
   BOOL fWaved = FALSE;
   const char *g_hddir = SDL_getenv("PAL_HDTILES");

   {  // one-shot: dump current map's tileset when PAL_TILEDUMP=dir (after fade settles)
      static int s_dumped = -2;
      const char *td = SDL_getenv("PAL_TILEDUMP");
      if (td && map && SDL_GetTicks() > 2500u && s_dumped != (map ? map->iMapNum : -1))
      { PAL_DumpCurrentMapTiles(td); s_dumped = map->iMapNum; }
   }
   {  // dump party sprites (indices, once) + palette (EVERY frame, overwrite) when PAL_SPRITEDUMP=dir
      static int s_spdumped = 0;
      const char *sd = SDL_getenv("PAL_SPRITEDUMP");
      if (sd)
      {
         PAL_DumpPaletteOnce(sd);  /* overwrites each frame -> last one is the fully-settled palette */
         {  static Uint32 dt = 0xFFFFFFFFu; if (dt == 0xFFFFFFFFu) { const char *e = SDL_getenv("PAL_SPRITEDUMP_T"); dt = e ? (Uint32)SDL_atoi(e) : 2500u; }
         if (!s_spdumped && SDL_GetTicks() > dt)
         { int i; char pfx[64];
           for (i = 0; i < MAX_PLAYABLE_PLAYER_ROLES; i++) { LPCSPRITE spr = PAL_GetPlayerSprite((BYTE)i);
              if (spr) { SDL_snprintf(pfx, sizeof pfx, "party%d_fr", i); PAL_DumpSpriteFrames(spr, sd, pfx); } }
           s_spdumped = 1; } }
      }
   }
   {  // one-shot: dump current scene's NPC (event-object) sprites when PAL_NPCDUMP=dir (after fade)
      static int s_npcdumped = -2;
      const char *nd = SDL_getenv("PAL_NPCDUMP");
      if (nd && SDL_GetTicks() > 2500u && s_npcdumped != (int)gpGlobals->wNumScene)
      { PAL_DumpEventObjectSprites(nd); s_npcdumped = (int)gpGlobals->wNumScene; }
   }
   {  // one-shot: dump ALL battle backgrounds (fbp.mkf) when PAL_FBPDUMP=dir (after fade settles)
      static int s_fbpdumped = 0;
      const char *fd = SDL_getenv("PAL_FBPDUMP");
      if (fd && !s_fbpdumped && SDL_GetTicks() > 2500u)
      { PAL_DumpBattleBackgrounds(fd); s_fbpdumped = 1; }
   }
   {  // one-shot: dump ALL battle sprites (f.mkf party + abc.mkf enemy) + faces (rgm.mkf) when PAL_BATTLEDUMP=dir
      static int s_bdumped = 0;
      const char *bd = SDL_getenv("PAL_BATTLEDUMP");
      if (bd && !s_bdumped && SDL_GetTicks() > 2500u)
      {
         FILE *fpABC = UTIL_OpenRequiredFile("abc.mkf");
         PAL_DumpPaletteOnce(bd);                       /* main palette + 8 sprite bank LUTs */
         PAL_DumpSpriteMKF(gpGlobals->f.fpF, bd, "party_");
         PAL_DumpSpriteMKF(fpABC, bd, "enemy_");
         PAL_DumpSpriteMKF(gpGlobals->f.fpFIRE, bd, "fx_");
         PAL_DumpFaces(bd);
         if (fpABC) fclose(fpABC);
         s_bdumped = 1;
      }
   }

   if (g_hddir && map)
   {
      PAL_HD_Load(map, g_hddir);   /* builds the queue on a scene change (non-blocking) */
      PAL_HD_LoadStep();           /* stream a chunk of HD files this frame */
      if (g_hdScreen == NULL)
         g_hdScreen = SDL_CreateRGBSurface(0, HD_W, HD_H, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
      if (g_hdScreen) SDL_FillRect(g_hdScreen, NULL, getenv("PAL_HDCLRMAG") ? 0xFFFF00FFu : 0xFF000000u);       /* clear every frame; compose HD below.
         While a scene's AI-HD assets still stream in (g_hdLoading), compose with the ×6-nearest fallback for the
         not-yet-loaded tiles/sprites (crisp pixel-art), upgrading each to the refined AI-HD image in place as it
         arrives -- so a scene load shows crisp HD throughout instead of the old blurry bilinear low-res. */
   }

   g_nSpriteToDraw = 0;

   //
   // Map tiles, in the Saturn's registration order.
   //
   PAL_SatViewTile(vx, vy, &tx, &ty, &th);
   ax = tx * 32 + th * 16 - vx - g_iSatMapShift; // view tile anchor relative to the view pixel
   ay = ty * 16 + th * 8 - vy - g_iSatMapShift;
   for (r = 0; r < SAT_ROWS; r++)
   {
      for (hf = 0; hf < 2; hf++)
      {
         for (c = 0; c < SAT_COLS; c++)
         {
            int mx = tx + c, my = ty + r, mh = th;
            if (th && hf) { mx++; my++; mh = 0; }
            else if (hf) mh = 1;
            if (mx < 0 || my < 0) continue;
            for (l = 0; l < 2; l++)
            {
               LPCBITMAPRLE lpTile = PAL_MapGetTileBitmap((BYTE)mx, (BYTE)my, (BYTE)mh, (BYTE)l, map);
               int h = PAL_MapGetTileHeight((BYTE)mx, (BYTE)my, (BYTE)mh, (BYTE)l, map) & 0xf;
               PAL_SatAdd(lpTile,
                  SAT_ORG_X + ax + 32 * c + 16 * hf,
                  SAT_TILE_ORG_Y + ay + 16 * r + 8 * hf,
                  h ? 16 * r + 8 * hf + 8 * h : 0);
            }
         }
      }
   }

   //
   // Players (PROG 0x06032c64)
   //
   for (i = 0; i <= (short)gpGlobals->wMaxPartyMemberIndex + gpGlobals->nFollower; i++)
   {
      LPCBITMAPRLE lpBitmap =
         PAL_SpriteGetFrame(PAL_GetPlayerSprite((BYTE)i), gpGlobals->rgParty[i].wFrame);
      int x = gpGlobals->rgParty[i].x, y = gpGlobals->rgParty[i].y;

      // bottom-anchored like event objects, 4 px higher: bottom row on screen line y + 1 (measured: lying Li
      // h=31 and standing Li h=50 share bottom line 113 at y = 112 in the opening room)
      if (lpBitmap != NULL && g_nSpriteToDraw < MAX_SPRITE_TO_DRAW)
      {
         PAL_AddSpriteToDraw(lpBitmap,
            SAT_ORG_X + x - (int)PAL_RLEGetWidth(lpBitmap) / 2,
            y + 1 - SAT_WINDOW_Y - (int)PAL_RLEGetHeight(lpBitmap),
            y + (SHORT)gpGlobals->wLayer + 6);
      }
   }

   //
   // Event Objects (PROG 0x06034fa0)
   //
   for (i = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
      i < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; i++)
   {
      LPCBITMAPRLE     lpFrame;
      LPCSPRITE        lpSprite;
      LPEVENTOBJECT    lpEvtObj = &(gpGlobals->g.lprgEventObject[i]);
      int              iFrame, x, y;

      if (lpEvtObj->sState == kObjStateHidden || lpEvtObj->sVanishTime > 0 ||
         lpEvtObj->sState < 0)
      {
         continue;
      }

      lpSprite = PAL_GetEventObjectSprite((WORD)i + 1);
      if (lpSprite == NULL)
      {
         continue;
      }

      iFrame = lpEvtObj->wCurrentFrameNum;
      if (lpEvtObj->nSpriteFrames == 3)
      {
         if (iFrame == 2) iFrame = 0;
         if (iFrame == 3) iFrame = 2;
      }

      lpFrame = PAL_SpriteGetFrame(lpSprite,
         lpEvtObj->wDirection * lpEvtObj->nSpriteFrames + iFrame);
      if (lpFrame == NULL)
      {
         continue;
      }

      x = (SHORT)lpEvtObj->x - vx;
      y = (SHORT)lpEvtObj->y - vy;
      if (x < -32 || x >= 320 + 64 || y < -128 || y >= 224 + 128)
      {
         continue;                                // Saturn culls x < -32; the rest is off-screen anyway
      }

      if (getenv("PAL_EVDUMP"))
      {
         static FILE *fev;
         if (!fev) fev = fopen(getenv("PAL_EVDUMP"), "w");
         if (fev) fprintf(fev, "t%u sc%d ev%d spr%d dir%d fr%d nf%d nfa%d st%d tmode%d tscr%d ascr%d layer%d xy%d,%d scr%d,%d\n", SDL_GetTicks(),
            gpGlobals->wNumScene, i + 1, lpEvtObj->wSpriteNum, lpEvtObj->wDirection, iFrame, lpEvtObj->nSpriteFrames,
            lpEvtObj->nSpriteFramesAuto, lpEvtObj->sState, lpEvtObj->wTriggerMode, lpEvtObj->wTriggerScript, lpEvtObj->wAutoScript,
            lpEvtObj->sLayer, lpEvtObj->x, lpEvtObj->y, x, y), fflush(fev);
      }
      // Event objects are anchored by their BOTTOM edge: measured in an emulator (opening room, 李大娘 h=61 and
      // the mat h=31, layer -3) the bottom row lands on screen line y' + 5 whatever the height or layer;
      // screen line = gpScreen row + SAT_WINDOW_Y.
      if (g_nSpriteToDraw < MAX_SPRITE_TO_DRAW)
      {
         PAL_AddSpriteToDraw(lpFrame,
            SAT_ORG_X + x - (int)PAL_RLEGetWidth(lpFrame) / 2,
            y + 5 - SAT_WINDOW_Y - (int)PAL_RLEGetHeight(lpFrame),
            y + lpEvtObj->sLayer * 8 + 6);
      }
   }

   //
   // Stable sort by key (back to front); equal keys keep registration order.
   //
   for (i = 1; i < g_nSpriteToDraw; i++)
   {
      SPRITE_TO_DRAW tmp = g_rgSpriteToDraw[i];
      for (j = i - 1; j >= 0 && g_rgSpriteToDraw[j].iLayer > tmp.iLayer; j--)
      {
         g_rgSpriteToDraw[j + 1] = g_rgSpriteToDraw[j];
      }
      g_rgSpriteToDraw[j + 1] = tmp;
   }

   //
   // Draw.  The screen wave (DOS feature, applied to the ground only) goes after the flat layer.
   //
   g_hdmSceneBusy = 1;   /* HD-mirror: scene blits below are the keyed-HD base, not overlays */
   for (i = 0; i < g_nSpriteToDraw; i++)
   {
      SPRITE_TO_DRAW *p = &g_rgSpriteToDraw[i];
      if (!fWaved && p->iLayer > 0)
      {
         PAL_ApplyWave(gpScreen);
         fWaved = TRUE;
      }
      PAL_RLEBlitToSurface(p->lpSpriteFrame, gpScreen, p->pos);
      if (g_hddir && map && g_hdScreen)
      {  // HD slice: terrain tiles AND party sprites composed at ×6 (matched by frame pointer)
         int fi = PAL_HD_FrameIdx(p->lpSpriteFrame);
         int useKeyed;
         if (getenv("PAL_HDTILELOG"))
         {  /* per-frame tally of keyed vs fallback (small = tile) -> diagnose #1/#4 seam/color flips */
            static FILE *ftl = NULL;
            static int frm = 0, cK = 0, cNK = 0, cDM = 0, cKs = 0, cNKs = 0, cDMs = 0;
            int tw = (int)PAL_RLEGetWidth(p->lpSpriteFrame);
            int thh = (int)PAL_RLEGetHeight(p->lpSpriteFrame);
            int sml = (tw <= 34 && thh <= 34);
            int keyed = (fi >= 0 && g_hd[fi].px && g_hd[fi].w == tw * HD_S && g_hd[fi].h == thh * HD_S);
            int nokey = (fi < 0);
            int dimmiss = (fi >= 0 && !keyed);
            if (!ftl) ftl = fopen(getenv("PAL_HDTILELOG"), "w");
            if (keyed) { cK++; if (sml) cKs++; }
            else if (nokey) { cNK++; if (sml) cNKs++; }
            else if (dimmiss) { cDM++; if (sml) cDMs++; }
            if (p == &g_rgSpriteToDraw[g_nSpriteToDraw - 1]) {   /* last entry of the frame -> flush */
               if (ftl) { fprintf(ftl, "f%d t%u keyed%d/%d nokey%d/%d dimmiss%d/%d (small/all)\n",
                  frm++, (unsigned)SDL_GetTicks(), cKs, cK, cNKs, cNK, cDMs, cDM); fflush(ftl); }
               cK = cNK = cDM = cKs = cNKs = cDMs = 0;
            }
         }
         // The keyed match is by frame POINTER, and the sprite decode buffer is reused across poses, so a
         // pointer can collide with a keyed entry built for a DIFFERENT pose (seen in the opening: 李逍遥's
         // lying frame 48x31 matched a keyed 144x300 standing image -> he showed standing in HD while the
         // low-res scene had him lying). Guard it: only trust the keyed HD image when its ×6 size matches
         // the frame being drawn; otherwise ×6-upscale the live sprite, which is always the correct pose.
         useKeyed = (fi >= 0 && g_hd[fi].px &&
             g_hd[fi].w == (int)PAL_RLEGetWidth(p->lpSpriteFrame) * HD_S &&
             g_hd[fi].h == (int)PAL_RLEGetHeight(p->lpSpriteFrame) * HD_S);
         /* #4 fix: the AI-upscaled terrain TILES have feathered/soft diamond edges; the blit writes any
            alpha>=40 pixel opaque, so each tile's dark soft rim renders as a boundary line -> a global
            diamond "wireframe" over floor, walls and stairs. The ×6-nearest fallback upscales the ORIGINAL
            hard-edged pixel-art tile, which tessellates seamlessly. So draw terrain tiles via the fallback
            (crisp + seam-free) while sprites/characters keep their keyed AI-HD image. PAL_HDTILEKEY=1 forces
            the old keyed-tile path for comparison. */
         if (useKeyed && g_hd[fi].isTile && !getenv("PAL_HDTILEKEY")) useKeyed = 0;
         if (useKeyed)
            PAL_HD_BlitImg(&g_hd[fi], PAL_X(p->pos) * HD_S, PAL_Y(p->pos) * HD_S);
         else   /* always: a scene with NO prebuilt HD at all (g_hdN == 0, e.g. map 4) used to draw nothing -> black twin presented */
            // No matching keyed HD (asset still streaming in, a cutscene-only pose, or a pointer-collision
            // mismatch): ×6-upscale the original sprite so the CORRECT pose shows crisp-nearest (not refined,
            // not blurry) instead of a wrong one. `|| g_hdLoading` keeps the compose crisp from the first
            // frame of a scene load, before any AI-HD file has finished decoding.
            PAL_HDM_BlitRLE(p->lpSpriteFrame, PAL_X(p->pos), PAL_Y(p->pos));
      }
   }
   if (!fWaved)
   {
      PAL_ApplyWave(gpScreen);
   }
   g_hdmSceneBusy = 0;   /* HD-mirror: scene base done; subsequent gpScreen blits are overlays */
   if (g_hddir && map && g_hdScreen)
   {  // mark this frame's HD compose fresh (for live window present), + throttled BMP dump
      static Uint64 last = 0;
      g_hdFresh = 1; g_hdmStale = 0;   /* HD twin now reflects the composed scene -> present it every frame */
      PAL_HDM_Backup();   /* snapshot the dialog-free scene so PAL_HDM_Restore (at each dialog line) can erase
                             a stale HD portrait/text -- otherwise the HD twin, never cleared during a held
                             dialog box, keeps a previous speaker's portrait over the next (faceless) reply. */
      {  // #5/#2/#9: subtle translucent sparkle star ON an interactable item (was a golden border box; NPCs
         //        excluded -- see PAL_IsHighlightEventObject). Drawn AFTER PAL_HDM_Backup so the animated border
         //        is NOT frozen into the dialog-restore snapshot, and ONLY when g_fHighlightOK (the free-roam
         //        PAL_MakeScene from PAL_StartFrame set it this frame) -- never during a scripted cutscene.
         //        #9: EVERY interactable prop within 3 walking steps of the leader gets it (any direction).
         extern BOOL PAL_IsHighlightEventObject(INT k);
         extern int g_fHighlightOK;
         int hk;
         for (hk = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
            g_fHighlightOK && hk < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; hk++)
         {
            LPEVENTOBJECT ph = &gpGlobals->g.lprgEventObject[hk];
            LPCSPRITE     lpSpr;
            int           iFr = ph->wCurrentFrameNum;
            LPCBITMAPRLE  lpFr;
            if (!PAL_IsHighlightEventObject(hk)) continue;
            lpSpr = PAL_GetEventObjectSprite((WORD)hk + 1);
            if (ph->nSpriteFrames == 3) { if (iFr == 2) iFr = 0; else if (iFr == 3) iFr = 2; }
            lpFr = lpSpr ? PAL_SpriteGetFrame(lpSpr, ph->wDirection * ph->nSpriteFrames + iFr) : NULL;
            if (lpFr != NULL)
            {
               int w = (int)PAL_RLEGetWidth(lpFr), h = (int)PAL_RLEGetHeight(lpFr);
               int sx = (SHORT)ph->x - vx, sy = (SHORT)ph->y - vy;
               int gx = SAT_ORG_X + sx - w / 2;          /* same anchor as the sprite draw (bottom-centred) */
               int gy = sy + 5 - SAT_WINDOW_Y - h;
               PAL_HDM_HighlightStar(gx + w / 2, gy + h / 2);   /* on the item itself */
            }
            else if (ph->wSpriteNum == 0)
            {
               /* Sprite-less search spot: the item (柜子/水缸/箱子...) is painted in the MAP tiles, so put the
                  star on the map cell the object sits on (a little above the floor diamond's centre). */
               int sx = (SHORT)ph->x - vx, sy = (SHORT)ph->y - vy;
               PAL_HDM_HighlightStar(SAT_ORG_X + sx, sy - SAT_WINDOW_Y - 6);
            }
         }
      }
      if (getenv("PAL_CARRYLOG"))
      {  /* #6: per-compose-frame trace of role-0 (carrying Li) HD decision — wFrame is the smoking gun
            (front dirs 0-2/9-11 hold the 酒菜 tray; back dirs 3-8 correctly hide it). */
         static FILE *fc = NULL;
         LPCSPRITE s0 = PAL_GetPlayerSprite(0);
         WORD wf0 = gpGlobals->rgParty[0].wFrame;
         LPCBITMAPRLE fr0 = s0 ? PAL_SpriteGetFrame(s0, wf0) : NULL;
         int fi0 = fr0 ? PAL_HD_FrameIdx(fr0) : -2;
         int w0 = fr0 ? (int)PAL_RLEGetWidth(fr0) : 0;
         int h0 = fr0 ? (int)PAL_RLEGetHeight(fr0) : 0;
         int keyed0 = (fi0 >= 0 && g_hd[fi0].px && g_hd[fi0].w == w0 * HD_S && g_hd[fi0].h == h0 * HD_S);
         if (!fc) fc = fopen(getenv("PAL_CARRYLOG"), "w");
         if (fc) { fprintf(fc, "t%u spNum0=%d wFrame=%d fr=%p fi=%d w=%d h=%d loading=%d keyed=%d hdN=%d\n",
            (unsigned)SDL_GetTicks(), gpGlobals->g.PlayerRoles.rgwSpriteNum[0],
            wf0, (void *)fr0, fi0, w0, h0, g_hdLoading, keyed0, g_hdN); fflush(fc); }
      }
      {  /* hd_frame dump; interval env-tunable (PAL_HDFRAMEMS, default 400) + optional numbered seq (PAL_HDSEQ=dir) */
         static Uint32 ival = 0xFFFFFFFFu;
         if (ival == 0xFFFFFFFFu) { const char *e = getenv("PAL_HDFRAMEMS"); ival = e ? (Uint32)SDL_atoi(e) : 400u; }
         if (SDL_GetTicks() - last >= ival)
         { char p[512]; const char *sq; last = SDL_GetTicks();
           SDL_snprintf(p, sizeof p, "%s/hd_frame.bmp", g_hddir);
           SDL_SaveBMP(g_hdScreen, p);
           sq = getenv("PAL_HDSEQ");
           if (sq) { static int sn = 0; char p2[512]; SDL_snprintf(p2, sizeof p2, "%s/seq%04d.bmp", sq, sn++); SDL_SaveBMP(g_hdScreen, p2); } }
      }
   }
}

VOID
PAL_ApplyWave(
   SDL_Surface         *lpSurface
)
/*++
   Purpose:

     Apply screen waving effect when needed.

   Parameters:

     [OUT] lpSurface - the surface to be proceed.

   Return value:

     None.

--*/
{
   int                  wave[32];
   int                  i, a, b;
   static int           index = 0;
   LPBYTE               p;
   BYTE                 buf[320];

   gpGlobals->wScreenWave += gpGlobals->sWaveProgression;

   if (gpGlobals->wScreenWave == 0 || gpGlobals->wScreenWave >= 256)
   {
      //
      // No need to wave the screen
      //
      gpGlobals->wScreenWave = 0;
      gpGlobals->sWaveProgression = 0;
      return;
   }

   //
   // Calculate the waving offsets.
   //
   a = 0;
   b = 60 + 8;

   for (i = 0; i < 16; i++)
   {
      b -= 8;
      a += b;

      //
      // WARNING: assuming the screen width is 320
      //
      wave[i] = a * gpGlobals->wScreenWave / 256;
      wave[i + 16] = 320 - wave[i];
   }

   //
   // Apply the effect.
   // WARNING: only works with 320x200 8-bit surface.
   //
   a = index;
   p = (LPBYTE)(lpSurface->pixels);

   //
   // Loop through all lines in the screen buffer.
   //
   for (i = 0; i < 200; i++)
   {
      b = wave[a];

      if (b > 0)
      {
         //
         // Do a shift on the current line with the calculated offset.
         //
         memcpy(buf, p, b);
         //memmove(p, p + b, 320 - b);
         memmove(p, &p[b], 320 - b);
         //memcpy(p + 320 - b, buf, b);
         memcpy(&p[320 - b], buf, b);
      }

      a = (a + 1) % 32;
      p += lpSurface->pitch;
   }

   index = (index + 1) % 32;
}

DWORD g_dwSatSceneDrawnHash = 0;   /* PAL_SatSceneHash() at the last PAL_MakeScene */

DWORD
PAL_SatSceneHash(
   VOID
)
/* what PAL_MakeScene would draw: viewport, party sprites, the scene's event objects */
{
   DWORD h = 2166136261u;
   int i;
#define SAT_HM(v) (h = (h ^ (DWORD)(v)) * 16777619u)
   SAT_HM(gpGlobals->viewport); SAT_HM(gpGlobals->wNumScene); SAT_HM(gpGlobals->wMaxPartyMemberIndex);
   for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
   {
      SAT_HM(gpGlobals->rgParty[i].x); SAT_HM(gpGlobals->rgParty[i].y); SAT_HM(gpGlobals->rgParty[i].wFrame);
   }
   for (i = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
      i < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; i++)
   {
      LPEVENTOBJECT p = &gpGlobals->g.lprgEventObject[i];
      SAT_HM(p->x); SAT_HM(p->y); SAT_HM(p->sState); SAT_HM(p->wCurrentFrameNum); SAT_HM(p->wDirection);
      SAT_HM(p->wSpriteNum); SAT_HM(p->sLayer); SAT_HM(p->sVanishTime);
   }
#undef SAT_HM
   return h;
}

VOID
PAL_MakeScene(
   VOID
)
/*++
   Purpose:

     Draw the scene of the current frame to the screen. Both the map and
     the sprites are handled here.

   Parameters:

     None.

   Return value:

     None.

--*/
{
   static SDL_Rect         rect = {0, 0, 320, PAL_SCREEN_H};
   extern BOOL             g_fSatRNGBackdrop;

   g_fSatRNGBackdrop = FALSE;   /* the map replaces the last movie frame */
   { extern VOID PAL_SatPrevViewSave(VOID); PAL_SatPrevViewSave(); }   /* the view of this draw, for the scene loader's fade-out redraw (res.c) */

   //
   // Step 1: Draw the complete map, for both of the layers.
   //
   rect.x = PAL_X(gpGlobals->viewport);
   rect.y = PAL_Y(gpGlobals->viewport);

   SDL_FillSurfaceRect(gpScreen, NULL, 0); SAT_BankFill(gpScreen, NULL, 0);   // Saturn maps leave the backdrop undrawn
   (void)rect;

   //
   // Step 2: map tiles + sprites are z-sorted together like the Saturn (see PAL_SceneDrawSprites);
   // the screen wave is applied there after the flat layer.
   //

   //
   // Step 3: Draw all the sprites.
   //
   PAL_SceneDrawSprites();

   //
   // The Saturn field display window ends at x = 316 (measured: columns 317..319 always black on the map).
   //
   {
      SDL_Rect clip = { 317, 0, 3, PAL_SCREEN_H };
      SDL_FillSurfaceRect(gpScreen, &clip, 0); SAT_BankFill(gpScreen, &clip, 0);
   }

#if PAL_DEBUG_SHOW_SEARCH_TRIGGER_RANGE
   //
   // Display the effective search trigger range
   //
   PAL_ShowSearchTriggerRange();
#endif

   g_dwSatSceneDrawnHash = PAL_SatSceneHash();

   //
   // Check if we need to fade in.
   //
   if (gpGlobals->fNeedToFadeIn)
   {
      VIDEO_UpdateScreen(NULL);
      extern BOOL g_fSatReloadFade; extern BOOL PAL_SatReloadFadeIn(INT, BOOL);
      if (!(g_fSatReloadFade && PAL_SatReloadFadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette)))
         PAL_SatFadeIn(gpGlobals->wNumPalette, gpGlobals->fNightPalette);   /* emulator: new scene appears at once */
      g_fSatReloadFade = FALSE;
      gpGlobals->fNeedToFadeIn = FALSE;
   }

   //
   // Op 0x78 blacked the screen for the stepped fade-in of the next 0x05, but the script draws the scene
   // without one (0x4fb0 after the Caiyi battle: 0x78, 0x46, 0x7F, 0x7A, 0x09, end -- the field stayed black
   // for good). The first scene drawn after the 0x78 does the fade-in then.
   //
   {
      extern BOOL PAL_SatReloadPending(VOID); extern BOOL PAL_SatRedrawFaded(INT, BOOL);
      static BOOL s_fIn = FALSE;
      if (!s_fIn && !gpGlobals->fInBattle && PAL_SatReloadPending())
      {
         s_fIn = TRUE;
         PAL_SatRedrawFaded(gpGlobals->wNumPalette, gpGlobals->fNightPalette);
         s_fIn = FALSE;
      }
   }
}

BOOL
PAL_CheckObstacle(
    PAL_POS         pos,
    BOOL            fCheckEventObjects,
    WORD            wSelfObject
)
{
    return PAL_CheckObstacleWithRange(pos, fCheckEventObjects, wSelfObject, FALSE);
}

BOOL
PAL_CheckObstacleWithRange(
    PAL_POS         pos,
    BOOL            fCheckEventObjects,
    WORD            wSelfObject,
    BOOL			fCheckRange
)
/*++
   Purpose:

     Check if the specified location has obstacle or not.

   Parameters:

     [IN]  pos - the position to check.

     [IN]  fCheckEventObjects - TRUE if check for event objects, FALSE if only
           check for the map.

     [IN]  wSelfObject - the event object which will be skipped.

     [IN]  fCheckRange - whether need to check range.

   Return value:

     TRUE if the location is obstacle, FALSE if not.

--*/
{
   int x, y, h, xr, yr;
   int blockX = PAL_X(gpGlobals->partyoffset)/32, blockY = PAL_Y(gpGlobals->partyoffset)/16;

   //
   // Check if the map tile at the specified position is blocking
   //
   x = PAL_X(pos) / 32;
   y = PAL_Y(pos) / 16;
   h = 0;

   //
   // Avoid walk out of range, look out of map
   //
   if( fCheckRange )
   if (x < blockX || x >= 2048/32 - blockX ||   /* Saturn: viewport x 0..0x6A0 -> party x up to 1856 = tile 58 (emu st_r45a: follower placed at x 1856) */ y < blockY || y >= 2048 - blockY -1 )
   {
      return TRUE;
   }

   xr = PAL_X(pos) % 32;
   yr = PAL_Y(pos) % 16;

   if (xr + yr * 2 >= 16)
   {
      if (xr + yr * 2 >= 48)
      {
         x++;
         y++;
      }
      else if (32 - xr + yr * 2 < 16)
      {
         x++;
      }
      else if (32 - xr + yr * 2 < 48)
      {
         h = 1;
      }
      else
      {
         y++;
      }
   }

   if (PAL_MapTileIsBlocked(x, y, h, PAL_GetCurrentMap()))
   {
      return TRUE;
   }

   if (fCheckEventObjects)
   {
      //
      // Loop through all event objects in the current scene
      //
      int i;
      for (i = gpGlobals->g.rgScene[gpGlobals->wNumScene - 1].wEventObjectIndex;
         i < gpGlobals->g.rgScene[gpGlobals->wNumScene].wEventObjectIndex; i++)
      {
         LPEVENTOBJECT p = &(gpGlobals->g.lprgEventObject[i]);
         if (i == wSelfObject - 1)
         {
            //
            // Skip myself
            //
            continue;
         }

         //
         // Is this object a blocking one?
         //
         if (p->sState >= kObjStateBlocker)
         {
            //
            // Check for collision
            //
            if (abs(p->x - PAL_X(pos)) + abs(p->y - PAL_Y(pos)) * 2 < 16)
            {
               return TRUE;
            }
         }
      }
   }

   return FALSE;
}

VOID
PAL_SatMemberPos(
   int i
)
// Position of party member i from the trail (walking branch, and Saturn 0x75 -> 0x06032ec4).
// Standing frames do NOT recompute it on the Saturn (party[i] keeps its screen position).
{
   gpGlobals->rgParty[i].x = gpGlobals->rgTrail[1].x - PAL_X(gpGlobals->viewport);
   gpGlobals->rgParty[i].y = gpGlobals->rgTrail[1].y - PAL_Y(gpGlobals->viewport);

   if (i == 2)
   {
      gpGlobals->rgParty[i].x +=
         (gpGlobals->rgTrail[1].wDirection == kDirEast || gpGlobals->rgTrail[1].wDirection == kDirWest) ? -16 : 16;
      gpGlobals->rgParty[i].y += 8;
   }
   else
   {
      gpGlobals->rgParty[i].x +=
         ((gpGlobals->rgTrail[1].wDirection == kDirWest || gpGlobals->rgTrail[1].wDirection == kDirSouth) ? 16 : -16);
      gpGlobals->rgParty[i].y +=
         ((gpGlobals->rgTrail[1].wDirection == kDirWest || gpGlobals->rgTrail[1].wDirection == kDirNorth) ? 8 : -8);
   }

   //
   // Adjust the position if there is obstacle
   //
   if (PAL_CheckObstacleWithRange(PAL_XY(gpGlobals->rgParty[i].x + PAL_X(gpGlobals->viewport),
      gpGlobals->rgParty[i].y + PAL_Y(gpGlobals->viewport)), TRUE, 0, TRUE))
   {
      gpGlobals->rgParty[i].x = gpGlobals->rgTrail[1].x - PAL_X(gpGlobals->viewport);
      gpGlobals->rgParty[i].y = gpGlobals->rgTrail[1].y - PAL_Y(gpGlobals->viewport);
   }
}

static int s_iSatStepLeader = 0, s_iSatStepFollower = 0;
static int s_iSatStepCounter = 0;

VOID
PAL_SatPartyGesturesFromSteps(
   VOID
)
/* Saturn 0x06032ec4 (called by 0x75): frames from the current step counters, walking or not
   (emu s_tr3: after scene 21 0x1316's pans the leader stays on step 2 through 0x75 + 0x09 8). */
{
   int i;
   if (gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[0].wPlayerRole] == 4)
      gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * 4 + s_iSatStepCounter;
   else
      gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * 3 + s_iSatStepLeader;
   for (i = 1; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
   {
      if (gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[i].wPlayerRole] == 4)
         gpGlobals->rgParty[i].wFrame = gpGlobals->rgTrail[2].wDirection * 4 + s_iSatStepCounter;
      else
         gpGlobals->rgParty[i].wFrame = gpGlobals->rgTrail[2].wDirection * 3 + s_iSatStepFollower;
   }
}

VOID
PAL_UpdatePartyGestures(
   BOOL             fWalking
)
/*++
   Purpose:

     Update the gestures of all the party members.

   Parameters:

     [IN]  fWalking - whether the party is walking or not.

   Return value:

     None.

--*/
{
   static int       s_iThisStepFrame = 0;
   int              iStepFrameFollower = 0, iStepFrameLeader = 0;
   int              i;

   if (fWalking)
   {
      //
      // Update the gesture for party leader
      //
      s_iThisStepFrame = (s_iThisStepFrame + 1) % 4;
      if (s_iThisStepFrame & 1)
      {
         iStepFrameLeader = (s_iThisStepFrame + 1) / 2;
         iStepFrameFollower = 3 - iStepFrameLeader;
      }
      else
      {
         iStepFrameLeader = 0;
         iStepFrameFollower = 0;
      }
      s_iSatStepLeader = iStepFrameLeader;       // 0x06076980 / 0x06076982 keep their values while standing
      s_iSatStepFollower = iStepFrameFollower;

      gpGlobals->rgParty[0].x = PAL_X(gpGlobals->partyoffset);
      gpGlobals->rgParty[0].y = PAL_Y(gpGlobals->partyoffset);

      if (gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[0].wPlayerRole] == 4)
      {
         gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * 4 + s_iThisStepFrame;
      }
      else
      {
         gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * 3 + iStepFrameLeader;
      }

      //
      // Update the gestures and positions for other party members
      //
      for (i = 1; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
      {
         PAL_SatMemberPos(i);

         //
         // Update gesture for this party member
         //
         if (gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[i].wPlayerRole] == 4)
         {
            gpGlobals->rgParty[i].wFrame = gpGlobals->rgTrail[2].wDirection * 4 + s_iThisStepFrame;
         }
         else
         {
            gpGlobals->rgParty[i].wFrame = gpGlobals->rgTrail[2].wDirection * 3 + iStepFrameFollower;   // Saturn 0x060331c0: members use the follower step 0x06076982 (DOS: leader step)
         }
      }

      for (i = 1; i <= gpGlobals->nFollower; i++)
      {
         //
         // Update the position and gesture for the follower
         //
         gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + i].x =
            gpGlobals->rgTrail[2+i].x - PAL_X(gpGlobals->viewport);
         gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + i].y =
            gpGlobals->rgTrail[2+i].y - PAL_Y(gpGlobals->viewport);
         gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + i].wFrame =
            gpGlobals->rgTrail[2+i].wDirection * 3 + iStepFrameFollower;
      }
   }
   else
   {
      //
      // Player is not moved. Use the "standing" gesture instead of "walking" one.
      //
      i = gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[0].wPlayerRole];
      if (i == 0)
      {
         i = 3;
      }
      gpGlobals->rgParty[0].wFrame = gpGlobals->wPartyDirection * i;

      for (i = 1; i <= (short)gpGlobals->wMaxPartyMemberIndex; i++)
      {
         int f = gpGlobals->g.PlayerRoles.rgwWalkFrames[gpGlobals->rgParty[i].wPlayerRole];
         if (f == 0)
         {
            f = 3;
         }
         gpGlobals->rgParty[i].wFrame = gpGlobals->rgTrail[2].wDirection * f;
      }

      for (i = 1; i <= gpGlobals->nFollower; i++)
      {
         gpGlobals->rgParty[gpGlobals->wMaxPartyMemberIndex + i].wFrame =
            gpGlobals->rgTrail[2+i].wDirection * 3;
      }

      // Saturn idle keeps toggling s 0 <-> 2 every frame (emu s_tr3 frames 1151+: 0x06076984 = 0,2,0,2...), as DOS.
      s_iThisStepFrame &= 2;
      s_iThisStepFrame ^= 2;
   }
   s_iSatStepCounter = s_iThisStepFrame;
}

static BOOL s_fMoving = FALSE;
static int  s_rgDirHist[2] = { kDirUnknown, kDirUnknown };

VOID
PAL_ResetPartyWalkHeld(
   VOID
)
{
   /* Saturn: after a trigger script the pad history holds what is pressed now (emu o_m8, scene 59 obj923: 0x2ada
      ends at frame d556 with LEFT held -> the party steps 784,1016 -> 768,1008 in that frame, and 0x2add triggers
      there; a direction released during the script gives no step, as in st_corr) */
   s_fMoving = FALSE;
   s_rgDirHist[0] = g_InputState.dir; s_rgDirHist[1] = 0;   /* treat as already-held (no immediate step) */
}

VOID
PAL_ResetPartyWalk(
   VOID
)
{
   /* forget the walking state and the 2-frame pad history (held direction ignored until released) */
   s_fMoving = FALSE;
   s_rgDirHist[0] = kDirUnknown; s_rgDirHist[1] = 0;   /* no held direction, hold-counter cleared */
}

/* Saturn party step (0x06033654 after its turn check): step or slide in direction dir.
   Returns TRUE when the party moved (walk gesture set), FALSE otherwise (nothing changed). */
BOOL
PAL_SatPartyStep(
   int dir
)
{
   int              xSource, ySource, xTarget, yTarget, xOffset, yOffset, i;
   static const int rgSlide[4][2][4] = {
      /* S */ { { 0, 16, 16, 8 },    { -32, 0, -16, -8 } },
      /* W */ { { -32, 0, -16, 8 },  { 0, -16, 16, -8 } },
      /* N */ { { 0, -16, -16, -8 }, { 32, 0, 16, 8 } },
      /* E */ { { 32, 0, 16, -8 },   { 0, 16, -16, 8 } },
   };
   int vx0 = PAL_X(gpGlobals->viewport), vy0 = PAL_Y(gpGlobals->viewport), vx, vy, n = 0;
   int dx = 0, dy = 0;

   xOffset = ((dir == kDirWest || dir == kDirSouth) ? -16 : 16);
   yOffset = ((dir == kDirWest || dir == kDirNorth) ? -8 : 8);

   xSource = vx0 + PAL_X(gpGlobals->partyoffset);
   ySource = vy0 + PAL_Y(gpGlobals->partyoffset);

   xTarget = xSource + xOffset;
   yTarget = ySource + yOffset;

   if (!PAL_CheckObstacleWithRange(PAL_XY(xTarget, yTarget), TRUE, 0, TRUE))
   {
      dx = xOffset; dy = yOffset; n = 1;
   }
   else if (!PAL_CheckObstacleWithRange(PAL_XY(xTarget, yTarget), FALSE, 0, TRUE))
   {
      //
      // The tile straight ahead is blocked by an EVENT OBJECT (an NPC), not by the map
      // (map-only check above came back clear). When walking straight into an NPC the player
      // should stay put -- so you can face & talk to it -- instead of auto-sliding around it.
      // (Wall slides, handled below, still apply because those ARE map-blocked.)   [user request]
      //
      return FALSE;
   }
   /* user request: no wall slide -- blocked straight ahead (map or object) means the party stays put.
      (Saturn 0x06033654 slid diagonally along the obstacle here; rgSlide kept for reference.) */
   (void)rgSlide;

   if (n > 0)
   {
      vx = vx0 + dx; vy = vy0 + dy;
      if (vx > 0x6A0) vx = 0x6A0;
      if (vy > 0x730) vy = 0x730;
      if (vx < 0) vx = 0;
      if (vy < 0) vy = 0;
      if (n == 1)
      {
         if (vx == vx0) vy = vy0;
         if (vy == vy0) vx = vx0;
      }

      if (vx != vx0 || vy != vy0)
      {
         for (i = 3; i >= 0; i--)
         {
            gpGlobals->rgTrail[i + 1] = gpGlobals->rgTrail[i];
         }
         gpGlobals->rgTrail[0].wDirection = dir;
         gpGlobals->rgTrail[0].x = xSource;
         gpGlobals->rgTrail[0].y = ySource;

         gpGlobals->viewport = PAL_XY(vx, vy);
         PAL_UpdatePartyGestures(TRUE);
         return TRUE;
      }
   }
   return FALSE;
}

VOID
PAL_UpdateParty(
   VOID
)
/*++
   Purpose:

     Update the location and walking gesture of all the party members.

   Parameters:

     None.

   Return value:

     None.

--*/
{
   int              dir = g_InputState.dir;

   //
   // Modern walk controls (user request, replaces the Saturn turn-first behaviour): a short tap on a
   // direction moves exactly ONE tile, turning to face it in the same press; holding the direction keeps
   // walking after a brief repeat delay. (Saturn 0x06033654 turned the party on a new direction and only
   // stepped on the next frame, so a tap just turned -- which felt clunky here.)
   //
   // s_rgDirHist[0] carries the direction held on the previous frame (edge detection); s_rgDirHist[1]
   // counts how many frames it has been held. kDirUnknown resets both.
   //
#define PAL_WALK_GRACE 3          /* frames without a direction that still count as "walking" */
   static int s_iWalkGrace = 0;
#define PAL_WALK_REPEAT_DELAY 5   /* frames to hold before continuous walking kicks in (~250ms at 50ms/frame) */

   g_iSatSynch = 6;   /* Saturn 0x06026e5e: 6 vsync/frame while standing, 3 while moving (set below) */

   if (dir == kDirUnknown)
   {
      /* was walking continuously: remember it for a few frames, so that changing fingers (a short gap
         with no direction held) carries on walking instead of going through the tap delay again */
      if (s_rgDirHist[0] != kDirUnknown) s_iWalkGrace = (s_rgDirHist[1] >= PAL_WALK_REPEAT_DELAY) ? PAL_WALK_GRACE : 0;
      else if (s_iWalkGrace > 0) s_iWalkGrace--;
      s_fMoving = FALSE;
      s_rgDirHist[0] = kDirUnknown; s_rgDirHist[1] = 0;
      PAL_UpdatePartyGestures(FALSE);
      return;
   }

   g_iSatSynch = 3;

   if (gpGlobals->wPartyDirection != dir)
      gpGlobals->wPartyDirection = dir;   /* face the pressed direction on the same frame as the step */

   {
      int fFreshPress = (dir != s_rgDirHist[0]);
      int fStep;
      if (fFreshPress)
      {
         /* User report: holding up and then pressing left stalled for the tap delay before walking on.
            A change of direction while already walking (or right after, see s_iWalkGrace) keeps walking. */
         BOOL fWalking = (s_rgDirHist[0] != kDirUnknown) ? (s_rgDirHist[1] >= PAL_WALK_REPEAT_DELAY) : (s_iWalkGrace > 0);
         fStep = 1;                                                  /* tap -> one tile */
         s_rgDirHist[1] = fWalking ? PAL_WALK_REPEAT_DELAY : 0;
         s_iWalkGrace = 0;
      }
      else { s_rgDirHist[1]++; fStep = (s_rgDirHist[1] >= PAL_WALK_REPEAT_DELAY); }   /* held -> walk */
      s_rgDirHist[0] = dir;

      if (fStep)
      {
         s_fMoving = TRUE;
         if (PAL_SatPartyStep(dir))   /* 0x06033654: step (or slide); updates walk gesture + trail */
            return;
         /* blocked by a wall/object: fall through to show the turn/stand gesture */
      }
      else
      {
         s_fMoving = FALSE;   /* held, but still inside the repeat delay: stand facing the direction */
      }
   }

   PAL_UpdatePartyGestures(FALSE);
}

VOID
PAL_NPCWalkOneStep(
   WORD          wEventObjectID,
   INT           iSpeed
)
/*++
  Purpose:

    Move and animate the specified event object (NPC).

  Parameters:

    [IN]  wEventObjectID - the event object to move.

    [IN]  iSpeed - speed of the movement.

  Return value:

    None.

--*/
{
   LPEVENTOBJECT        p;

   //
   // Check for invalid parameters
   //
   if (wEventObjectID == 0 || wEventObjectID > gpGlobals->g.nEventObject)
   {
      return;
   }

   p = &(gpGlobals->g.lprgEventObject[wEventObjectID - 1]);

   //
   // Move the event object by the specified direction
   //
   p->x += ((p->wDirection == kDirWest || p->wDirection == kDirSouth) ? -2 : 2) * iSpeed;
   p->y += ((p->wDirection == kDirWest || p->wDirection == kDirNorth) ? -1 : 1) * iSpeed;

   //
   // Update the gesture
   //
   if (p->nSpriteFrames > 0)
   {
      p->wCurrentFrameNum++;
      p->wCurrentFrameNum %= (p->nSpriteFrames == 3 ? 4 : p->nSpriteFrames);
   }
   else if (p->nSpriteFramesAuto > 0)
   {
      p->wCurrentFrameNum++;
      p->wCurrentFrameNum %= p->nSpriteFramesAuto;
   }
}
