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
#include "tpl.h"

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"

#include "gbm.h"


#define GBMDEC_EMULATED_FPS  15

typedef struct mmioPluginInstance_s
{
  int                      iPlaybackDirection;
  int                      iCurrImageNum;
  int                      iNumImages;
  int                      bDiscontinuity;

  mmioMediaSpecificInfo_p  pSource;
  void                    *pSourceInstance;

  mmioURLSpecificInfo_p    pURLInfo;

  int                      iGuessedFormat;
  int                      iImageWidth;
  int                      iImageHeight;
  unsigned char           *pLastDecodedFrame;
  GBMFT                    gbmDescriptiveFormat;

} mmioPluginInstance_t, *mmioPluginInstance_p;


MMIOPLUGINEXPORT long         MMIOCALL gbmdec_rs_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  /* Message handler for channels, elementary streams and raw streams */
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT long         MMIOCALL gbmdec_es_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return gbmdec_rs_SendMsg(pInstance, lCommandWord, pParam1, pParam2);
}

MMIOPLUGINEXPORT long         MMIOCALL gbmdec_ch_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return gbmdec_rs_SendMsg(pInstance, lCommandWord, pParam1, pParam2);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_SetDirection(void *pInstance, void *pRSID, int iDirection)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We do support every direction value! */
  pPluginInstance->iPlaybackDirection = iDirection;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_ReleaseForeignBuffers(void *pInstance, void *pRSID)
{
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_GetOneFrame(void *pInstance, void *pRSID, mmioDataDesc_p pDataDesc, void **ppDataBuf, long long llDataBufSize, mmioRSFormatRequest_p pRequestedFormat)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  int iImageNumToDecode;
  char achOptions[32];
  int iStride;
  unsigned char *pchDecodedData;
  mmioRSFormatRequest_t rfRequestedFormat;
  int iXPos, iYPos;
  int iXStartOffset, iYStartOffset;
  unsigned char *pchDest;
  unsigned char *pchDestR;
  unsigned char *pchDestG;
  unsigned char *pchDestB;
  unsigned char *pchDestA;
  unsigned char *pchSource;
  GBM gbmImageInfo;
  GBMRGB gbmRgb[0x100];

  if ((!pInstance) || (!pDataDesc) || (!ppDataBuf) || (!*ppDataBuf))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check if the output buffer is big enough */
  if (llDataBufSize<(pPluginInstance->iImageWidth * pPluginInstance->iImageHeight * 4))
  {
    /* Output buffer too small! */
    return MMIO_ERROR_BUFFER_TOO_SMALL;
  }

  /* Do some preparatinos for the first frame */
  if (pPluginInstance->pLastDecodedFrame == NULL)
  {
    pPluginInstance->pLastDecodedFrame = MMIOmalloc(pPluginInstance->iImageWidth * pPluginInstance->iImageHeight * 4);
    if (!pPluginInstance->pLastDecodedFrame)
      return MMIO_ERROR_OUT_OF_MEMORY;
    memset(pPluginInstance->pLastDecodedFrame, 0, pPluginInstance->iImageWidth * pPluginInstance->iImageHeight * 4);
  }

  /* Decode the current image, if possible */
  /* Get the image number we have to decode and prepare the next number */
  iImageNumToDecode = pPluginInstance->iCurrImageNum;
  if (pPluginInstance->iPlaybackDirection<0)
  {
    if (iImageNumToDecode<0)
    {
      /* Reached first frame (backwards) */
      return MMIO_ERROR_OUT_OF_DATA;
    }
  } else
  {
    if (iImageNumToDecode>=pPluginInstance->iNumImages)
    {
      /* Reached last frame (forward) */
      return MMIO_ERROR_OUT_OF_DATA;
    }

    if (iImageNumToDecode<0)
      iImageNumToDecode = 0;
  }

  /* Do the decoding */

  /* Read the image header first */
  snprintf(achOptions, sizeof(achOptions),
           "ext_bpp,index=%d", iImageNumToDecode);
  if (gbm_read_header(pPluginInstance->pURLInfo->achURL,
                      (int) pPluginInstance,
                      pPluginInstance->iGuessedFormat,
                      &gbmImageInfo,
                      achOptions) != GBM_ERR_OK)
  {
    /* Could not read the required header */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Read the palette next */
  memset(gbmRgb, 0, sizeof(gbmRgb));
  if (gbm_read_palette((int) pPluginInstance,
                       pPluginInstance->iGuessedFormat,
                       &gbmImageInfo,
                       gbmRgb) != GBM_ERR_OK)
  {
    /* Could not read the palette information */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Create the requested format structure */
  if (pRequestedFormat)
    memcpy(&rfRequestedFormat, pRequestedFormat, sizeof(rfRequestedFormat));
  else
  {
    /* Red component */
    rfRequestedFormat.VideoStruct.aiFieldStartOffset[0] = 0;
    rfRequestedFormat.VideoStruct.aiFieldNextPixel[0] = 4;
    rfRequestedFormat.VideoStruct.aiFieldPitch[0] = pPluginInstance->iImageWidth * 4;
    /* Green component */
    rfRequestedFormat.VideoStruct.aiFieldStartOffset[1] = 1;
    rfRequestedFormat.VideoStruct.aiFieldNextPixel[1] = 4;
    rfRequestedFormat.VideoStruct.aiFieldPitch[1] = pPluginInstance->iImageWidth * 4;
    /* Blue component */
    rfRequestedFormat.VideoStruct.aiFieldStartOffset[2] = 2;
    rfRequestedFormat.VideoStruct.aiFieldNextPixel[2] = 4;
    rfRequestedFormat.VideoStruct.aiFieldPitch[2] = pPluginInstance->iImageWidth * 4;
    /* Alpha component */
    rfRequestedFormat.VideoStruct.aiFieldStartOffset[3] = 3;
    rfRequestedFormat.VideoStruct.aiFieldNextPixel[3] = 4;
    rfRequestedFormat.VideoStruct.aiFieldPitch[3] = pPluginInstance->iImageWidth * 4;
  }

  /* Calculate needed memory and allocate memory for the decoded data */
  iStride = (((gbmImageInfo.w * gbmImageInfo.bpp) + 31) / 32) * 4;
  pchDecodedData = MMIOmalloc(iStride * gbmImageInfo.h);
  if (!pchDecodedData)
  {
    /* Out of memory! */
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Read the image data then */
  if (gbm_read_data((int) pPluginInstance,
                    pPluginInstance->iGuessedFormat,
                    &gbmImageInfo,
                    pchDecodedData) != GBM_ERR_OK)
  {
    /* Could not decode image */
    MMIOfree(pchDecodedData);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Calculate X and Y Start Offsets */
  iXStartOffset = (pPluginInstance->iImageWidth - gbmImageInfo.w)/2;
  iYStartOffset = (pPluginInstance->iImageHeight - gbmImageInfo.h)/2;
  /*
  fprintf(stderr, "Decoded: %d x %d, FullSize: %d x %d, Offsets: %d x %d, BPP: %d        \n",
          gbmImageInfo.w, gbmImageInfo.h,
          pPluginInstance->iImageWidth,
          pPluginInstance->iImageHeight,
          iXStartOffset, iYStartOffset,
          gbmImageInfo.bpp);
  */

  /* Convert the image data to the requested format */
  switch (gbmImageInfo.bpp)
  {
    case 1:
        /* 1 BPP (Palettized) */
        {
          int iColorIndex;

          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              iColorIndex = (pchSource[iXPos/8] >> (8 - (iXPos % 8))) & 0x01;

              *(pchDest++) = gbmRgb[iColorIndex].r;
              *(pchDest++) = gbmRgb[iColorIndex].g;
              *(pchDest++) = gbmRgb[iColorIndex].b;
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 2:
        /* 2 BPP (Palettized) */
        {
          int iColorIndex;

          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              iColorIndex = (pchSource[iXPos/4] >> (6 - (iXPos % 4)*2)) & 0x03;

              *(pchDest++) = gbmRgb[iColorIndex].r;
              *(pchDest++) = gbmRgb[iColorIndex].g;
              *(pchDest++) = gbmRgb[iColorIndex].b;
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 4:
        /* 4 BPP (Palettized) */
        {
          int iColorIndex;

          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              iColorIndex = (pchSource[iXPos/2] >> (4 - (iXPos % 2)*4)) & 0x0f;

              *(pchDest++) = gbmRgb[iColorIndex].r;
              *(pchDest++) = gbmRgb[iColorIndex].g;
              *(pchDest++) = gbmRgb[iColorIndex].b;
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 8:
        /* 8 BPP (Palettized) */
        {
          int iColorIndex;

          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              iColorIndex = pchSource[iXPos];

              *(pchDest++) = gbmRgb[iColorIndex].r;
              *(pchDest++) = gbmRgb[iColorIndex].g;
              *(pchDest++) = gbmRgb[iColorIndex].b;
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 24:
        /* 24 BPP (BGR) */
        {
          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              *(pchDest++) = pchSource[iXPos*3 + 2];
              *(pchDest++) = pchSource[iXPos*3 + 1];
              *(pchDest++) = pchSource[iXPos*3 + 0];
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 32:
        /* 32 BPP (BGRA) */
        {
          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              *(pchDest++) = pchSource[iXPos*4 + 3];
              *(pchDest++) = pchSource[iXPos*4 + 2];
              *(pchDest++) = pchSource[iXPos*4 + 1];
              *(pchDest++) = pchSource[iXPos*4 + 0];
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 48:
        /* 48 BPP (BGR, 2 bytes per pixel) */
        {
          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              unsigned short *pusSource = (unsigned short *) pchSource;
              *(pchDest++) = pusSource[iXPos*3 + 2] / 256;
              *(pchDest++) = pusSource[iXPos*3 + 1] / 256;
              *(pchDest++) = pusSource[iXPos*3 + 0] / 256;
              *(pchDest++) = 0;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    case 64:
        /* 64 BPP (BGRA, 2 bytes per pixel) */
        {
          /* Decoded image is upside down, so last line is the top. */
          pchSource = pchDecodedData + (iStride * (gbmImageInfo.h-1));
          /* Process all lines */
          for (iYPos = 0; iYPos < gbmImageInfo.h; iYPos++)
          {
            /* Set destination pointer */
            pchDest =
              pPluginInstance->pLastDecodedFrame +
              ((iYStartOffset+iYPos) * pPluginInstance->iImageWidth + iXStartOffset) * 4;
            /* Go through all pixels of one line */
            for (iXPos = 0; iXPos < gbmImageInfo.w; iXPos++)
            {
              unsigned short *pusSource = (unsigned short *) pchSource;
              *(pchDest++) = pusSource[iXPos*4 + 3] / 256;
              *(pchDest++) = pusSource[iXPos*4 + 2] / 256;
              *(pchDest++) = pusSource[iXPos*4 + 1] / 256;
              *(pchDest++) = pusSource[iXPos*4 + 0] / 256;
            }
            /* Go to next line in source */
            pchSource -= iStride;
          }
        }
        break;
    default:
        /* Unknown or unsupported bits per pixel value */
        MMIOfree(pchDecodedData);
        return MMIO_ERROR_UNKNOWN;
  }

  /* Free the allocated memory */
  MMIOfree(pchDecodedData);

  /* Now that we have the full decoded image (containing the previous images too, if needed) */
  /* we should move the pixels into the destination buffer, sorted to match that format. */
  pchSource = pPluginInstance->pLastDecodedFrame;
  for (iYPos = 0; iYPos < pPluginInstance->iImageHeight; iYPos++)
  {
    pchDestR =
      ((unsigned char *) (*ppDataBuf)) + rfRequestedFormat.VideoStruct.aiFieldStartOffset[0] +
      (iYPos * rfRequestedFormat.VideoStruct.aiFieldPitch[0]);
    pchDestG =
      ((unsigned char *) (*ppDataBuf)) + rfRequestedFormat.VideoStruct.aiFieldStartOffset[1] +
      (iYPos * rfRequestedFormat.VideoStruct.aiFieldPitch[1]);
    pchDestB =
      ((unsigned char *) (*ppDataBuf)) + rfRequestedFormat.VideoStruct.aiFieldStartOffset[2] +
      (iYPos * rfRequestedFormat.VideoStruct.aiFieldPitch[2]);
    pchDestA =
      ((unsigned char *) (*ppDataBuf)) + rfRequestedFormat.VideoStruct.aiFieldStartOffset[3] +
      (iYPos * rfRequestedFormat.VideoStruct.aiFieldPitch[3]);

    for (iXPos = 0; iXPos < pPluginInstance->iImageWidth; iXPos++)
    {
      *pchDestR = *(pchSource++);
      *pchDestG = *(pchSource++);
      *pchDestB = *(pchSource++);
      *pchDestA = *(pchSource++);

      pchDestR += rfRequestedFormat.VideoStruct.aiFieldNextPixel[0];
      pchDestG += rfRequestedFormat.VideoStruct.aiFieldNextPixel[1];
      pchDestB += rfRequestedFormat.VideoStruct.aiFieldNextPixel[2];
      pchDestA += rfRequestedFormat.VideoStruct.aiFieldNextPixel[3];
    }
  }

  /* Update the current image number, if the decoding succeeded */
  if (pPluginInstance->iPlaybackDirection<0)
    pPluginInstance->iCurrImageNum--;
  else
    pPluginInstance->iCurrImageNum = iImageNumToDecode+1;

  /* Fill the pDataDesc structure */
  if (pDataDesc)
  {
    pDataDesc->llPTS = iImageNumToDecode * 1000LL / GBMDEC_EMULATED_FPS;
    pDataDesc->llDataSize = pPluginInstance->iImageWidth * pPluginInstance->iImageHeight * 4;
    pDataDesc->StreamInfo.VideoStruct.iFPSCount = GBMDEC_EMULATED_FPS;
    pDataDesc->StreamInfo.VideoStruct.iFPSDenom = 1;
    pDataDesc->StreamInfo.VideoStruct.iWidth = pPluginInstance->iImageWidth;
    pDataDesc->StreamInfo.VideoStruct.iHeight = pPluginInstance->iImageHeight;
    pDataDesc->StreamInfo.VideoStruct.iPixelAspectRatioCount = 1;
    pDataDesc->StreamInfo.VideoStruct.iPixelAspectRatioDenom = 1;
    if (pPluginInstance->bDiscontinuity)
    {
      pDataDesc->iExtraStreamInfo = MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY;
      pPluginInstance->bDiscontinuity = 0;
    }
    else
      pDataDesc->iExtraStreamInfo = 0;
  }

  /* Return with success */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_GetStreamLength(void *pInstance, void *pRSID, long long *pllLength)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if ((!pInstance) || (!pllLength))
    return MMIO_ERROR_INVALID_PARAMETER;

  *pllLength = 1000LL * pPluginInstance->iNumImages / GBMDEC_EMULATED_FPS;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_GetTimeBase(void *pInstance, void *pRSID, long long *pllFirstTimeStamp)
{
  if (!pllFirstTimeStamp)
    return MMIO_ERROR_INVALID_PARAMETER;

  *pllFirstTimeStamp = 0;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_SetPosition(void *pInstance, void *pRSID, long long llPos, int iPosType, long long *pllPosFound)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if ((!pInstance) || (!pllPosFound))
    return MMIO_ERROR_INVALID_PARAMETER;

  if ((iPosType != MMIO_POSTYPE_TIME) && (iPosType != MMIO_POSTYPE_BYTE))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (iPosType == MMIO_POSTYPE_TIME)
  {
    pPluginInstance->iCurrImageNum = llPos * GBMDEC_EMULATED_FPS / 1000LL;
  }

  if (iPosType == MMIO_POSTYPE_BYTE)
  {
    pPluginInstance->iCurrImageNum =
      llPos /
      (4LL * pPluginInstance->iImageWidth * pPluginInstance->iImageHeight); /* 4 is for 4 bytes per pixel (RGBA) */
  }

  if (pPluginInstance->iCurrImageNum < 0)
    pPluginInstance->iCurrImageNum = 0;
  if (pPluginInstance->iCurrImageNum >= pPluginInstance->iNumImages)
    pPluginInstance->iCurrImageNum = pPluginInstance->iNumImages-1;

  /* Return with the result */
  if (iPosType == MMIO_POSTYPE_TIME)
  {
    *pllPosFound =
      1000LL * pPluginInstance->iCurrImageNum / GBMDEC_EMULATED_FPS;
  } else
  {
    *pllPosFound =
      pPluginInstance->iCurrImageNum *
      (4LL * pPluginInstance->iImageWidth * pPluginInstance->iImageHeight); /* 4 is for 4 bytes per pixel (RGBA) */
  }

  pPluginInstance->bDiscontinuity = 1;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_DropFrames(void *pInstance, void *pRSID, long long llAmount, int iPosType, long long *pllDropped)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  long long llFrameAmount;
  long long llOldPos, llNewPos;

  if ((iPosType != MMIO_POSTYPE_TIME) && (iPosType != MMIO_POSTYPE_BYTE))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (iPosType == MMIO_POSTYPE_TIME)
  {
    llFrameAmount = llAmount * GBMDEC_EMULATED_FPS / 1000LL;
    llOldPos = 1000LL * pPluginInstance->iCurrImageNum / GBMDEC_EMULATED_FPS;
  }

  if (iPosType == MMIO_POSTYPE_BYTE)
  {
    llFrameAmount =
      llAmount /
      (4LL * pPluginInstance->iImageWidth * pPluginInstance->iImageHeight); /* 4 is for 4 bytes per pixel (RGBA) */
    llOldPos =
      pPluginInstance->iCurrImageNum *
      (4LL * pPluginInstance->iImageWidth * pPluginInstance->iImageHeight); /* 4 is for 4 bytes per pixel (RGBA) */
  }

  if (pPluginInstance->iPlaybackDirection<0)
  {
    /* Playing backwards */
    if (pPluginInstance->iCurrImageNum<llFrameAmount)
      pPluginInstance->iCurrImageNum = 0;
    else
      pPluginInstance->iCurrImageNum -= llFrameAmount;
  } else
  {
    /* Playing forward */
    pPluginInstance->iCurrImageNum += llFrameAmount;
  }

  /* Return with the result */
  if (iPosType == MMIO_POSTYPE_TIME)
  {
    llNewPos = 1000LL * pPluginInstance->iCurrImageNum / GBMDEC_EMULATED_FPS;
  } else
  {
    llNewPos =
      pPluginInstance->iCurrImageNum *
      (4LL * pPluginInstance->iImageWidth * pPluginInstance->iImageHeight); /* 4 is for 4 bytes per pixel (RGBA) */
  }

  *pllDropped = llNewPos - llOldPos;

  pPluginInstance->bDiscontinuity = 1;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  snprintf(pchDescBuffer, iDescBufferSize,
           "Image decoder plugin v1.0 for MMIO (using GBM v%d.%d)", gbm_version()/100, gbm_version()%100 );
  return MMIO_NOERROR;
}

static int  GBMENTRY gbmdec_io_open(const char *fn, int mode)
{
  /* No support for opening extra files */
  return 0;
}

static int  GBMENTRY gbmdec_io_create(const char *fn, int mode)
{
  /* No support for creation of extra files */
  return 0;
}

static void GBMENTRY gbmdec_io_close(int fd)
{
  /* Empty function */
}

static long GBMENTRY gbmdec_io_lseek(int fd, long pos, int whence)
{
  if (fd)
  {
    mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) fd;
    int mmioWhence;
    long lResult;

    switch (whence)
    {
      case GBM_SEEK_CUR:
          mmioWhence = MMIO_MEDIA_SEEK_CUR;
          break;
      case GBM_SEEK_SET:
          mmioWhence = MMIO_MEDIA_SEEK_SET;
          break;
      case GBM_SEEK_END:
          mmioWhence = MMIO_MEDIA_SEEK_END;
          break;
      default:
          return -1; /* Error */
    }

    if (pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance,
                                                 pos, mmioWhence) != MMIO_NOERROR)
      lResult = -1;
    else
      lResult = pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance);

    return lResult;
  } else
  {
    /* Return an error */
    return -1;
  }
}

static int  GBMENTRY gbmdec_io_read(int fd, void *buf, int len)
{
  if (fd)
  {
    mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) fd;

    return (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                     buf, len) == MMIO_NOERROR) ? len : 0;
  } else
  {
    /* Return an error */
    return 0;
  }
}

static int  GBMENTRY gbmdec_io_write(int fd, const void *buf, int len)
{
  /* Write is not supported */
  return 0;
}


MMIOPLUGINEXPORT void *       MMIOCALL gbmdec_Initialize()
{
  mmioPluginInstance_p pPluginInstance = NULL;

  pPluginInstance = (mmioPluginInstance_p) MMIOmalloc(sizeof(mmioPluginInstance_t));
  if (pPluginInstance == NULL)
    return NULL;

  // Initialize GBM
  gbm_init();
  gbm_io_setup(gbmdec_io_open,
               gbmdec_io_create,
               gbmdec_io_close,
               gbmdec_io_lseek,
               gbmdec_io_read,
               gbmdec_io_write);

  // Initialize fields of instance variables
  memset(pPluginInstance, 0, sizeof(mmioPluginInstance_t));

  return pPluginInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_Uninitialize(void *pInstance)
{
  mmioPluginInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free all our resources! */
  if (pPluginInstance->pLastDecodedFrame)
    MMIOfree(pPluginInstance->pLastDecodedFrame);

  MMIOfree(pPluginInstance);

  gbm_deinit();

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  mmioMediaSpecificInfo_p pMediaInfo;
  mmioProcessTreeNode_p pURLNode;
  int iImageNum;

  if ((!pInstance) || (!pNode) || (!ppExamineResult) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We should examine the file here, to know what we can produce from it. */
  /* For the examination of image files, we need a media which can read, seek and tell. */
  pMediaInfo = pNode->pTypeSpecificInfo;
  if ((!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_READ)) ||
      (!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_SEEK)) ||
      (!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_TELL)))
  {
    printf("[gbmdec_Examine] : Media cannot be read!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  /* So, let's try to parse the image file! */
  pPluginInstance->pSourceInstance = pNode->pNodeOwnerPluginInstance;
  pPluginInstance->pSource = pMediaInfo;

  /* Find the URL node to get the "filename" so we can guess the filetype from there */
  pURLNode = pNode;
  while ((pURLNode) && (pURLNode->iNodeType != MMIO_NODETYPE_URL))
    pURLNode = pURLNode->pParent;

  if (!pURLNode)
  {
    printf("[gbmdec_Examine] : Cannot find URL node!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }
  pPluginInstance->pURLInfo = pURLNode->pTypeSpecificInfo;

  if (gbm_guess_filetype(pPluginInstance->pURLInfo->achURL, &(pPluginInstance->iGuessedFormat)) != GBM_ERR_OK)
  {
    printf("[gbmdec_Examine] : Could not guess filetype!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }
  /* Get descriptive filetype information */
  gbm_query_filetype(pPluginInstance->iGuessedFormat, &(pPluginInstance->gbmDescriptiveFormat));

  /* Read the number of images */
  if (gbm_read_imgcount(pPluginInstance->pURLInfo->achURL,
                        (int) pPluginInstance,
                        pPluginInstance->iGuessedFormat,
                        &(pPluginInstance->iNumImages)) != GBM_ERR_OK)
  {
    printf("[gbmdec_Examine] : Could not read number of images from file!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  if (pPluginInstance->iNumImages < 1)
  {
    printf("[gbmdec_Examine] : Got invalid number of images from GBM (%d)!\n", pPluginInstance->iNumImages);
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  /* Read bitmap header to get a general image information (resolution, etc...)*/
  /* We have to go through all the images and look for the greatest one */
  pPluginInstance->iImageWidth = 0;
  pPluginInstance->iImageHeight = 0;
  for (iImageNum=0; iImageNum<pPluginInstance->iNumImages; iImageNum++)
  {
    char achOptions[32];
    GBM  gbmImageInfo;

    snprintf(achOptions, sizeof(achOptions),
             "index=%d", iImageNum);
    if (gbm_read_header(pPluginInstance->pURLInfo->achURL,
                        (int) pPluginInstance,
                        pPluginInstance->iGuessedFormat,
                        &gbmImageInfo,
                        achOptions) != GBM_ERR_OK)
    {
      printf("[gbmdec_Examine] : Could not read header!\n");
      return MMIO_ERROR_NOT_SUPPORTED;
    }
    if (pPluginInstance->iImageWidth < gbmImageInfo.w)
      pPluginInstance->iImageWidth = gbmImageInfo.w;
    if (pPluginInstance->iImageHeight < gbmImageInfo.h)
      pPluginInstance->iImageHeight = gbmImageInfo.h;
  }

  /* Create examine result */
  (*ppExamineResult) = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc((*ppExamineResult)->iNumOfEntries * sizeof(mmioFormatDesc_t));

  /* Assemble the format specified from stream types we found! */
  snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t),
           "rs_v_RGBA");

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = pInstance;
  mmioProcessTreeNode_p     pNewCh, pNewES, pNewRS;
  mmioChannelSpecificInfo_p pChannelInfo;
  mmioESSpecificInfo_p      pESInfo;
  mmioRSSpecificInfo_p      pRSInfo;

  /* Check Params*/
  if ((!pInstance) || (!pchNeededOutputFormat) || (!pNode) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Might check the needed output format, if it matches the one we've created before (in Examine()), */
  /* but we trust it for now. :) */

  /* We'll create one channel with channel info, one ES with ES info, and one RS with RS info: */
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

  /* Fill the Channel node with information */
  snprintf(pNewCh->achNodeOwnerOutputFormat, sizeof(pNewCh->achNodeOwnerOutputFormat),
           "ch_%s", pPluginInstance->gbmDescriptiveFormat.short_name);
  pNewCh->bUnlinkPoint = 1;
  pNewCh->iNodeType = MMIO_NODETYPE_CHANNEL;
  pNewCh->pTypeSpecificInfo = pChannelInfo;
  pChannelInfo->pChannelID = NULL;
  snprintf(pChannelInfo->achDescriptionText, sizeof(pChannelInfo->achDescriptionText),
           "Image: %s", pPluginInstance->gbmDescriptiveFormat.long_name);
  pChannelInfo->mmiochannel_SendMsg = gbmdec_ch_SendMsg;

  /* Create the ES node then! */
  pNewES = MMIOPsCreateAndLinkNewNodeStruct(pNewCh);
  pESInfo = MMIOmalloc(sizeof(mmioESSpecificInfo_t));
  if ((!pESInfo) || (!pNewES))
  {
    /* Out of memory */
    /* Could not create new node! */
    if (pESInfo)
      MMIOfree(pESInfo);
    if (pNewES)
      MMIOPsUnlinkAndDestroyNodeStruct(pNewES);

    MMIOfree(pChannelInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Ok, fill the new ES node with information */
  pNewES->bUnlinkPoint = 0;
  pNewES->iNodeType = MMIO_NODETYPE_ELEMENTARYSTREAM;
  pNewES->pTypeSpecificInfo = pESInfo;
  pESInfo->pESID = NULL;
  snprintf(pESInfo->achDescriptionText, sizeof(pESInfo->achDescriptionText),
           "Image: %s", pPluginInstance->gbmDescriptiveFormat.long_name);

  pESInfo->mmioes_SendMsg = gbmdec_es_SendMsg;
  pESInfo->mmioes_SetDirection = NULL;
  pESInfo->mmioes_SetPosition = NULL;
  pESInfo->mmioes_GetOneFrame = NULL;
  pESInfo->mmioes_GetStreamLength = NULL;
  pESInfo->mmioes_DropFrames = NULL;
  pESInfo->mmioes_GetTimeBase = NULL;

  /* Let's see what are our capabilities! */
  pESInfo->iESCapabilities =
    MMIO_ES_CAPS_DIRECTION_CUSTOMREVERSE | /* simply reading frames backward */
    MMIO_ES_CAPS_DIRECTION_REVERSE |       /* detto */
    MMIO_ES_CAPS_DIRECTION_STOP |
    MMIO_ES_CAPS_DIRECTION_PLAY |
    MMIO_ES_CAPS_DIRECTION_CUSTOMPLAY |    /* reading frames forward */
    MMIO_ES_CAPS_SETPOSITION |
    MMIO_ES_CAPS_DROPFRAMES |
    MMIO_ES_CAPS_PTS;

  pESInfo->iStreamType = MMIO_STREAMTYPE_VIDEO;
  pESInfo->StreamInfo.VideoStruct.iFPSCount = GBMDEC_EMULATED_FPS;
  pESInfo->StreamInfo.VideoStruct.iFPSDenom = 1;
  pESInfo->StreamInfo.VideoStruct.iWidth = pPluginInstance->iImageWidth;
  pESInfo->StreamInfo.VideoStruct.iHeight = pPluginInstance->iImageHeight;
  pESInfo->StreamInfo.VideoStruct.iPixelAspectRatioCount = 1;
  pESInfo->StreamInfo.VideoStruct.iPixelAspectRatioDenom = 1;
  snprintf(pESInfo->achDescriptiveESFormat, sizeof(pESInfo->achDescriptiveESFormat),
           "%dx%d %s (%s)",
           pESInfo->StreamInfo.VideoStruct.iWidth,
           pESInfo->StreamInfo.VideoStruct.iHeight,
           pPluginInstance->gbmDescriptiveFormat.short_name,
           pPluginInstance->gbmDescriptiveFormat.long_name);

  snprintf(pNewES->achNodeOwnerOutputFormat, sizeof(pNewES->achNodeOwnerOutputFormat),
           "es_v_%s",
           pPluginInstance->gbmDescriptiveFormat.short_name);

  /* Create the RS node then! */
  pNewRS = MMIOPsCreateAndLinkNewNodeStruct(pNewES);
  pRSInfo = MMIOmalloc(sizeof(mmioRSSpecificInfo_t));
  if ((!pRSInfo) || (!pNewRS))
  {
    /* Out of memory */
    /* Could not create new node! */
    if (pRSInfo)
      MMIOfree(pRSInfo);
    if (pNewRS)
      MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);

    MMIOfree(pESInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewES);
    MMIOfree(pChannelInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Ok, fill the new RS node with information */
  pNewRS->bUnlinkPoint = 0;
  pNewRS->iNodeType = MMIO_NODETYPE_RAWSTREAM;
  pNewRS->pTypeSpecificInfo = pRSInfo;
  pRSInfo->pRSID = NULL;
  snprintf(pRSInfo->achDescriptionText, sizeof(pRSInfo->achDescriptionText),
           "Image: %s", pPluginInstance->gbmDescriptiveFormat.long_name);

  pRSInfo->mmiors_SendMsg = gbmdec_rs_SendMsg;
  pRSInfo->mmiors_SetDirection = gbmdec_SetDirection;
  pRSInfo->mmiors_SetPosition = gbmdec_SetPosition;
  pRSInfo->mmiors_GetOneFrame = gbmdec_GetOneFrame;
  pRSInfo->mmiors_ReleaseForeignBuffers = gbmdec_ReleaseForeignBuffers;
  pRSInfo->mmiors_GetStreamLength = gbmdec_GetStreamLength;
  pRSInfo->mmiors_DropFrames = gbmdec_DropFrames;
  pRSInfo->mmiors_GetTimeBase = gbmdec_GetTimeBase;

  /* Let's see what are our capabilities! */
  pRSInfo->iRSCapabilities =
    MMIO_RS_CAPS_DIRECTION_CUSTOMREVERSE | /* simply reading frames backward */
    MMIO_RS_CAPS_DIRECTION_REVERSE |       /* detto */
    MMIO_RS_CAPS_DIRECTION_STOP |
    MMIO_RS_CAPS_DIRECTION_PLAY |
    MMIO_RS_CAPS_DIRECTION_CUSTOMPLAY |    /* reading frames forward */
    MMIO_RS_CAPS_SETPOSITION |
    MMIO_RS_CAPS_DROPFRAMES |
    MMIO_RS_CAPS_PTS;

  pRSInfo->iStreamType = MMIO_STREAMTYPE_VIDEO;
  pRSInfo->StreamInfo.VideoStruct.iFPSCount = GBMDEC_EMULATED_FPS;
  pRSInfo->StreamInfo.VideoStruct.iFPSDenom = 1;
  pRSInfo->StreamInfo.VideoStruct.iWidth = pPluginInstance->iImageWidth;
  pRSInfo->StreamInfo.VideoStruct.iHeight = pPluginInstance->iImageHeight;
  pRSInfo->StreamInfo.VideoStruct.iPixelAspectRatioCount = 1;
  pRSInfo->StreamInfo.VideoStruct.iPixelAspectRatioDenom = 1;
  snprintf(pRSInfo->achDescriptiveRSFormat, sizeof(pRSInfo->achDescriptiveRSFormat),
           "%dx%d RGBA (%s)",
           pRSInfo->StreamInfo.VideoStruct.iWidth,
           pRSInfo->StreamInfo.VideoStruct.iHeight,
           pPluginInstance->gbmDescriptiveFormat.long_name);

  snprintf(pNewRS->achNodeOwnerOutputFormat, sizeof(pNewRS->achNodeOwnerOutputFormat),
           "rs_v_RGBA");

  return MMIO_NOERROR;
}

static void gbmdec_internal_DestroyTree(mmioProcessTreeNode_p pRoot)
{
  if (!pRoot) return;

  if (pRoot->pFirstChild)
    gbmdec_internal_DestroyTree(pRoot->pFirstChild);

  if (pRoot->pNextBrother)
    gbmdec_internal_DestroyTree(pRoot->pNextBrother);

  MMIOfree(pRoot->pTypeSpecificInfo);
  MMIOPsUnlinkAndDestroyNodeStruct(pRoot);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL gbmdec_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mmioResult_t iResult = MMIO_NOERROR;

  if ((!pInstance) || (!pNode))
    iResult = MMIO_ERROR_INVALID_PARAMETER;
  else
  {
    /* Free resources allocated at gbmdec_Link: */
    /*   Nothing to do for this plugin here */

    /* Destroy all the levels we've created */
    gbmdec_internal_DestroyTree(pNode);
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
    *ppchInternalName = "gbmdec";
    *ppchSupportedFormats =
      "cont_BMP;cont_VGA;cont_BGA;cont_RLE;cont_DIB;cont_RL4;cont_RL8" /* OS/2 / Windows bitmap */
      ";cont_CVP" /* Portrait */
      ";cont_IMG;cont_XIMG" /* GEM Raster */
      ";cont_GIF" /* CompuServe Graphics Interchange Format */
      ";cont_IAX" /* IBM Image Access eXecutive */
      ";cont_JPG;cont_JPEG;cont_JPE" /* JPEG File Interchange Format */
      ";cont_KPS" /* IBM KIPS file format */
      ";cont_IFF;cont_LBM" /* Amiga IFF / ILBM Interleaved bitmap */
      ";cont_PCX;cont_PCC" /* ZSoft PC Paintbrush Image format */
      ";cont_PNG" /* Portable Network Graphics Format */
      ";cont_PBM" /* Portable Bit-map */
      ";cont_PGM" /* Portable Greyscale-map */
      ";cont_PPM" /* Portable Pixel-map */
      ";cont_PNM" /* Portable Any-map */
      ";cont_PSE;cont_PSEG;cont_PSEG38PP;cont_PSEG3820" /* IBM Printer Page Segment */
      ";cont_SPR;cont_SPRITE" /* Archimedes Sprite from RiscOS */
      ";cont_TGA;cont_VST;cont_AFI" /* Truevision Targa/Vista bitmap */
      ";cont_TIF;cont_TIFF" /* Tagged Image File Format support (TIFF 6.0) */
      ";cont_VID" /* YUV12C M-Motion Video Frame Buffer */
      ";cont_XBM" /* X Windows Bitmap */
      ;
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

