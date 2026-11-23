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

#include "font.h"
#include "util.h"
#include "text.h"
#include <math.h>

/* ---- optional TrueType (楷体) HD glyphs: env PAL_HDFONT=<path.ttf>; else the bitmap ×6 path is used ---- */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
static stbtt_fontinfo s_ttf;
static unsigned char *s_ttfBuf = NULL;
static int s_ttfState = 0;   /* 0 untried, 1 ready, -1 unavailable */
/* Translated text uses the typographic apostrophe / hyphen (the ASCII ones are colour codes of the dialogue
   renderer); the CJK font draws them as wide glyphs, so take the ASCII shapes instead. */
static int PAL_TTF_Map(int uc) { return uc == 0x2019 ? 0x27 : uc == 0x2010 ? 0x2D : uc; }

static int PAL_TTF_Ready(void)
{
   if (s_ttfState == 0)
   {
      const char *p = SDL_getenv("PAL_HDFONT");
      s_ttfState = -1;
      if (p && *p)
      {
         FILE *f = fopen(p, "rb");
         if (f)
         {
            long n; fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
            s_ttfBuf = (unsigned char *)malloc(n > 0 ? n : 1);
            if (s_ttfBuf && n > 0 && fread(s_ttfBuf, 1, n, f) == (size_t)n &&
                stbtt_InitFont(&s_ttf, s_ttfBuf, stbtt_GetFontOffsetForIndex(s_ttfBuf, 0)))
               s_ttfState = 1;
            fclose(f);
         }
      }
   }
   return s_ttfState == 1;
}

#define _FONT_C

#include "fontglyph.h"
#include "fontglyph_cn.h"
#include "fontglyph_tw.h"
#include "fontglyph_jp.h"
#include "ascii.h"

static int _font_height = 16;

//#define POINT_IMPORT 1

char *font_offset_x;
char *font_offset_y;

static uint8_t reverseBits(uint8_t x) {
    uint8_t y = 0;
    for (int i = 0 ; i < 8; i++){
        y <<= 1;
        y |= (x & 1);
        x >>= 1;
    }
    return y;
}

#ifdef DEBUG
void dump_font(BYTE *buf,int rows, int cols)
{
   for(int row=0;row<rows;row++)
   {
      for( int bit=0;bit<cols;bit++)
         if( buf[row] & (uint8_t)pow(2,bit) )
            printf("*");
         else
            printf(" ");
      printf("\n");
   }
}

static uint16_t reverseBits16(uint16_t x)
{
   //specified to unifont structure; not common means
   uint8_t l = reverseBits(x);
   uint8_t h = reverseBits(x >> 8);
   return h << 8 | l;
}

void dump_font16(WORD *buf,int rows, int cols)
{
   for(int row=0;row<rows;row++)
   {
      for( int bit=0;bit<cols;bit++)
         if( reverseBits16(buf[row]) & (uint16_t)pow(2,bit) )
            printf("*");
         else
            printf(" ");
      printf("\n");
   }
}
#endif

static void PAL_LoadISOFont(void)
{
    int         i, j;

    for (i = 0; i < sizeof(iso_font) / 15; i++)
    {
        for (j = 0; j < 15; j++)
        {
            unicode_font[i][j] = reverseBits(iso_font[i * 15 + j]);
        }

        unicode_font[i][15] = 0;
        font_width[i] = 16;
    }
}

static void PAL_LoadEmbeddedFont(void)
{
	FILE *fp;
	char *char_buf;
	wchar_t *wchar_buf;
	size_t nBytes;
	int nChars, i;

	//
	// Load the wor16.asc file.
	//
	if (NULL == (fp = UTIL_OpenFile("wor16.asc")))
	{
		return;
	}

	//
	// Get the size of wor16.asc file.
	//
	fseek(fp, 0, SEEK_END);
	nBytes = ftell(fp);

	//
	// Allocate buffer & read all the character codes.
	//
	if (NULL == (char_buf = (char *)malloc(nBytes)))
	{
		fclose(fp);
		return;
	}
	fseek(fp, 0, SEEK_SET);
	if (fread(char_buf, 1, nBytes, fp) < nBytes)
	{
		fclose(fp);
		return;
	}

	//
	// Close wor16.asc file.
	//
	fclose(fp);

	//
	// Detect the codepage of 'wor16.asc' and exit if not BIG5 or probability < 99
	// Note: 100% probability is impossible as the function does not recognize some special
	// characters such as bopomofo that may be used by 'wor16.asc'.
	//
	if (PAL_DetectCodePageForString(char_buf, nBytes, CP_BIG5, &i) != CP_BIG5 || i < 99)
	{
		free(char_buf);
		return;
	}

	//
	// Convert characters into unicode
	// Explictly specify BIG5 here for compatibility with codepage auto-detection
	//
	nChars = PAL_MultiByteToWideCharCP(CP_BIG5, char_buf, nBytes, NULL, 0);
	if (NULL == (wchar_buf = (wchar_t *)malloc(nChars * sizeof(wchar_t))))
	{
		free(char_buf);
		return;
	}
	PAL_MultiByteToWideCharCP(CP_BIG5, char_buf, nBytes, wchar_buf, nChars);
	free(char_buf);

	//
	// Read bitmaps from wor16.fon file.
	//
	fp = UTIL_OpenFile("wor16.fon");

	//
	// The font glyph data begins at offset 0x682 in wor16.fon.
	//
	fseek(fp, 0x682, SEEK_SET);

	//
	// Replace the original fonts
	//
	for (i = 0; i < nChars; i++)
	{
		wchar_t w = (wchar_buf[i] >= unicode_upper_base) ? (wchar_buf[i] - unicode_upper_base + unicode_lower_top) : wchar_buf[i];
		if (w >= sizeof(unicode_font) / sizeof(unicode_font[0])) { continue; }
		if (fread(unicode_font[w], 30, 1, fp) == 1)
		{
			unicode_font[w][30] = 0;
			unicode_font[w][31] = 0;
		}
		font_width[w] = 32;
	}
	free(wchar_buf);

	fclose(fp);

	_font_height = 15;
}

INT
PAL_LoadUserFont(
   LPCSTR      pszBdfFileName
)
/*++
  Purpose:

    Loads a BDF bitmap font file.

  Parameters:

    [IN]  pszBdfFileName - Name of BDF bitmap font file..

  Return value:

    0 = success, -1 = failure.

--*/
{
   char buf[4096];
   int state = 0, fstate = 0;
   int codepage = -1;
   DWORD dwEncoding = 0;
   BYTE bFontGlyph[32] = {0};
   int iCurHeight = 0;
   int bbw = 0, bbh = 0, bbox, bboy;
   int got_size;
   int size_x, size_y;
   int cstate = 0;
   int font_w_global = 0;
   int font_w = 16;

   FILE *fp = UTIL_OpenFileForMode(pszBdfFileName, "r");

   if (fp == NULL)
   {
      return -1;
   }

   _font_height = 16;

   while (fgets(buf, 4096, fp) != NULL)
   {
      if (state == 0)
      {
         if (strncmp(buf, "CHARSET_REGISTRY", 16) == 0)
         {
            if (strcasestr(buf, "Big5") != NULL)
            {
               codepage = CP_BIG5;
            }
            else if (strcasestr(buf, "GBK") != NULL || strcasestr(buf, "GB2312") != NULL)
            {
               codepage = CP_GBK;
            }
            else if (strcasestr(buf, "ISO10646") != NULL)
            {
               codepage = CP_UCS;
            }
            //else if (strstr(buf, "JISX0208") != NULL)
            //
            //  codepage = CP_JISX0208;
            //}
            else
            {
               TerminateOnError("Charset of %s is %s, which is not a supported yet.",pszBdfFileName,buf);
            }
         }
         else if (strncmp(buf, "STARTCHAR", 9) == 0)
         {
            cstate = 1;
            font_w = font_w_global;
         }
         else if (strncmp(buf, "ENCODING", 8) == 0)
         {
            dwEncoding = atoi(buf + 8);
         }
         else if (strncmp(buf, "SIZE", 3) == 0)
         {
            int bytes_consumed = 0, bytes_now;
            BOOL got_expected = FALSE;
            char size[10];
            sscanf(buf + bytes_consumed, "%s%n", size, &bytes_now);
            bytes_consumed += bytes_now;
            sscanf(buf + bytes_consumed, "%d%n", &got_size, &bytes_now); bytes_consumed += bytes_now;
            sscanf(buf + bytes_consumed, "%d%n", &size_x, &bytes_now); bytes_consumed += bytes_now;
            sscanf(buf + bytes_consumed, "%d%n", &size_y, &bytes_now); bytes_consumed += bytes_now;
               if (got_size == 16 || got_size == 15)
               {
                  _font_height = got_size;
                  got_expected = TRUE;
               }
            if (!got_expected)
               TerminateOnError("%s not contains expected font size 15/16!", pszBdfFileName);
         }
         /*else if (strncmp(buf, "SWIDTH", 6) == 0)
         {
             int bytes_consumed = 0, bytes_now;
             char swidth[10];
             int swidth_x, swidth_y;
             sscanf(buf + bytes_consumed, "%s%n", swidth, &bytes_now); bytes_consumed += bytes_now;
             sscanf(buf + bytes_consumed, "%d%n", &swidth_x, &bytes_now); bytes_consumed += bytes_now;
             sscanf(buf + bytes_consumed, "%d%n", &swidth_y, &bytes_now); bytes_consumed += bytes_now;
             font_w = (int)ceil(swidth_x / 1000.0 * (size_x / 72) * got_size / 2) << 1;
             if( font_w > 8 && font_w <= 16 )
                font_w = 16;
             if( !cstate ) {
                 swidth_x_global = swidth_x;
                 swidth_y_global = swidth_y;
                 font_w_global = font_w;
             }
         }*/
         else if (strncmp(buf, "DWIDTH", 6) == 0)
         {
             int bytes_consumed = 0, bytes_now;
             char dwidth[10];
             int dwidth_x, dwidth_y;
             sscanf(buf + bytes_consumed, "%s%n", dwidth, &bytes_now); bytes_consumed += bytes_now;
             sscanf(buf + bytes_consumed, "%d%n", &dwidth_x, &bytes_now); bytes_consumed += bytes_now;
             sscanf(buf + bytes_consumed, "%d%n", &dwidth_y, &bytes_now); bytes_consumed += bytes_now;
             font_w = dwidth_x;
             if (font_w > 8 && font_w <= 16)
                 font_w = 16;
             if (!cstate) {
                 font_w_global = font_w;
             }
         }
         else if (strncmp(buf, "BBX", 3) == 0)
         {
            int bytes_consumed = 0, bytes_now;
            char bbx[10];
            sscanf(buf+bytes_consumed,"%s%n",bbx,&bytes_now);bytes_consumed += bytes_now;
            sscanf(buf+bytes_consumed,"%d%n",&bbw,&bytes_now);bytes_consumed += bytes_now;
            sscanf(buf+bytes_consumed,"%d%n",&bbh,&bytes_now);bytes_consumed += bytes_now;
            sscanf(buf+bytes_consumed,"%d%n",&bbox,&bytes_now);bytes_consumed += bytes_now;
            sscanf(buf+bytes_consumed,"%d%n",&bboy,&bytes_now);bytes_consumed += bytes_now;
         }
         else if (strncmp(buf, "BITMAP", 6) == 0)
         {
            state = 1;
            iCurHeight = 0;
            memset(bFontGlyph, 0, sizeof(bFontGlyph));
         }
      }
      else if (state == 1)
      {
         if (strncmp(buf, "ENDCHAR", 7) == 0)
         {
            //
            // Replace the original fonts
            //
            BYTE szCp[3];
            szCp[0] = (dwEncoding >> 8) & 0xFF;
            szCp[1] = dwEncoding & 0xFF;
            szCp[2] = 0;

            if (codepage == CP_GBK && dwEncoding > 0xFF)
            {
               szCp[0] |= 0x80;
               szCp[1] |= 0x80;
            }

            wchar_t wc[2] = { 0 };
            PAL_MultiByteToWideCharCP(codepage, (LPCSTR)szCp, 2, wc, 1);
            if (wc[0] != 0)
            {
               wchar_t w = (wc[0] >= unicode_upper_base) ? (wc[0] - unicode_upper_base + unicode_lower_top) : wc[0];
#if POINT_IMPORT
               if (font_w <= 8) {
                   bFontGlyph[0] = bFontGlyph[14] = 0xff;
                   for (int m = 1; m <= 14; m++)
                       bFontGlyph[m] |= 0x81;
               }
               else {
                   bFontGlyph[0] = bFontGlyph[1] = bFontGlyph[28] = bFontGlyph[29] = 0xff;
                   for (int m = 2; m <= 27; m++)
                       bFontGlyph[m] |= ((m % 2) ? 1 : 0x80);
               }
#endif
               if (w < sizeof(unicode_font) / sizeof(unicode_font[0]))
               {
                  memcpy(unicode_font[w], bFontGlyph, sizeof(bFontGlyph));
                  font_width[w] = font_w << 1;
                  font_offset_x[w] = bbox;
                  font_offset_y[w] = _font_height - bboy - bbh - 4;
               }
               else
               {
                   UTIL_LogOutput(LOGLEVEL_DEBUG, "this user font has much code than unifont! ignoring %d\n", w);
               }
            }

            state = 0;
            cstate = 0;
         }
         else
         {
            if (iCurHeight < _font_height )
            {
               WORD wCode = strtoul(buf, NULL, 16);
               if(font_w <= 8)
               {
                  switch(fstate)
                  {
                     case 0:
                        bFontGlyph[iCurHeight * 2] = wCode;
                        fstate = 1;
                        break;
                     case 1:
                        bFontGlyph[iCurHeight * 2+1] = wCode;
                        fstate = 0;
                        iCurHeight++;
                        break;
                  }
               }else{
                  if(strnlen(buf,sizeof(buf))<=3){
                     bFontGlyph[iCurHeight * 2] = wCode;
                     bFontGlyph[iCurHeight * 2 + 1] = 0;
                  } else {
                     bFontGlyph[iCurHeight * 2] = (wCode >> 8);
                     bFontGlyph[iCurHeight * 2 + 1] = (wCode & 0xFF);
                  }
                  iCurHeight++;
               }
            }
         }
      }
   }

   fclose(fp);
   return 0;
}

int
PAL_InitFont(
   const CONFIGURATION* cfg
)
{
#define PAL_LOAD_INTERNAL_FONT(fontdata, height) \
   { \
      for (int i = 0; i < sizeof(fontdata) / sizeof(fontdata[0]); i++) \
      { \
         wchar_t w = fontdata[i].code; \
         w = (w >= unicode_upper_base) ? (w - unicode_upper_base + unicode_lower_top) : w; \
         if (w < sizeof(unicode_font) / sizeof(unicode_font[0])) \
         { \
            memcpy(unicode_font[w], fontdata[i].data, 32); \
            font_width[w] = 32; \
         } \
      } \
      _font_height = height; \
   }

    size_t fonts = sizeof(font_width);
    font_offset_x = calloc(1, fonts);
    font_offset_y = calloc(1, fonts);

   if (!cfg->pszMsgFile)
   {
      PAL_LoadEmbeddedFont();
   }

   if (g_TextLib.fUseISOFont)
   {
      PAL_LoadISOFont();
   }

   if (cfg->pszFontFile)
   {
      PAL_LoadUserFont(cfg->pszFontFile);
   }
   else if (_font_height != 15)
   {
      switch (g_TextLib.iFontFlavor)
      {
      case kFontFlavorAuto:
         switch (PAL_GetCodePage())
         {
         case CP_GBK:
            PAL_LOAD_INTERNAL_FONT(fontglyph_cn, 16);
            break;

         case CP_BIG5:
            PAL_LOAD_INTERNAL_FONT(fontglyph_tw, 15);
            break;

         default:
            break;
         }
         break;

      case kFontFlavorSimpChin:
         PAL_LOAD_INTERNAL_FONT(fontglyph_cn, 16);
         break;

      case kFontFlavorTradChin:
         PAL_LOAD_INTERNAL_FONT(fontglyph_tw, 15);
         break;

      case kFontFlavorJapanese:
         PAL_LOAD_INTERNAL_FONT(fontglyph_jp, 16);
         break;

      case kFontFlavorUnifont:
      default:
         break;
      }
   }

   /* Saturn: half-width characters come from ASCFONT.15 (256 glyphs x 15 rows, 8 px wide, MSB left) */
   {
      FILE *fpa = UTIL_OpenFile("ascfont.15");
      if (fpa != NULL)
      {
         BYTE a[256 * 15];
         if (fread(a, 1, sizeof(a), fpa) == sizeof(a))
         {
            int c, r;
            for (c = 0x20; c < 0x7F; c++)
            {
               for (r = 0; r < 15; r++) unicode_font[c][r] = a[c * 15 + r];
               unicode_font[c][15] = 0;
               font_width[c] = 16;
               if (font_offset_x) font_offset_x[c] = 0;
               if (font_offset_y) font_offset_y[c] = 0;
            }
         }
         fclose(fpa);
      }
   }

   return 0;
}

void
PAL_FreeFont(
	void
)
{
    free(font_offset_x);
    free(font_offset_y);
}

unsigned char g_bSatFontBank = 0;

/* Port: a text line in the TrueType HD font at an arbitrary size (hpx = glyph cell height in twin pixels),
   drawn straight into the HD twin with a soft drop shadow. Used for secondary UI text (save times, battle
   hints) that should be smaller than the 16-px game font. Returns the width in twin pixels, or -1 when the
   HD font is not available (caller falls back to the normal font). fDraw = 0 only measures. */
Uint32 g_dwHdTtfRGB = 0;
int
PAL_HDM_TTFText(const uint16_t *wsz, int hx, int hy, int hpx, uint8_t bColor, int fDraw)
{
   extern int PAL_HDM_On(void);
   extern VOID PAL_HDM_BlitRGBA(const Uint32 *px, int w, int h, int hx, int hy);
   extern const Uint32 *SAT_GetBankLUT(int bank);
   int ascent = 0, descent = 0, lineGap = 0, w = 0, pass, k, baseline;
   float scale; const Uint32 *lut; Uint32 col, *out;
   if (!PAL_HDM_On() || !PAL_TTF_Ready() || wsz == NULL || hpx < 8) return -1;
   scale = stbtt_ScaleForMappingEmToPixels(&s_ttf, (float)hpx * 0.92f);
   stbtt_GetFontVMetrics(&s_ttf, &ascent, &descent, &lineGap);
   for (k = 0; wsz[k]; k++)
   {
      int adv = 0, lsb = 0, gi = stbtt_FindGlyphIndex(&s_ttf, PAL_TTF_Map((int)wsz[k]));
      stbtt_GetGlyphHMetrics(&s_ttf, gi, &adv, &lsb);
      w += (int)(adv * scale + 0.5f);
   }
   if (!fDraw || w <= 0) return w;
   lut = SAT_GetBankLUT(3); col = lut ? (lut[bColor] & 0xFFFFFF) : 0xFFFFFF;
   if (g_dwHdTtfRGB) col = g_dwHdTtfRGB & 0xFFFFFF;   /* direct colour (dialogue log) */
   out = (Uint32 *)malloc((size_t)(w + 8) * (hpx + 8) * 4);
   if (out == NULL) return w;
   baseline = (hpx - (int)((ascent - descent) * scale)) / 2 + (int)(ascent * scale);
   for (pass = 0; pass < 2; pass++)   /* 0 = shadow (black, offset), 1 = text */
   {
      int pen = 0, sh = pass ? 0 : hpx / 16 + 1;
      memset(out, 0, (size_t)(w + 8) * (hpx + 8) * 4);
      for (k = 0; wsz[k]; k++)
      {
         int adv = 0, lsb = 0, bw = 0, bh = 0, bx0 = 0, by0 = 0, xx, yy, gi = stbtt_FindGlyphIndex(&s_ttf, PAL_TTF_Map((int)wsz[k]));
         unsigned char *bmp;
         stbtt_GetGlyphHMetrics(&s_ttf, gi, &adv, &lsb);
         bmp = gi > 0 ? stbtt_GetGlyphBitmap(&s_ttf, scale, scale, gi, &bw, &bh, &bx0, &by0) : NULL;
         if (bmp)
         {
            for (yy = 0; yy < bh; yy++) for (xx = 0; xx < bw; xx++)
            {
               int dx = pen + bx0 + xx, dy = baseline + by0 + yy; unsigned a = bmp[yy * bw + xx];
               if (a && dx >= 0 && dx < w + 8 && dy >= 0 && dy < hpx + 8) out[dy * (w + 8) + dx] = (a << 24) | (pass ? col : 0);
            }
            stbtt_FreeBitmap(bmp, 0);
         }
         pen += (int)(adv * scale + 0.5f);
      }
      PAL_HDM_BlitRGBA(out, w + 8, hpx + 8, hx + sh, hy + sh);
   }
   free(out);
   return w;
}
extern unsigned char *SAT_BankPlane(SDL_Surface *s, BOOL fCreate);

/* HD-mirror: render the same glyph at ×6 with anti-aliased edges into g_hdScreen (PAL_HDFULL). */
VOID
PAL_HDM_Char(uint16_t wChar, int x, int y, uint8_t bColor, BOOL fUse8x8)
{
   extern int PAL_HDM_On(void);
   extern VOID PAL_HDM_BlitRGBA(const Uint32 *px, int w, int h, int hx, int hy);
   extern const Uint32 *SAT_GetBankLUT(int bank);
   const int S = 6;
   int gw, gh, i, j, ox, oy, xoff, yoff, slot;
   unsigned uc = wChar;   /* original Unicode codepoint (for TTF), before the bitmap-table remap */
   int ttf = PAL_TTF_Ready();
   float *cov; Uint32 *out; const Uint32 *lut; Uint32 col;
   /* glyph cache: rendered HD glyph is independent of (x,y) -> cache by (uc,col,use8x8) */
   #define HDM_GC 1024
   static struct { int used, wch, u8, gw, gh, xoff, yoff; Uint32 col; Uint32 *px; } gc[HDM_GC];
   if (!PAL_HDM_On()) return;
   if ((wChar >= unicode_lower_top && wChar < unicode_upper_base) || wChar >= unicode_upper_top ||
       (_font_height == 8 && wChar >= 0x100))
      return;
   if (wChar >= unicode_upper_base) wChar -= (unicode_upper_base - unicode_lower_top);
   xoff = font_offset_x[wChar]; yoff = font_offset_y[wChar];
   lut = SAT_GetBankLUT(g_bSatFontBank); col = lut ? (lut[bColor] & 0xFFFFFF) : 0xFFFFFF;
   slot = (int)(((unsigned)uc * 131u + col * 2654435761u + (fUse8x8 ? 7u : 0u)) % HDM_GC);
   if (gc[slot].used && gc[slot].wch == (int)uc && gc[slot].u8 == (fUse8x8 ? 1 : 0) && gc[slot].col == col && gc[slot].px)
   {  /* cache hit */
      PAL_HDM_BlitRGBA(gc[slot].px, gc[slot].gw * S, gc[slot].gh * S, (x + gc[slot].xoff) * S, (y + gc[slot].yoff) * S);
      return;
   }
   if (fUse8x8) { gw = 8; gh = 8; }
   else if (font_width[wChar] == 32) { gw = 16; gh = _font_height; }
   else { gw = 8; gh = _font_height; }

   if (ttf && !fUse8x8)
   {  /* TrueType HD glyph: rasterize the Unicode codepoint, placed by baseline + advance width so
        the em sits inside the ×6 cell (robust to a font's side-bearings; no ink-bbox clipping). */
      int gi = stbtt_FindGlyphIndex(&s_ttf, PAL_TTF_Map((int)uc));
      int cw = gw * S, ch = gh * S;
      out = (Uint32 *)calloc((size_t)cw * ch, 4);
      if (gi > 0 && out)
      {
         int ascent = 0, descent = 0, lineGap = 0, adv = 0, lsb = 0, bw = 0, bh = 0, bx0 = 0, by0 = 0, xx, yy;
         float scale = stbtt_ScaleForMappingEmToPixels(&s_ttf, (float)ch * 0.92f);  /* em -> 92% of cell */
         unsigned char *bmp;
         stbtt_GetFontVMetrics(&s_ttf, &ascent, &descent, &lineGap);
         stbtt_GetGlyphHMetrics(&s_ttf, gi, &adv, &lsb);
         bmp = stbtt_GetGlyphBitmap(&s_ttf, scale, scale, gi, &bw, &bh, &bx0, &by0);
         if (bmp)
         {
            int empx = (int)((ascent - descent) * scale);
            int baseline = (ch - empx) / 2 + (int)(ascent * scale);   /* center the em vertically */
            int penx = (cw - (int)(adv * scale)) / 2;                 /* center by advance width */
            int ox0 = penx + bx0, oy0 = baseline + by0;
            for (yy = 0; yy < bh; yy++) for (xx = 0; xx < bw; xx++)
            {
               int dx = ox0 + xx, dy = oy0 + yy; unsigned a = bmp[yy * bw + xx];
               if (a && dx >= 0 && dx < cw && dy >= 0 && dy < ch) out[dy * cw + dx] = (a << 24) | col;
            }
            stbtt_FreeBitmap(bmp, 0);
         }
      }
      if (out)
      {
         PAL_HDM_BlitRGBA(out, cw, ch, (x + xoff) * S, (y + yoff) * S);
         if (gc[slot].px) free(gc[slot].px);
         gc[slot].used = 1; gc[slot].wch = (int)uc; gc[slot].u8 = 0; gc[slot].col = col;
         gc[slot].gw = gw; gc[slot].gh = gh; gc[slot].xoff = xoff; gc[slot].yoff = yoff; gc[slot].px = out;
      }
      return;
   }

   cov = (float *)calloc((size_t)gw * gh, sizeof(float));
   if (!cov) return;
   for (i = 0; i < gh; i++)
   {
      if (fUse8x8) { for (j = 0; j < 8; j++) if (iso_font_8x8[wChar][i] & (1 << j)) cov[i*gw+j] = 1.f; }
      else if (gw == 16)
      { for (j = 0; j < 8; j++) { if (unicode_font[wChar][i*2]   & (1 << (7-j))) cov[i*gw+j]   = 1.f;
                                  if (unicode_font[wChar][i*2+1] & (1 << (7-j))) cov[i*gw+8+j] = 1.f; } }
      else { for (j = 0; j < 8; j++) if (unicode_font[wChar][i] & (1 << (7-j))) cov[i*gw+j] = 1.f; }
   }
   out = (Uint32 *)malloc((size_t)gw * S * gh * S * 4);
   if (out)
   {
      for (oy = 0; oy < gh*S; oy++)
      {
         float fy = (oy + 0.5f) / S - 0.5f; int y0 = (int)floorf(fy); float ty = fy - y0;
         for (ox = 0; ox < gw*S; ox++)
         {
            float fx = (ox + 0.5f) / S - 0.5f; int x0 = (int)floorf(fx); float tx = fx - x0;
            float c00 = (x0>=0&&x0<gw&&y0>=0&&y0<gh)         ? cov[y0*gw+x0]       : 0;
            float c10 = (x0+1>=0&&x0+1<gw&&y0>=0&&y0<gh)     ? cov[y0*gw+x0+1]     : 0;
            float c01 = (x0>=0&&x0<gw&&y0+1>=0&&y0+1<gh)     ? cov[(y0+1)*gw+x0]   : 0;
            float c11 = (x0+1>=0&&x0+1<gw&&y0+1>=0&&y0+1<gh) ? cov[(y0+1)*gw+x0+1] : 0;
            float a = (c00*(1-tx)+c10*tx)*(1-ty) + (c01*(1-tx)+c11*tx)*ty;
            unsigned A = (unsigned)(a*255 + 0.5f); if (A > 255) A = 255;
            out[oy*gw*S+ox] = (A << 24) | col;
         }
      }
      PAL_HDM_BlitRGBA(out, gw*S, gh*S, (x+xoff)*S, (y+yoff)*S);
      /* store in cache (replace whatever was in the slot) */
      if (gc[slot].px) free(gc[slot].px);
      gc[slot].used = 1; gc[slot].wch = (int)uc; gc[slot].u8 = (fUse8x8 ? 1 : 0);
      gc[slot].col = col; gc[slot].gw = gw; gc[slot].gh = gh; gc[slot].xoff = xoff; gc[slot].yoff = yoff;
      gc[slot].px = out;   /* keep the buffer in cache (do NOT free) */
   }
   free(cov);
}

/* ---------------------------------------------------------------------------------------------
   Bake a TrueType (楷体) glyph into the LOW-RES bitmap-font table (unicode_font[idx]) the first
   time a codepoint is drawn. This removes the old Saturn bitmap font ENTIRELY: the HD twin already
   renders the TTF directly, and now the low-res path (shown whenever the HD present falls back --
   scene HD streaming, movies, etc.) reads these TTF-baked bits too, so the old bitmap font can no
   longer appear on ANY present path. No-op when the TTF is unavailable (keeps the original glyph),
   and glyphs the TTF lacks keep their original bitmap. uc = original codepoint, idx = table index.
   --------------------------------------------------------------------------------------------- */
static unsigned char s_ttfBaked[65536 / 8];
/* Translated UI text: draw a string squeezed horizontally so that it is at most maxw pixels wide (a long English
   item name in a column made for five Chinese characters).  Glyphs come straight from the TTF at the narrower
   horizontal scale; without the TTF, or when the text already fits, this is the normal PAL_DrawText. */
void PAL_DrawTextFit(const wchar_t *text, PAL_POS pos, unsigned char color, int shadow, int maxw)
{
   extern SDL_Surface *gpScreen; extern int PAL_TextWidth(LPCWSTR);
   int w = PAL_TextWidth(text), x = PAL_X(pos), y = PAL_Y(pos), k, pass;
   int ascent = 0, descent = 0, lineGap = 0, gh = 16;
   float scale, ratio;
   if (text == NULL || text[0] == 0) return;
   if (w <= maxw || maxw <= 0 || !PAL_TTF_Ready() || gpScreen == NULL) { PAL_DrawText(text, pos, color, shadow, FALSE, FALSE); return; }
   ratio = (float)maxw / (float)w;
   scale = stbtt_ScaleForMappingEmToPixels(&s_ttf, (float)gh * 0.92f);
   stbtt_GetFontVMetrics(&s_ttf, &ascent, &descent, &lineGap);
   for (pass = shadow ? 0 : 1; pass < 2; pass++)   /* 0 = shadow (one pixel right and down), 1 = the text */
   {
      float penx = (float)x;
      for (k = 0; text[k]; k++)
      {
         int cell = PAL_CharWidth(text[k]);
         int gi = stbtt_FindGlyphIndex(&s_ttf, PAL_TTF_Map((int)text[k]));
         int adv = 0, lsb = 0, bw = 0, bh = 0, bx0 = 0, by0 = 0, xx, yy;
         unsigned char *bmp;
         if (gi > 0 && (bmp = stbtt_GetGlyphBitmap(&s_ttf, scale * ratio, scale, gi, &bw, &bh, &bx0, &by0)) != NULL)
         {
            int empx = (int)((ascent - descent) * scale);
            int baseline = (gh - empx) / 2 + (int)(ascent * scale);
            int ox, oy = y + baseline + by0 + (pass == 0 ? 1 : 0);
            stbtt_GetGlyphHMetrics(&s_ttf, gi, &adv, &lsb);
            ox = (int)(penx + (cell * ratio - adv * scale * ratio) / 2 + 0.5f) + bx0 + (pass == 0 ? 1 : 0);
            for (yy = 0; yy < bh; yy++) for (xx = 0; xx < bw; xx++)
            {
               int dx = ox + xx, dy = oy + yy;
               if (bmp[yy * bw + xx] >= 96 && dx >= 0 && dx < gpScreen->w && dy >= 0 && dy < gpScreen->h)
                  ((unsigned char *)gpScreen->pixels)[dy * gpScreen->pitch + dx] = (pass == 0) ? 0 : color;
            }
            stbtt_FreeBitmap(bmp, 0);
         }
         penx += cell * ratio;
      }
   }
}

static void PAL_TTF_BakeLowRes(unsigned uc, int idx)
{
   int gw, gh, xx, yy, gi, ascent = 0, descent = 0, lineGap = 0, adv = 0, lsb = 0, bw = 0, bh = 0, bx0 = 0, by0 = 0;
   float scale; unsigned char *bmp; float *cov;
   if (idx < 0 || idx >= 65536) return;
   if (s_ttfBaked[idx >> 3] & (1 << (idx & 7))) return;   /* already handled this slot */
   if (!PAL_TTF_Ready()) return;                           /* no TTF -> keep original bitmap */
   s_ttfBaked[idx >> 3] |= (unsigned char)(1 << (idx & 7));/* try once; missing glyphs stay bitmap */

   gi = stbtt_FindGlyphIndex(&s_ttf, PAL_TTF_Map((int)uc));
   if (gi <= 0) return;

   gh = _font_height;
   gw = (font_width[idx] == 32) ? 16 : 8;
   cov = (float *)calloc((size_t)gw * gh, sizeof(float));
   if (!cov) return;

   scale = stbtt_ScaleForMappingEmToPixels(&s_ttf, (float)gh * 0.92f);  /* match the HD bake's em fit */
   stbtt_GetFontVMetrics(&s_ttf, &ascent, &descent, &lineGap);
   stbtt_GetGlyphHMetrics(&s_ttf, gi, &adv, &lsb);
   bmp = stbtt_GetGlyphBitmap(&s_ttf, scale, scale, gi, &bw, &bh, &bx0, &by0);
   if (bmp)
   {
      int empx = (int)((ascent - descent) * scale);
      int baseline = (gh - empx) / 2 + (int)(ascent * scale);   /* center the em vertically in the cell */
      int penx = (gw - (int)(adv * scale)) / 2;                 /* center by advance width */
      int ox0 = penx + bx0, oy0 = baseline + by0;
      for (yy = 0; yy < bh; yy++) for (xx = 0; xx < bw; xx++)
      {
         int dx = ox0 + xx, dy = oy0 + yy;
         if (dx >= 0 && dx < gw && dy >= 0 && dy < gh) cov[dy * gw + dx] = bmp[yy * bw + xx] / 255.0f;
      }
      stbtt_FreeBitmap(bmp, 0);
   }
   /* threshold the anti-aliased coverage to 1-bit and pack MSB-first, exactly how the draw loop reads it */
   memset(unicode_font[idx], 0, 32);
   for (yy = 0; yy < gh && yy < 16; yy++)
      for (xx = 0; xx < gw; xx++)
         if (cov[yy * gw + xx] >= 0.34f)
         {
            if (gw == 16) unicode_font[idx][yy * 2 + (xx >> 3)] |= (unsigned char)(1 << (7 - (xx & 7)));
            else          unicode_font[idx][yy]                 |= (unsigned char)(1 << (7 - xx));
         }
   /* the glyph is now centered in-cell; drop any per-glyph offset tuned for the old bitmap shape */
   if (font_offset_x) font_offset_x[idx] = 0;
   if (font_offset_y) font_offset_y[idx] = 0;
   free(cov);
}

void
PAL_DrawCharOnSurface(
	uint16_t                 wChar,
	SDL_Surface             *lpSurface,
	PAL_POS                  pos,
	uint8_t                  bColor,
	BOOL                     fUse8x8Font
)
{
	int       i, j;
	int       x = PAL_X(pos), y = PAL_Y(pos);
    int       x_offset, y_offset;
	uint16_t  uc_orig = wChar;   /* original Unicode codepoint, before the bitmap-table remap */

	//
	// Check for NULL pointer & invalid char code.
	//
	if (lpSurface == NULL || (wChar >= unicode_lower_top && wChar < unicode_upper_base) ||
		wChar >= unicode_upper_top || (_font_height == 8 && wChar >= 0x100))
	{
		return;
	}

	{  // HD-mirror: also render this glyph at ×6 into the HD surface
	   extern SDL_Surface *gpScreen; extern int PAL_TextWidth(LPCWSTR);
	   if (lpSurface == gpScreen) PAL_HDM_Char(wChar, PAL_X(pos), PAL_Y(pos), bColor, fUse8x8Font);
	}

	//
	// Locate for this character in the font lib.
	//
	if (wChar >= unicode_upper_base)
	{
		wChar -= (unicode_upper_base - unicode_lower_top);
	}

	//
	// Replace the old Saturn bitmap glyph with the new TTF 楷体 shape (once per codepoint) so the
	// low-res text path never shows the old font. Skipped for the tiny 8x8 ISO font (ASCII-only UI).
	//
	if (!fUse8x8Font) PAL_TTF_BakeLowRes(uc_orig, wChar);

    x_offset = font_offset_x[wChar];
    y_offset = font_offset_y[wChar];

	//
	// Draw the character to the surface.
	//
	LPBYTE dest = (LPBYTE)lpSurface->pixels + (int)max(y + y_offset, 0) * lpSurface->pitch + x;
	LPBYTE top = (LPBYTE)lpSurface->pixels + lpSurface->h * lpSurface->pitch;
	/* Saturn palette bank of the glyph pixels (g_bSatFontBank, set by the text renderer) */
	unsigned char *pb = SAT_BankPlane(lpSurface, g_bSatFontBank != 0);
#define FONT_PUT(d, o, c) do { (d)[o] = (c); if (pb) { size_t ofs_ = (size_t)((d) + (o) - (LPBYTE)lpSurface->pixels); 	pb[(ofs_ / lpSurface->pitch) * lpSurface->w + ofs_ % lpSurface->pitch] = g_bSatFontBank; } } while (0)
	if (fUse8x8Font)
	{
		for (i = 0; i < 8 && dest < top; i++, dest += lpSurface->pitch)
		{
			for (j = 0; j < 8 && x + j < lpSurface->w; j++)
			{
				if (iso_font_8x8[wChar][i] & (1 << j))
				{
					FONT_PUT(dest, j, bColor);
				}
			}
		}
	}
	else
	{
		if (font_width[wChar] == 32)
		{
			for (i = 0; i < _font_height * 2 && dest < top; i += 2, dest += lpSurface->pitch)
			{
				for (j = 0; j < 8 && x + j + x_offset < lpSurface->w && x + j + x_offset >= 0; j++)
				{
					if (unicode_font[wChar][i] & (1 << (7 - j)))
					{
						FONT_PUT(dest, j + x_offset, bColor);
					}
				}
				for (j = 0; j < 8 && x + j + 8 + x_offset < lpSurface->w && x + j + 8 + x_offset >= 0; j++)
				{
					if (unicode_font[wChar][i + 1] & (1 << (7 - j)))
					{
						FONT_PUT(dest, j + 8 + x_offset, bColor);
					}
				}
			}
		}
		else
		{
			for (i = 0; i < _font_height && dest < top; i++, dest += lpSurface->pitch)
			{
				for (j = 0; j < 8 && x + j + x_offset < lpSurface->w && x + j + x_offset >= 0; j++)
				{
					if (unicode_font[wChar][i] & (1 << (7 - j)))
					{
						FONT_PUT(dest, j + x_offset, bColor);
					}
				}
			}
		}
	}
}

int
PAL_CharWidth(
	uint16_t                 wChar
)
{
	if ((wChar >= unicode_lower_top && wChar < unicode_upper_base) || wChar >= unicode_upper_top)
	{
		return 0;
	}

	//
	// Locate for this character in the font lib.
	//
	if (wChar >= unicode_upper_base)
	{
		wChar -= (unicode_upper_base - unicode_lower_top);
	}

	return font_width[wChar] >> 1;
}

int
PAL_FontHeight(
	void
)
{
	return _font_height;
}
