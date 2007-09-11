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
#include <process.h>
#include <direct.h>
#include <limits.h>

#define INCL_DOS
#define INCL_DOSMISC
#define INCL_WIN
#define INCL_ERRORS
#include <os2.h>

/* Includes for DART */
#define INCL_MCIOS2
#include <os2me.h>     // DART stuff and MMIO stuff

#include "AudMixer.h"

/* Code configuration defines: */
#define AUDMIX_NO_INFINITE_WAIT


#ifdef AUDMIX_NO_INFINITE_WAIT
/* Default wait timeout is 5 seconds */
#define AUDMIX_DEFAULT_WAIT_TIMEOUT  5000
#else
/* Default is infinite wait timeout */
#define AUDMIX_DEFAULT_WAIT_TIMEOUT  SEM_INDEFINITE_WAIT
#endif

#define GLOBAL_VAR  __based( __segname("GLOBAL_DATA_SEG") )

/* Remote commands sent to Daemon process: */
#define AUDMIX_COMMAND_NOTHING              0
#define AUDMIX_COMMAND_SHUTDOWN             1
#define AUDMIX_COMMAND_TAKECLIENT           2
#define AUDMIX_COMMAND_RELEASECLIENT        3
#define AUDMIX_COMMAND_TAKEBUFFER           4
#define AUDMIX_COMMAND_RELEASEBUFFER        5
#define AUDMIX_COMMAND_GETCLIENTFORPID      6

/* AudioMixer configuration: */
/* Please note that the current implementation is hard coded for these values! */
#define AUDMIX_PRIMARYBUFFER_SAMPLERATE 44100
#define AUDMIX_PRIMARYBUFFER_BITS          16
#define AUDMIX_PRIMARYBUFFER_CHANNELS       2
#define AUDMIX_PRIMARYBUFFER_ISSIGNED       1

/* Audio buffer status values: */
#define AUDMIX_BUFFERSTATUS_EMPTY     0
#define AUDMIX_BUFFERSTATUS_FILLING   1
#define AUDMIX_BUFFERSTATUS_FULL      2

typedef struct audmixBufferDesc_s
{
  unsigned int          uiAllocatedSize;
  unsigned int          uiBufferStatus;
  unsigned int          uiDataInside;
  unsigned long long    ullTimeStamp;
  unsigned long long    ullTimeStampIncPerSec;
  audmixBufferFormat_t  fmtFormat;

  unsigned int          uiBytesPerSec; /* Calculated from fmtFormat at Put time */

  void *pNextAllocated;

  void *pNextEmpty;
  void *pPrevEmpty;

  void *pNextFull;
  void *pPrevFull;

  /* And buffer data area comes right here */
} audmixBufferDesc_t, *audmixBufferDesc_p;

#define AUDMIX_DESC2PTR(pBufferDesc) ((void *) (((char *)(pBufferDesc)) + sizeof(audmixBufferDesc_t)) )
#define AUDMIX_PTR2DESC(pBuffer)     ((audmixBufferDesc_p) (((char *)(pBuffer)) - sizeof(audmixBufferDesc_t)) )

typedef struct audmixClient_s
{
  PID                 pidClient;

  HMTX                hmtxUseClient;
  unsigned long long  ullTimeStamp;
  tplTime_t          timTimeStampTime;

  audmixBufferDesc_p  pAllocatedBufListHead;
  audmixBufferDesc_p  pEmptyBufListHead;
  audmixBufferDesc_p  pEmptyBufListLast;
  audmixBufferDesc_p  pFullBufListHead;
  audmixBufferDesc_p  pFullBufListLast;

  HEV                 hevBufferWasMovedFromFullToEmpty;

  /* Work stuffs for mixer: */
  void               *pSource;
  void               *pDestination;
  void               *pFirstNonSourceAddr;
  void               *pFirstNonDestinationAddr;
  unsigned int        uiSourceStepCounter;

  /* Work stuff for real audio buffers to keep track of timestamps */
  unsigned long long *pullTimeStampsAtHW;

  void               *pNext;
};

#define AUDMIX_DAEMONTHREADSTATUS_STARTUP         0
#define AUDMIX_DAEMONTHREADSTATUS_RUNNING_OK      1
#define AUDMIX_DAEMONTHREADSTATUS_STOPPED         2

// ---------- Global (Shared) variables ----------------
int             GLOBAL_VAR g_bInitialized = 0;

PID             GLOBAL_VAR g_pidDaemon;

HEV             GLOBAL_VAR g_hevTimeStampUpdated;
tplTime_t      GLOBAL_VAR g_timTimeStampUpdateInterval;

HMTX            GLOBAL_VAR g_hmtxUseDaemonMsgArea;
int             GLOBAL_VAR g_iDaemonMsgValue;
void          * GLOBAL_VAR g_pDaemonMsgParameter;
HEV             GLOBAL_VAR g_hevDaemonMsgReady;
HEV             GLOBAL_VAR g_hevDaemonMsgResultReady;

unsigned int    GLOBAL_VAR g_uiNumMixBuffers;
unsigned int    GLOBAL_VAR g_uiMixBufferSize;


// ---------- Local (per-process) variables ------------

static char           *pchAudmixVersionString = "Audio Mixer v"AUDMIX_VERSION_STR" - Build date: "__DATE__" "__TIME__;
static PID             pidThisProcess;

// Local (per-process) variables for Daemon:
static TID             tidDaemonThread;

static int             iDaemonThreadStatus;

static signed int     *pWorkBuffer;               /* The work buffer in which we do the mixing */
static unsigned int    uiWorkBufferSizeInBytes;   /* Number of bytes in this buffer */
static unsigned int    uiWorkBufferSizeInSamples; /* Number of multichannel multibyte samples in this buffer */

static HMTX            hmtxUseClientList;
static audmixClient_p  pClientListHead;

// DART-specific stuffs
static MCI_BUFFER_PARMS     DARTBufferParms;      // Sound buffer parameters
static MCI_MIX_BUFFER      *pDARTMixBuffers;      // Sound buffers
static MCI_MIXSETUP_PARMS   DARTMixSetupParms;

static unsigned char       *pMixerCodeArea;

static int                  iNoMixCounter;


// ---------- Function implementations -----------------

static void internal_SetupMixerSpecificClientFields(audmixClient_p pClient)
{
  /* We set up only the needed ones */
  pClient->pSource = NULL;
  pClient->uiSourceStepCounter = 0;
}

static int internal_GenerateCustomMixer(audmixClient_p pClient)
{
  unsigned int uiTemp;
  unsigned int uiDestChannel;
  unsigned char *pchCode;
  audmixBufferDesc_p pBuffer;
  unsigned char *pchMixMoreAddress;

  pBuffer = pClient->pFullBufListHead;

  /* TODO: Some of these code generation lines could be replaced by */
  /* immediate values. For example, the per-buffer stuffs, like samplerate... */

  pchCode = pMixerCodeArea;

  /* Generate Startup code */
  /*                             PUSHAD */
  *(pchCode++) = 0x60;
  /*                             MOV ESI, pClient->pSource */
  *(pchCode++) = 0x8B;
  *(pchCode++) = 0x35;
  *((void **)pchCode) = &(pClient->pSource);
  pchCode+=4;
  /*                             MOV EDI, pClient->pDestination */
  *(pchCode++) = 0x8B;
  *(pchCode++) = 0x3D;
  *((void **)pchCode) = &(pClient->pDestination);
  pchCode+=4;
  /*                             MOV EBX, pBuffer->uiVolume */
  *(pchCode++) = 0x8B;
  *(pchCode++) = 0x1D;
  *((void **)pchCode) = &(pBuffer->fmtFormat.uiVolume);
  pchCode+=4;
  /*                             MOV ECX, pClient->uiSourceStepCounter */
  *(pchCode++) = 0x8B;
  *(pchCode++) = 0x0D;
  *((void **)pchCode) = &(pClient->uiSourceStepCounter);
  pchCode+=4;

  /*                         @MIXMORE: */
  pchMixMoreAddress = pchCode;

  for (uiDestChannel = 0; uiDestChannel<AUDMIX_PRIMARYBUFFER_CHANNELS; uiDestChannel++)
  {
    /* Create Left or Right sample from source channel(s) */
    /*                           XOR EAX, EAX */
    *(pchCode++) = 0x31;
    *(pchCode++) = 0xC0;

    if (pBuffer->fmtFormat.uiVolume>0)
    {
      /* No need to read source and add it to output buffer if the volume is zero! */
      /* That's why we have this IF here... */

      if ((pBuffer->fmtFormat.uchBits == 16) && (pBuffer->fmtFormat.uchSigned))
      {
        /* Mixing 16bits signed samples */
        for (uiTemp=0; uiTemp<pBuffer->fmtFormat.uiChannels; uiTemp+=AUDMIX_PRIMARYBUFFER_CHANNELS)
        {
          unsigned int uiChannelToMix;

          uiChannelToMix = (uiDestChannel+uiTemp) % pBuffer->fmtFormat.uiChannels;

          /* Every second channel will be mixed into this side */
          if (uiChannelToMix==0)
          {
            /*                      MOVSX EDX, WORD PTR [ESI] */
            *(pchCode++) = 0x0F;
            *(pchCode++) = 0xBF;
            *(pchCode++) = 0x16;
          } else
          {
            /*                      MOVSX EDX, WORD PTR [ESI+uiChannelToMix*SampleSize] */
            *(pchCode++) = 0x0F;
            *(pchCode++) = 0xBF;
            *(pchCode++) = 0x56;
            *(pchCode++) = uiChannelToMix * ((pBuffer->fmtFormat.uchBits+7)/8);
          }
          /*                        ADD EAX, EDX */
          *(pchCode++) = 0x01;
          *(pchCode++) = 0xD0;
        }
      } else
      {
        /* Unsupported format! */
        /* TODO */
        return 0;
      }

      if (pBuffer->fmtFormat.uiVolume!=256)
      {
        /* No neeed to adjust volume if it's full volume */

        /* Adjust volume */
        /*                             IMUL EBX */
        *(pchCode++) = 0xF7;
        *(pchCode++) = 0xEB;
        /*                             SAR EAX, 0x08 */
        *(pchCode++) = 0xC1;
        *(pchCode++) = 0xF8;
        *(pchCode++) = 0x08;
      }

      /* Add it to output */
      /*                             ADD DWORD PTR [EDI], EAX */
      *(pchCode++) = 0x01;
      *(pchCode++) = 0x07;
    }

    /* Move destination pointer */
    /*                             ADD EDI, 0x04 */
    *(pchCode++) = 0x83;
    *(pchCode++) = 0xC7;
    *(pchCode++) = 0x04;
  }

  /* Move in source if needed */
  /*                             ADD ECX, pBuffer->uiSampleRate */
  *(pchCode++) = 0x03;
  *(pchCode++) = 0x0D;
  *((void **)pchCode) = &(pBuffer->fmtFormat.uiSampleRate);
  pchCode+=4;
  /*                        @SOURCEPOSITIONSTEP: */
  /*                             CMP ECX, AUDMIX_PRIMARYBUFFER_SAMPLERATE (Note that this will be an immediate constant) */
  *(pchCode++) = 0x81;
  *(pchCode++) = 0xF9;
  *((unsigned int*)pchCode) = AUDMIX_PRIMARYBUFFER_SAMPLERATE;
  pchCode+=4;
  /*                             JB  @SRCMOVED */
  *(pchCode++) = 0x0F;
  *(pchCode++) = 0x82;
  *((signed int *)pchCode) = 0x00000017; /* TO BE CALCULATED! */
  pchCode+=4;
  /* Move in source pointer to the next multichannel sample */
  /*                             ADD ESI, pBuffer->uiChannels * (pBuffer->uchBits/8) */
  *(pchCode++) = 0x83;
  *(pchCode++) = 0xC6;
  *(pchCode++) = pBuffer->fmtFormat.uiChannels * ((pBuffer->fmtFormat.uchBits+7)/8);
  /* Housekeeping in SourceStepCounter */
  /*                             SUB ECX, AUDMIX_PRIMARYBUFFER_SAMPLERATE (Note that this will be an immediate constant) */
  *(pchCode++) = 0x81;
  *(pchCode++) = 0xE9;
  *((unsigned int*)pchCode) = AUDMIX_PRIMARYBUFFER_SAMPLERATE;
  pchCode+=4;
  /* Check if source pointer is still below limits */
  /*                             CMP ESI, pClient->pFirstNonSourceAddr */
  *(pchCode++) = 0x3B;
  *(pchCode++) = 0x35;
  *((void **)pchCode) = &(pClient->pFirstNonSourceAddr);
  pchCode+=4;
  /*                             JAE @CLEANUPANDRETURN */
  *(pchCode++) = 0x0F;
  *(pchCode++) = 0x83;
  *((signed int *)pchCode) = 0x0000000E; /* TO BE CALCULATED! */
  pchCode+=4;
  /*                             JMP @SOURCEPOSITIONSTEP */
  *(pchCode++) = 0xEB;
  *(pchCode++) = 0xDD;                   /* TO BE CALCULATED! */
  /*                        @SRCMOVED: */
  /*                             CMP EDI, pClient->pFirstNonDestinationAddr */
  *(pchCode++) = 0x3B;
  *(pchCode++) = 0x3D;
  *((void **)pchCode) = &(pClient->pFirstNonDestinationAddr);
  pchCode+=4;
  /*                             JB  @MIXMORE (using full displacement address) */
  *(pchCode++) = 0x0F;
  *(pchCode++) = 0x82;
  *((signed int *)pchCode) = (signed int) (pchMixMoreAddress - (pchCode+4));
  pchCode+=4;

  /*                        @CLEANUPANDRETURN: */
  /*                             MOV pClient->uiSourceStepCounter, ECX */
  *(pchCode++) = 0x89;
  *(pchCode++) = 0x0D;
  *((void **)pchCode) = &(pClient->uiSourceStepCounter);
  pchCode+=4;
  /*                             MOV pClient->pDestination, EDI */
  *(pchCode++) = 0x89;
  *(pchCode++) = 0x3D;
  *((void **)pchCode) = &(pClient->pDestination);
  pchCode+=4;
  /*                             MOV pClient->pSource, ESI */
  *(pchCode++) = 0x89;
  *(pchCode++) = 0x35;
  *((void **)pchCode) = &(pClient->pSource);
  pchCode+=4;

  /* Cleanup code */
  /*                             POPAD */
  *(pchCode++) = 0x61;
  /*                             RET */
  *(pchCode++) = 0xC3;

  return 1;
}

static void internal_MoveFullBufferToEmpty(audmixClient_p pOneClient)
{
  APIRET rc;
  audmixBufferDesc_p pBuffer;

  pBuffer = pOneClient->pFullBufListHead;

  /* Move buffer from Full to Empty list */
  pOneClient->pFullBufListHead = pBuffer->pNextFull;
  if (pOneClient->pFullBufListHead == NULL)
    pOneClient->pFullBufListLast = NULL;
  
  pBuffer->uiBufferStatus = AUDMIX_BUFFERSTATUS_EMPTY;
  pBuffer->pPrevFull = NULL;
  pBuffer->pNextFull = NULL;
  pBuffer->pPrevEmpty = NULL;
  pBuffer->pNextEmpty = pOneClient->pEmptyBufListHead;
  
  if (pOneClient->pEmptyBufListHead)
    pOneClient->pEmptyBufListHead->pPrevEmpty = pBuffer;
  else
    pOneClient->pEmptyBufListLast = pBuffer;

  pOneClient->pEmptyBufListHead = pBuffer;

   /* Post event semaphore to note that a buffer has been moved */
  rc = DosOpenEventSem(NULL, &(pOneClient->hevBufferWasMovedFromFullToEmpty));
  if (rc == NO_ERROR)
  {
    DosPostEventSem(pOneClient->hevBufferWasMovedFromFullToEmpty);
    DosCloseEventSem(pOneClient->hevBufferWasMovedFromFullToEmpty);
  }
}

static void internal_CalculateBufferTimestamp(audmixClient_p pClient, unsigned long long *pullNewTimeStamp)
{
  void *pBufferDataStart;
  audmixBufferDesc_p pBuffer;

  pBuffer = pClient->pFullBufListHead;
  pBufferDataStart = AUDMIX_DESC2PTR(pBuffer);

  *pullNewTimeStamp =
    pBuffer->ullTimeStamp +
    pBuffer->ullTimeStampIncPerSec *
    (((unsigned char *) (pClient->pSource)) - pBufferDataStart) /
    pBuffer->uiBytesPerSec;
}

static void internal_MixClient(audmixClient_p pClient, unsigned long long *pullNewTimeStamp)
{
  /* Set up mixing destination */
  pClient->pDestination = pWorkBuffer;
  pClient->pFirstNonDestinationAddr = ((unsigned char *) pWorkBuffer) + uiWorkBufferSizeInBytes;

  if (pClient->pSource == NULL)
  {
    /* Seems like a new buffer arrived meanwhile, because */
    /* pFullBufListHead is not null, but pSource is null, so set up pSource! */
    pClient->pSource = AUDMIX_DESC2PTR(pClient->pFullBufListHead);
    pClient->pFirstNonSourceAddr = ((unsigned char *) (pClient->pSource)) + pClient->pFullBufListHead->uiDataInside;
  }

  do {
    /* Generate mixer function for this buffer */
    /* TODO: What about having per-client code-buffers for this?? */
    if (!internal_GenerateCustomMixer(pClient))
    {
      /* Could not generate mixer for this, so simply consume this buffer */
      internal_CalculateBufferTimestamp(pClient, pullNewTimeStamp);
      internal_MoveFullBufferToEmpty(pClient);
      /* Also update pSource! */
      if (pClient->pFullBufListHead == NULL)
      {
        /* Ran out of full buffers, so return! */
        pClient->pSource = NULL;
        return;
      } else
      {
        pClient->pSource = AUDMIX_DESC2PTR(pClient->pFullBufListHead);
        pClient->pFirstNonSourceAddr = ((unsigned char *) (pClient->pSource)) + pClient->pFullBufListHead->uiDataInside;
      }
    } else
    {
      /* Okay, we have a custom mixer set up for this buffer, so run it! */

      _asm {
        call pMixerCodeArea
      }

      /* Check the results */
      if (pClient->pSource >= pClient->pFirstNonSourceAddr)
      {
        /* We ran out of source buffer, so get a new one! */
        /* Could not generate mixer for this, so simply consume this buffer */
        internal_CalculateBufferTimestamp(pClient, pullNewTimeStamp);
        internal_MoveFullBufferToEmpty(pClient);
        /* Also update pSource! */
        if (pClient->pFullBufListHead == NULL)
        {
          /* Ran out of full buffers, so return! */
          pClient->pSource = NULL;
          return;
        } else
        {
          pClient->pSource = AUDMIX_DESC2PTR(pClient->pFullBufListHead);
          pClient->pFirstNonSourceAddr = ((unsigned char *) (pClient->pSource)) + pClient->pFullBufListHead->uiDataInside;
        }
      }
    }
  } while (pClient->pDestination < pClient->pFirstNonDestinationAddr);


  internal_CalculateBufferTimestamp(pClient, pullNewTimeStamp);
  /* Okay, done with mixing! */
}


static void internal_MixChannelsIntoBuffer(void *pDestBuffer, unsigned long ulDestBufferSize)
{
  audmixClient_p pOneClient;
  audmixBufferDesc_p pBuffer;
  APIRET rc;
  unsigned int uiTemp;
  unsigned long long ullNewTimeStamp;

  /* Mix the channels in the WorkBuffer first, and then convert that to final buffer format */

  if (iNoMixCounter<=g_uiNumMixBuffers)
  {
    /* Set WorkBuffer to silence */
    memset(pWorkBuffer, 0, uiWorkBufferSizeInBytes);
    iNoMixCounter++;
  }

  /* Now go through all the channels and mix them into this buffer */
  rc = DosRequestMutexSem(hmtxUseClientList, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc == NO_ERROR)
  {
    pOneClient = pClientListHead;
    while (pOneClient)
    {
//      printf("* Mixing client 0x%p (PID is %x, Daemon PID is %x)\n", pOneClient, pOneClient->pidClient, pidThisProcess);

      /* Take one buffer out of filled list and put into empty list */
      rc = DosOpenMutexSem(NULL, &(pOneClient->hmtxUseClient));
      if (rc == NO_ERROR)
      {
        rc = DosRequestMutexSem(pOneClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
        if (rc == NO_ERROR)
        {
          /* Update timestamp info at client */
          pOneClient->ullTimeStamp = pOneClient->pullTimeStampsAtHW[0];
          pOneClient->timTimeStampTime = tpl_hrtimerGetTime();
          for (uiTemp=0; uiTemp<g_uiNumMixBuffers-1; uiTemp++)
            pOneClient->pullTimeStampsAtHW[uiTemp] = pOneClient->pullTimeStampsAtHW[uiTemp+1];

//          printf("* Mixing client 0x%p (Pos: %d)\n", pOneClient, (int) (pOneClient->ullTimeStamp));

          /* Check if we have a full buffer in here */
          pBuffer = pOneClient->pFullBufListHead;
          if (pBuffer)
          {
            /* Ok, we have it, so we can mix something from this client */
            internal_MixClient(pOneClient, &ullNewTimeStamp);
            /* Store new client timestamp into timestamp queue */
            pOneClient->pullTimeStampsAtHW[g_uiNumMixBuffers-1] = ullNewTimeStamp;
            /* Reset "no mix occured" counter */
            iNoMixCounter = 0;
          }
          DosReleaseMutexSem(pOneClient->hmtxUseClient);
        }
        DosCloseMutexSem(pOneClient->hmtxUseClient);
      }

      pOneClient = pOneClient->pNext;
    }

    DosReleaseMutexSem(hmtxUseClientList);
  }

  /* Notify others that one mix is completed, and client timestamp informations were updated */
  DosPostEventSem(g_hevTimeStampUpdated);

  /* Convert the Work buffer format to final audio format */
  /* Please note that this is currently hard coded to 16bits signed stereo format! */
  /* TODO : Resolve this hardcoding! */
  if (iNoMixCounter<=g_uiNumMixBuffers)
#if 0
  {
    /* Simple C version */
    signed short *psDestBuffer;
    signed int   *piSrcBuffer;
    unsigned int uiTemp;
  
    psDestBuffer = (signed short *) pDestBuffer;
    piSrcBuffer = pWorkBuffer;
    for (uiTemp=0; uiTemp<uiWorkBufferSizeInSamples; uiTemp++)
    {
      register signed int iLeft, iRight;
  
      /* Read */
      iLeft = *(piSrcBuffer++);
      iRight= *(piSrcBuffer++);
  
      /* Saturate */
      if (iLeft>SHRT_MAX)
        iLeft = SHRT_MAX;
      if (iRight>SHRT_MAX)
        iRight = SHRT_MAX;
      if (iLeft<SHRT_MIN)
        iLeft = SHRT_MIN;
      if (iRight<SHRT_MIN)
        iRight = SHRT_MIN;
  
      /* Store */
      *(psDestBuffer++) = iLeft;
      *(psDestBuffer++) = iRight;
    }
  }
#else
  /* Simple Assembly version */
  {
    /* TODO: Use MMX to speed it up */
    _asm {
      pusha
      mov esi, pWorkBuffer
      mov edi, pDestBuffer
      mov ecx, uiWorkBufferSizeInSamples
      shl ecx, 1       /* 2 channel in one sample (AUDMIX_PRIMARYBUFFER_CHANNELS == 2) */
  
      @saturate_loop:
      lodsd
      cmp eax, SHRT_MAX
      jng @MaxOk
      mov eax, SHRT_MAX
      stosw
      loop @saturate_loop
      jmp @Done
      @MaxOk:
      cmp eax, SHRT_MIN
      jnl @MinOk
      mov eax, SHRT_MIN
      @MinOk:
      stosw
      loop @saturate_loop
      @Done:
      popa
    }
  }
#endif
}


/*************************************************************************
 * internal_DARTEventFunc()                                              *
 *                                                                       *
 * This function is called by DART, when an event occures, like end of   *
 * playback of a buffer, etc...                                          *
 *************************************************************************/
static LONG APIENTRY internal_DARTEventFunc(ULONG ulStatus,
                                            PMCI_MIX_BUFFER pBuffer,
                                            ULONG ulFlags)
{
  if (ulFlags & MIX_WRITE_COMPLETE)
  {
    /* Playback of buffer completed! */

    /* Mix new stuffs into this buffer, and update client states */
    internal_MixChannelsIntoBuffer(pBuffer->pBuffer, pBuffer->ulBufferLength);
    /* Send this buffer to DART again! */
    DARTMixSetupParms.pmixWrite(DARTMixSetupParms.ulMixHandle, pBuffer, 1);
  }
  return TRUE;
}



/*************************************************************************
 * internal_OpenDART()                                                   *
 *                                                                       *
 * Opens the DART device (with the given ordinal, or 0 for the default)  *
 * for playback. Then queries if that device is capable of handling the  *
 * given sound format. If it is, sets up, allocates buffers, and starts  *
 * the playback.                                                         *
 *************************************************************************/
static int internal_OpenDART(int iDeviceOrd,
                             int bOpenShared,
                             int iFreq, int iBits, int iChannels,
                             int iNumBufs, int iBufSize)
{
  MCI_AMP_OPEN_PARMS   AmpOpenParms;
  MCI_GENERIC_PARMS    GenericParms;
  int iOpenMode;
  int rc;
  int i;

  // First thing is to try to open a given DART device!
  memset(&AmpOpenParms, 0, sizeof(MCI_AMP_OPEN_PARMS));
  // pszDeviceType should contain the device type in low word, and device ordinal in high word!
  AmpOpenParms.pszDeviceType = (PSZ) (MCI_DEVTYPE_AUDIO_AMPMIX | (iDeviceOrd << 16));

  iOpenMode = MCI_WAIT | MCI_OPEN_TYPE_ID;
  if (bOpenShared)
    iOpenMode |= MCI_OPEN_SHAREABLE;

  rc = mciSendCommand( 0, MCI_OPEN,
                       iOpenMode,
                       (PVOID) &AmpOpenParms, 0);
  if (rc==MCIERR_SUCCESS)
  {
    // Save the device ID we got from DART!
    // We will use this in the next calls!
    iDeviceOrd = AmpOpenParms.usDeviceID;

    // Now query this device if it supports the given freq/bits/channels!
    memset(&DARTMixSetupParms, 0, sizeof(MCI_MIXSETUP_PARMS));
    DARTMixSetupParms.ulBitsPerSample = iBits;
    DARTMixSetupParms.ulFormatTag = MCI_WAVE_FORMAT_PCM;
    DARTMixSetupParms.ulSamplesPerSec = iFreq;
    DARTMixSetupParms.ulChannels = iChannels;
    DARTMixSetupParms.ulFormatMode = MCI_PLAY;
    DARTMixSetupParms.ulDeviceType = MCI_DEVTYPE_WAVEFORM_AUDIO;
    DARTMixSetupParms.pmixEvent = internal_DARTEventFunc;
    rc = mciSendCommand (iDeviceOrd, MCI_MIXSETUP,
                         MCI_WAIT | MCI_MIXSETUP_QUERYMODE,
                         &DARTMixSetupParms, 0);
    if (rc!=MCIERR_SUCCESS)
    {
      // The device cannot handle this format!
      // Close DART, and exit with error code!
      mciSendCommand(iDeviceOrd, MCI_CLOSE, MCI_WAIT, &GenericParms, 0);
      return 0;
    }

    // The device can handle this format, so initialize!
    rc = mciSendCommand(iDeviceOrd, MCI_MIXSETUP,
                        MCI_WAIT | MCI_MIXSETUP_INIT,
                        &DARTMixSetupParms, 0);
    if (rc!=MCIERR_SUCCESS)
    {
      // The device could not be opened!
      // Close DART, and exit with error code!
      mciSendCommand(iDeviceOrd, MCI_CLOSE, MCI_WAIT, &GenericParms, 0);
      return 0;
    }
    // Ok, the device is initialized.
    // Now we should allocate buffers. For this, we need a place where
    // the buffer descriptors will be:
    pDARTMixBuffers = (MCI_MIX_BUFFER *) malloc(sizeof(MCI_MIX_BUFFER)*iNumBufs);
    if (!pDARTMixBuffers)
    {
      // Not enough memory!
      // Close DART, and exit with error code!
      mciSendCommand(iDeviceOrd, MCI_CLOSE, MCI_WAIT, &GenericParms, 0);
      return 0;
    }
    // Now that we have the place for buffer list, we can ask DART for the
    // buffers!
    DARTBufferParms.ulNumBuffers = iNumBufs;             // Number of buffers
    DARTBufferParms.ulBufferSize = iBufSize;             // each with this size
    DARTBufferParms.pBufList = pDARTMixBuffers;          // getting descriptorts into this list
    // Allocate buffers!
    rc = mciSendCommand(iDeviceOrd, MCI_BUFFER,
                        MCI_WAIT | MCI_ALLOCATE_MEMORY,
                        &DARTBufferParms, 0);
    if ((rc!=MCIERR_SUCCESS) || (iNumBufs != DARTBufferParms.ulNumBuffers) || (DARTBufferParms.ulBufferSize==0))
    { // Could not allocate memory!
      // Close DART, and exit with error code!
      free(pDARTMixBuffers); pDARTMixBuffers = NULL;
      mciSendCommand(iDeviceOrd, MCI_CLOSE, MCI_WAIT, &GenericParms, 0);
      return 0;
    }

    // Ok, we have all the buffers allocated, let's prepare them and send them to DART to be played!
    for (i=0; i<iNumBufs; i++)
    {
      pDARTMixBuffers[i].ulBufferLength = DARTBufferParms.ulBufferSize;
      pDARTMixBuffers[i].ulUserParm = 0;
      pDARTMixBuffers[i].ulFlags = 0;

      memset(pDARTMixBuffers[i].pBuffer, 0, pDARTMixBuffers[i].ulBufferLength);
      DARTMixSetupParms.pmixWrite(DARTMixSetupParms.ulMixHandle, &(pDARTMixBuffers[i]), 1);
    }
    // Ok, buffers ready
  }

  return (rc==MCIERR_SUCCESS);
}

/************************************************************************
 * internal_CloseDART()                                                 *
 *                                                                      *
 * Stops playback, frees DART buffers, closes DART.                     *
 ************************************************************************/
static void internal_CloseDART(int iDeviceOrd)
{
  MCI_GENERIC_PARMS GenericParms;

  // Stop DART playback
  mciSendCommand(iDeviceOrd, MCI_STOP, MCI_WAIT, &GenericParms, 0);

  // Deallocate buffers
  mciSendCommand(iDeviceOrd, MCI_BUFFER, MCI_WAIT | MCI_DEALLOCATE_MEMORY, &DARTBufferParms, 0);

  // Free bufferlist
  free(pDARTMixBuffers);

  // Close dart
  mciSendCommand(iDeviceOrd, MCI_CLOSE, MCI_WAIT, &GenericParms, 0);
}

static int internal_InitializeDaemonLocals()
{
  APIRET rc;

  /* Initialize client list and its company */
  pClientListHead = NULL;
  rc = DosCreateMutexSem(NULL, /* Unnamed semaphore */
                         &hmtxUseClientList,
                         0,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create mutex semaphore! */
    return 0;
  }

  /* Calculate 16-bits mixer work buffer size */
  uiWorkBufferSizeInBytes =
    (g_uiMixBufferSize / ((AUDMIX_PRIMARYBUFFER_BITS+7)/8)) * sizeof(signed int);
  uiWorkBufferSizeInSamples = uiWorkBufferSizeInBytes / AUDMIX_PRIMARYBUFFER_CHANNELS / sizeof(signed int);

  /* Allocate 16-bits mixer work buffers */
  pWorkBuffer = (signed int *) malloc(uiWorkBufferSizeInBytes);

  if (!pWorkBuffer)
  {
    /* Ooops, could not allocate work buffer! */
    free(pWorkBuffer);
    DosCloseMutexSem(hmtxUseClientList);
    return 0;
  }

  /* Initialize "no mixing occured" counter */
  iNoMixCounter = 0;

  /* Ok, all the locals are initialized! */
  return 1;
}

static void internal_UninitializeDaemonLocals()
{
  /* Free mix buffers */
  free(pWorkBuffer);

  /* Destroy semaphores */
  DosCloseMutexSem(hmtxUseClientList);
}

static int internal_InitializeDaemonGlobals()
{
  APIRET rc;

  /* Calculate timestamp update interval (primary buffer length) */
  g_timTimeStampUpdateInterval =
    tpl_hrtimerGetOneSecTime() * (g_uiMixBufferSize / ((AUDMIX_PRIMARYBUFFER_BITS+7)/8) / AUDMIX_PRIMARYBUFFER_CHANNELS) / AUDMIX_PRIMARYBUFFER_SAMPLERATE;

  /* Initialize some more simple variables */
  g_pidDaemon = pidThisProcess;

  /* Create event semaphore to notify Mix events */
  /* Make it auto-reset, so we won't have to call DosResetEventSem() */
  rc = DosCreateEventSem(NULL, /* Unnamed semaphore */
                         &g_hevTimeStampUpdated,
                         DC_SEM_SHARED | DCE_AUTORESET,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create semaphore! */
    return 0;
  }

  /* Initialize daemon command stuffs */
  g_iDaemonMsgValue = AUDMIX_COMMAND_NOTHING;
  g_pDaemonMsgParameter = NULL;

  rc = DosCreateMutexSem(NULL, /* Unnamed semaphore */
                         &g_hmtxUseDaemonMsgArea,
                         DC_SEM_SHARED,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create semaphore! */
    DosCloseEventSem(g_hevTimeStampUpdated);
    return 0;
  }

  rc = DosCreateEventSem(NULL, /* Unnamed semaphore */
                         &g_hevDaemonMsgReady,
                         DC_SEM_SHARED | DCE_AUTORESET,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create semaphore! */
    DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
    DosCloseEventSem(g_hevTimeStampUpdated);
    return 0;
  }

  rc = DosCreateEventSem(NULL, /* Unnamed semaphore */
                         &g_hevDaemonMsgResultReady,
                         DC_SEM_SHARED | DCE_AUTORESET,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create semaphore! */
    DosCloseEventSem(g_hevDaemonMsgReady);
    DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
    DosCloseEventSem(g_hevTimeStampUpdated);
    return 0;
  }

  return 1;
}

static void internal_UninitializeDaemonGlobals()
{
  /* TODO: Destroy the client list */
  DosCloseEventSem(g_hevDaemonMsgResultReady);
  DosCloseEventSem(g_hevDaemonMsgReady);
  DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
  DosCloseEventSem(g_hevTimeStampUpdated);
}

static int internal_StartMixing()
{
  int iTemp;
  APIRET rc;

  /* Allocate memory for custom mixer code */
  rc = DosAllocMem((void **) &pMixerCodeArea, 65536, PAG_COMMIT | PAG_EXECUTE | PAG_READ | PAG_WRITE);

  /* Initialize DART */
  iTemp = internal_OpenDART(0, /* Default device */
                            1, /* Open it in shared mode */
                            AUDMIX_PRIMARYBUFFER_SAMPLERATE,
                            AUDMIX_PRIMARYBUFFER_BITS,
                            AUDMIX_PRIMARYBUFFER_CHANNELS,
                            g_uiNumMixBuffers,
                            g_uiMixBufferSize);

  if (!iTemp)
    DosFreeMem(pMixerCodeArea);

  return iTemp;
}

static void internal_StopMixing()
{
  internal_CloseDART(0); /* Default device */

  DosFreeMem(pMixerCodeArea);
}

static void internal_ProcessCommands()
{
  int bShutdownRequest;
  APIRET rc;

  /* Set priority of this thread to high */
  DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);

  bShutdownRequest = 0;
  while (!bShutdownRequest)
  {
    rc = DosWaitEventSem(g_hevDaemonMsgReady, AUDMIX_DEFAULT_WAIT_TIMEOUT);
    if (rc!=NO_ERROR)
    {
      /* Oops, internal error! */
#ifndef AUDMIX_NO_INFINITE_WAIT
      bShutdownRequest = 1;
#endif
    } else
    {
      /* We have a command to process! */
      switch (g_iDaemonMsgValue)
      {
        case AUDMIX_COMMAND_TAKECLIENT:
          rc = DosGetSharedMem(g_pDaemonMsgParameter, PAG_READ | PAG_WRITE );
          if (rc!=NO_ERROR)
            g_iDaemonMsgValue = 0; /* Report failure */
          else
          {
            /* Put this into the list of clients */
            rc = DosRequestMutexSem(hmtxUseClientList, AUDMIX_DEFAULT_WAIT_TIMEOUT);
            if (rc!=NO_ERROR)
              g_iDaemonMsgValue = 0; /* Report failure */
            else
            {
              audmixClient_p pClient = (audmixClient_p) g_pDaemonMsgParameter;

              pClient->pNext = pClientListHead;
              pClientListHead = pClient;

              pClient->pullTimeStampsAtHW = (unsigned long long *) malloc(sizeof(unsigned long long) * g_uiNumMixBuffers);
              if (pClient->pullTimeStampsAtHW)
                memset(pClient->pullTimeStampsAtHW, 0, sizeof(unsigned long long) * g_uiNumMixBuffers);

              DosReleaseMutexSem(hmtxUseClientList);
              g_iDaemonMsgValue = 1; /* Report success */
            }
          }
          break;
        case AUDMIX_COMMAND_RELEASECLIENT:
          /* Take this out from the list of clients */
          rc = DosRequestMutexSem(hmtxUseClientList, AUDMIX_DEFAULT_WAIT_TIMEOUT);
          if (rc!=NO_ERROR)
            g_iDaemonMsgValue = 0; /* Report failure */
          else
          {
            audmixClient_p pPrevClient, pThisClient;
            audmixClient_p pClient = (audmixClient_p) g_pDaemonMsgParameter;

            /* Search for this client in list of clients */

            pPrevClient = NULL;
            pThisClient = pClientListHead;

            while ((pThisClient) && (pThisClient!=pClient))
            {
              pPrevClient = pThisClient;
              pThisClient = pThisClient->pNext;
            }

            if (!pThisClient)
            {
              /* Client not found in list! */
              g_iDaemonMsgValue = 0; /* Report failure */
            } else
            {
              /* Unlink */
              if (pPrevClient)
                pPrevClient->pNext = pClient->pNext;
              else
                pClientListHead = pClient->pNext;

              /* Free timestamp info */
              free(pClient->pullTimeStampsAtHW);

              /* Free shared memory mapping */
              DosFreeMem(g_pDaemonMsgParameter);

              g_iDaemonMsgValue = 1; /* Report success */
            }

            DosReleaseMutexSem(hmtxUseClientList);
          }
          break;
        case AUDMIX_COMMAND_TAKEBUFFER:
          rc = DosGetSharedMem(g_pDaemonMsgParameter, PAG_READ | PAG_WRITE );
          g_iDaemonMsgValue = (rc == NO_ERROR);
          break;
        case AUDMIX_COMMAND_RELEASEBUFFER:
          rc = DosFreeMem(g_pDaemonMsgParameter);
          g_iDaemonMsgValue = (rc == NO_ERROR);
          break;
        case AUDMIX_COMMAND_GETCLIENTFORPID:
          /* Look through the list of clients for a client with this PID and return that pointer */
          rc = DosRequestMutexSem(hmtxUseClientList, AUDMIX_DEFAULT_WAIT_TIMEOUT);
          if (rc!=NO_ERROR)
            g_iDaemonMsgValue = 0; /* Report failure */
          else
          {
            audmixClient_p pThisClient;
            PID pidClient = (PID) g_pDaemonMsgParameter;

            /* Search for this client in list of clients */
            pThisClient = pClientListHead;
            while ((pThisClient) && (pThisClient->pidClient!=pidClient))
              pThisClient = pThisClient->pNext;

            if (!pThisClient)
            {
              /* Client not found in list! */
              g_iDaemonMsgValue = 0; /* Report failure */
            } else
            {
              g_pDaemonMsgParameter = pThisClient; /* Return client handle in pExtraResult */
              g_iDaemonMsgValue = 1; /* Report success */
            }

            DosReleaseMutexSem(hmtxUseClientList);
          }
          break;

        case AUDMIX_COMMAND_SHUTDOWN:
          bShutdownRequest = 1;
          g_iDaemonMsgValue = 1; /* Report success */
          break;
        default:
          /* Hmm, unknown command. */
          g_iDaemonMsgValue = 0; /* Report failure */
          break;
      }
      /* Command has been processed! */
      DosPostEventSem(g_hevDaemonMsgResultReady);
    }
  }
  /* Restore priority of this thread to normal */
  DosSetPriority(PRTYS_THREAD, PRTYC_REGULAR, 0, 0);
}

static int internal_SendDaemonCommand(int iCommandValue, void *pParameter, void **ppExtraResult)
{
  int iResult;
  APIRET rc;

  /* Open shared semaphores */

  rc = DosOpenMutexSem(NULL, &g_hmtxUseDaemonMsgArea);
  if (rc != NO_ERROR)
    return 0;

  rc = DosOpenEventSem(NULL, &g_hevDaemonMsgReady);
  if (rc != NO_ERROR)
  {
    DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
    return 0;
  }

  rc = DosOpenEventSem(NULL, &g_hevDaemonMsgResultReady);
  if (rc != NO_ERROR)
  {
    DosCloseEventSem(g_hevDaemonMsgReady);
    DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
    return 0;
  }

  /* Take ownership of Daemon command processing part */
  rc = DosRequestMutexSem(g_hmtxUseDaemonMsgArea, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc != NO_ERROR)
  {
    DosCloseEventSem(g_hevDaemonMsgResultReady);
    DosCloseEventSem(g_hevDaemonMsgReady);
    DosCloseMutexSem(g_hmtxUseDaemonMsgArea);
    return 0;
  }

  iResult = 0;

  g_iDaemonMsgValue = iCommandValue;
  g_pDaemonMsgParameter = pParameter;

  rc = DosPostEventSem(g_hevDaemonMsgReady);
  if (rc==NO_ERROR)
  {
    rc = DosWaitEventSem(g_hevDaemonMsgResultReady, AUDMIX_DEFAULT_WAIT_TIMEOUT);
    if (rc == NO_ERROR)
    {
      iResult = g_iDaemonMsgValue;
      if (ppExtraResult)
        *ppExtraResult = g_pDaemonMsgParameter;
    }
  }

  /* Release ownership of daemon command processing */
  DosReleaseMutexSem(g_hmtxUseDaemonMsgArea);

  /* Close shared semaphores */
  DosCloseEventSem(g_hevDaemonMsgResultReady);
  DosCloseEventSem(g_hevDaemonMsgReady);
  DosCloseMutexSem(g_hmtxUseDaemonMsgArea);

  return iResult;
}


static void internal_DaemonThreadFunc(void *pParam)
{
  /* Initialize local (per-process) variables */
  if (internal_InitializeDaemonLocals())
  {
    /* Initialize global (shared) variables */
    if (internal_InitializeDaemonGlobals())
    {
      /* Start up DART mixing */
      if (internal_StartMixing())
      {
        /* Ok, everything is set up, go for the main message processing loop! */
        iDaemonThreadStatus = AUDMIX_DAEMONTHREADSTATUS_RUNNING_OK;
        internal_ProcessCommands();

        /* Stop mixing */
        internal_StopMixing();
      }
      /* Uninitialize global variables */
      internal_UninitializeDaemonGlobals();
    }

    /* Uninitialize local variables */
    internal_UninitializeDaemonLocals();
  }

  iDaemonThreadStatus = AUDMIX_DAEMONTHREADSTATUS_STOPPED;
  _endthread();
}


AUDMIXIMPEXP int            AUDMIXCALL  AudMixStartDaemonThread(unsigned int uiNumBuffers, unsigned int uiBufSize)
{
  if (g_bInitialized)
  {
    /* Already initialized! */
    return 0;
  }

  if ((!uiNumBuffers) || (!uiBufSize) || (uiNumBuffers<2))
  {
    /* Invalid parameters */
    return 0;
  }

  g_uiNumMixBuffers = uiNumBuffers;
  g_uiMixBufferSize = uiBufSize;

  iDaemonThreadStatus = AUDMIX_DAEMONTHREADSTATUS_STARTUP;

  tidDaemonThread = _beginthread(internal_DaemonThreadFunc, 0, 1024*1024, NULL);
  if (tidDaemonThread==-1)
  {
    /* Could not create new thread! */
    return 0;
  }

  /* Wait for the daemon thread to stop! */
  while (iDaemonThreadStatus == AUDMIX_DAEMONTHREADSTATUS_STARTUP)
  {
    DosSleep(32);
  }

  if (iDaemonThreadStatus == AUDMIX_DAEMONTHREADSTATUS_STOPPED)
  {
    /* We had an error in the daemon thread's startup! */
    DosWaitThread(&tidDaemonThread, DCWW_WAIT);
    return 0;
  }

  /* Ok, the daemon thread has been started up and running! */

  g_bInitialized = 1;

  return 1;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixStopDaemonThread()
{
  int iResult;

  if (!g_bInitialized)
  {
    /* Not yet initialized! */
    return 0;
  }

  if (pidThisProcess != g_pidDaemon)
  {
    /* Not allowed */
    return 0;
  }

  iResult = internal_SendDaemonCommand(AUDMIX_COMMAND_SHUTDOWN, NULL, NULL);
  if (iResult)
  {
    /* Ok, command got, so wait for the thread to really die! */
    DosWaitThread(&tidDaemonThread, DCWW_WAIT);

    g_bInitialized = 0;
  }

  return iResult;
}


AUDMIXIMPEXP audmixClient_p AUDMIXCALL  AudMixCreateClient(unsigned int uiNumBuffers, unsigned int uiBufSize)
{
  audmixClient_p pResult;
  APIRET rc;
  unsigned int uiTemp;

  if (!g_bInitialized)
    return NULL;

  if ((uiNumBuffers<2) || (uiBufSize<512))
    return NULL;

  /* Allocate shared memory */
  rc = DosAllocSharedMem((void **) &pResult,
                         NULL,
                         sizeof(audmixClient_t),
                         PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_ANY | OBJ_GIVEABLE | OBJ_GETTABLE);
  if (rc!=NO_ERROR)
  {
    /* Could not allocate shared memory! */
    return NULL;
  }

  /* Fill in the structure */
  pResult->pidClient = pidThisProcess;
  rc = DosCreateMutexSem(NULL,
                         &(pResult->hmtxUseClient),
                         DC_SEM_SHARED,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create mutex semaphore! */
    DosFreeMem(pResult);
    return NULL;
  }

  pResult->ullTimeStamp = 0;
  pResult->timTimeStampTime = tpl_hrtimerGetTime();
  pResult->pAllocatedBufListHead = NULL;
  pResult->pEmptyBufListHead = NULL;
  pResult->pEmptyBufListLast = NULL;
  pResult->pFullBufListHead = NULL;
  pResult->pFullBufListLast = NULL;

  rc = DosCreateEventSem(NULL,
                         &(pResult->hevBufferWasMovedFromFullToEmpty),
                         DC_SEM_SHARED,
                         FALSE);
  if (rc!=NO_ERROR)
  {
    /* Could not create event semaphore! */
    DosCloseMutexSem(pResult->hmtxUseClient);
    DosFreeMem(pResult);
    return NULL;
  }

  internal_SetupMixerSpecificClientFields(pResult);

  pResult->pNext = NULL;

  /* Allocate requested buffers from shared memory! */
  for (uiTemp = 0; uiTemp < uiNumBuffers; uiTemp++)
  {
    audmixBufferDesc_p pBuffer;

    rc = DosAllocSharedMem((void **) &pBuffer,
                           NULL,
                           sizeof(audmixBufferDesc_t) + uiBufSize,
                           PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_ANY | OBJ_GETTABLE);
    if (rc!=NO_ERROR)
    {
      /* Could not allocate shared memory! */
      break;
    }

    /* We could allocate it, so fill the structure */
    pBuffer->uiAllocatedSize = uiBufSize;
    pBuffer->uiBufferStatus = AUDMIX_BUFFERSTATUS_EMPTY;
    pBuffer->uiDataInside = 0;

    pBuffer->pNextAllocated = pResult->pAllocatedBufListHead;
    pResult->pAllocatedBufListHead = pBuffer;

    pBuffer->pPrevEmpty = NULL;
    pBuffer->pNextEmpty = pResult->pEmptyBufListHead;
    if (pResult->pEmptyBufListHead)
      pResult->pEmptyBufListHead->pPrevEmpty = pBuffer;
    else
      pResult->pEmptyBufListLast = pBuffer;
    pResult->pEmptyBufListHead = pBuffer;

    pBuffer->pPrevFull = NULL;
    pBuffer->pNextFull = NULL;

    /* Ok, now make sure that the daemon process will be able to access this structure
     * and buffer.
     */

    internal_SendDaemonCommand(AUDMIX_COMMAND_TAKEBUFFER, pBuffer, NULL);
    /* TODO: check result! */
  }

  if (rc!=NO_ERROR)
  {
    audmixBufferDesc_p pBufferToDelete;
    /* Do cleanup, there was not enough memory to allocate all buffers! */
    while (pResult->pAllocatedBufListHead)
    {
      pBufferToDelete = pResult->pAllocatedBufListHead;
      pResult->pAllocatedBufListHead = pBufferToDelete->pNextAllocated;
      internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASEBUFFER, pBufferToDelete, NULL);
      DosFreeMem(pBufferToDelete);
    }
    DosCloseEventSem(pResult->hevBufferWasMovedFromFullToEmpty);
    DosCloseMutexSem(pResult->hmtxUseClient);
    DosFreeMem(pResult);
    return NULL;
  }

  /* Ok, this client is ready to be used by daemon! */
  /* Now insert this new client into shared list of clients */

  if (!internal_SendDaemonCommand(AUDMIX_COMMAND_TAKECLIENT, pResult, NULL))
  {
    audmixBufferDesc_p pBufferToDelete;
    /* Could not put new client into list! */
    /* Do cleanup! */
    while (pResult->pAllocatedBufListHead)
    {
      pBufferToDelete = pResult->pAllocatedBufListHead;
      pResult->pAllocatedBufListHead = pBufferToDelete->pNextAllocated;
      internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASEBUFFER, pBufferToDelete, NULL);
      DosFreeMem(pBufferToDelete);
    }
    DosCloseEventSem(pResult->hevBufferWasMovedFromFullToEmpty);
    DosCloseMutexSem(pResult->hmtxUseClient);
    DosFreeMem(pResult);
    return NULL;
  }

  return pResult;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixDestroyClient(audmixClient_p hClient)
{
  audmixBufferDesc_p pBufferToDelete;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (hClient->pidClient != pidThisProcess))
    return 0;

  /* Go through the list of clients, and look for this client. */
  /* Meanwhile note the previous client pointer */

  if (!internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASECLIENT, hClient, NULL))
  {
    /* Invalid client pointer, not present in list of clients! */
    return 0;
  }

  /* Ok, memory chunk is unlinked from list of clients, time to clean it up! */
  while (hClient->pAllocatedBufListHead)
  {
    pBufferToDelete = hClient->pAllocatedBufListHead;
    hClient->pAllocatedBufListHead = pBufferToDelete->pNextAllocated;
    internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASEBUFFER, pBufferToDelete, NULL);
    DosFreeMem(pBufferToDelete);
  }
  DosCloseEventSem(hClient->hevBufferWasMovedFromFullToEmpty);
  DosCloseMutexSem(hClient->hmtxUseClient);
  DosFreeMem(hClient);

  return 1;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixResizeBuffer(audmixClient_p hClient, void **ppBuffer, unsigned int uiNewBufSize)
{
  APIRET rc;
  int iResult;
  audmixBufferDesc_p pOneBuffer, pPrevBuffer, pBufferDesc, pNewBuffer;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!uiNewBufSize) || (!ppBuffer) || (!(*ppBuffer)) || (hClient->pidClient != pidThisProcess))
  {
    /* Invalid parameter */
    return 0;
  }

  /* Create normal buffer handle from user buffer pointer */
  pBufferDesc = AUDMIX_PTR2DESC(*ppBuffer);

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  /* Look for this buffer in the list of allocated client buffers */
  pOneBuffer = hClient->pAllocatedBufListHead;
  pPrevBuffer = NULL;

  while ((pOneBuffer) && (pOneBuffer!=pBufferDesc))
  {
    pPrevBuffer = pOneBuffer;
    pOneBuffer = pOneBuffer->pNextAllocated;
  }

  if (!pOneBuffer)
  {
    /* Could not find this buffer in list of buffers! */
    iResult = 0;
  } else
  {
    /* Ok, we have the buffer. */

    /* Check if it's a buffer being at user space! */
    /* It's because (currently) that's the only one supported for resizing. */
    /* Later, it might be possible to resize other kind of buffers, for example */
    /* the empty ones being enqueued in AudMixer, but I don't think it's so */
    /* much of a restriction for now... We'll see. */

    if (pBufferDesc->uiBufferStatus != AUDMIX_BUFFERSTATUS_FILLING)
    {
      /* Only those buffers can be resized that are at "user space"! */
      /* Use AudMixGetEmptyBuffer() first! */
      iResult = 0;
    } else
    {
      /* Ok, it's a resizable buffer. */

      /* So, try to allocate a new buffer instead of this one! */
      rc = DosAllocSharedMem((void **) &pNewBuffer,
                             NULL,
                             sizeof(audmixBufferDesc_t) + uiNewBufSize,
                             PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_ANY | OBJ_GETTABLE);
      if (rc!=NO_ERROR)
      {
        /* Could not allocate shared memory! */
       iResult = 0;
      } else
      {
        /* We could allocate it, so fill the structure */
        memcpy(pNewBuffer, pBufferDesc, sizeof(audmixBufferDesc_t));
        pNewBuffer->uiAllocatedSize = uiNewBufSize;

        /* No need to handle empty and full buffer lists as this buffer */
        /* is not part of any of those, because its status is FILLING! */

        /* However, we have to update the allocated buffer list! */
        if (pPrevBuffer)
          pPrevBuffer->pNextAllocated = pNewBuffer;
        else
          hClient->pAllocatedBufListHead = pNewBuffer;

        /* Ok, now make sure that the daemon process will be able to access this structure
         * and buffer.
         */
        internal_SendDaemonCommand(AUDMIX_COMMAND_TAKEBUFFER, pNewBuffer, NULL);

        /* Now destroy (free) the old buffer and its structure */
        internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASEBUFFER, pBufferDesc, NULL);
        DosFreeMem(pBufferDesc);

        /* Report the new pointer to the user */
        *ppBuffer = AUDMIX_DESC2PTR(pNewBuffer);

        iResult = 1;
      }
    }
  }

  DosReleaseMutexSem(hClient->hmtxUseClient);

  return iResult;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixAddNewBuffer(audmixClient_p hClient, unsigned int uiBufSize)
{
  APIRET rc;
  int iResult;
  audmixBufferDesc_p pNewBuffer;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!uiBufSize) || (hClient->pidClient != pidThisProcess))
  {
    /* Invalid parameter */
    return 0;
  }

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  /* Try to allocate a new buffer! */
  rc = DosAllocSharedMem((void **) &pNewBuffer,
                         NULL,
                         sizeof(audmixBufferDesc_t) + uiBufSize,
                         PAG_READ | PAG_WRITE | PAG_COMMIT | OBJ_ANY | OBJ_GETTABLE);
  if (rc!=NO_ERROR)
  {
    /* Could not allocate shared memory! */
    iResult =  0;
  } else
  {
    /* We could allocate it, so fill the structure */
    pNewBuffer->uiAllocatedSize = uiBufSize;
    pNewBuffer->uiBufferStatus = AUDMIX_BUFFERSTATUS_EMPTY;
    pNewBuffer->uiDataInside = 0;

    /* Add it to list of allocated buffers */
    pNewBuffer->pNextAllocated = hClient->pAllocatedBufListHead;
    hClient->pAllocatedBufListHead = pNewBuffer;

    /* Add it to head of list of empty buffers */
    pNewBuffer->pPrevEmpty = NULL;
    pNewBuffer->pNextEmpty = hClient->pEmptyBufListHead;
    if (hClient->pEmptyBufListHead)
      hClient->pEmptyBufListHead->pPrevEmpty = pNewBuffer;
    else
      hClient->pEmptyBufListLast = pNewBuffer;
    hClient->pEmptyBufListHead = pNewBuffer;

    pNewBuffer->pPrevFull = NULL;
    pNewBuffer->pNextFull = NULL;

    /* Ok, now make sure that the daemon process will be able to access this structure
     * and buffer
     */
    internal_SendDaemonCommand(AUDMIX_COMMAND_TAKEBUFFER, pNewBuffer, NULL);

    iResult = 1;
  }

  DosReleaseMutexSem(hClient->hmtxUseClient);

  return iResult;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixDestroyBuffer(audmixClient_p hClient, void *pBuffer)
{
  APIRET rc;
  int iResult;
  audmixBufferDesc_p pOneBuffer, pPrevBuffer, pBufferDesc;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!pBuffer) || (hClient->pidClient != pidThisProcess))
  {
    /* Invalid parameter */
    return 0;
  }

  /* Create normal buffer handle from user buffer pointer */
  pBufferDesc = AUDMIX_PTR2DESC(pBuffer);

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  /* Look for this buffer in the list of allocated client buffers */
  pOneBuffer = hClient->pAllocatedBufListHead;
  pPrevBuffer = NULL;

  while ((pOneBuffer) && (pOneBuffer!=pBufferDesc))
  {
    pPrevBuffer = pOneBuffer;
    pOneBuffer = pOneBuffer->pNextAllocated;
  }

  if (!pOneBuffer)
  {
    /* Could not find this buffer in list of buffers! */
    iResult = 0;
  } else
  {
    /* Ok, we have the buffer. */

    /* Check if it's a buffer being at user space! */
    /* It's because (currently) that's the only one supported for destroying. */
    /* Later, it might be possible to destroy other kind of buffers, for example */
    /* the empty ones being enqueued in AudMixer, but I don't think it's so */
    /* much of a restriction for now... We'll see. */

    if (pBufferDesc->uiBufferStatus != AUDMIX_BUFFERSTATUS_FILLING)
    {
      /* Only those buffers can be destroyed that are at "user space"! */
      /* Use AudMixGetEmptyBuffer() first! */
      iResult = 0;
    } else
    {
      /* Ok, it's a destroyable buffer. */

      /* No need to handle empty and full buffer lists as this buffer */
      /* is not part of any of those, because its status is FILLING! */

      /* However, we have to update the allocated buffer list! */
      if (pPrevBuffer)
        pPrevBuffer->pNextAllocated = pBufferDesc->pNextAllocated;
      else
        hClient->pAllocatedBufListHead = pBufferDesc->pNextAllocated;

      /* Now destroy (free) the old buffer and its structure */
      internal_SendDaemonCommand(AUDMIX_COMMAND_RELEASEBUFFER, pBufferDesc, NULL);
      DosFreeMem(pBufferDesc);

      iResult = 1;
    }
  }

  DosReleaseMutexSem(hClient->hmtxUseClient);

  return iResult;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetEmptyBuffer(audmixClient_p hClient, void **ppBuffer, unsigned int *puiBufferSize, int iTimeOut)
{
  APIRET rc;
  int iResult;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!ppBuffer) || (!puiBufferSize) || (hClient->pidClient != pidThisProcess))
    return 0;

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  if (hClient->pEmptyBufListLast)
  {
    /* Take the last empty buffer out from list of empty buffers! */
    audmixBufferDesc_p pPrevBuf, pEmptyBuf;

    pEmptyBuf = hClient->pEmptyBufListLast;
    pPrevBuf = pEmptyBuf->pPrevEmpty;

    if (pPrevBuf)
      pPrevBuf->pNextEmpty = NULL;
    else
      hClient->pEmptyBufListHead = NULL;
    hClient->pEmptyBufListLast = pPrevBuf;

    /* Note that this buffer is at userspace */
    pEmptyBuf->uiBufferStatus = AUDMIX_BUFFERSTATUS_FILLING;

    /* Give the user the values it wants */
    *ppBuffer = AUDMIX_DESC2PTR(pEmptyBuf);
    *puiBufferSize = pEmptyBuf->uiAllocatedSize;

    iResult = 1;
  } else
  {
    ULONG ulPostCount;

    /* There are no more empty buffers, so we'll wait for one to arrive! */
    iResult = 0;
    DosResetEventSem(hClient->hevBufferWasMovedFromFullToEmpty, &ulPostCount);
  }

  DosReleaseMutexSem(hClient->hmtxUseClient);

  if ((!iResult) && ((iTimeOut>0) || (iTimeOut==AUDMIX_DEFAULT_WAIT_TIMEOUT)))
  {
    /* There is no empty buffer, so we have to wait for one */
    rc = DosWaitEventSem(hClient->hevBufferWasMovedFromFullToEmpty, iTimeOut);
    if (rc == NO_ERROR)
    {
      /* The event semaphore was posted, so we might have a new buffer! */
      iResult = AudMixGetEmptyBuffer(hClient, ppBuffer, puiBufferSize, 0);
    }
  }

  return iResult;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixPutEmptyBuffer(audmixClient_p hClient,
                                                             void *pBuffer)
{
  APIRET rc;
  audmixBufferDesc_p pEmptyBuf;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!pBuffer) || (hClient->pidClient != pidThisProcess))
    return 0;

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  /* Put this new full buffer to the end of the list of full buffers */
  pEmptyBuf = AUDMIX_PTR2DESC(pBuffer);
  pEmptyBuf->pPrevFull = NULL;
  pEmptyBuf->pNextFull = NULL;
  pEmptyBuf->pPrevEmpty = hClient->pEmptyBufListLast;
  pEmptyBuf->pNextEmpty = NULL;
  pEmptyBuf->uiBufferStatus = AUDMIX_BUFFERSTATUS_EMPTY;
  pEmptyBuf->uiDataInside = 0;

  if (hClient->pEmptyBufListLast)
    hClient->pEmptyBufListLast->pNextEmpty = pEmptyBuf;
  else
    hClient->pEmptyBufListHead = pEmptyBuf;

  hClient->pEmptyBufListLast = pEmptyBuf;

  DosReleaseMutexSem(hClient->hmtxUseClient);
  return 1;

}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixPutFullBuffer(audmixClient_p hClient,
                                                            void *pBuffer,
                                                            unsigned int uiDataInside,
                                                            unsigned long long ullTimeStamp,
                                                            unsigned long long ullTimeStampIncPerSec,
                                                            audmixBufferFormat_p pfmtFormat)
{
  APIRET rc;
  audmixBufferDesc_p pFullBuf;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!pBuffer) || (!pfmtFormat) || (hClient->pidClient != pidThisProcess))
    return 0;

  /* Don't accept invalid audio format! */
  if ((pfmtFormat->uiSampleRate < 100) ||
      (pfmtFormat->uchBits<8) ||
      (pfmtFormat->uiChannels < 1))
    return 0;

  /* Putting zero length buffer means putting an empty buffer */
  if (uiDataInside< (((pfmtFormat->uchBits+7)/8) * (pfmtFormat->uiChannels)))
  {
    return AudMixPutEmptyBuffer(hClient, pBuffer);
  }

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  /* Put this new full buffer to the end of the list of full buffers */
  pFullBuf = AUDMIX_PTR2DESC(pBuffer);
  pFullBuf->pPrevFull = hClient->pFullBufListLast;
  pFullBuf->pNextFull = NULL;
  pFullBuf->pPrevEmpty = NULL;
  pFullBuf->pNextEmpty = NULL;
  pFullBuf->uiBufferStatus = AUDMIX_BUFFERSTATUS_FULL;
  pFullBuf->uiDataInside = uiDataInside;
  pFullBuf->ullTimeStamp = ullTimeStamp;
  pFullBuf->ullTimeStampIncPerSec = ullTimeStampIncPerSec;
  memcpy(&(pFullBuf->fmtFormat), pfmtFormat, sizeof(audmixBufferFormat_t));

  pFullBuf->uiBytesPerSec = pfmtFormat->uiSampleRate * ((pfmtFormat->uchBits+7)/8) * pfmtFormat->uiChannels;

  if (hClient->pFullBufListLast)
    hClient->pFullBufListLast->pNextFull = pFullBuf;
  else
    hClient->pFullBufListHead = pFullBuf;

  hClient->pFullBufListLast = pFullBuf;

  DosReleaseMutexSem(hClient->hmtxUseClient);
  return 1;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetNumOfBuffers(audmixClient_p hClient, unsigned int *puiNumAllBufs, unsigned int *puiNumInSystem)
{
  APIRET rc;
  audmixBufferDesc_p pBuffer;
  unsigned int uiNumAllBufs, uiNumInSystem;

  if (!g_bInitialized)
    return 0;

  if ((!hClient) || !((puiNumAllBufs) || (puiNumInSystem)) || (hClient->pidClient != pidThisProcess))
    return 0;

  rc = DosRequestMutexSem(hClient->hmtxUseClient, AUDMIX_DEFAULT_WAIT_TIMEOUT);
  if (rc!=NO_ERROR)
  {
    /* Could not get client mutex! */
    return 0;
  }

  uiNumAllBufs = uiNumInSystem = 0;

  pBuffer = hClient->pAllocatedBufListHead;
  while (pBuffer)
  {
    uiNumAllBufs++;
    if (pBuffer->uiBufferStatus != AUDMIX_BUFFERSTATUS_FILLING)
      uiNumInSystem++;

    pBuffer = pBuffer->pNextAllocated;
  }

  DosReleaseMutexSem(hClient->hmtxUseClient);

  if (puiNumAllBufs)
    *puiNumAllBufs = uiNumAllBufs;

  if (puiNumInSystem)
    *puiNumInSystem = uiNumInSystem;

  return 1;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixWaitForTimestampUpdate(int iTimeOut)
{
  APIRET rc;

  if (!g_bInitialized)
    return 0;

  rc = DosOpenEventSem(NULL, &g_hevTimeStampUpdated);
  if (rc != NO_ERROR)
  {
    /* Ooops, could not open semaphore handle */
    return 0;
  }

  /* Block on semaphore for given timeout */
  rc = DosWaitEventSem(g_hevTimeStampUpdated, iTimeOut);
  DosCloseEventSem(g_hevTimeStampUpdated);

  return (rc == NO_ERROR);
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetTimestampInfo(audmixClient_p hClient, unsigned long long *pullTimeStamp, tplTime_t *ptimTimeStampTime, tplTime_t *ptimTimeStampUpdateInterval)
{
  if (!g_bInitialized)
    return 0;

  if ((!hClient) || (!pullTimeStamp) || (!ptimTimeStampTime) || (!ptimTimeStampUpdateInterval) || (hClient->pidClient!=pidThisProcess))
  {
    /* Invalid parameter or wrong process */
    return 0;
  }

  *pullTimeStamp = hClient->ullTimeStamp;
  *ptimTimeStampTime = hClient->timTimeStampTime;
  *ptimTimeStampUpdateInterval = g_timTimeStampUpdateInterval;

  return 1;
}

AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetInformation(audmixInfoBuffer_p pInfoBuffer, unsigned int uiInfoBufferSize)
{
  audmixInfoBuffer_t InfoBuffer;

  if (!pInfoBuffer)
    return 0;

  InfoBuffer.iVersion = AUDMIX_VERSION_INT;
  InfoBuffer.pchVersion = pchAudmixVersionString;

  InfoBuffer.bIsDaemonRunning = g_bInitialized;

  if (!g_bInitialized)
  {
    InfoBuffer.uiPrimaryNumBuffers = 0;
    InfoBuffer.uiPrimaryBufSize = 0;

    InfoBuffer.uiPrimarySampleRate = 0;
    InfoBuffer.uiPrimaryChannels = 0;
    InfoBuffer.uchPrimaryBits = 0;
    InfoBuffer.uchPrimarySigned = 0;
  } else
  {
    InfoBuffer.uiPrimaryNumBuffers = g_uiNumMixBuffers;
    InfoBuffer.uiPrimaryBufSize = g_uiMixBufferSize;

    InfoBuffer.uiPrimarySampleRate = AUDMIX_PRIMARYBUFFER_SAMPLERATE;
    InfoBuffer.uiPrimaryChannels = AUDMIX_PRIMARYBUFFER_CHANNELS;
    InfoBuffer.uchPrimaryBits = AUDMIX_PRIMARYBUFFER_BITS;
    InfoBuffer.uchPrimarySigned = AUDMIX_PRIMARYBUFFER_ISSIGNED;
  }

  memset(pInfoBuffer, 0, uiInfoBufferSize);
  memcpy(pInfoBuffer, &InfoBuffer, (sizeof(audmixInfoBuffer_t)<uiInfoBufferSize)?sizeof(audmixInfoBuffer_t):uiInfoBufferSize);

  return 1;
}


static unsigned long internal_GetCurrentPID(void)
{
  PPIB pib;
  DosGetInfoBlocks(NULL, &pib);
  return((unsigned long) (pib->pib_ulpid));
}

static void internal_CleanupClient()
{
  audmixClient_p pOneClient;

  if (!g_bInitialized)
    return;

  while (internal_SendDaemonCommand(AUDMIX_COMMAND_GETCLIENTFORPID, (void *) pidThisProcess, (void **) &pOneClient))
    AudMixDestroyClient(pOneClient);
}

//---------------------------------------------------------------------
// LibMain
//
// This gets called at DLL initialization and termination.
//---------------------------------------------------------------------
unsigned _System LibMain(unsigned hmod, unsigned termination)
{
  if (termination)
  {
    // Cleanup!

    // If a client disconnects which created clients and forgot to
    // unintialize, then uninitialize!
    internal_CleanupClient();

  } else
  {
    // Startup!

    // Save PID information
    pidThisProcess = internal_GetCurrentPID();
  }
  return 1;
}
