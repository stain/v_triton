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

/*
 ----- MP3 demuxer plugin -----
 If the media handler can seek and tell, then we create an index table first,
 so we can seek back and forth.
 If it cannot, then we can only go forward in the stream, and we don't create
 the index table.
 So, if you plan to change something in the logic of reading of stream, be careful, that
 you'll have to change the logic at 2 places:
  - CreateIndexTable
  - GetOneFrame, branch of "not bFullControlAvailable"
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"

#include "mpmp3dem.h"


typedef struct mp3demuxIndexTable_s
{
  long long llPTS;

  long lFilePosition;
  long lFrameSize;
} mp3demuxIndexTable_t, *mp3demuxIndexTable_p;

typedef struct mp3demuxInstance_s
{
  mmioProcessTreeNode_p pMedia;          // Pointer to media-handler node
  int                   bFullControlAvailable;
  int                   iDirection;      // Current play direction
  int                   bDiscontinuity;  // Discontinuity indicator

  // If at least one frame has been processed (by creating index table, or
  // by playing some of the stream), then the following info is available:
  int                   bFirstFrameProcessed;
  int                   iMPEGAudioVersion; // Currently valid: 10, 20 and 25 for MPEG 1, 2 and 2.5
  int                   iLayer;            // 1..3
  int                   iBitRate;          //
  int                   iSamplingRate;
  int                   iPadding;
  int                   iChannelMode;      // 0 = stereo, 1 = Joint stereo, 2 = dual channel, 3 = single channel (Mono)
  int                   iModeExtension;    // 0 = nothing, 1 = Intensity stereo, 2 = MS stereo, 3 = both
  int                   iCopyright;
  int                   iOriginal;

  // If we have full control on the stream, we have an index table too, and
  // we know the length of the stream!
  long long             llStreamLength;
  mp3demuxIndexTable_p  pIndexTable;
  unsigned int          uiIndexTableSize;
  int                   iStreamPosition;

  // If we don't have full control of the stream, we need these:
  long long             llCurrPTS;
  long                  lLastBufPlayTime;
  long                  lLastBufPlayTimeModulo;

  // IDv1 TAG:
  int                   bTAGAvailable;
  mp3demuxMP3TAG_t      TAG; // MP3 TAG data, if available

} mp3demuxInstance_t, *mp3demuxInstance_p;

// Index, Version, Layer
//                           MP1L1   MP1L2   MP1L3     MP2L1   MP2L2   MP2L3    MP25L1  MP25L2  MP25L3
int aBitRate[16][3][3] = {{{128000, 128000, 128000}, {128000, 128000, 128000}, {128000, 128000, 128000}}, // VBR
                          {{ 32000,  32000,  32000}, { 32000,   8000,   8000}, { 32000,   8000,   8000}},
                          {{ 64000,  48000,  40000}, { 48000,  16000,  16000}, { 48000,  16000,  16000}},
                          {{ 96000,  56000,  48000}, { 56000,  24000,  24000}, { 56000,  24000,  24000}},
                          {{128000,  64000,  56000}, { 64000,  32000,  32000}, { 64000,  32000,  32000}},
                          {{160000,  80000,  64000}, { 80000,  40000,  40000}, { 80000,  40000,  40000}},
                          {{192000,  96000,  80000}, { 96000,  48000,  48000}, { 96000,  48000,  48000}},
                          {{224000, 112000,  96000}, {112000,  56000,  56000}, {112000,  56000,  56000}},
                          {{256000, 128000, 112000}, {128000,  64000,  64000}, {128000,  64000,  64000}},
                          {{288000, 160000, 128000}, {144000,  80000,  80000}, {144000,  80000,  80000}},
                          {{320000, 192000, 160000}, {160000,  96000,  96000}, {160000,  96000,  96000}},
                          {{352000, 224000, 192000}, {176000, 112000, 112000}, {176000, 112000, 112000}},
                          {{384000, 256000, 224000}, {192000, 128000, 128000}, {192000, 128000, 128000}},
                          {{416000, 320000, 256000}, {224000, 144000, 144000}, {224000, 144000, 144000}},
                          {{448000, 384000, 320000}, {256000, 160000, 160000}, {256000, 160000, 160000}},
                          {{128000, 128000, 128000}, {128000, 128000, 128000}, {128000, 128000, 128000}}}; // BAD

//                          MPEG1  MPEG2  MPEG2.5
int aSamplingRate[4][3] = {{44100, 22050, 11025},
                           {48000, 24000, 12000},
                           {32000, 16000, 8000},
                           {44100, 22050, 11025}};


MMIOPLUGINEXPORT long         MMIOCALL mp3demux_es_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  /* Message handler for both channels and elementary streams */
  mp3demuxInstance_p pPluginInstance = pInstance;
  mp3demuxMP3TAG_p *ppTAG;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (lCommandWord == MMIO_MP3DEMUX_MSG_GETTAG)
  {
    if (pPluginInstance->bTAGAvailable)
    {
      ppTAG = pParam1;
      *ppTAG = &(pPluginInstance->TAG);
      return MMIO_NOERROR;
    } else
      return MMIO_ERROR_NOT_FOUND; /* TAG not available */
  } else
  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT long         MMIOCALL mp3demux_ch_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return mp3demux_es_SendMsg(pInstance, lCommandWord, pParam1, pParam2);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_SetDirection(void *pInstance, void *pESID, int iDirection)
{
  mp3demuxInstance_p pPluginInstance = pInstance;

  /* Check parameters. */
  /* We don't take care of pESID, as mpeg audio only has one stream inside. */
  if (!pInstance)
  {
    printf("[mp3demux_SetDirection] : Invalid parameter!\n");
    return MMIO_ERROR_INVALID_PARAMETER;
  }

  if (pPluginInstance->bFullControlAvailable)
  {
    // If we can seek back and forth, then we accept every direction.
    pPluginInstance->iDirection = iDirection;
    return MMIO_NOERROR;
  } else
  {
    // If we can not seek back and forth, then we accept a subset of
    // stream directions!
    if (iDirection >= MMIO_DIRECTION_STOP)
    {
      pPluginInstance->iDirection = iDirection;
      return MMIO_NOERROR;
    } else
      return MMIO_ERROR_NOT_SUPPORTED;
  }
}

static void mp3demux_internal_SaveHeaderInfo(mp3demuxInstance_p pPluginInstance, long lData)
{
  int iBitRateIndex;
  int iBitRateLayerIndex;
  int iBitRateVersionIndex;
  int iSamplingRateIndex;

  pPluginInstance->iMPEGAudioVersion = (lData >> 19) & 3;
  switch (pPluginInstance->iMPEGAudioVersion)
  {
    case 0:
      pPluginInstance->iMPEGAudioVersion = 25; // MPEG Version 2.5
      iBitRateVersionIndex = 2;
      break;
    case 2:
      pPluginInstance->iMPEGAudioVersion = 20; // MPEG Version 2
      iBitRateVersionIndex = 1;
      break;
    case 3:
      pPluginInstance->iMPEGAudioVersion = 10; // MPEG Version 1
      iBitRateVersionIndex = 0;
      break;
    default:
      if (!(pPluginInstance->bFirstFrameProcessed))
        printf("[mp3demux_internal_SaveHeaderInfo] : Warning, unknown MPEG Audio version!\n");

      // Use the previous one, if available!
      if (pPluginInstance->iMPEGAudioVersion == 0)
        pPluginInstance->iMPEGAudioVersion = 20; // Defaults to MPEG Version 2

      iBitRateVersionIndex = ((pPluginInstance->iMPEGAudioVersion+5)/10) -1;
      break;
  }
  pPluginInstance->iLayer = (lData >> 17) & 3;
  switch (pPluginInstance->iLayer)
  {
    case 1:
      pPluginInstance->iLayer = 3; // Layer III
      iBitRateLayerIndex = 2;
      break;
    case 2:
      pPluginInstance->iLayer = 2; // Layer II
      iBitRateLayerIndex = 1;
      break;
    case 3:
      pPluginInstance->iLayer = 1; // Layer I
      iBitRateLayerIndex = 0;
      break;
    default:
      if (!(pPluginInstance->bFirstFrameProcessed))
        printf("[mp3demux_internal_SaveHeaderInfo] : Warning, unknown MPEG Audio layer!\n");

      // Use the previous one, if available
      if (pPluginInstance->iLayer == 0)
        pPluginInstance->iLayer = 3; // Defaults to Layer III

      iBitRateLayerIndex = pPluginInstance->iLayer-1;
      break;
  }

  iBitRateIndex = (lData >> 12) & 15;
  pPluginInstance->iBitRate = aBitRate[iBitRateIndex][iBitRateVersionIndex][iBitRateLayerIndex];

  iSamplingRateIndex = (lData >> 10) & 3;
  pPluginInstance->iSamplingRate = aSamplingRate[iSamplingRateIndex][iBitRateVersionIndex];

  pPluginInstance->iPadding = (lData >> 9) & 1;

  pPluginInstance->iChannelMode = (lData >> 6) & 3;
  pPluginInstance->iModeExtension = (lData >> 4) & 3;

  pPluginInstance->iCopyright = (lData >> 3) & 1;
  pPluginInstance->iOriginal = (lData >> 2) & 1;

  /*
  if (!(pPluginInstance->bFirstFrameProcessed))
  {
    printf("  MPEG Audio Frame:\n");
    printf("   iMPEGAudioVersion = %d\n"
           "   iLayer            = %d\n"
           "   iBitRate          = %d\n"
           "   iSamplingRate     = %d\n"
           "   iPadding          = %d\n"
           "   iChannelMode      = %d\n"
           "   iModeExtension    = %d\n"
           "   iCopyright        = %d\n"
           "   iOriginal         = %d\n",
           pPluginInstance->iMPEGAudioVersion,
           pPluginInstance->iLayer,
           pPluginInstance->iBitRate,
           pPluginInstance->iSamplingRate,
           pPluginInstance->iPadding,
           pPluginInstance->iChannelMode,
           pPluginInstance->iModeExtension,
           pPluginInstance->iCopyright,
           pPluginInstance->iOriginal);
           }
           */
  if (!pPluginInstance->bFirstFrameProcessed)
    pPluginInstance->bFirstFrameProcessed = 1;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_GetOneFrame(void *pInstance, void *pESID, mmioDataDesc_p pDataDesc, void *pDataBuf, long long llDataBufSize)
{
  mp3demuxInstance_p pPluginInstance = pInstance;
  mmioResult_t iResult;
  void *pSourceInstance;
  mmioMediaSpecificInfo_p pSource;

  if ((!pInstance) || (!pDataDesc) || (!pDataBuf))
     return MMIO_ERROR_INVALID_PARAMETER;

  /* Get pointer to media handler node stuff */
  pSourceInstance = pPluginInstance->pMedia->pNodeOwnerPluginInstance;
  pSource = pPluginInstance->pMedia->pTypeSpecificInfo;

  /* Set extra stream info */
  pDataDesc->iExtraStreamInfo = 0;
  if (pPluginInstance->bDiscontinuity)
  {
    pDataDesc->iExtraStreamInfo |= MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY;
    pPluginInstance->bDiscontinuity = 0;
  }

  /* Read and move in stream */
  if (pPluginInstance->bFullControlAvailable)
  {
    /* If we have full control, then we have an index table */
    if (pPluginInstance->iDirection>=0)
    {
      /* Going forward in stream */
      if (pPluginInstance->iStreamPosition>=pPluginInstance->uiIndexTableSize)
        return MMIO_ERROR_OUT_OF_DATA;

      if (llDataBufSize<pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize)
        return MMIO_ERROR_BUFFER_TOO_SMALL;

      iResult = pSource->mmiomedia_Seek(pSourceInstance,
                                        pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFilePosition,
                                        MMIO_MEDIA_SEEK_SET);
      if (iResult!=MMIO_NOERROR)
        return MMIO_ERROR_IN_MEDIA_HANDLER;

      iResult = pSource->mmiomedia_Read(pSourceInstance,
                                        pDataBuf,
                                        pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize);
      if (iResult!=MMIO_NOERROR)
        return MMIO_ERROR_OUT_OF_DATA;

      pDataDesc->llPTS = pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].llPTS;
      pDataDesc->llDataSize = pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize;
      pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->iSamplingRate;
      pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
      pDataDesc->StreamInfo.AudioStruct.iBits = 16;
      pDataDesc->StreamInfo.AudioStruct.iChannels = 2;

      pPluginInstance->iStreamPosition++;
      return MMIO_NOERROR;
    } else
    {
      /* Going backward in stream */
      if (pPluginInstance->iStreamPosition<=0)
        return MMIO_ERROR_OUT_OF_DATA;

      if (llDataBufSize<pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize)
        return MMIO_ERROR_BUFFER_TOO_SMALL;

      iResult = pSource->mmiomedia_Seek(pSourceInstance,
                                        pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFilePosition,
                                        MMIO_MEDIA_SEEK_SET);
      if (iResult!=MMIO_NOERROR)
        return MMIO_ERROR_IN_MEDIA_HANDLER;

      iResult = pSource->mmiomedia_Read(pSourceInstance,
                                        pDataBuf,
                                        pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize);
      if (iResult!=MMIO_NOERROR)
        return MMIO_ERROR_OUT_OF_DATA;

      pDataDesc->llPTS = pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].llPTS;
      pDataDesc->llDataSize = pPluginInstance->pIndexTable[pPluginInstance->iStreamPosition].lFrameSize;
      pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->iSamplingRate;
      pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
      pDataDesc->StreamInfo.AudioStruct.iBits = 16;
      pDataDesc->StreamInfo.AudioStruct.iChannels = 2;

      pPluginInstance->iStreamPosition--;
      return MMIO_NOERROR;
    }
  } else
  {
    long lData, lTempData, lFrameSize;
    int iNumSamples;

    /*
     No full control, we can go only forward in the stream
     Also note that we have no index table, so we have to look
     for valid MPEG Audio frames here, while reading!
     */

    if (pPluginInstance->iDirection<0)
      return MMIO_ERROR_OUT_OF_DATA;

    iResult = MMIO_NOERROR;
    while (iResult == MMIO_NOERROR)
    {
      iResult = pSource->mmiomedia_Read(pSourceInstance, &lTempData, 4);
      if (iResult!=MMIO_NOERROR)
        return MMIO_ERROR_OUT_OF_DATA;

      /* Swap byte order... */
      lData = (lTempData & 0xFF) << 24;
      lData |= (lTempData & 0xFF00) << 8;
      lData |= (lTempData & 0xFF0000) >> 8;
      lData |= (lTempData & 0xFF000000) >> 24;

      /* Let's check what we read! */
      if ((lData & 0xFFE00000) == 0xFFE00000) // Bits 32..21 are set to 1
      {
        /* Good, found a frame header! */

        /* Get stream parameters */
        mp3demux_internal_SaveHeaderInfo(pPluginInstance, lData);
        if (!pPluginInstance->bFirstFrameProcessed)
          pPluginInstance->bFirstFrameProcessed = 1;

        /* Get frame size */
        if (pPluginInstance->iLayer == 1)
        {
          lFrameSize =
            ((12 * pPluginInstance->iBitRate) / pPluginInstance->iSamplingRate + pPluginInstance->iPadding) * 4;
        } else
        {
          lFrameSize =
            ((144 * pPluginInstance->iBitRate) / pPluginInstance->iSamplingRate) + pPluginInstance->iPadding;
        }

        memcpy(pDataBuf, &lTempData, 4);
        iResult = pSource->mmiomedia_Read(pSourceInstance,
                                        (void *)(((char *)pDataBuf)+4),
                                        lFrameSize-4);
        if (iResult!=MMIO_NOERROR)
          return MMIO_ERROR_OUT_OF_DATA;

        /* Timestamp: */
        /* First get the number of samples in this frame    */
        /*  MPEG 1,2,2.5 Layer 1    : 384 samples           */
        /*  MPEG 1       Layer 2,3  : 1152 samples          */
        /*  MPEG 2, 2.5  Layer 2,3  : 1152/2 samples        */
        if (pPluginInstance->iLayer == 1)
          iNumSamples = 384;
        else
        if (pPluginInstance->iLayer == 2)
          iNumSamples = 1152;
        else
        {
          if (pPluginInstance->iMPEGAudioVersion == 10)
            iNumSamples = 1152;
          else
            iNumSamples = 576;

          /* For mono stuffs, we double the number of samples */
          if (pPluginInstance->iChannelMode==3) iNumSamples*=2;
        }
        /* Buffer play time in msec: iNumSamples / pPluginInstance->iSampleRate * 1000; */
        pPluginInstance->lLastBufPlayTime = 1000;
        pPluginInstance->lLastBufPlayTime *= iNumSamples;
        pPluginInstance->lLastBufPlayTime += pPluginInstance->lLastBufPlayTimeModulo;
        pPluginInstance->lLastBufPlayTimeModulo = pPluginInstance->lLastBufPlayTime % pPluginInstance->iSamplingRate;
        pPluginInstance->lLastBufPlayTime /= pPluginInstance->iSamplingRate;

        pDataDesc->llPTS = pPluginInstance->llCurrPTS;

        pPluginInstance->llCurrPTS += pPluginInstance->lLastBufPlayTime;

        pDataDesc->llDataSize = lFrameSize;
        pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->iSamplingRate;
        pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
        pDataDesc->StreamInfo.AudioStruct.iBits = 16;
        pDataDesc->StreamInfo.AudioStruct.iChannels = 2;

        return MMIO_NOERROR;
      } else
      {
        /* Not an MPEG Audio frame, so what? */
        /* It can be a MP3 ID tag... */
        if ((((lData & 0xFF000000)>>24)=='T') &&
            (((lData & 0xFF0000)>>16)=='A') &&
            (((lData & 0xFF00)>>8)=='G'))
        {
          /* Cool, it's an MP3 TAG v1.0! */
          /* Read TAG data! */
          pPluginInstance->TAG.achTitle[0] = lData & 0xFF;
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achTitle[1]), 29);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achArtist), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achAlbum), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achYear), 4);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achComment), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.chGenre), 1);
          pPluginInstance->bTAGAvailable = 1;
          /* We do not return, so we'll try to read more data in this loop. */
        }
      }
    }
    return MMIO_ERROR_OUT_OF_DATA;
  }
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_GetStreamLength(void *pInstance, void *pESID, long long *pllLength)
{
  mp3demuxInstance_p pPluginInstance = pInstance;

  if ( !pInstance || !pllLength)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Return stream length if we know it! */
  if (pPluginInstance->bFullControlAvailable)
  {
    *pllLength = pPluginInstance->llStreamLength;
    return MMIO_NOERROR;
  } else
    return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_GetTimeBase(void *pInstance, void *pESID, long long *pllFirstTimeStamp)
{
  if ((!pInstance) || (!pllFirstTimeStamp))
    return MMIO_ERROR_INVALID_PARAMETER;

  *pllFirstTimeStamp = 0; /* We always start timing from 0 in mp3 files */

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_SetPosition(void *pInstance, void *pESID, long long llPos, int iPosType, long long *pllPosFound)
{
  mp3demuxInstance_p pPluginInstance = pInstance;

  if ((!pInstance) || (!pllPosFound))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pPluginInstance->bFullControlAvailable)
  {
    long l;
    if (iPosType == MMIO_POSTYPE_TIME)
    {
      for (l=0; l<pPluginInstance->uiIndexTableSize; l++)
      {
        if (pPluginInstance->pIndexTable[l].llPTS>=llPos) break;
      }
      if (l>=pPluginInstance->uiIndexTableSize) l = pPluginInstance->uiIndexTableSize-1;

      *pllPosFound = pPluginInstance->pIndexTable[l].llPTS;
      pPluginInstance->iStreamPosition = l;
      pPluginInstance->bDiscontinuity = 1; /* Note that there will be some discontinuity */
      return MMIO_NOERROR;
    } else
    if (iPosType == MMIO_POSTYPE_BYTE)
    {
      for (l=0; l<pPluginInstance->uiIndexTableSize; l++)
      {
        if (pPluginInstance->pIndexTable[l].lFilePosition>=llPos) break;
      }
      if (l>=pPluginInstance->uiIndexTableSize) l = pPluginInstance->uiIndexTableSize-1;

      *pllPosFound = pPluginInstance->pIndexTable[l].lFilePosition;
      pPluginInstance->iStreamPosition = l;
      pPluginInstance->bDiscontinuity = 1; /* Note that there will be some discontinuity */
      return MMIO_NOERROR;
    } else
      return MMIO_ERROR_NOT_SUPPORTED;
  } else
    return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_DropFrames(void *pInstance, void *pESID, long long llAmount, int iPosType, long long *pllDropped)
{
  /* TODO: Implement it! */
  return MMIO_ERROR_NOT_IMPLEMENTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "MPEG Audio File Demuxer v1.0", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void * MMIOCALL mp3demux_Initialize()
{
  mp3demuxInstance_p pPluginInstance = NULL;

  pPluginInstance = (mp3demuxInstance_p) MMIOmalloc(sizeof(mp3demuxInstance_t));
  if (pPluginInstance == NULL)
    return NULL;

  memset(pPluginInstance, 0, sizeof(mp3demuxInstance_t));

  return pPluginInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_Uninitialize(void *pInstance)
{
  mp3demuxInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free all our resources! */
  MMIOfree(pPluginInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  mmioMediaSpecificInfo_p pMediaInfo;

  if ((!pInstance) || (!pNode) || (!ppExamineResult) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We should examine the file here, to know what we can produce from it. */
  /* It's simple for mpeg audio, we know that we can produce only one mp3 elementary stream. */

  pMediaInfo = pNode->pTypeSpecificInfo;
  if (!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_READ))
  {
    printf("[mp3demux_Examine] : Media cannot be read!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  (*ppExamineResult) = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc((*ppExamineResult)->iNumOfEntries * sizeof(mmioFormatDesc_t));

  /* We produce mpeg audio elementary stream */
  snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t), "es_a_MPA");


  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

static int mp3demux_internal_CreateIndexTable(mp3demuxInstance_p pPluginInstance)
{
  void *pSourceInstance;
  mmioMediaSpecificInfo_p pSource;
  mmioResult_t iResult;

  long long llCurrPTS;
  long lBufPlayTime;
  long lBufPlayTimeModulo;
  long lData, lTempData;
  int iNumSamples;

  /* Initialize variables */
  llCurrPTS = 0;
  lBufPlayTime = 0;
  lBufPlayTimeModulo = 0;
  pSourceInstance = pPluginInstance->pMedia->pNodeOwnerPluginInstance;
  pSource = pPluginInstance->pMedia->pTypeSpecificInfo;

  /* Seek to start of stream */
  if (pSource->mmiomedia_Seek(pSourceInstance, 0, MMIO_MEDIA_SEEK_SET)!=MMIO_NOERROR)
  {
    printf("[mp3demux_internal_CreateIndexTable] : Error at seeking!\n");
    return 0;
  }

  /* Now go through all the stream/file! */
  pPluginInstance->bFirstFrameProcessed = 0;
  if (pPluginInstance->pIndexTable)
  {
    MMIOfree(pPluginInstance->pIndexTable);
    pPluginInstance->pIndexTable = NULL;
  }
  pPluginInstance->uiIndexTableSize = 0;
  iResult = MMIO_NOERROR;
  while (iResult == MMIO_NOERROR)
  {
    lTempData = 0;
    iResult = pSource->mmiomedia_Read(pSourceInstance, &lTempData, 4);
    /* Swap byte order... */
    lData = (lTempData & 0xFF) << 24;
    lData |= (lTempData & 0xFF00) << 8;
    lData |= (lTempData & 0xFF0000) >> 8;
    lData |= (lTempData & 0xFF000000) >> 24;

    if (iResult == MMIO_NOERROR)
    {
      /* Let's check what we read! */
      if ((lData & 0xFFE00000) == 0xFFE00000) /* Bits 32..21 are set to 1 */
      {
        /* Good, found a frame header! */
        /* Get stream parameters */
        mp3demux_internal_SaveHeaderInfo(pPluginInstance, lData);
        /* Now create the index table! */
        /* First expand the table */
        if (!pPluginInstance->pIndexTable)
        {
          pPluginInstance->pIndexTable = (mp3demuxIndexTable_p) MMIOmalloc(sizeof(mp3demuxIndexTable_t));
          if (!(pPluginInstance->pIndexTable))
          {
            printf("[mp3demux_internal_CreateIndexTable] : Out of memory! (@1)\n");
            return 0;
          }
          pPluginInstance->uiIndexTableSize++;
        } else
        {
          void *pNew;
          pNew = MMIOrealloc(pPluginInstance->pIndexTable, (pPluginInstance->uiIndexTableSize+1) * sizeof(mp3demuxIndexTable_t));
          if (!pNew)
          {
            printf("[mp3demux_internal_CreateIndexTable] : Out of memory! (@2)\n");
            MMIOfree(pPluginInstance->pIndexTable); pPluginInstance->pIndexTable = NULL;
            pPluginInstance->uiIndexTableSize = 0;
            return 0;
          }
          pPluginInstance->pIndexTable = pNew;
          pPluginInstance->uiIndexTableSize++;
        }
        /* Then fill the new index table entry */
        /*  - position */
        pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFilePosition =
          pSource->mmiomedia_Tell(pSourceInstance) - 4;
        /*  - frame size */
        if (pPluginInstance->iLayer == 1)
        {
          pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFrameSize =
            ((12 * pPluginInstance->iBitRate) / pPluginInstance->iSamplingRate + pPluginInstance->iPadding) * 4;
        } else
        {
          pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFrameSize =
            ((144 * pPluginInstance->iBitRate) / pPluginInstance->iSamplingRate) + pPluginInstance->iPadding;
        }
        /*  - PTS */
        /* First get the number of samples in this frame  */
        /*  MPEG 1,2,2.5 Layer 1    : 384 samples         */
        /*  MPEG 1       Layer 2,3  : 1152 samples        */
        /*  MPEG 2, 2.5  Layer 2,3  : 1152/2 samples      */
        if (pPluginInstance->iLayer == 1)
          iNumSamples = 384;
        else
        if (pPluginInstance->iLayer == 2)
          iNumSamples = 1152;
        else
        {
          if (pPluginInstance->iMPEGAudioVersion == 10)
            iNumSamples = 1152;
          else
            iNumSamples = 576;

          /* For mono stuffs, we double the number of samples */
          if (pPluginInstance->iChannelMode==3) iNumSamples*=2;
        }
        /* Buffer play time in msec: iNumSamples / pPluginInstance->iSampleRate * 1000; */
        lBufPlayTime = 1000;
        lBufPlayTime *= iNumSamples;
        lBufPlayTime += lBufPlayTimeModulo;
        lBufPlayTimeModulo = lBufPlayTime % pPluginInstance->iSamplingRate;
        lBufPlayTime /= pPluginInstance->iSamplingRate;

        pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].llPTS = llCurrPTS;

        llCurrPTS += lBufPlayTime;
        /* Skip the audio bytes */
        if (pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFrameSize == 0)
          pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFrameSize = 1;
        iResult = pSource->mmiomedia_Seek(pSourceInstance,
                                          pPluginInstance->pIndexTable[pPluginInstance->uiIndexTableSize-1].lFrameSize - 4,
                                          MMIO_MEDIA_SEEK_CUR);
      } else
      {
        /* Not an MPEG Audio frame, so what? */
        /* It can be a MP3 ID tag... */
        if ((((lData & 0xFF000000)>>24)=='T') &&
            (((lData & 0xFF0000)>>16)=='A') &&
            (((lData & 0xFF00)>>8)=='G'))
        {
          /* Cool, it's an MP3 TAG v1.0! */
          /* Read TAG data! */
          pPluginInstance->TAG.achTitle[0] = lData & 0xFF;
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achTitle[1]), 29);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achArtist), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achAlbum), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achYear), 4);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.achComment), 30);
          iResult = pSource->mmiomedia_Read(pSourceInstance, &(pPluginInstance->TAG.chGenre), 1);
          pPluginInstance->bTAGAvailable = 1;
          /*
           printf("[mp3demux_internal_CreateIndexTable] : Found IDv1 TAG:\n");
           printf("   Title:   [%s]\n", pPluginInstance->TAG.achTitle);
           printf("   Artist:  [%s]\n", pPluginInstance->TAG.achArtist);
           printf("   Album:   [%s]\n", pPluginInstance->TAG.achAlbum);
           printf("   Year:    [%s]\n", pPluginInstance->TAG.achYear);
           printf("   Comment: [%s]\n", pPluginInstance->TAG.achComment);
           printf("   Genre:   [%d]\n", pPluginInstance->TAG.chGenre);
           */
        } else
        {
          /* Skip the unknown byte */
//        printf(" No idea what can this be!: 0x%x\n", lData);
          iResult = pSource->mmiomedia_Seek(pSourceInstance, -3, MMIO_MEDIA_SEEK_CUR);
        }
      }
    }
  }

  /* Seek back to start of stream */
  if (pSource->mmiomedia_Seek(pSourceInstance, 0, MMIO_MEDIA_SEEK_SET)!=MMIO_NOERROR)
  {
    printf("[mp3demux_internal_CreateIndexTable] : Error at seeking!\n");
    if (pPluginInstance->pIndexTable)
      MMIOfree(pPluginInstance->pIndexTable);
    pPluginInstance->pIndexTable = NULL;
    pPluginInstance->uiIndexTableSize = 0;
    return 0;
  }

  pPluginInstance->llStreamLength = llCurrPTS;

  return 1;
}

static void mp3demux_internal_DestroyIndexTable(mp3demuxInstance_p pPluginInstance)
{
  if (pPluginInstance->pIndexTable)
  {
    MMIOfree(pPluginInstance->pIndexTable);
    pPluginInstance->pIndexTable = NULL;
  }
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mp3demuxInstance_p pPluginInstance = pInstance;
  mmioProcessTreeNode_p     pNewCh, pNewES;
  mmioMediaSpecificInfo_p   pMediaInfo;
  mmioChannelSpecificInfo_p pChannelInfo;
  mmioESSpecificInfo_p      pESInfo;

  /* Check Params*/
  if ((!pInstance) || (!pchNeededOutputFormat) || (!pNode) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check the needed output format: */
  if (strcmp(pchNeededOutputFormat, "es_a_MPA"))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We'll create a channel with channel info, and an ES with ES info: */
  pNewCh = MMIOPsCreateAndLinkNewNodeStruct(pNode);
  if (!pNewCh)
  {
    /* Could not create new node! */
    return MMIO_ERROR_OUT_OF_MEMORY;
  }
  pChannelInfo = MMIOmalloc(sizeof(mmioChannelSpecificInfo_t));
  if (!pChannelInfo)
  {
    /* Out of memory */
    MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  pNewES = MMIOPsCreateAndLinkNewNodeStruct(pNewCh);
  if (!pNewES)
  {
    /* Could not create new node! */
    MMIOfree(pChannelInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }
  pESInfo = MMIOmalloc(sizeof(mmioESSpecificInfo_t));
  if (!pESInfo)
  {
    /* Out of memory */
    MMIOPsUnlinkAndDestroyNodeStruct(pNewES);
    MMIOfree(pChannelInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Ok, fill the new nodes with information */

  /* First the Channel node */
  strncpy(pNewCh->achNodeOwnerOutputFormat, "ch_MPA", sizeof(pNewCh->achNodeOwnerOutputFormat));
  pNewCh->bUnlinkPoint = 1;
  pNewCh->iNodeType = MMIO_NODETYPE_CHANNEL;
  pNewCh->pTypeSpecificInfo = pChannelInfo;
  pChannelInfo->pChannelID = NULL;
  strncpy(pChannelInfo->achDescriptionText, "Unknown Artist - Unknown song", sizeof(pChannelInfo->achDescriptionText));
  pChannelInfo->mmiochannel_SendMsg = mp3demux_ch_SendMsg;

  /* Then the ES node */
  strncpy(pNewES->achNodeOwnerOutputFormat, pchNeededOutputFormat, sizeof(pNewES->achNodeOwnerOutputFormat));
  pNewES->bUnlinkPoint = 0;
  pNewES->iNodeType = MMIO_NODETYPE_ELEMENTARYSTREAM;
  pNewES->pTypeSpecificInfo = pESInfo;
  pESInfo->pESID = NULL;
  strncpy(pESInfo->achDescriptionText, "Unknown Artist - Unknown song", sizeof(pESInfo->achDescriptionText));
  pESInfo->mmioes_SendMsg = mp3demux_es_SendMsg;
  pESInfo->mmioes_SetDirection = mp3demux_SetDirection;
  pESInfo->mmioes_SetPosition = mp3demux_SetPosition;
  pESInfo->mmioes_GetOneFrame = mp3demux_GetOneFrame;
  pESInfo->mmioes_GetStreamLength = mp3demux_GetStreamLength;
  pESInfo->mmioes_DropFrames = mp3demux_DropFrames;
  pESInfo->mmioes_GetTimeBase = mp3demux_GetTimeBase;

  /* Let's see what are our capabilities! */
  /* If the media can seek and tell, then we can seek too, otherwise we can play */
  /* only forward! */
  pMediaInfo = pNode->pTypeSpecificInfo;
  if ((pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_SEEK) &&
      (pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_TELL))
    pPluginInstance->bFullControlAvailable = 1;
  else
    pPluginInstance->bFullControlAvailable = 0;

  if (pPluginInstance->bFullControlAvailable)
    pESInfo->iESCapabilities =
      MMIO_ES_CAPS_DIRECTION_CUSTOMREVERSE | /* simply reading frames backward */
      MMIO_ES_CAPS_DIRECTION_REVERSE |       /* detto */
      MMIO_ES_CAPS_DIRECTION_STOP |
      MMIO_ES_CAPS_DIRECTION_PLAY |
      MMIO_ES_CAPS_DIRECTION_CUSTOMPLAY |    /* reading frames forward */
      MMIO_ES_CAPS_SETPOSITION |
      MMIO_ES_CAPS_DROPFRAMES |
      MMIO_ES_CAPS_PTS;
  else
    pESInfo->iESCapabilities =
      MMIO_ES_CAPS_DIRECTION_STOP |
      MMIO_ES_CAPS_DIRECTION_PLAY |
      MMIO_ES_CAPS_DIRECTION_CUSTOMPLAY |
      MMIO_ES_CAPS_PTS;

  pESInfo->iStreamType = MMIO_STREAMTYPE_AUDIO;
  /* No problem what stream we give here, as everything depends on what the */
  /* mpeg audio decoder produces. So, we give here something. */
  pESInfo->StreamInfo.AudioStruct.iBits = 16;
  pESInfo->StreamInfo.AudioStruct.iChannels = 2;
  pESInfo->StreamInfo.AudioStruct.iSampleRate = 44100;
  pESInfo->StreamInfo.AudioStruct.iIsSigned = 1;

  /* Save handle to media handler, so we can read stream :) */
  pPluginInstance->pMedia = pNode;

  /* If we can seek all the way around, then */
  /* we should create the index table of the stream! */
  if (pPluginInstance->bFullControlAvailable)
  {
    if (!mp3demux_internal_CreateIndexTable(pPluginInstance))
    {
      printf("[mp3demux_Link] : Could not create index table,\n");
      printf("                  switching to streaming mode!\n");
      pPluginInstance->bFullControlAvailable = 0;
    }
  }

  /* Fill stream description from ID3 TAG if available */
  if (pPluginInstance->bTAGAvailable)
  {
    snprintf(pESInfo->achDescriptionText, sizeof(pESInfo->achDescriptionText),
             "%s - %s", pPluginInstance->TAG.achArtist, pPluginInstance->TAG.achTitle);
    snprintf(pChannelInfo->achDescriptionText, sizeof(pChannelInfo->achDescriptionText),
             "%s - %s", pPluginInstance->TAG.achArtist, pPluginInstance->TAG.achTitle);
  }

  if (pPluginInstance->pIndexTable)
  {
    char *pchChannelMode;
    switch (pPluginInstance->iChannelMode)
    {
      case 0:
        pchChannelMode = "Stereo";
        break;
      case 1:
        pchChannelMode = "Joint-Stereo";
        break;
      case 2:
        pchChannelMode = "Dual Channel";
        break;
      case 3:
        pchChannelMode = "Single Channel";
        break;
      default:
        pchChannelMode = "Unknown-Channel-Mode";
        break;
    }
    snprintf(pESInfo->achDescriptiveESFormat, sizeof(pESInfo->achDescriptiveESFormat),
             "MPEG-%d.%d Layer%d %dKbps %s",
             pPluginInstance->iMPEGAudioVersion/10,
             pPluginInstance->iMPEGAudioVersion%10,
             pPluginInstance->iLayer,
             pPluginInstance->iBitRate/1000,
             pchChannelMode
            );
  } else
  {
    snprintf(pESInfo->achDescriptiveESFormat, sizeof(pESInfo->achDescriptiveESFormat),
             "MPEG Audio");
  }
  return MMIO_NOERROR;
}

static void mp3demux_internal_DestroyTree(mmioProcessTreeNode_p pRoot)
{
  if (!pRoot) return;

  if (pRoot->pFirstChild)
    mp3demux_internal_DestroyTree(pRoot->pFirstChild);

  if (pRoot->pNextBrother)
    mp3demux_internal_DestroyTree(pRoot->pNextBrother);

  MMIOfree(pRoot->pTypeSpecificInfo);
  MMIOPsUnlinkAndDestroyNodeStruct(pRoot);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL mp3demux_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mp3demuxInstance_p pPluginInstance = pInstance;
  mmioResult_t iResult = MMIO_NOERROR;

  if ((!pInstance) || (!pNode))
    iResult = MMIO_ERROR_INVALID_PARAMETER;
  else
  {
    /* Free resources allocated at mp3demux_Link: */

    if (pPluginInstance->bFullControlAvailable)
      mp3demux_internal_DestroyIndexTable(pPluginInstance);

    /* Destroy all the levels we've created */
    mp3demux_internal_DestroyTree(pNode);
  }
  return iResult;
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
    *ppchInternalName = "mp3demux";
    *ppchSupportedFormats = "cont_MP2;cont_MP3;cont_MPA"; /* MPEG Audio containers */
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
