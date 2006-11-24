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
#include <math.h>

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"
#include "tpl.h"               /* Porting layer */
#include "tpl_threadstatics.h" /* We need this too, because we use threads */
#include "AudMixer.h"          /* Audio Mixer API */

#define TERM_OUTPUT_FORMAT  "Output to Audio Mixer"

#define WORKER_STATE_STARTING       0
#define WORKER_STATE_RUNNING        1
#define WORKER_STATE_STOPPED_OK     2
#define WORKER_STATE_STOPPED_ERROR  3


/* Max read buffer size is 8 megs */
#define AOUT_MAX_GETONEFRAME_BUF_SIZE  (1024*1024*8)
/* Increase buffer size in 2K steps */
#define AOUT_BUF_INCREASE              (2*1024)

typedef struct mmioPluginInstance_s
{
  TPL_TID               tidWorker;
  int                   iWorkerState;
  TPL_TID               tidReporter;
  int                   bShutdownRequest;

  mmioProcessTreeNode_p pOurNode;

  TPL_MTXSEM            hmtxUseRSNode;
  mmioRSSpecificInfo_p  pRSInfo;
  void                 *pRSInstance;
  long long             llRSTimeBase;
  int                   bStreamHasPTS;

  int                   iDirection;
  long long             llTimeOffset;

  audmixInfoBuffer_t    AudMixInfoBuffer;
  audmixClient_p        hAudMixClient;

} mmioPluginInstance_t, *mmioPluginInstance_p;


static void MMIOCALL aout_ReporterThread(void *pParm)
{
  mmioPluginInstance_p pPluginInstance = pParm;
  unsigned long long ullTimeStamp;
  tplTime_t timTimeStampTime;
  tplTime_t timTimeStampUpdateInterval;
  int rc;
  mmioResult_t mmiorc;
  mmioSystemTime_t timOneSecSystemTime;
  long long llSyncDiff;

  timOneSecSystemTime = MMIOPsGetOneSecSystemTime();

  while (pPluginInstance->bShutdownRequest == 0)
  {
    rc = AudMixWaitForTimestampUpdate(250);
    if (rc)
    {
      rc = AudMixGetTimestampInfo(pPluginInstance->hAudMixClient,
                                  &ullTimeStamp,
                                  &timTimeStampTime,
                                  &timTimeStampUpdateInterval);
      if (rc)
      {
        /* Ok, we have a new timestamp information, so report it to MMIO core! */
        mmiorc = MMIOPsReportPosition(pPluginInstance->pOurNode,
                                      timTimeStampTime,
                                      ullTimeStamp,
                                      timTimeStampUpdateInterval * 1000 / timOneSecSystemTime, /* Convert system time to msec */
                                      &llSyncDiff);
        if (mmiorc == MMIO_NOERROR)
        {
          /* Ok, MMIO got our report, and told us the difference from the main stream, if there is any */

//          printf("POS: %d, diff %d\n", (int) ullTimeStamp, (int) llSyncDiff);

          /* TODO */
          /* Speed up playback or slow down it or seek or framedrop if needed! */

        }

        /* Also report position to user code */
        MMIOPsReportEvent(pPluginInstance->pOurNode,
                          MMIO_EVENTCODE_POSITION_INFO, ullTimeStamp);
      }
    }
  }
}

static void MMIOCALL aout_WorkerThread(void *pParm)
{
  mmioPluginInstance_p pPluginInstance = pParm;
  void *pBuffer;
  void *pSentBuffer;
  unsigned int uiBufferSize;
  mmioResult_t rc;
  int arc;
  long long llFramePTS;
  long long llSampleRate;
  unsigned long long ullPlayTime;
  unsigned long long ullPlayTimeModulo;
  audmixBufferFormat_t fmtBufferFormat;
  mmioDataDesc_t         DataDesc;


  /* Set thread priority to high to provide smooth audio playback */
  tpl_threadSetPriority(0, TPL_THREAD_PRIORITY_HIGH);
  /* Main loop */
  pPluginInstance->iWorkerState = WORKER_STATE_RUNNING;
  while (pPluginInstance->bShutdownRequest == 0)
  {
    if (pPluginInstance->iDirection == MMIO_DIRECTION_STOP)
    {
      /* Nothing to do. Just sit, and wait for the start of playback. */
      tpl_schedDelay(32);
    } else
    {
      /* Get an empty buffer in which we can decode audio */
      arc = AudMixGetEmptyBuffer(pPluginInstance->hAudMixClient, &pBuffer, &uiBufferSize, 100);
      if (arc)
      {
        rc = MMIO_NOERROR;

        /* Decode a frame into the buffer */

//        printf("Worker: Decode into buffer\n"); fflush(stdout);

        do {
          /* Loop for reading a chunk */

          tpl_mtxsemRequest(pPluginInstance->hmtxUseRSNode, TPL_WAIT_FOREVER);

          //printf("Worker: Get one frame into buffer at %p (Enter)\n", pBuffer); fflush(stdout);
          pSentBuffer = pBuffer; /* Save the original pointer we send in there */
          rc = pPluginInstance->pRSInfo->mmiors_GetOneFrame(pPluginInstance->pRSInstance,
                                                            pPluginInstance->pRSInfo->pRSID,
                                                            &DataDesc,
                                                            &pBuffer,
                                                            uiBufferSize,
                                                            NULL);
          //printf("Worker: Get one frame (Leave, rc is %d)\n", rc); fflush(stdout);
          tpl_mtxsemRelease(pPluginInstance->hmtxUseRSNode);

          /* Check result code! */
          switch (rc)
          {
            case MMIO_ERROR_BUFFER_TOO_SMALL:
              {
                /* Try to dynamically increase stream buffer size! */
                if (uiBufferSize<AOUT_MAX_GETONEFRAME_BUF_SIZE - AOUT_BUF_INCREASE)
                {
                  /* It's not yet oversized, so we can increase! */
                  uiBufferSize+=AOUT_BUF_INCREASE; /* Increase it by some Kbytes */
                  arc = AudMixResizeBuffer(pPluginInstance->hAudMixClient, &pBuffer, uiBufferSize);
                  if (!arc)
                  {
                    /* Could not increase its size */
                    uiBufferSize-=AOUT_BUF_INCREASE;
                    break;
                  } else
                  {
                    /*
                     * Ok, buffer increased!
                     * Change error code so the loop will go back and try again
                     * with increased buffer size!
                     */
                    rc = MMIO_ERROR_NEED_MORE_BUFFERS;
                  }
                }
              }
              break;
            case MMIO_ERROR_NEED_MORE_BUFFERS:
              {
                /* Buffer kept there. We have to get another work buffer! */
                /* So, we have to allocate an extra buffer! */
                arc = AudMixAddNewBuffer(pPluginInstance->hAudMixClient, uiBufferSize);
                if (!arc)
                {
                  /* Ooops, could not allocate a new buffer */
                  rc = MMIO_ERROR_UNKNOWN; /* This will take us out of the loop */
                } else
                {
                  /* Ok, we could allocate a new buffer */
                  /* Get that new buffer! */
                  arc = AudMixGetEmptyBuffer(pPluginInstance->hAudMixClient, &pBuffer, &uiBufferSize, -1);
                  if (!arc)
                  {
                    /* Ooops, some kind of error! */
                    rc = MMIO_ERROR_UNKNOWN;
                  }
                  /* Otherwise we won't break the loop and go another round! */
                }
              }
              break;
            case MMIO_ERROR_BUFFER_NOT_USED:
              /*
               * We got back a buffer, and the buffer we gave was
               * not used (MMIO_ERROR_BUFFER_NOT_USED), so we got back an extra buffer.
               * Let's destroy it!
               */
              AudMixDestroyBuffer(pPluginInstance->hAudMixClient, pSentBuffer);
              rc = MMIO_NOERROR; /* Note that we have a good buffer! */
              break;
            case MMIO_NOERROR:
              /* Fine, all is right! */
              break;
            default:
              /* In case of other errors */
              /* the buffer we gave was not used, so give it back to AudMix! */
              AudMixPutEmptyBuffer(pPluginInstance->hAudMixClient, pSentBuffer);
              break;
          }
        } while (rc == MMIO_ERROR_NEED_MORE_BUFFERS);

        if (rc == MMIO_NOERROR)
        {
          /* We have a good decoded buffer. */

//          printf("Worker: Got a decoded buffer, size %d, data %d\n", uiBufferSize, (int) DataDesc.llDataSize); fflush(stdout);


          /* If we have PTS info, then we'll use that one to see how many system time we have to */
          /* wait to reach that PTS from the previous (already displayed) PTS. */
          /* If we don't have PTS info, then we'll use the calculated frame length of the previous frame */
          /* to know how much we have to wait. */

          ullPlayTime = 1000;
          ullPlayTime *= DataDesc.StreamInfo.AudioStruct.iSampleRate * ((DataDesc.StreamInfo.AudioStruct.iBits+7)/8) * DataDesc.StreamInfo.AudioStruct.iChannels;
          ullPlayTime += ullPlayTimeModulo;
          ullPlayTimeModulo = ullPlayTime % DataDesc.llDataSize;
          ullPlayTime /= DataDesc.llDataSize;

          /* Now calculate the PTS of this new frame! */
          /* If there is PTS info in stream, use that one */
          if ((pPluginInstance->bStreamHasPTS) && (DataDesc.llPTS!=-1))
          {
            /* Okay, this frame contains PTS info */
            /* Make sure that our PTS will be zero based (use llRSTimeBase to corrigate it)! */
            llFramePTS = DataDesc.llPTS - pPluginInstance->llRSTimeBase;
          } else
          {
            /* There is no PTS info, so use picture play time to get new PTS info! */

            /* Calculate PTS for next frame */
            if (pPluginInstance->iDirection>0)
              llFramePTS += ullPlayTime;
            else
              llFramePTS -= ullPlayTime;
          }

          /* Send this new audio buffer to Audio Mixer */
          /* Modify samplerate according to current playback speed */
          llSampleRate = DataDesc.StreamInfo.AudioStruct.iSampleRate;
          llSampleRate *= abs(pPluginInstance->iDirection);
          llSampleRate /= 1000;
          fmtBufferFormat.uiSampleRate = (unsigned int) llSampleRate;
          fmtBufferFormat.uiChannels = DataDesc.StreamInfo.AudioStruct.iChannels;
          fmtBufferFormat.uchBits = DataDesc.StreamInfo.AudioStruct.iBits;
          fmtBufferFormat.uchSigned = DataDesc.StreamInfo.AudioStruct.iIsSigned;
          fmtBufferFormat.uchDoReversePlay = (pPluginInstance->iDirection<0);
          fmtBufferFormat.uiVolume = 256;

          arc = AudMixPutFullBuffer(pPluginInstance->hAudMixClient,
                                    pBuffer,
                                    (int) DataDesc.llDataSize,
                                    llFramePTS + pPluginInstance->llTimeOffset, abs(pPluginInstance->iDirection),
                                    &fmtBufferFormat);

//          printf("AudMixPutFullBuffer(%Ld) rc = %d\n", DataDesc.llDataSize, arc);

          if (!arc)
            AudMixPutEmptyBuffer(pPluginInstance->hAudMixClient, pBuffer);
        } else
        {
          /* No buffer available, something went wrong. */
          tpl_schedDelay(32);

          /* Call event callback with flicker and so on... */
          if (rc == MMIO_ERROR_OUT_OF_DATA)
            MMIOPsReportEvent(pPluginInstance->pOurNode,
                              MMIO_EVENTCODE_OUT_OF_DATA, 0);
          else
            MMIOPsReportEvent(pPluginInstance->pOurNode,
                              MMIO_EVENTCODE_ERROR_IN_STREAM, 0);
        }
    }
    }
  } /* End of while (!bShutdownRequest) */

  /* Cleanup */

  /* Make sure there will no buffer remain in the upper nodes */
  pPluginInstance->pRSInfo->mmiors_ReleaseForeignBuffers(pPluginInstance->pRSInstance,
                                                         pPluginInstance->pRSInfo->pRSID);

  pPluginInstance->iWorkerState = WORKER_STATE_STOPPED_OK;
}


MMIOPLUGINEXPORT long         MMIOCALL aout_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_SetDirection(void *pInstance, void *pTermID, int iDirection)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_SetPosition(void *pInstance, void *pTermID, long long llPos, int iPosType, long long *pllPosFound)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_GetStreamLength(void *pInstance, void *pTermID, long long *pllLength)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_SetTimeOffset(void *pInstance, void *pTermID, long long llTimeOffset)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  pPluginInstance->llTimeOffset = llTimeOffset;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "Audio Output v1.0", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL aout_Initialize(void)
{
  mmioPluginInstance_p pInstance;
  int rc;

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
    } else
    {

      /* Check for Audio Mixer Daemon */
      rc = AudMixGetInformation(&(pInstance->AudMixInfoBuffer), sizeof(audmixInfoBuffer_t));
      if (!rc)
      {
        /* Error in Audio Mixer! */
        tpl_mtxsemDelete(pInstance->hmtxUseRSNode);
        MMIOfree(pInstance);
        return NULL;
      }

      /* Check Audio Mixer version, and check if Daemon is running! */
      if ((pInstance->AudMixInfoBuffer.iVersion < AUDMIX_VERSION_INT) ||
          (!(pInstance->AudMixInfoBuffer.bIsDaemonRunning)))
      {
        /* Either the Audio Mixer Daemon is not running, or */
        /* the Audio Mixer we've found is too old for us! */
        tpl_mtxsemDelete(pInstance->hmtxUseRSNode);
        MMIOfree(pInstance);
        return NULL;
      }

      /* Register as a new client to Audio Mixer */
      pInstance->hAudMixClient = AudMixCreateClient(5,
                                                    8192);
      if (!(pInstance->hAudMixClient))
      {
        /* Could not create new Audio Mixer client */
        tpl_mtxsemDelete(pInstance->hmtxUseRSNode);
        MMIOfree(pInstance);
        return NULL;
      }
    }
  }

  return pInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_Uninitialize(void *pInstance)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free resources allocated for instance structure */
  AudMixDestroyClient(pPluginInstance->hAudMixClient);

  tpl_mtxsemDelete(pPluginInstance->hmtxUseRSNode);
  MMIOfree(pInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  mmioRSSpecificInfo_p pRSInfo;

  /* Check parameters */
  if ((!pInstance) ||
      (!pNode) ||
      (!ppExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Terminator plugins only support RS nodes */
  if (pNode->iNodeType != MMIO_NODETYPE_RAWSTREAM)
    return MMIO_ERROR_NOT_SUPPORTED;

  pRSInfo = pNode->pTypeSpecificInfo;
  if (!pRSInfo)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We're an audio out plugin, so we only support audio nodes */
  if (pRSInfo->iStreamType != MMIO_STREAMTYPE_AUDIO)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Any other stuffs is irrelevant, we support this node then! */
  *ppExamineResult = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats =
    (mmioFormatDesc_t *) MMIOmalloc(sizeof(mmioFormatDesc_t) * (*ppExamineResult)->iNumOfEntries);

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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
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

  if (pRSInfo->iStreamType != MMIO_STREAMTYPE_AUDIO)
    return MMIO_ERROR_NOT_SUPPORTED;

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
  pTermInfo->mmioterm_SendMsg         = aout_SendMsg;
  pTermInfo->mmioterm_SetDirection    = aout_SetDirection;
  pTermInfo->mmioterm_SetPosition     = aout_SetPosition;
  pTermInfo->mmioterm_GetStreamLength = aout_GetStreamLength;
  pTermInfo->mmioterm_SetTimeOffset   = aout_SetTimeOffset;

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
                                                aout_WorkerThread,
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

    /* Free resources */
    MMIOfree(pTermInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);

    /* Then return with error. */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Worker is running */

  /* Now start a new thread that will report position information */
  pPluginInstance->tidReporter = tpl_threadCreate(1024*1024, // 1 megabyte stack
                                                  aout_ReporterThread,
                                                  pPluginInstance);
  if (!(pPluginInstance->tidReporter))
  {
    /* Ooops, could not create new thread! */

    pPluginInstance->bShutdownRequest = 1;
    /* Wait for worker to really die first! */
    tpl_threadWaitForDie(pPluginInstance->tidWorker, TPL_WAIT_FOREVER);

    MMIOfree(pTermInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Worker is running, reporter is running, everything is set up. Done. */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL aout_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if ((!pInstance) ||
      (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pNode->iNodeType != MMIO_NODETYPE_TERMINATOR)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Stop threads first */
  pPluginInstance->bShutdownRequest = 1;
  /* Ok, wait for the worker thread to stop */
  while (pPluginInstance->iWorkerState == WORKER_STATE_STARTING)
    tpl_schedDelay(32);
  tpl_threadWaitForDie(pPluginInstance->tidWorker, TPL_WAIT_FOREVER);

  /* Ok, wait for the pos-reporter thread to stop */
  tpl_threadWaitForDie(pPluginInstance->tidReporter, TPL_WAIT_FOREVER);

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
    *ppchInternalName = "aout";
    *ppchSupportedFormats = "rs_a_*"; /* Every kind of audio raw stream */
    *piImportance = 1000;

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
