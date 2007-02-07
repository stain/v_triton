/* ***** BEGIN LICENSE BLOCK *****
 * Version: CDDL 1.0/LGPL 2.1
 *
 * The contents of this file are subject to the COMMON DEVELOPMENT AND
 * DISTRIBUTION LICENSE (CDDL) Version 1.0 (the "License"); you may not use
 * this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.sun.com/cddl/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is "Triton" Multimedia Library
 *
 * The Initial Developer of the Original Code is
 * netlabs.org: Doodle <doodle@netlabs.org>.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU Lesser General Public License Version 2.1 (the "LGPL"), in which
 * case the provisions of the LGPL are applicable instead of those above. If
 * you wish to allow use of your version of this file only under the terms of 
 * the LGPL, and not to allow others to use your version of this file under
 * the terms of the CDDL, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by the
 * LGPL. If you do not delete the provisions above, a recipient may use your
 * version of this file under the terms of any one of the CDDL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define INCL_TYPES
#define INCL_WIN
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_GPI
#include <os2.h>

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"
#include "tpl.h"
#include "tpl_threadstatics.h" // We need this too, because we use threads

#define TERM_OUTPUT_FORMAT  "Video Output"

#define WORKER_STATE_STARTING       0
#define WORKER_STATE_RUNNING        1
#define WORKER_STATE_STOPPED_OK     2
#define WORKER_STATE_STOPPED_ERROR  3

#define PMTHREAD_STATE_STARTING       0
#define PMTHREAD_STATE_RUNNING        1
#define PMTHREAD_STATE_STOPPED_OK     2
#define PMTHREAD_STATE_STOPPED_ERROR  3

/* Special window ID to be able to recognize our hidden data window child */
#define ID_PLUGINDATAWINDOW  0xd114

/* Special window messages for our PM Thread */
#define WM_VOUT_CREATEWINDOW   (WM_USER+0x114)
#define WM_VOUT_UNINITIALIZE   (WM_USER+0x115)
#define WM_VOUT_SHOWNEWFRAME   (WM_USER+0x116)

// Max buffer size is 32 megs, initial is 64K
// Num of buffers in buffer pool is 16
#define VOUT_MAX_GETONEFRAME_BUF_SIZE  (1024*1024*32)
#define VOUT_INITIAL_GETONEFRAME_BUF_SIZE   (1024*64)
#define VOUT_NUM_BUFFERS_IN_POOL                 (16)

typedef struct mmioPluginInstance_s
{
  TPL_TID               tidWorker;
  int                   iWorkerState;
  TPL_TID               tidPMThread;
  int                   iPMThreadState;
  int                   bShutdownRequest;

  mmioProcessTreeNode_p pOurNode;

  TPL_MTXSEM            hmtxUseRSNode;
  mmioRSSpecificInfo_p  pRSInfo;
  void                 *pRSInstance;
  long long             llRSTimeBase;
  int                   bStreamHasPTS;

  int                   iDirection;
  long long             llTimeOffset;

  // To report timestamps to MMIO, we need the followings:
  mmioSystemTime_t      stLastTimeStampTime; // it's system time
  long long             llLastTimeStamp;     // It's in msec
  long                  lLastBufferPlayTime; // In msec, length of lastly inserted buffer
  int                   iLastBufferPlayDirection;

  unsigned long long    ullNextPicTime;      // system time of next image to show

  // This can be set by the SendMsg() API:
  HWND                  hwndRequestedWindowToUse;

  // Some common stuffs for PM thread
  HMQ                   hmqPMThreadMsgQueue;
  PFNWP                 pfnOldWindowProc;

  // These tell the PM thread the image to be shown
  TPL_MTXSEM            hmtxUseImageBuffer;
  unsigned char        *pImage;
  mmioDataDesc_t        ImageDesc;
  BITMAPINFO2           bmiImageBitmapInfo;
  HWND                  hwndClient;

} mmioPluginInstance_t, *mmioPluginInstance_p;

typedef struct mmioBufferPool_s
{
  void               *pBuffer;
  unsigned long       ulBufferSize;
  int                 bInUse;
} mmioBufferPool_t, *mmioBufferPool_p;


#ifdef __WATCOMC__
# if __WATCOMC__<1250
/* OpenWatcom only has llabs() implemented from v1.5 */
static long long llabs(long long llValue)
{
  if (llValue<0)
    return -llValue;
  else
    return llValue;
}
# endif
#endif

static MRESULT EXPENTRY vout_VideoOutVisualWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  HWND hwndData;
  mmioPluginInstance_p pPluginInstance = NULL;

  hwndData = WinWindowFromID(hwnd, ID_PLUGINDATAWINDOW);
  if (hwndData)
    pPluginInstance = (mmioPluginInstance_p) WinQueryWindowPtr(hwndData, 0L);

  switch (msg)
  {
    case WM_CLOSE:
        MMIOPsReportEvent(pPluginInstance->pOurNode,
                          MMIO_EVENTCODE_CLOSE_REQUEST, 0);
        return 0;

    case WM_PAINT:
      {
        HPS hpsBeginPaint;
        RECTL rclRect;

        hpsBeginPaint = WinBeginPaint(hwnd, NULL, &rclRect);

        if (pPluginInstance)
        {
          POINTL aptlPoints[4];
          SWP swpWindow;

          WinQueryWindowPos(hwnd, &swpWindow);

          tpl_mtxsemRequest(pPluginInstance->hmtxUseImageBuffer, TPL_WAIT_FOREVER);

          /* Target coordinates (Noninclusive) */
          aptlPoints[0].x = 0;
          aptlPoints[0].y = 0;

          aptlPoints[1].x = swpWindow.cx;
          aptlPoints[1].y = swpWindow.cy;

          /* Source coordinates (Inclusive) */
          aptlPoints[2].x = 0;
          aptlPoints[2].y = 0;

          aptlPoints[3].x = pPluginInstance->ImageDesc.StreamInfo.VideoStruct.iWidth;
          aptlPoints[3].y = pPluginInstance->ImageDesc.StreamInfo.VideoStruct.iHeight;
          GpiDrawBits (hpsBeginPaint,
                       pPluginInstance->pImage,
                       &(pPluginInstance->bmiImageBitmapInfo),
                       4,
                       aptlPoints,
                       ROP_SRCCOPY,
                       BBO_IGNORE);

          tpl_mtxsemRelease(pPluginInstance->hmtxUseImageBuffer);
        }

        // All done!
        WinEndPaint(hpsBeginPaint);
        return (MRESULT) FALSE;
      }

    case WM_VOUT_SHOWNEWFRAME:
      {
        WinInvalidateRegion(hwnd, NULLHANDLE, FALSE);
        return (MRESULT) FALSE;
      }
  }
  return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

static MRESULT EXPENTRY vout_VideoOutDataWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2 )
{
  switch (msg)
  {
    case WM_CREATE:
        return (MRESULT) FALSE;
    default:
        break;
  }
  return WinDefWindowProc(hwnd, msg, mp1, mp2);

}

static void MMIOCALL vout_PMThread(void *pParm)
{
  mmioPluginInstance_p pPluginInstance = pParm;
  HAB hab;
  QMSG msg;
  HWND hwndClient = NULLHANDLE;
  HWND hwndFrame = NULLHANDLE;
  HWND hwndDataWindow = NULLHANDLE;

  pPluginInstance->pfnOldWindowProc = NULL;

  /* Initialize Presentation Manager for this thread */
  hab = WinInitialize(0);
  if (hab == NULLHANDLE)
  {
    printf("Could not initialize PM. Make sure the application using Triton is a PM application!\n");
    pPluginInstance->iPMThreadState = PMTHREAD_STATE_STOPPED_ERROR;
    return;
  }
  pPluginInstance->hmqPMThreadMsgQueue = WinCreateMsgQueue(hab, 0);
  if (pPluginInstance->hmqPMThreadMsgQueue == NULLHANDLE)
  {
    printf("Could not create PM message queue. Make sure the application using Triton is a PM application!\n");
    WinTerminate(hab);
    pPluginInstance->iPMThreadState = PMTHREAD_STATE_STOPPED_ERROR;
    return;
  }

  /* Register a private class */
  WinRegisterClass(hab,
                   "TritonVideoOutPluginDataWindowClass",
                   vout_VideoOutDataWindowProc,
                   0,
                   32); /* Extra window storage */
  WinRegisterClass(hab,
                   "TritonVideoOutPluginVisualWindowClass",
                   vout_VideoOutVisualWindowProc,
                   CS_SIZEREDRAW,
                   0); /* Extra window storage */

  pPluginInstance->iPMThreadState = PMTHREAD_STATE_RUNNING;

  printf("[PMThread] : Reached WinProcessDlg\n");
  while (WinGetMsg(hab, &msg, 0, 0, 0))
  {
    if (msg.msg == WM_VOUT_UNINITIALIZE)
    {
      printf("[PMThread] : Processing WM_VOUT_UNINITIALIZE\n");
      break;
    }
    else
    if ((msg.msg == WM_VOUT_CREATEWINDOW) && (hwndClient == NULLHANDLE))
    {
      printf("[PMThread] : Processing WM_VOUT_CREATEWINDOW\n");
      if (pPluginInstance->hwndRequestedWindowToUse)
      {
        hwndClient = pPluginInstance->hwndRequestedWindowToUse;

        hwndDataWindow = WinCreateWindow(hwndClient,
                                         "TritonVideoOutPluginDataWindowClass", /* Class */
                                         "Triton VOut Plugin Data", /* Name */
                                         0L, /* Flags */
                                         0, 0, /* Position */
                                         0, 0, /* Size */
                                         hwndClient, /* Owner */
                                         HWND_TOP, /* Sibling */
                                         ID_PLUGINDATAWINDOW,   /* Window ID */
                                         NULL,     /* Control data */
                                         NULL);    /* Class-specific pres. data */
        WinSetWindowPtr(hwndDataWindow, 0, pPluginInstance);

        pPluginInstance->pfnOldWindowProc = WinSubclassWindow(hwndClient,
                                                              vout_VideoOutVisualWindowProc);
      } else
      {
        RECTL rclTemp;
        SWP swpTemp;
        unsigned long flCreateFlags =
          FCF_TASKLIST | FCF_SYSMENU | FCF_TITLEBAR | FCF_SIZEBORDER |
          FCF_DLGBORDER | FCF_MINMAX |
          FCF_SHELLPOSITION | FCF_NOBYTEALIGN | FCF_AUTOICON;
        unsigned long flFrameStyle =
          FS_NOBYTEALIGN | FS_BORDER;

        hwndFrame = WinCreateStdWindow(HWND_DESKTOP,
                                       flFrameStyle,       // Frame window style
                                       &flCreateFlags,
                                       "TritonVideoOutPluginVisualWindowClass",
                                       "Video Output Window",
                                       0,
                                       NULLHANDLE,
                                       0,
                                       &hwndClient);

        hwndDataWindow = WinCreateWindow(hwndClient,
                                         "TritonVideoOutPluginDataWindowClass", /* Class */
                                         "Triton VOut Plugin Data", /* Name */
                                         0L, /* Flags */
                                         0, 0, /* Position */
                                         0, 0, /* Size */
                                         hwndClient, /* Owner */
                                         HWND_TOP, /* Sibling */
                                         ID_PLUGINDATAWINDOW,   /* Window ID */
                                         NULL,     /* Control data */
                                         NULL);    /* Class-specific pres. data */
        WinSetWindowPtr(hwndDataWindow, 0, pPluginInstance);


        rclTemp.xLeft = 0;
        rclTemp.yBottom = 0;
        rclTemp.xRight = pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth;
        rclTemp.yTop = pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iHeight;
        WinMapWindowPoints(hwndClient, HWND_DESKTOP, (PPOINTL) &rclTemp, 2);
        WinCalcFrameRect(hwndFrame, &rclTemp, FALSE);
        WinSetWindowPos(hwndFrame, HWND_TOP,
                        0, 0,
                        rclTemp.xRight - rclTemp.xLeft,
                        rclTemp.yTop - rclTemp.yBottom,
                        SWP_SIZE | SWP_SHOW);
        // Now check if we could set the required size.
        // PM does not allow too small windows...
        WinQueryWindowPos(hwndClient, &swpTemp);
        if (swpTemp.cx != pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth)
        {
          // Yes, it became too wide, so increase the height too!
          swpTemp.cy = (rclTemp.yTop - rclTemp.yBottom) * swpTemp.cx / (pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth);
          WinSetWindowPos(hwndFrame, HWND_TOP,
                          0, 0,
                          swpTemp.cx, swpTemp.cy,
                          SWP_SIZE | SWP_ZORDER | SWP_SHOW | SWP_ACTIVATE);
        } else
          WinSetWindowPos(hwndFrame, HWND_TOP,
                          0, 0,
                          0, 0,
                          SWP_ZORDER | SWP_SHOW | SWP_ACTIVATE);

      }
      pPluginInstance->hwndClient = hwndClient;
    } else
    {
      if (hwndClient)
        WinDispatchMsg(hab, &msg);
      else printf("[PMThread] : Error, window was not yet created but message should be dispatched!\n");
    }
  }
  printf("[PMThread] : Shutting down!\n");

  /* Clean up PM stuffs */
  if (hwndDataWindow)
  WinDestroyWindow(hwndDataWindow);

  if (hwndFrame)
    WinDestroyWindow(hwndFrame);

  if (pPluginInstance->pfnOldWindowProc)
    WinSubclassWindow(hwndClient,
                      pPluginInstance->pfnOldWindowProc);

  WinDestroyMsgQueue(pPluginInstance->hmqPMThreadMsgQueue);
  WinTerminate(hab);
  pPluginInstance->iPMThreadState = PMTHREAD_STATE_STOPPED_OK;
}


static void MMIOCALL vout_WorkerThread(void *pParm)
{
  mmioPluginInstance_p pPluginInstance = pParm;
  void                *pBuffer;
  mmioBufferPool_t     aBufferPool[VOUT_NUM_BUFFERS_IN_POOL];
  mmioDataDesc_t       DataDesc;
  int                  i;
  int                  iGotBackBufID;
  mmioResult_t         rc;
  mmioSystemTime_t     stNow;
  long long            llSyncDiff;
  int                  iAbsStreamDirection;
  unsigned long long   ullDivider;
  unsigned long long   ullPicPlayTime;
  unsigned long long   ullPicPlayTimeModulo;
  mmioSystemTime_t     stLastFrameShowTime, stNewFrameShowTime, stOneTimesliceTime;
  int                  bNewFrameShowTimeWithTurnOver;
  long long            llLastFramePTS, llNewFramePTS;
  long long            llLastFramePlayLength;
  long long            llLastFramePTSModulo;
  int                  bIsFirstFrameToShow;
  int                  iIndexOfBufferBeingShown;
  mmioRSFormatRequest_t rfRequestedFormat;

  /* Setup */

  /* Start PM-Thread */
  pPluginInstance->iPMThreadState = PMTHREAD_STATE_STARTING;
  pPluginInstance->tidPMThread = tpl_threadCreate(1024*1024, // 1 megabyte stack
                                                  vout_PMThread,
                                                  pPluginInstance);
  if (!(pPluginInstance->tidPMThread))
  {
    /* Ooops, could not create new thread! */
    printf("Could not create PM thread!\n");
    pPluginInstance->iWorkerState = WORKER_STATE_STOPPED_ERROR;
    return;
  }

  /* Ok, wait for the PM thread to start up and to be initialized! */
  while (pPluginInstance->iPMThreadState ==PMTHREAD_STATE_STARTING)
    tpl_schedDelay(32);

  if (pPluginInstance->iPMThreadState != PMTHREAD_STATE_RUNNING)
  {
    /* There was some kind of an error while the PMThread was starting, */
    /* so it could not start. Report the error then! */

    /* Wait for the thread to really die first! */
    tpl_threadWaitForDie(pPluginInstance->tidPMThread, TPL_WAIT_FOREVER);

    /* Then return with error. */
    printf("Could not initialize PM thread!\n");
    pPluginInstance->iWorkerState = WORKER_STATE_STOPPED_ERROR;
    return;
  }
  /* Otherwise everything is ok, the PM Thread is running */

  /* Initialize buffer pool */
  for (i=0; i<VOUT_NUM_BUFFERS_IN_POOL; i++)
  {
    aBufferPool[i].pBuffer = NULL;
    aBufferPool[i].ulBufferSize = 0;
    aBufferPool[i].bInUse = 0;
  }
  stLastFrameShowTime = stNewFrameShowTime = MMIOPsGetCurrentSystemTime();
  llLastFramePTS = llNewFramePTS = -1;
  llLastFramePTSModulo = 0;

  /* Initialize other variables */
  bIsFirstFrameToShow = 1;
  iIndexOfBufferBeingShown = -1;
  /* Requested format specification */
  /* Ask the decoder to decode upside down, BGRA (needed by OS/2's API)! */
  rfRequestedFormat.VideoStruct.aiFieldStartOffset[0] =
    pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4 *
    (pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iHeight-1) + 2;
  rfRequestedFormat.VideoStruct.aiFieldNextPixel[0] = 4;
  rfRequestedFormat.VideoStruct.aiFieldPitch[0] = -pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4;
  rfRequestedFormat.VideoStruct.aiFieldStartOffset[1] =
    pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4 *
    (pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iHeight-1) + 1;
  rfRequestedFormat.VideoStruct.aiFieldNextPixel[1] = 4;
  rfRequestedFormat.VideoStruct.aiFieldPitch[1] = -pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4;
  rfRequestedFormat.VideoStruct.aiFieldStartOffset[2] =
    pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4 *
    (pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iHeight-1) + 0;
  rfRequestedFormat.VideoStruct.aiFieldNextPixel[2] = 4;
  rfRequestedFormat.VideoStruct.aiFieldPitch[2] = -pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4;
  rfRequestedFormat.VideoStruct.aiFieldStartOffset[3] =
    pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4 *
    (pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iHeight-1) + 4;
  rfRequestedFormat.VideoStruct.aiFieldNextPixel[3] = 4;
  rfRequestedFormat.VideoStruct.aiFieldPitch[3] = -pPluginInstance->pRSInfo->StreamInfo.VideoStruct.iWidth * 4;

  /* TODO */

  /* Main loop */
  pPluginInstance->iWorkerState = WORKER_STATE_RUNNING;
  while (pPluginInstance->bShutdownRequest == 0)
  {
    if (pPluginInstance->iDirection == MMIO_DIRECTION_STOP)
    {
      /* Nothing to do. Just sit, and wait for the start of playback. */
      tpl_schedDelay(32);
      stLastFrameShowTime = stNewFrameShowTime = MMIOPsGetCurrentSystemTime();
    } else
    {
      /*
       Main loop is the following:
       - Decode a frame into working buffer
       - Wait until that frame becomes valid, and needs to be shown
       - Replace working buffer with current buffer, and tell
         audio/video output service that a new buffer is ready (so show it)
       - restart the whole stuff
       */

      rc = MMIO_NOERROR;

      /* Decode a frame into a buffer from buffer pool! */

      /* Get a buffer from buffer pool where we can ask the decoder to decode a new frame! */
//      printf("Worker: Get buffer from pool\n"); fflush(stdout);
      i=0;
      while ((i<VOUT_NUM_BUFFERS_IN_POOL) && (aBufferPool[i].bInUse))
        i++;

      if (i>=VOUT_NUM_BUFFERS_IN_POOL)
      {
//        printf("Worker: Error: no more buffers\n"); fflush(stdout);
        /* We ran out of buffers, the decoder ate all. So, we can do nothing. */
        rc = MMIO_ERROR_UNKNOWN;
      } else
      {
        /* We have a free slot in buffer pool, so let's try to decode something into there! */
        if (aBufferPool[i].pBuffer == NULL)
        {
//          printf("Worker: Allocate buffer\n"); fflush(stdout);
          /* This buffer in the pool is not yet initialized, so initialize it! */
          aBufferPool[i].ulBufferSize = VOUT_INITIAL_GETONEFRAME_BUF_SIZE;
          aBufferPool[i].pBuffer = MMIOmalloc(aBufferPool[i].ulBufferSize);
          if (!(aBufferPool[i].pBuffer))
          {
            /* Could not allocate memory for buffer, we're out of memory! */
//            printf("Worker: Could not allocate buffer %d (%d bytes)\n", i, aBufferPool[i].ulBufferSize); fflush(stdout);
            rc = MMIO_ERROR_OUT_OF_MEMORY;
          }
        }

        /* Decode something if we're on the safe side yet */
        if (rc == MMIO_NOERROR)
        {
//          printf("Worker: Decode into buffer\n"); fflush(stdout);
          /* Ok, we have a buffer slot, try to decode something in there... */
          do { // Loop for reading a chunk

            aBufferPool[i].bInUse = 1;
            pBuffer = aBufferPool[i].pBuffer;

            tpl_mtxsemRequest(pPluginInstance->hmtxUseRSNode, TPL_WAIT_FOREVER);

//            printf("Worker: Get one frame into buffer at %p (Enter)\n", pBuffer); fflush(stdout);
            rc = pPluginInstance->pRSInfo->mmiors_GetOneFrame(pPluginInstance->pRSInstance,
                                                              pPluginInstance->pRSInfo->pRSID,
                                                              &DataDesc,
                                                              &pBuffer,
                                                              aBufferPool[i].ulBufferSize,
                                                              &rfRequestedFormat);
//            printf("Worker: Get one frame (Leave, rc is %d)\n", rc); fflush(stdout);

            tpl_mtxsemRelease(pPluginInstance->hmtxUseRSNode);

            /* Check result code! */
            switch (rc)
            {
              case MMIO_ERROR_BUFFER_TOO_SMALL:
                {
                  unsigned char *pNewBuf;

                  aBufferPool[i].bInUse = 0;

                  // Try to dynamically increase stream buffer size!
                  if (aBufferPool[i].ulBufferSize<VOUT_MAX_GETONEFRAME_BUF_SIZE-64*1024)
                  {
                    /* It's not yet oversized, so we can increase! */
                    aBufferPool[i].ulBufferSize+=64*1024; // Increase it by 64K
                    pNewBuf=(unsigned char *)MMIOrealloc(aBufferPool[i].pBuffer, aBufferPool[i].ulBufferSize);
                    if (!pNewBuf)
                    {
                      /* Could not increase its size */
                      aBufferPool[i].ulBufferSize-=64*1024;
                      break;
                    } else
                    {
                      // Ok, buffer increased!
                      aBufferPool[i].pBuffer = pNewBuf;
                      // Set error code so the loop will go back and try again
                      // with increased buffer size!
                      rc = MMIO_ERROR_NEED_MORE_BUFFERS;
                    }
                  }
                }
                break;
              case MMIO_ERROR_NEED_MORE_BUFFERS:
                {
                  // Buffer kept there. We have to get another work buffer!

                  i=0;
                  while ((i<VOUT_NUM_BUFFERS_IN_POOL) && (aBufferPool[i].bInUse))
                    i++;

                  if (i>=VOUT_NUM_BUFFERS_IN_POOL)
                  {
                    /* We ran out of buffers, the decoder ate all. So, we can do nothing. */
                    rc = MMIO_ERROR_UNKNOWN; /* This will take us out of the loop */
                  }
                  // Otherwise we won't break the loop and go another round!
                }
                break;
              case MMIO_ERROR_BUFFER_NOT_USED:
              case MMIO_NOERROR:
                {
                  // We got back a buffer, and the buffer we gave was
                  // either kept there (GP_OK), or not used (GP_ERROR_BUFFER_NOT_USED).
                  // So, get the pointer, and use that!

                  iGotBackBufID = 0;
                  while ((iGotBackBufID<VOUT_NUM_BUFFERS_IN_POOL) && (aBufferPool[iGotBackBufID].pBuffer != pBuffer))
                    iGotBackBufID++;

                  if (iGotBackBufID < VOUT_NUM_BUFFERS_IN_POOL)
                  {
                    // Fine, we know the buffer we got back!

                    if (rc==MMIO_ERROR_BUFFER_NOT_USED)
                    {
                      // The buffer we gave was not used, so set it to be free
                      aBufferPool[i].bInUse = 0;
                    }

                    // Also, the buffer we got back will be the current one, and it is free now
                    aBufferPool[iGotBackBufID].bInUse = 0;

                    rc = MMIO_NOERROR; // Note that we have a good buffer!
                  } else
                  {
                    // Could not find old buffer!!
                    // This buffer is not used anymore, so set it to be free
                    aBufferPool[i].bInUse = 0;
                  }
                }
                break;
              default:
                // In case of other errors, the buffer was not used, we got it back.
                aBufferPool[i].bInUse = 0;
                break;
            }
          } while (rc == MMIO_ERROR_NEED_MORE_BUFFERS);
        }
      }

      if (rc == MMIO_NOERROR)
      {
        /* We have a good decoded buffer. */

//        printf("Worker: Got a decoded buffer\n"); fflush(stdout);

        /* Report the previously sent frame position to PE (now that we know how long it will be displayed)! */
        /* For this, we have to get PTS for this new frame, so we can calculate PTS diff from last frame */


        /* Calculate how much ticks we have to show this picture for, and what */
        /* timestamp we'll have at that time!                                  */

        /* If we have PTS info, then we'll use that one to see how many system time we have to */
        /* wait to reach that PTS from the previous (already displayed) PTS. */
        /* If we don't have PTS info, then we'll use the calculated frame length of the previous frame */
        /* to know how much we have to wait. */

        /* Calculate absolute stream direction */
        iAbsStreamDirection = (pPluginInstance->iDirection<0)?(-pPluginInstance->iDirection):pPluginInstance->iDirection;
        if (iAbsStreamDirection==0)
          iAbsStreamDirection = MMIO_DIRECTION_PLAY;

        /* Calculate play time for new picture in system time */
        /* For video frames:
         TicksPerFrame = (TicksPerSec) / (FramesPerSec * (AbsStreamDirection / 1000));
         TicksPerFrame = (TicksPerSec) / (FramesPerSecCount / FramesPerSecDenom * AbsStreamDirection / 1000);
         TicksPerFrame = (TicksPerSec) / (FramesPerSecCount * AbsStreamDirection / (FramesPerSecDenom * 1000));
         TicksPerFrame = (TicksPerSec) * (FramesPerSecDenom * 1000) / (FramesPerSecCount * AbsStreamDirection);
         */

        /* For audio frames:
         TicksPerFrame = (TicksPerSec) * (PlaybackLenInSec)                         / (AbsStreamDirection / 1000);
         TicksPerFrame = (TicksPerSec) * (BufferSize / (SampleRate*BytesPerSample)) / (AbsStreamDirection / 1000);
         TicksPerFrame = (TicksPerSec) * (BufferSize / (SampleRate*BytesPerSample)) * 1000 / AbsStreamDirection;
         TicksPerFrame = (TicksPerSec) * 1000 / AbsStreamDirection * (SampleRate*BytesPerSample) / BufferSize;
         TicksPerFrame = (TicksPerSec) * 1000 * SampleRate*BytesPerSample / (AbsStreamDirection  * BufferSize);
         */

        if (pPluginInstance->pRSInfo->iStreamType == MMIO_STREAMTYPE_VIDEO)
        {
          ullPicPlayTime = MMIOPsGetOneSecSystemTime();
          ullPicPlayTime *= DataDesc.StreamInfo.VideoStruct.iFPSDenom * 1000;
          ullDivider = DataDesc.StreamInfo.VideoStruct.iFPSCount;
          ullDivider *= iAbsStreamDirection;
          ullPicPlayTime += ullPicPlayTimeModulo;
          ullPicPlayTimeModulo = ullPicPlayTime % ullDivider;
          ullPicPlayTime /= ullDivider;
        } else
        if (pPluginInstance->pRSInfo->iStreamType == MMIO_STREAMTYPE_AUDIO)
        {
          ullPicPlayTime = MMIOPsGetOneSecSystemTime();
          ullPicPlayTime *= 1000;
          ullPicPlayTime *= DataDesc.StreamInfo.AudioStruct.iSampleRate * ((DataDesc.StreamInfo.AudioStruct.iBits+7)/8);
          ullDivider = DataDesc.llDataSize;
          ullDivider *= iAbsStreamDirection;
          ullPicPlayTime += ullPicPlayTimeModulo;
          ullPicPlayTimeModulo = ullPicPlayTime % ullDivider;
          ullPicPlayTime /= ullDivider;
        } else
        {
          /* Unknown stream type! */
          /* TODO! */
          ullPicPlayTime = MMIOPsGetOneSecSystemTime();
        }


        /* Now calculate the PTS of this new frame! */
        /* If there is PTS info in stream, use that one */

        if ((pPluginInstance->bStreamHasPTS) && (DataDesc.llPTS!=-1))
        {
          /* Okay, this frame contains PTS info, so calculate difference from previous frame in msec, */
          /* then convert that to system time! */

          stNewFrameShowTime =
            stLastFrameShowTime +
            llabs(llLastFramePTS - DataDesc.llPTS) * MMIOPsGetOneSecSystemTime() / iAbsStreamDirection; // Plus the : * 1000 / 1000

          /* Store PTS for next frame */
          llNewFramePTS = DataDesc.llPTS;
          llLastFramePTSModulo = 0;
//          printf("PTS: %d  (old %d)", (int) (DataDesc.llPTS) , (int) (llLastFramePTS));
        } else
        {
          /* There is no PTS info, so use picture play time to get new PTS info! */

          stNewFrameShowTime =
            stLastFrameShowTime + ullPicPlayTime;

          /* Calculate PTS for next frame */
          llNewFramePTS = llLastFramePTS + (ullPicPlayTime * 1000 + llLastFramePTSModulo) / MMIOPsGetOneSecSystemTime();
          llLastFramePTSModulo = ullPicPlayTime * 1000 / MMIOPsGetOneSecSystemTime();
        }

        /* Also, immediately display the new frame if the stream was seeked or some other
         * discontinuity happened. */
        if (DataDesc.iExtraStreamInfo & MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY)
          stNewFrameShowTime = stLastFrameShowTime;

        /* Check for system turn over! */
        bNewFrameShowTimeWithTurnOver = (stNewFrameShowTime < stLastFrameShowTime);

        /* Ok, now we can calculate last frame play length, and report info about that! */
        llLastFramePlayLength = llNewFramePTS - llLastFramePTS;
        if (llLastFramePTS!=-1)
        {
          MMIOPsReportPosition(pPluginInstance->pOurNode,              /* Node */
                               stLastFrameShowTime,                    /* TimeWhenPositionReached */
                               llLastFramePTS,                         /* Position of playback */
                               llLastFramePlayLength,                  /* Length of frame */
                               &llSyncDiff);

//          printf("Reported: PTS: %d, len: %d  (s-diff: %d)\n", (int) llLastFramePTS, (int) llLastFramePlayLength, (int) llSyncDiff);
        }

        /* Now let's wait for the time of the current frame and show it then. */

        /* TODO:
         Raise priority to time critical for the time while waiting for the next frame to show,
         but limit this between 2 and 32msecs, to be on the safe side!
         */

        stOneTimesliceTime = MMIOPsGetOneSecSystemTime() * TPL_REGULAR_TIMESLICE_MSEC / 1000;

        stNow = MMIOPsGetCurrentSystemTime();

        if (bNewFrameShowTimeWithTurnOver)
        {
          /* The system time will have to turn over first, so wait for that turnover first! */
          while (stNow>=MMIOPsGetCurrentSystemTime())
          {
            stNow = MMIOPsGetCurrentSystemTime();
            tpl_schedYield();
          }
        }

        /*
        printf("%Ld compared to %Ld  is  %d   (One sec is %Ld)\n",
               stNewFrameShowTime - stNow,
               stOneTimesliceTime,
               stNewFrameShowTime - stNow >= stOneTimesliceTime,
               MMIOPsGetOneSecSystemTime());
        */
        while (stNow<stNewFrameShowTime)
        {
          if (stNewFrameShowTime - stNow >= stOneTimesliceTime)
            tpl_schedDelay(TPL_REGULAR_TIMESLICE_MSEC);
          else
            tpl_schedYield();

          stNow = MMIOPsGetCurrentSystemTime();
        }

        /*
        stNow = MMIOPsGetCurrentSystemTime();

        printf("Diff: %Ld  (One sec is %Ld)\n",
               stNewFrameShowTime - stNow,
               MMIOPsGetOneSecSystemTime());
               */

        /* Oookay, it's time to show the new frame! */
        /* First take back the latest decoded frame from public */
        tpl_mtxsemRequest(pPluginInstance->hmtxUseImageBuffer, TPL_WAIT_FOREVER);
        if (iIndexOfBufferBeingShown!=-1)
        {
          aBufferPool[iIndexOfBufferBeingShown].bInUse = 0;
          iIndexOfBufferBeingShown=-1;
          pPluginInstance->pImage = NULL;
        }
        if (iGotBackBufID < VOUT_NUM_BUFFERS_IN_POOL)
        {
          iIndexOfBufferBeingShown = iGotBackBufID;
          aBufferPool[iIndexOfBufferBeingShown].bInUse = 1;
          pPluginInstance->pImage = pBuffer;
          memcpy(&(pPluginInstance->ImageDesc), &DataDesc, sizeof(DataDesc));
          /* Prepare BITMAPINFO2 structure for our buffer */
          memset (&(pPluginInstance->bmiImageBitmapInfo), 0, sizeof(BITMAPINFO2));
          pPluginInstance->bmiImageBitmapInfo.cbFix = sizeof (BITMAPINFOHEADER2);
          pPluginInstance->bmiImageBitmapInfo.cx = DataDesc.StreamInfo.VideoStruct.iWidth;
          pPluginInstance->bmiImageBitmapInfo.cy = DataDesc.StreamInfo.VideoStruct.iHeight;
          pPluginInstance->bmiImageBitmapInfo.cPlanes = 1;
          pPluginInstance->bmiImageBitmapInfo.cBitCount = 32;
        }
        tpl_mtxsemRelease(pPluginInstance->hmtxUseImageBuffer);

        if (bIsFirstFrameToShow)
        {
          bIsFirstFrameToShow = 0;
          WinPostQueueMsg(pPluginInstance->hmqPMThreadMsgQueue, WM_VOUT_CREATEWINDOW, 0, 0);
        }

//        printf("Worker: Showing new frame\n"); fflush(stdout);

        WinPostMsg(pPluginInstance->hwndClient, WM_VOUT_SHOWNEWFRAME, 0, 0);

        /* Make some house-keeping: New* -> Last* */
        stLastFrameShowTime = stNewFrameShowTime;
        llLastFramePTS = llNewFramePTS;

        /* Report new position to user code */
        MMIOPsReportEvent(pPluginInstance->pOurNode,
                          MMIO_EVENTCODE_POSITION_INFO, llNewFramePTS);

      } else
      {
        /* No buffer available, something went wrong. */

        /* Call event callback with flicker and so on... */
        if (rc == MMIO_ERROR_OUT_OF_DATA)
          MMIOPsReportEvent(pPluginInstance->pOurNode,
                            MMIO_EVENTCODE_OUT_OF_DATA, 0);
        else
          MMIOPsReportEvent(pPluginInstance->pOurNode,
                            MMIO_EVENTCODE_ERROR_IN_STREAM, 0);

        /* Also wait some */
        tpl_schedDelay(32);
      }
    }
  }

  /* Cleanup */

  /* Stop the PM thread */
  WinPostQueueMsg(pPluginInstance->hmqPMThreadMsgQueue, WM_VOUT_UNINITIALIZE, 0, 0);
  tpl_threadWaitForDie(pPluginInstance->tidPMThread, TPL_WAIT_FOREVER);

  /* Uninitialize buffer pool */
  pPluginInstance->pRSInfo->mmiors_ReleaseForeignBuffers(pPluginInstance->pRSInstance,
                                                       pPluginInstance->pRSInfo->pRSID);

  for (i=0; i<VOUT_NUM_BUFFERS_IN_POOL; i++)
  {
    if (aBufferPool[i].pBuffer)
    {
      MMIOfree(aBufferPool[i].pBuffer);
      aBufferPool[i].pBuffer = NULL;
      aBufferPool[i].ulBufferSize = 0;
    }
  }
  /* TODO */

  pPluginInstance->iWorkerState = WORKER_STATE_STOPPED_OK;
}


/* TODO: Move this interface out to a public header file */
#define MMIO_VOUT_MSG_SETOUTPUTWINDOW    0

MMIOPLUGINEXPORT long         MMIOCALL vout_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (lCommandWord == MMIO_VOUT_MSG_SETOUTPUTWINDOW)
  {
    pPluginInstance->hwndRequestedWindowToUse = (HWND) pParam1;
    return MMIO_NOERROR;
  }

  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_SetDirection(void *pInstance, void *pTermID, int iDirection)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  mmioResult_t rc;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pPluginInstance->hmtxUseRSNode, 5000))
  {
    /* Could not get mutex in 5 secs! */
    return MMIO_ERROR_UNKNOWN;
  }

  rc = pPluginInstance->pRSInfo->mmiors_SetDirection(pPluginInstance->pRSInstance,
                                                   pPluginInstance->pRSInfo->pRSID,
                                                   iDirection);
  if (rc == MMIO_NOERROR)
  {
    pPluginInstance->iDirection = iDirection;
    MMIOPsReportEvent(pPluginInstance->pOurNode,
                      MMIO_EVENTCODE_DIRECTION_CHANGED, iDirection);
  }

  /* TODO: Notify worker that something was changed, the next frame should come right now */

  tpl_mtxsemRelease(pPluginInstance->hmtxUseRSNode);

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_SetPosition(void *pInstance, void *pTermID, long long llPos, int iPosType, long long *pllPosFound)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  mmioResult_t rc;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pPluginInstance->hmtxUseRSNode, 5000))
  {
    /* Could not get mutex in 5 secs! */
    return MMIO_ERROR_UNKNOWN;
  }

  rc = pPluginInstance->pRSInfo->mmiors_SetPosition(pPluginInstance->pRSInstance,
                                                  pPluginInstance->pRSInfo->pRSID,
                                                  llPos, iPosType, pllPosFound);

  /* TODO: Notify worker that something was changed, the next frame should come right now */

  tpl_mtxsemRelease(pPluginInstance->hmtxUseRSNode);

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_GetStreamLength(void *pInstance, void *pTermID, long long *pllLength)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  mmioResult_t rc;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pPluginInstance->hmtxUseRSNode, 5000))
  {
    /* Could not get mutex in 5 secs! */
    return MMIO_ERROR_UNKNOWN;
  }

  rc = pPluginInstance->pRSInfo->mmiors_GetStreamLength(pPluginInstance->pRSInstance,
                                                      pPluginInstance->pRSInfo->pRSID,
                                                      pllLength);

  tpl_mtxsemRelease(pPluginInstance->hmtxUseRSNode);

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_SetTimeOffset(void *pInstance, void *pTermID, long long llTimeOffset)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  pPluginInstance->llTimeOffset = llTimeOffset;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "General Video Output Plugin v1.0", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL vout_Initialize(void)
{
  mmioPluginInstance_p pInstance;

  pInstance = (mmioPluginInstance_p) MMIOmalloc(sizeof(mmioPluginInstance_t));
  if (pInstance)
  {
    memset(pInstance, 0, sizeof(mmioPluginInstance_t));

    pInstance->hmtxUseRSNode = tpl_mtxsemCreate(0);
    if (!(pInstance->hmtxUseRSNode))
    {
      /* Could not create mutex semaphore! */
      /* Do clean up then! */
      MMIOfree(pInstance); pInstance = NULL;
    }
    pInstance->hmtxUseImageBuffer = tpl_mtxsemCreate(0);
    if (!(pInstance->hmtxUseImageBuffer))
    {
      /* Could not create mutex semaphore! */
      /* Do clean up then! */
      tpl_mtxsemDelete(pInstance->hmtxUseRSNode);
      MMIOfree(pInstance); pInstance = NULL;
    }
  }

  return pInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_Uninitialize(void *pInstance)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free resources allocated for instance structure */
  tpl_mtxsemDelete(pPluginInstance->hmtxUseImageBuffer);
  tpl_mtxsemDelete(pPluginInstance->hmtxUseRSNode);
  MMIOfree(pInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  /* Check parameters */
  if ((!pInstance) ||
      (!pNode) ||
      (!ppExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Terminator plugins only support RS nodes */
  if (pNode->iNodeType != MMIO_NODETYPE_RAWSTREAM)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Return success, because we can handle every kind of RS node */
  *ppExamineResult = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc(sizeof(mmioFormatDesc_t) * (*ppExamineResult)->iNumOfEntries);

  if (!((*ppExamineResult)->pOutputFormats))
  {
    /* Could not allocate memory for possible output formats! */
    MMIOfree(*ppExamineResult); *ppExamineResult = NULL;
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  strncpy(&((*ppExamineResult)->pOutputFormats[0]),
          TERM_OUTPUT_FORMAT,
           sizeof(mmioFormatDesc_t));

  /* Done! */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
{
  /* Check parameters */
  if ((!pInstance) ||
      (!pExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free resources allocated for examination result structure */
  MMIOfree(pExamineResult->pOutputFormats);
  MMIOfree(pExamineResult);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;
  mmioRSSpecificInfo_p pRSInfo;
  mmioTermSpecificInfo_p pTermInfo;
  mmioProcessTreeNode_p pNewNode;

  /* Check parameters */
  if ((!pInstance) ||
      (!pchNeededOutputFormat) ||
      (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pNode->iNodeType!=MMIO_NODETYPE_RAWSTREAM)
    return MMIO_ERROR_NOT_SUPPORTED;

  if (strcmp(pchNeededOutputFormat, TERM_OUTPUT_FORMAT))
    return MMIO_ERROR_NOT_SUPPORTED;

  pRSInfo = pNode->pTypeSpecificInfo;
  if (!pRSInfo)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Build the new node(s) then! */
  pNewNode = MMIOPsCreateAndLinkNewNodeStruct(pNode);
  if (!pNewNode)
  {
    /* Could not create new node for some reason! */
    return MMIO_ERROR_UNKNOWN;
  }
  /* Fill some fields of the new node */
  strncpy(pNewNode->achNodeOwnerOutputFormat, pchNeededOutputFormat, sizeof(pNewNode->achNodeOwnerOutputFormat));
  pNewNode->bUnlinkPoint = 1;
  pNewNode->iNodeType = MMIO_NODETYPE_TERMINATOR;
  pTermInfo = (mmioTermSpecificInfo_p) MMIOmalloc(sizeof(mmioTermSpecificInfo_t));
  if (!pTermInfo)
  {
    /* Out of memory! */
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }
  pNewNode->pTypeSpecificInfo = pTermInfo;

  /* Fill the type-specific structure */
  pTermInfo->pTermID = NULL;
  strncpy(pTermInfo->achDescriptionText, pRSInfo->achDescriptionText, sizeof(pTermInfo->achDescriptionText));
  pTermInfo->iStreamType = pRSInfo->iStreamType;
  memcpy(&(pTermInfo->StreamInfo), &(pRSInfo->StreamInfo), sizeof(pTermInfo->StreamInfo));
  pTermInfo->iTermCapabilities = pRSInfo->iRSCapabilities;
  pTermInfo->mmioterm_SendMsg         = vout_SendMsg;
  pTermInfo->mmioterm_SetDirection    = vout_SetDirection;
  pTermInfo->mmioterm_SetPosition     = vout_SetPosition;
  pTermInfo->mmioterm_GetStreamLength = vout_GetStreamLength;
  pTermInfo->mmioterm_SetTimeOffset   = vout_SetTimeOffset;

  /* All done, new node is ready! */

  /* Now set up some internal variables */
  pPluginInstance->pOurNode = pNewNode;
  pPluginInstance->pRSInfo = pRSInfo;
  pPluginInstance->pRSInstance = pNode->pNodeOwnerPluginInstance;
  pPluginInstance->llRSTimeBase = 0;
  pPluginInstance->bStreamHasPTS = (pRSInfo->iRSCapabilities & MMIO_RS_CAPS_PTS);
  pRSInfo->mmiors_GetTimeBase(pPluginInstance->pRSInstance,
                            pRSInfo->pRSID,
                            &(pPluginInstance->llRSTimeBase));

  pPluginInstance->iDirection = MMIO_DIRECTION_STOP;
  pPluginInstance->bShutdownRequest = 0;

  /* Now start a new thread that will do the job for us */
  pPluginInstance->iWorkerState = WORKER_STATE_STARTING;
  pPluginInstance->tidWorker = tpl_threadCreate(1024*1024, // 1 megabyte stack
                                                 vout_WorkerThread,
                                                 pPluginInstance);
  if (!(pPluginInstance->tidWorker))
  {
    /* Ooops, could not create new thread! */
    MMIOfree(pTermInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Ok, wait for the worker thread to start up and to be initialized! */
  while (pPluginInstance->iWorkerState == WORKER_STATE_STARTING)
    tpl_schedDelay(32);

  if (pPluginInstance->iWorkerState != WORKER_STATE_RUNNING)
  {
    /* There was some kind of an error while the worker was starting, */
    /* so it could not start. Report the error then! */

    /* Wait for worker to really die first! */
    tpl_threadWaitForDie(pPluginInstance->tidWorker, TPL_WAIT_FOREVER);

    /* Clean up */
    MMIOfree(pTermInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);

    /* Then return with error. */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Worker is running, everything is set up. Done. */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL vout_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if ((!pInstance) ||
      (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pNode->iNodeType != MMIO_NODETYPE_TERMINATOR)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Stop worker first */
  pPluginInstance->bShutdownRequest = 1;
  /* Ok, wait for the worker thread to stop */
  while (pPluginInstance->iWorkerState == WORKER_STATE_STARTING)
    tpl_schedDelay(32);
  tpl_threadWaitForDie(pPluginInstance->tidWorker, TPL_WAIT_FOREVER);

  /* Free type-specific structure and resources */
  MMIOfree(pNode->pTypeSpecificInfo);
  /* Destroy node we've created at link time */
  MMIOPsUnlinkAndDestroyNodeStruct(pNode);

  /* Unlink is done */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL MMIOQueryPluginInfoForRegistry(int iPluginIndex, char **ppchInternalName, char **ppchSupportedFormats, int *piImportance)
{
  /* Check parameters */
  if ((!ppchInternalName) || (!ppchSupportedFormats) || (!piImportance))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Now return the plugin info, based on iPluginIndex */
  /* As we only have one plugin implemented in this DLL, we only handle the iPluginIndex==0 case. */
  if (iPluginIndex==0)
  {
    /* We could allocate memory here for the returned strings, but we always return the same */
    /* data, so we simply return pointers to some contant strings */
    *ppchInternalName = "vout";
    *ppchSupportedFormats = "rs_v_RGBA"; /* RGBA raw streams (YUV support will come later) */
    *piImportance = 1000;                /* Normal importance */

    return MMIO_NOERROR;
  } else
  {
    return MMIO_ERROR_INVALID_PARAMETER;
  }
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL MMIOFreePluginInfoForRegistry(char *pchInternalName, char *pchSupportedFormats)
{
  /* Check parameters */
  if ((!pchInternalName) || (!pchSupportedFormats))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We could free memory here for the returned strings, but our strings here are constants, */
  /* they don't have to be freed. (See MMIOQueryPluginInfoForRegistry() call for more info) */
  return MMIO_NOERROR;
}
