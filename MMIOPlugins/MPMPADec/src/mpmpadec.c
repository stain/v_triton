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

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"

#include "interface.h"     /* mpglib interface */

/* The input buffer starts with this size: */
#define MPADEC_INITIAL_BUFFER_SIZE (16*1024)
/* The logic gets encoded audio frames until the buffer has this much data: */
#define MPADEC_MIN_BUFFER_DATA     (128)
/* If the buffer is too small, it is increased, until it reaches this size: */
#define MPADEC_MAX_BUFFER_SIZE     (512*1024)

typedef struct mpadecoderInstance_s
{
  mmioProcessTreeNode_p     pES;         /* Pointer to Elementary Stream node */
  mmioESSpecificInfo_p      pESInfo;     /* And some shortcuts from pES */
  void                     *pESInstance;

  int                       iDirection;
  MPSTR                     mpstrHandle; /* MPSTR structure of mpglib (Decoder instance data) */

  unsigned char            *pBuf;                 /* Decode buffer */
  long long                 llBufSize;            /* Size of buffer */
  mmioDataDesc_t            BufDataDesc;          /* Data desc of buffer */
  long long                 llPTS;                /* Presentation Time Stamp */
  long long                 llBufPosPTSDiffRemainder;

  int                       decrc; /* Last rc of mp3 decoder */
  int                       bDidSeek;

} mpadecoderInstance_t, *mpadecoderInstance_p;


MMIOPLUGINEXPORT long         MMIOCALL mpadecoder_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_SetDirection(void *pInstance, void *pESID, int iDirection)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if (!pInstance) return MMIO_ERROR_INVALID_PARAMETER;

  /* Set direction of upper layer */
  rc = pPluginInstance->pESInfo->mmioes_SetDirection(pPluginInstance->pESInstance,
                                                     pPluginInstance->pESInfo->pESID,
                                                     iDirection);

  /* and if it could set its direction, we can do it, too. */
  if (rc == MMIO_NOERROR)
    pPluginInstance->iDirection = iDirection;

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_ReleaseForeignBuffers(void *pInstance, void *pRSID)
{
  /* We never keep incoming buffers, so we have nothing to release. */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_GetOneFrame(void *pInstance, void *pRSID, mmioDataDesc_p pDataDesc, void **ppDataBuf, long long llDataBufSize, mmioRSFormatRequest_p pRequestedFormat)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;
  unsigned int uiDecoded;
  long long llBufSizemsec;

  if ((!pInstance) || (!pDataDesc) || (!ppDataBuf) || (!(*ppDataBuf)))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pRequestedFormat)
    return MMIO_ERROR_REQUESTED_FORMAT_NOT_SUPPORTED;

  /* This is a libmpeg limitation */
  if (llDataBufSize<4608)
    return MMIO_ERROR_BUFFER_TOO_SMALL;

  if (pPluginInstance->bDidSeek)
  {
    /* Reset decoder, we did seek in the stream! */
    ExitMP3(&(pPluginInstance->mpstrHandle));
    InitMP3(&(pPluginInstance->mpstrHandle));
    pPluginInstance->decrc = MP3_NEED_MORE;
  }

  do {
    if (pPluginInstance->decrc == MP3_OK)
    {
      do {
//        printf(" * Decoding previous data (NULL)\n");
        pPluginInstance->decrc = decodeMP3(&(pPluginInstance->mpstrHandle),
                                           NULL,
                                           0,
                                           *ppDataBuf,
                                           llDataBufSize,
                                           (int *) (&uiDecoded));
//        printf(" * Decoded : rc is %d, data is %d\n", pPluginInstance->decrc, uiDecoded);

        if ((pPluginInstance->decrc == MP3_OK) && (uiDecoded>0))
        {
          /* Fine, we could decode data without reading more. */

          /* Make pDataDesc from current ptsdiff and decoded bytes */
          pDataDesc->llDataSize = uiDecoded;
          pDataDesc->llPTS = pPluginInstance->llPTS;
          pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iSampleRate;
          pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
          pDataDesc->StreamInfo.AudioStruct.iBits = 16;
          pDataDesc->StreamInfo.AudioStruct.iChannels = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iChannels;

          /* Calculate new PTS */
          llBufSizemsec = 1000;
          llBufSizemsec *= (uiDecoded / (16/8) / pDataDesc->StreamInfo.AudioStruct.iChannels);
          llBufSizemsec += pPluginInstance->llBufPosPTSDiffRemainder;
          pPluginInstance->llBufPosPTSDiffRemainder = llBufSizemsec % pDataDesc->StreamInfo.AudioStruct.iSampleRate;
          llBufSizemsec /= pDataDesc->StreamInfo.AudioStruct.iSampleRate;

          if (pPluginInstance->iDirection>=0)
            pPluginInstance->llPTS += llBufSizemsec;
          else
            pPluginInstance->llPTS -= llBufSizemsec;

//          printf("[mpadec_GetOneFrame: Return PTS %d\n", (int) (pDataDesc->llPTS));
          return MMIO_NOERROR;
        }
      } while (pPluginInstance->decrc == MP3_OK);
    }

    /* Otherwise we could not decode the stuff, so try to read some data and feed it in there! */
    do {
      /* There is not enough data in buffer, we have to read some more! */
      do {
//        printf("[mpadec_GetOneFrame: Get ES frame\n");
        rc = pPluginInstance->pESInfo->mmioes_GetOneFrame(pPluginInstance->pESInstance,
                                                          pPluginInstance->pESInfo->pESID,
                                                          &(pPluginInstance->BufDataDesc),
                                                          pPluginInstance->pBuf,
                                                          pPluginInstance->llBufSize);

        if (rc == MMIO_ERROR_BUFFER_TOO_SMALL)
        {
          /* Try to increase the buffer! */
//          printf("   Trying to increase buffer...\n");
          if (pPluginInstance->llBufSize<MPADEC_MAX_BUFFER_SIZE)
          {
            unsigned char *pNewBuf;

            pNewBuf = MMIOrealloc(pPluginInstance->pBuf, pPluginInstance->llBufSize+2*1024);
            if (!pNewBuf)
            {
              //              printf("[mpadec_GetOneFrame] : Out of memory!\n");
              return MMIO_ERROR_OUT_OF_MEMORY;
            }

            /* Buffer size increased! We can retry with new size! */
            pPluginInstance->pBuf = pNewBuf;
            pPluginInstance->llBufSize += 2*1024;

            //            printf("   Buffer increased.\n");
          } else
            return MMIO_ERROR_OUT_OF_MEMORY;
        } else
        if (rc == MMIO_ERROR_OUT_OF_DATA)
        {
          /* End of stream! */
          return MMIO_ERROR_OUT_OF_DATA;
        } else
        if (rc != MMIO_NOERROR)
        {
          /* Some other kind of error! */
          return MMIO_ERROR_UNKNOWN;
        }
      } while (rc==MMIO_ERROR_BUFFER_TOO_SMALL);

      /* Ok, data read! */
//      printf(" * Data read (%Ld bytes).\n", pPluginInstance->BufDataDesc.llDataSize);

      /* If we could read some */

      /* Check for discontinuity! (jump in PTS and in elementary stream!) */
      if (pPluginInstance->BufDataDesc.iExtraStreamInfo & MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY)
      {
//        printf(" * Stream discontinuity, resetting decoder.\n");

        /* Discontinuity in stream! */
        /* Reset decoder before decoding this frame! */

        ExitMP3(&(pPluginInstance->mpstrHandle));
        InitMP3(&(pPluginInstance->mpstrHandle));

        /* Make sure we'll re-sync and get PTS! */
        pPluginInstance->bDidSeek = 1;
      }

#if 0
      /* Make sure the MP3 decoder will get data starting with a valid frame,
       * in case of seeking!
       * This is needed because libmpg can get confused and crash in some cases when it's not met! */

      if (pPluginInstance->bDidSeek)
      {
        long long llBufPos;
        int h;

        llBufPos = 0;
        h = 0;
        while (llBufPos < pPluginInstance->BufDataDesc.llDataSize)
        {
          unsigned long head;

          head = pPluginInstance->pBuf[llBufPos+0];
          head <<= 8;
          head |= pPluginInstance->pBuf[llBufPos+1];
          head <<= 8;
          head |= pPluginInstance->pBuf[llBufPos+2];
          head <<= 8;
          head |= pPluginInstance->pBuf[llBufPos+3];
          h = head_check(head,0);
          if (h)
          {
            /* Seems to be a nice header */
            struct frame fr;

//            printf(" * Found something at %lld\n", llBufPos);
            h = decode_header(&fr, head);
            if (h)
            {
//              printf(" * Seems to be a header at %lld\n", llBufPos);
//              print_header(&fr);
              if (llBufPos + fr.framesize+4 < pPluginInstance->BufDataDesc.llDataSize)
              {
                /* Check if the next frame is still a valid header! */
                head = pPluginInstance->pBuf[llBufPos+0 + fr.framesize+4];
                head <<= 8;
                head |= pPluginInstance->pBuf[llBufPos+1 + fr.framesize+4];
                head <<= 8;
                head |= pPluginInstance->pBuf[llBufPos+2 + fr.framesize+4];
                head <<= 8;
                head |= pPluginInstance->pBuf[llBufPos+3 + fr.framesize+4];

//                printf(" * Check next at %lld\n", llBufPos+fr.framesize);
                h = head_check(head,fr.lay);
                if (h)
                  break;
//                else
//                  printf(" * Next at %lld is not a header:/\n", llBufPos+fr.framesize);
              } else
              {
                /* Eek, too small buffer, or we cannot find a sync point. */
                /* Well, trust the decoder then to find the real sync point! */
                llBufPos = 0;
                h = 1;
                break;
              }
            }
          }
          llBufPos++;
        }

        if (!h)
        {
//          printf(" * Found no sync point, trying to read more!\n");
          pPluginInstance->BufDataDesc.llDataSize = MPADEC_MIN_BUFFER_DATA-1;
        } else
        {
          long long llTemp, llToCopy;

//          printf(" * Found a sync point at %lld\n", llBufPos);
          llToCopy = pPluginInstance->BufDataDesc.llDataSize - llBufPos;

          for (llTemp = 0; llTemp < llToCopy; llTemp++)
            pPluginInstance->pBuf[llTemp] = pPluginInstance->pBuf[llTemp + llBufPos];

          pPluginInstance->BufDataDesc.llDataSize = llToCopy;
        }
      }
#endif

      //printf(" * Input buffer to decode, size is %d\n", (int) (pPluginInstance->BufDataDesc.llDataSize));
    } while ((pPluginInstance->BufDataDesc.llDataSize) < MPADEC_MIN_BUFFER_DATA);


    do {

      /* Now try to decode this stuff we've read! */
      if (pPluginInstance->decrc == MP3_OK)
      {
        //printf(" * Decode (prev)\n");
        pPluginInstance->decrc = decodeMP3(&(pPluginInstance->mpstrHandle),
                                           NULL,
                                           0,
                                           *ppDataBuf,
                                           llDataBufSize,
                                           (int *) (&uiDecoded));
      }
      else
      {
        //printf(" * Decode (NewBuffer) %p %lld %p %lld\n",
        //       pPluginInstance->pBuf,
        //       pPluginInstance->BufDataDesc.llDataSize,
        //       *ppDataBuf,
        //       llDataBufSize
        //      );
        pPluginInstance->decrc = decodeMP3(&(pPluginInstance->mpstrHandle),
                                           pPluginInstance->pBuf,
                                           pPluginInstance->BufDataDesc.llDataSize,
                                           *ppDataBuf,
                                           llDataBufSize,
                                           (int *) (&uiDecoded));
      }

      //printf(" * Decoded : rc is %d, data is %d\n", pPluginInstance->decrc, uiDecoded);

      if ((pPluginInstance->decrc == MP3_OK) && (uiDecoded>0))
      {
        /* Fine, we could decode some data */

        /*
        printf("\n");
        printf("PTS: From container: %lld\n", pPluginInstance->BufDataDesc.llPTS);
        printf("     Calculated    : %lld\n", pPluginInstance->llPTS);
        printf("     Diff          : %lld\n", pPluginInstance->llPTS - pPluginInstance->BufDataDesc.llPTS);
        printf("\n");
        */

        if ((pPluginInstance->BufDataDesc.iExtraStreamInfo & MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY) ||
            (pPluginInstance->bDidSeek))
        {
          /* We've seeked or something similar happened, so we must */
          /* re-sync the PTS calculation */
          pPluginInstance->llPTS = pPluginInstance->BufDataDesc.llPTS;

          /* Make sure we won't take this into account again. */
          pPluginInstance->BufDataDesc.iExtraStreamInfo &= (~MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY);
        }

        pPluginInstance->bDidSeek = 0;

        /* Make pDataDesc from current ptsdiff and decoded bytes */
        pDataDesc->llDataSize = uiDecoded;
        pDataDesc->llPTS = pPluginInstance->llPTS;
        pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iSampleRate;
        pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
        pDataDesc->StreamInfo.AudioStruct.iBits = 16;
        pDataDesc->StreamInfo.AudioStruct.iChannels = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iChannels;

        /* Calculate new PTS */
        llBufSizemsec = 1000;
        llBufSizemsec *= (uiDecoded / (16/8) / pDataDesc->StreamInfo.AudioStruct.iChannels);
        llBufSizemsec += pPluginInstance->llBufPosPTSDiffRemainder;
        pPluginInstance->llBufPosPTSDiffRemainder = llBufSizemsec % pDataDesc->StreamInfo.AudioStruct.iSampleRate;
        llBufSizemsec /= pDataDesc->StreamInfo.AudioStruct.iSampleRate;

        if (pPluginInstance->iDirection>=0)
            pPluginInstance->llPTS += llBufSizemsec;
          else
            pPluginInstance->llPTS -= llBufSizemsec;

        // printf("[mpadec_GetOneFrame: Return PTS %d\n", (int) (pDataDesc->llPTS));
        return MMIO_NOERROR;
      }
    } while (pPluginInstance->decrc == MP3_OK);
  } while (pPluginInstance->decrc == MP3_NEED_MORE);

  /* Execution should never reach this, but who knows... */
  return MMIO_ERROR_UNKNOWN;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_GetStreamLength(void *pInstance, void *pRSID, long long *pllLength)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllLength))
     return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_GetStreamLength(pPluginInstance->pESInstance,
                                                        pPluginInstance->pESInfo->pESID,
                                                        pllLength);
  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_GetTimeBase(void *pInstance, void *pRSID, long long *pllFirstTimeStamp)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllFirstTimeStamp))
     return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_GetTimeBase(pPluginInstance->pESInstance,
                                                    pPluginInstance->pESInfo->pESID,
                                                    pllFirstTimeStamp);
  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_SetPosition(void *pInstance, void *pRSID, long long llPos, int iPosType, long long *pllPosFound)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllPosFound))
     return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_SetPosition(pPluginInstance->pESInstance,
                                                    pPluginInstance->pESInfo->pESID,
                                                    llPos,
                                                    iPosType,
                                                    pllPosFound);
  if (rc == MMIO_NOERROR)
    pPluginInstance->bDidSeek = 1;

  return rc;
}


MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_DropFrames(void *pInstance, void *pRSID, long long llAmount, int iPosType, long long *pllDropped)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllDropped))
    return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_DropFrames(pPluginInstance->pESInstance,
                                                   pPluginInstance->pESInfo->pESID,
                                                   llAmount, iPosType, pllDropped);
  if (rc == MMIO_NOERROR)
    pPluginInstance->bDidSeek = 1;

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "MPEG Audio Decoder v1.0", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL mpadecoder_Initialize()
{
  BOOL rc;
  mpadecoderInstance_p pPluginInstance = NULL;

  pPluginInstance = (mpadecoderInstance_p) MMIOmalloc(sizeof(mpadecoderInstance_t));
  if (pPluginInstance == NULL)
  {
    printf("[mpadecoder_Initialize] : Out of memory!\n");
    return NULL;
  }

  memset(pPluginInstance, 0, sizeof(mpadecoderInstance_t));

  pPluginInstance->iDirection = MMIO_DIRECTION_STOP;
  pPluginInstance->pESInstance = NULL;
  pPluginInstance->pESInfo = NULL;

  pPluginInstance->decrc = MP3_NEED_MORE;
  pPluginInstance->bDidSeek = 0;

  /* Initialize decoder engine */
  rc = InitMP3(&(pPluginInstance->mpstrHandle));

  if (!rc)
  {
    printf("[mpadecoder_Initialize] : Could not initialize decoder engine!\n");
    MMIOfree(pPluginInstance); pPluginInstance = NULL;
  } else
  {
    /* Allocate internal buffer */
    pPluginInstance->pBuf = MMIOmalloc(MPADEC_INITIAL_BUFFER_SIZE);
    if (!(pPluginInstance->pBuf))
    {
      printf("[mpadecoder_Initialize] : Internal buffer allocation failed!\n");
      ExitMP3(&(pPluginInstance->mpstrHandle));
      MMIOfree(pPluginInstance); pPluginInstance = NULL;
    } else
    {
      /* Internal buffer pointer: */
      pPluginInstance->llBufSize = MPADEC_INITIAL_BUFFER_SIZE;
    }
  }

  return pPluginInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_Uninitialize(void *pInstance)
{
  mpadecoderInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free all our resources! */
  ExitMP3(&(pPluginInstance->mpstrHandle));
  MMIOfree(pPluginInstance->pBuf);
  MMIOfree(pPluginInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  if ((!pInstance) || (!pNode) || (!ppExamineResult) || (pNode->iNodeType!=MMIO_NODETYPE_ELEMENTARYSTREAM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We should examine the ES node here, to know what we can produce from it. */
  /* However, we know that we only can produce PCM. :) */

  /* What we might really do here is to probe the stream, if it's really a decodable one. */
  /* We now leave it for a later version. */

  (*ppExamineResult) = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc((*ppExamineResult)->iNumOfEntries * sizeof(mmioFormatDesc_t));

  // We produce PCM audio raw stream
  snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t), "rs_a_PCM");

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mpadecoderInstance_p pPluginInstance = pInstance;
  mmioProcessTreeNode_p     pNewRS;
  mmioRSSpecificInfo_p      pRSInfo;
  mmioESSpecificInfo_p      pESInfo;

  /* Check Params */
  if ((!pInstance) || (!pchNeededOutputFormat) || (!pNode) || (pNode->iNodeType!=MMIO_NODETYPE_ELEMENTARYSTREAM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check the needed output format: */
  if (strcmp(pchNeededOutputFormat, "rs_a_PCM"))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Get pointer to ES info */
  pESInfo = (mmioESSpecificInfo_p) (pNode->pTypeSpecificInfo);

  /* We'll create a raw stream node */
  pNewRS = MMIOPsCreateAndLinkNewNodeStruct(pNode);
  if (!pNewRS)
  {
    /* Could not create new node! */
    return MMIO_ERROR_OUT_OF_MEMORY;
  }
  pRSInfo = MMIOmalloc(sizeof(mmioRSSpecificInfo_t));
  if (!pRSInfo)
  {
    /* Out of memory */
    MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Ok, fill the new node with information */

  strncpy(pNewRS->achNodeOwnerOutputFormat, pchNeededOutputFormat, sizeof(pNewRS->achNodeOwnerOutputFormat));
  pNewRS->bUnlinkPoint = 1;
  pNewRS->iNodeType = MMIO_NODETYPE_RAWSTREAM;
  pNewRS->pTypeSpecificInfo = pRSInfo;
  pRSInfo->pRSID = NULL;
  strncpy(pRSInfo->achDescriptionText, pESInfo->achDescriptionText, sizeof(pRSInfo->achDescriptionText));
  pRSInfo->mmiors_SendMsg = mpadecoder_SendMsg;
  pRSInfo->mmiors_SetDirection = mpadecoder_SetDirection;
  pRSInfo->mmiors_SetPosition = mpadecoder_SetPosition;
  pRSInfo->mmiors_GetOneFrame = mpadecoder_GetOneFrame;
  pRSInfo->mmiors_ReleaseForeignBuffers = mpadecoder_ReleaseForeignBuffers;
  pRSInfo->mmiors_GetStreamLength = mpadecoder_GetStreamLength;
  pRSInfo->mmiors_DropFrames = mpadecoder_DropFrames;
  pRSInfo->mmiors_GetTimeBase = mpadecoder_GetTimeBase;

  /* Let's see what are our capabilities! */
  /* We support everything our parent (ES) supports, except backward playback! */
  pRSInfo->iRSCapabilities =
    pESInfo->iESCapabilities &
    ~(MMIO_ES_CAPS_DIRECTION_CUSTOMREVERSE | MMIO_ES_CAPS_DIRECTION_REVERSE);

  pRSInfo->iStreamType = MMIO_STREAMTYPE_AUDIO;

  /* Our output will probably be this: */
  /* Our decoder always outputs 16bits signed samples */
  pRSInfo->StreamInfo.AudioStruct.iBits = 16;
  pRSInfo->StreamInfo.AudioStruct.iIsSigned = 1;
  pRSInfo->StreamInfo.AudioStruct.iChannels = pESInfo->StreamInfo.AudioStruct.iChannels;
  pRSInfo->StreamInfo.AudioStruct.iSampleRate = pESInfo->StreamInfo.AudioStruct.iSampleRate;

  snprintf(pRSInfo->achDescriptiveRSFormat, sizeof(pRSInfo->achDescriptiveRSFormat),
           "PCM_%d_U%d_%dCH",
           pRSInfo->StreamInfo.AudioStruct.iSampleRate,
           pRSInfo->StreamInfo.AudioStruct.iBits,
           pRSInfo->StreamInfo.AudioStruct.iChannels);

  /* Save some pointers as shortcuts */
  pPluginInstance->pESInfo = pESInfo;
  pPluginInstance->pESInstance = pNode->pNodeOwnerPluginInstance;

  return MMIO_NOERROR;
}

static void mpadecoder_internal_DestroyTree(mmioProcessTreeNode_p pRoot)
{
  if (!pRoot) return;

  if (pRoot->pFirstChild)
    mpadecoder_internal_DestroyTree(pRoot->pFirstChild);

  if (pRoot->pNextBrother)
    mpadecoder_internal_DestroyTree(pRoot->pNextBrother);

  MMIOfree(pRoot->pTypeSpecificInfo);
  MMIOPsUnlinkAndDestroyNodeStruct(pRoot);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mpadecoder_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{

  if ((!pInstance) || (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Destroy all the levels we've created */
  mpadecoder_internal_DestroyTree(pNode);

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
    *ppchInternalName = "mpadecoder";
    *ppchSupportedFormats = "es_a_MP2;es_a_MP3;es_a_MPA"; /* MPEG Audio elementary streams */
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
