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
#include <setjmp.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

#if SDL_VERSION_ATLEAST(3,0,0)
#if __EMSCRIPTEN__
#define SDL_MAIN_HANDLED
#endif
    #include <SDL3/SDL_main.h>
#endif

#if defined(PAL_HAS_GIT_REVISION)
# undef PAL_GIT_REVISION
# include "generated.h"
#endif

static jmp_buf g_exit_jmp_buf;
static int g_exit_code = 0;

char gExecutablePath[PAL_MAX_PATH];

#define BITMAPNUM_SPLASH_UP         (gConfig.fIsWIN95 ? 0x03 : 0x26)
#define BITMAPNUM_SPLASH_DOWN       (gConfig.fIsWIN95 ? 0x04 : 0x27)
#define SPRITENUM_SPLASH_TITLE      0x47
#define SPRITENUM_SPLASH_CRANE      0x49
#define NUM_RIX_TITLE               0x05


static VOID
PAL_Init(
   VOID
)
/*++
  Purpose:

    Initialize everything needed by the game.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   int           e;
#if PAL_HAS_GIT_REVISION
   UTIL_LogOutput(LOGLEVEL_DEBUG, "SDLPal build revision: %s\n", PAL_GIT_REVISION);
#endif

   //
   // Initialize subsystems.
   //
   e = PAL_InitGlobals();
   if (e != 0)
   {
	   TerminateOnError("Could not initialize global data: %d.\n", e);
   }

   e = VIDEO_Startup();
   if (e != 0)
   {
      TerminateOnError("Could not initialize Video: %d.\n", e);
   }

   VIDEO_SetWindowTitle("Loading...");

   e = PAL_InitUI();
   if (e != 0)
   {
      TerminateOnError("Could not initialize UI subsystem: %d.\n", e);
   }

   e = PAL_InitText();
   if (e != 0)
   {
      TerminateOnError("Could not initialize text subsystem: %d.\n", e);
   }

   e = PAL_InitFont(&gConfig);
   if (e != 0)
   {
      TerminateOnError("Could not load fonts: %d.\n", e);
   }

   PAL_InitInput();
   PAL_InitResources();
   AUDIO_OpenDevice();
   PAL_AVIInit();

   VIDEO_SetWindowTitle(UTIL_va(UTIL_GlobalBuffer(0), PAL_GLOBAL_BUFFER_SIZE,
	   "Pal %s%s%s%s",
	   gConfig.fIsWIN95 ? "Win95" : "DOS",
#if defined(_DEBUG) || defined(DEBUG)
	   " (Debug) ",
#else
	   "",
#endif
#if defined(PAL_HAS_GIT_REVISION) && defined(PAL_GIT_REVISION)
	   " ["  PAL_GIT_REVISION "] "
#else
	   ""
#endif
       ,(gConfig.fEnableGLSL && gConfig.pszShader ? gConfig.pszShader : "")
   ));
}

VOID
PAL_Shutdown(
   int exit_code
)
/*++
  Purpose:

    Free everything needed by the game.

  Parameters:

    exit_code -  The exit code return to OS.

  Return value:

    None.

--*/
{
   AUDIO_CloseDevice();
   PAL_AVIShutdown();
   PAL_FreeFont();
   PAL_FreeResources();
   PAL_FreeUI();
   PAL_FreeText();
   { extern VOID PAL_AutomapFlush(VOID); PAL_AutomapFlush(); }   /* automap: keep what was seen in this scene */
   PAL_ShutdownInput();
   VIDEO_Shutdown();
   
   //
   // global needs be free in last
   // since subsystems may needs config content during destroy
   // which also cleared here
   //
   PAL_FreeGlobals();

   g_exit_code = exit_code;
#if !__EMSCRIPTEN__ && SDL_MAJOR_VERSION < 3
   longjmp(g_exit_jmp_buf, 1);
#else
   SDL_Quit();
   UTIL_Platform_Quit();
#if !__WINRT__ && !__IOS__
   exit(0);
#endif
   return;
#endif
}

VOID
PAL_TrademarkScreen(
   VOID
)
/*++
  Purpose:

    Show the trademark screen.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   if (PAL_PlayAVI("1.avi")) return;

   PAL_SetPalette(3, FALSE);
   PAL_RNGPlay(6, 0, -1, 25);
   UTIL_Delay(1000);
   PAL_FadeOut(1);
}

VOID
PAL_SplashScreen(
   VOID
)
/*++
  Purpose:

    Show the splash screen.

  Parameters:

    None.

  Return value:

    None.

--*/
{
   SDL_Color     *palette = PAL_GetPalette(1, FALSE);
   SDL_Color      rgCurrentPalette[256];
   SDL_Surface   *lpBitmapDown, *lpBitmapUp;
   SDL_Rect       srcrect, dstrect;
   LPSPRITE       lpSpriteCrane;
   LPBITMAPRLE    lpBitmapTitle;
   LPBYTE         buf, buf2;
   int            cranepos[9][3], i, iImgPos = 200, iCraneFrame = 0, iTitleHeight;
   DWORD          dwTime, dwBeginTime;
   BOOL           fUseCD = TRUE;

   memset(rgCurrentPalette, 0xff, sizeof(SDL_Color) * 256);

   if (PAL_PlayAVI("2.avi")) return;

   if (palette == NULL)
   {
      fprintf(stderr, "ERROR: PAL_SplashScreen(): palette == NULL\n");
      return;
   }

   //
   // Allocate all the needed memory at once for simplification
   //
   buf = (LPBYTE)UTIL_calloc(1, 320 * 200 * 2);
   buf2 = (LPBYTE)(buf + 320 * 200);
   lpSpriteCrane = (LPSPRITE)buf2 + 32000;

   //
   // Create the surfaces
   //
   lpBitmapDown = VIDEO_CreateCompatibleSurface(gpScreen);
   lpBitmapUp = VIDEO_CreateCompatibleSurface(gpScreen);

   //
   // Read the bitmaps
   //
   // (Saturn port) use the generic chunk decompressor so RAW!/YJ chunks both work
   PAL_MKFDecompressChunk(buf2, 320 * 200, BITMAPNUM_SPLASH_UP, gpGlobals->f.fpFBP);
   PAL_FBPBlitToSurface(buf2, lpBitmapUp);
   PAL_MKFDecompressChunk(buf2, 320 * 200, BITMAPNUM_SPLASH_DOWN, gpGlobals->f.fpFBP);
   PAL_FBPBlitToSurface(buf2, lpBitmapDown);
   memset(buf2, 0, 32000);
   PAL_MKFDecompressChunk(buf2, 32000, SPRITENUM_SPLASH_TITLE, gpGlobals->f.fpMGO);
   lpBitmapTitle = (LPBITMAPRLE)PAL_SpriteGetFrame(buf2, 0);
   memset(lpSpriteCrane, 0, 32000);
   PAL_MKFDecompressChunk(lpSpriteCrane, 32000, SPRITENUM_SPLASH_CRANE, gpGlobals->f.fpMGO);
   if (lpBitmapTitle == NULL)
   {
      static BYTE emptyTitle[8] = { 1, 0, 1, 0, 0x81, 0, 0, 0 };
      lpBitmapTitle = (LPBITMAPRLE)emptyTitle;
   }

   iTitleHeight = PAL_RLEGetHeight(lpBitmapTitle);
   lpBitmapTitle[2] = 0;
   lpBitmapTitle[3] = 0; // HACKHACK

   //
   // Generate the positions of the cranes
   //
   for (i = 0; i < 9; i++)
   {
      cranepos[i][0] = RandomLong(300, 600);
      cranepos[i][1] = RandomLong(0, 80);
      cranepos[i][2] = RandomLong(0, 8);
   }

   //
   // Play the title music
   //
   if (!AUDIO_PlayCDTrack(7))
   {
      fUseCD = FALSE;
      AUDIO_PlayMusic(NUM_RIX_TITLE, TRUE, 2);
   }

   //
   // Clear all of the events and key states
   //
   PAL_ProcessEvent();
   PAL_ClearKeyState();

   dwBeginTime = SDL_GetTicks();

   srcrect.x = 0;
   srcrect.w = 320;
   dstrect.x = 0;
   dstrect.w = 320;

   while (TRUE)
   {
      PAL_ProcessEvent();
      dwTime = SDL_GetTicks() - dwBeginTime;

      //
      // Set the palette
      //
      if (dwTime < 15000)
      {
         for (i = 0; i < 256; i++)
         {
            rgCurrentPalette[i].r = (BYTE)(palette[i].r * ((float)dwTime / 15000));
            rgCurrentPalette[i].g = (BYTE)(palette[i].g * ((float)dwTime / 15000));
            rgCurrentPalette[i].b = (BYTE)(palette[i].b * ((float)dwTime / 15000));
         }
      }

      VIDEO_SetPalette(rgCurrentPalette);
      VIDEO_UpdateSurfacePalette(lpBitmapDown);
      VIDEO_UpdateSurfacePalette(lpBitmapUp);

      //
      // Draw the screen
      //
      if (iImgPos > 1)
      {
         iImgPos--;
      }

      //
      // The upper part...
      //
      srcrect.y = iImgPos;
      srcrect.h = 200 - iImgPos;

      dstrect.y = 0;
      dstrect.h = srcrect.h;

	  VIDEO_CopySurface(lpBitmapUp, &srcrect, gpScreen, &dstrect);

      //
      // The lower part...
      //
      srcrect.y = 0;
      srcrect.h = iImgPos;

      dstrect.y = 200 - iImgPos;
      dstrect.h = srcrect.h;

	  VIDEO_CopySurface(lpBitmapDown, &srcrect, gpScreen, &dstrect);

      //
      // Draw the cranes...
      //
      for (i = 0; i < 9; i++)
      {
         LPCBITMAPRLE lpFrame = PAL_SpriteGetFrame(lpSpriteCrane,
            cranepos[i][2] = (cranepos[i][2] + (iCraneFrame & 1)) % 8);
         cranepos[i][1] += ((iImgPos > 1) && (iImgPos & 1)) ? 1 : 0;
         PAL_RLEBlitToSurface(lpFrame, gpScreen,
            PAL_XY(cranepos[i][0], cranepos[i][1]));
         cranepos[i][0]--;
      }
      iCraneFrame++;

      //
      // Draw the title...
      //
      if (PAL_RLEGetHeight(lpBitmapTitle) < iTitleHeight)
      {
         //
         // HACKHACK
         //
         WORD w = lpBitmapTitle[2] | (lpBitmapTitle[3] << 8);
         w++;
         lpBitmapTitle[2] = (w & 0xFF);
         lpBitmapTitle[3] = (w >> 8);
      }

      PAL_RLEBlitToSurface(lpBitmapTitle, gpScreen, PAL_XY(255, 10));
      VIDEO_UpdateScreen(NULL);

      //
      // Check for keypress...
      //
      if (g_InputState.dwKeyPress & (kKeyMenu | kKeySearch))
      {
         //
         // User has pressed a key...
         //
         lpBitmapTitle[2] = iTitleHeight & 0xFF;
         lpBitmapTitle[3] = iTitleHeight >> 8; // HACKHACK

         PAL_RLEBlitToSurface(lpBitmapTitle, gpScreen, PAL_XY(255, 10));

         VIDEO_UpdateScreen(NULL);

         if (dwTime < 15000)
         {
            //
            // If the picture has not completed fading in, complete the rest
            //
            while (dwTime < 15000)
            {
               for (i = 0; i < 256; i++)
               {
                  rgCurrentPalette[i].r = (BYTE)(palette[i].r * ((float)dwTime / 15000));
                  rgCurrentPalette[i].g = (BYTE)(palette[i].g * ((float)dwTime / 15000));
                  rgCurrentPalette[i].b = (BYTE)(palette[i].b * ((float)dwTime / 15000));
               }
               VIDEO_SetPalette(rgCurrentPalette);
               VIDEO_UpdateSurfacePalette(lpBitmapDown);
               VIDEO_UpdateSurfacePalette(lpBitmapUp);
               UTIL_Delay(8);
               dwTime += 250;
            }
            UTIL_Delay(500);
         }

         //
         // Quit the splash screen
         //
         break;
      }

      //
      // Delay a while...
      //
      PAL_ProcessEvent();
      while (SDL_GetTicks() - dwBeginTime < dwTime + 85)
      {
         SDL_Delay(1);
         PAL_ProcessEvent();
      }
   }

   VIDEO_FreeSurface(lpBitmapDown);
   VIDEO_FreeSurface(lpBitmapUp);
   free(buf);

   if (!fUseCD)
   {
      AUDIO_PlayMusic(0, FALSE, 1);
   }

   PAL_FadeOut(1);
}



int
main(
   int      argc,
   char    *argv[]
)
/*++
  Purpose:

    Program entry.

  Parameters:

    argc - Number of arguments.

    argv - Array of arguments.

  Return value:

    Integer value.

--*/
{
#if !defined( __EMSCRIPTEN__ ) && !defined(__WINRT__) && !defined(__N3DS__)
   memset(gExecutablePath,0,PAL_MAX_PATH);
   strncpy(gExecutablePath, argv[0], PAL_MAX_PATH);
#endif

#if defined(_WIN32)
   //
   // Development-stage session logs (always on; PAL_NODEVLOG=1 disables).  Every run writes into
   // <exe dir>/devlog/, named by the start time:
   //    <stamp>_op.txt    every executed script instruction     (PAL_OPLOG:  ms entry op operand0..2)
   //    <stamp>_fr.txt    every free-roam frame                 (PAL_FRLOG:  ms scene vx vy dir inputdir vanish keys)
   //    <stamp>_hang.txt  last 15 main-thread call stacks, 2 s apart (PAL_HANGDUMP, see win32.cpp)
   // A variable already set in the environment wins, so test scripts can still redirect a log.
   // Files older than 7 days are removed at startup.
   //
   if (getenv("PAL_DEVLOG") != NULL && getenv("PAL_NODEVLOG") == NULL)   /* release build: session logs only on request (PAL_DEVLOG=1) */
   {
      extern int _mkdir(const char *);
      extern int _putenv_s(const char *, const char *);
      const char *base = SDL_GetBasePath();
      char dir[900], fn[1024], stamp[32];
      time_t now = time(NULL);
      struct tm *lt = localtime(&now);
      static const char *rgVar[3] = { "PAL_OPLOG", "PAL_FRLOG", "PAL_HANGDUMP" };
      static const char *rgSuf[3] = { "op", "fr", "hang" };
      int li;

      SDL_snprintf(dir, sizeof dir, "%sdevlog", base ? base : "");
      _mkdir(dir);
      {
         WIN32_FIND_DATAA fd; HANDLE h;
         SDL_snprintf(fn, sizeof fn, "%s/*.txt", dir);
         h = FindFirstFileA(fn, &fd);
         if (h != INVALID_HANDLE_VALUE)
         {
            FILETIME ftNow; ULARGE_INTEGER a, b;
            GetSystemTimeAsFileTime(&ftNow);
            a.LowPart = ftNow.dwLowDateTime; a.HighPart = ftNow.dwHighDateTime;
            do
            {
               b.LowPart = fd.ftLastWriteTime.dwLowDateTime; b.HighPart = fd.ftLastWriteTime.dwHighDateTime;
               if (a.QuadPart > b.QuadPart && a.QuadPart - b.QuadPart > 7ULL * 24 * 3600 * 10000000ULL)
               {
                  SDL_snprintf(fn, sizeof fn, "%s/%s", dir, fd.cFileName);
                  DeleteFileA(fn);
               }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
         }
      }
      strftime(stamp, sizeof stamp, "%Y%m%d_%H%M%S", lt);
      for (li = 0; li < 3; li++)
      {
         if (getenv(rgVar[li]) != NULL) continue;
         SDL_snprintf(fn, sizeof fn, "%s/%s_%s.txt", dir, stamp, rgSuf[li]);
         _putenv_s(rgVar[li], fn);
         SDL_setenv_unsafe(rgVar[li], fn, 1);
      }
   }
#endif

#if PAL_HAS_PLATFORM_STARTUP
   UTIL_Platform_Startup(argc,argv);
#endif

#if !__EMSCRIPTEN__ && SDL_MAJOR_VERSION < 3
   if (setjmp(g_exit_jmp_buf) != 0)
   {
	   // A longjmp is made, should exit here
	   SDL_Quit();
	   UTIL_Platform_Quit();
	   return g_exit_code;
   }
#endif

#if !defined(UNIT_TEST) || defined(UNIT_TEST_GAME_INIT)
   //
   // Initialize SDL
   //
   if (SDL_Init(PAL_SDL_INIT_FLAGS) == SDL_FAIL)
   {
	   TerminateOnError("Could not initialize SDL: %s.\n", SDL_GetError());
   }

   {  // HD remaster auto-enable: if the HD assets sit next to the exe, turn on full HD + battle HD +
      // TTF font automatically (self-contained distributable). Explicit env vars still take priority.
      const char *base = SDL_GetBasePath();   /* SDL3: const, do not free */
      if (base)
      {
         char p[1024]; FILE *tf;
         SDL_snprintf(p, sizeof p, "%shd_battle/fbp000.rgba", base);
         tf = fopen(p, "rb");
         if (tf)
         {
            char hd[1024], hb[1024], ft[1024]; FILE *ff;
            fclose(tf);
            SDL_snprintf(hd, sizeof hd, "%shd", base);
            SDL_snprintf(hb, sizeof hb, "%shd_battle", base);
            SDL_setenv_unsafe("PAL_HDTILES", hd, 0);
            SDL_setenv_unsafe("PAL_HDBATTLE", hb, 0);
            SDL_setenv_unsafe("PAL_HDFULL", "1", 0);
            SDL_snprintf(ft, sizeof ft, "%skaiti.ttf", base);
            ff = fopen(ft, "rb");
            if (ff) { fclose(ff); SDL_setenv_unsafe("PAL_HDFONT", ft, 0); }
         }
      }
   }

   PAL_LoadConfig(TRUE);

   //
   // Platform-specific initialization
   //
   if (UTIL_Platform_Init(argc, argv) != 0)
	   return -1;

   //
   // Should launch setting?
   // Generally, the condition should never be TRUE as the UTIL_Platform_Init is assumed
   // to handle gConfig.fLaunchSetting correctly. However, it may actually be true due to
   // the activatation event on WinRT platform, so close the current process to make new
   // process go to setting.
   // For platforms without configuration page available, this condition will NEVER be true.
   //
   if (PAL_HAS_CONFIG_PAGE && gConfig.fLaunchSetting)
	   return 0;

   //
   // If user requests a file-based log, then add it after the system-specific one.
   //
   if (gConfig.pszLogFile)
	   UTIL_LogAddOutputCallback(UTIL_LogToFile, gConfig.iLogLevel);

   //
   // Initialize everything
   //
   PAL_Init();
#endif

#if !defined(UNIT_TEST)
   //
   // Show the trademark screen and splash screen
   //
   // Saturn port: the Saturn disc has neither the DOS trademark RNG nor the sky/crane splash
   // (Saturn A1/A2 movies converted to 1.avi/2.avi; the DOS RNG/FBP fallbacks do not exist)
   if (!SDL_getenv("PAL_LOADSLOT"))   /* debug: test save states skip the movies */
   {
      {
         /* user request: the first start shows a notice; declining it closes the game */
         extern BOOL PAL_FirstRunNotice(VOID);
         if (!PAL_FirstRunNotice()) PAL_Shutdown(0);
      }
      //
      // user request: the opening movies play automatically only the first time on this machine.  A marker
      // file "intro_seen" beside the saves records that; System menu -> 開場動畫 replays them any time.
      //
      FILE *fpSeen = UTIL_OpenFileAtPath(gConfig.pszSavePath, "intro_seen");
      if (fpSeen != NULL)
      {
         fclose(fpSeen);
      }
      else
      {
         BOOL fPlayed = PAL_PlayAVI("1.avi");
         fPlayed = PAL_PlayAVI("2.avi") || fPlayed;
         if (fPlayed && (fpSeen = UTIL_OpenFileAtPathForMode(gConfig.pszSavePath, "intro_seen", "wb")) != NULL)
         {
            fputs("1", fpSeen);
            fclose(fpSeen);
         }
      }
   }

   //
   // Run the main game routine
   //
   PAL_GameMain();

   //
   // Should not really reach here...
   //
   assert(FALSE);
   return 255;
#else
   extern int testmain(int argc, char *argv[]);
   return testmain(argc, argv);
#endif
}
