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
#include <malloc.h>
#include <ctype.h>

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"

#include "avcodec.h"     /* libavcodec interface */
#include "avformat.h"    /* libavcodec interface */
#include "allformats.h"
extern struct AVCodecTag codec_wav_tags[];
extern struct AVCodecTag codec_bmp_tags[];

static struct AVCodecTag *codec_all_tags[3] =
{
  codec_wav_tags, codec_bmp_tags, NULL
};

#define MPAVCOD_MAX_BUFFER_SIZE (1024*1024)

typedef struct avcdecoderInstance_s
{
  mmioProcessTreeNode_p     pES;         /* Pointer to Elementary Stream node */
  mmioESSpecificInfo_p      pESInfo;     /* And some shortcuts from pES */
  void                     *pESInstance;

  int                       iDirection;

  AVCodec                  *hCodec;               /* Handle of a codec */
  AVCodecContext           *hCodecContext;        /* Codec context */
  AVCodecParserContext     *hCodecParser;         /* Parser for the codec */

  long long                 llOutputBufferSize;   /* Required size of output buffer */
  unsigned char            *pInputBuffer;         /* We get elementary stream data into this buffer */
  long long                 llInputBufferPos;     /* Current parsing position of buffer */
  long long                 llInputBufferSize;    /* Size of input buffer */
  mmioDataDesc_t            BufDataDesc;          /* Data desc of buffer */
  long long                 llPTS;                /* Presentation Time Stamp */
  long long                 llBufPosPTSDiffRemainder;

  int                       bDidSeek;

} avcdecoderInstance_t, *avcdecoderInstance_p;

/* We'll use an usage counter for libavcodec, because it might be that this plugin will  */
/* be loaded and initialized twice in a process to handle two streams, but we don't want */
/* to initialize and uninitialize libavcodec twice as that would screw things up.        */
static unsigned int uiEngineUsageCounter=0;


MMIOPLUGINEXPORT long         MMIOCALL avcdecoder_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_SetDirection(void *pInstance, void *pESID, int iDirection)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_ReleaseForeignBuffers(void *pInstance, void *pRSID)
{
  /* We never keep incoming buffers, so we have nothing to release. */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_GetOneFrame(void *pInstance, void *pRSID, mmioDataDesc_p pDataDesc, void **ppDataBuf, long long llDataBufSize, mmioRSFormatRequest_p pRequestedFormat)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
  long rc;
//  long long llBufSizemsec;
  mmioRSSpecificInfo_p pRSInfo = (mmioRSSpecificInfo_p) pRSID;
  int iExtraStreamInfo;

  if ((!pInstance) || (!pDataDesc) || (!ppDataBuf) || (!(*ppDataBuf)) || (!pRSID))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pRequestedFormat)
    return MMIO_ERROR_REQUESTED_FORMAT_NOT_SUPPORTED;

  /* This is a libavcodec limitation that we can't give it the */
  /* output buffer size, but each of the codecs assume a given */
  /* minimum size... :(                                        */
  if (llDataBufSize<pPluginInstance->llOutputBufferSize)
    return MMIO_ERROR_BUFFER_TOO_SMALL;

  iExtraStreamInfo = MMIO_EXTRASTREAMINFO_NOTHING;
  if (pPluginInstance->bDidSeek)
  {
    /* We did seek in the stream. */
    /* Flush input buffer */
    pPluginInstance->llInputBufferPos = 0;
    pPluginInstance->BufDataDesc.llDataSize = 0;

    /* Reset decoder */
    /* TODO! */

    iExtraStreamInfo = MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY;
    pPluginInstance->bDidSeek = 0;
  }

  // Decode into buffer, set pDataDesc and things like that.
  // Get an encoded frame from the demuxer
  do {
    //printf("[avcdecoder_GetOneFrame] : What to do?\n"); fflush(stdout);
    // Parse in some data, if we have in the input buffer
    if ((pPluginInstance->llInputBufferSize > 0) &&
        (pPluginInstance->BufDataDesc.llDataSize > 0) &&
        (pPluginInstance->llInputBufferPos < pPluginInstance->BufDataDesc.llDataSize))
    {
      unsigned char *decode_pos = NULL;
      int decode_size = 0;
      int parsed_len = 0;
      long long llToParse = pPluginInstance->BufDataDesc.llDataSize - pPluginInstance->llInputBufferPos;

      //printf("[avcdecoder_GetOneFrame] : Parsing from %lld (size %lld)\n", pPluginInstance->llInputBufferPos, llToParse); fflush(stdout);

      parsed_len = av_parser_parse(pPluginInstance->hCodecParser, pPluginInstance->hCodecContext,
                                   &decode_pos, &decode_size,
                                   pPluginInstance->pInputBuffer + pPluginInstance->llInputBufferPos,
                                   llToParse,
                                   AV_NOPTS_VALUE, AV_NOPTS_VALUE);

      //fprintf(stderr, "Parse results: rc=%d, decode_pos=%p, decode_size=%d\n", parsed_len, decode_pos, decode_size);
      if (parsed_len < 0)
      {
        fprintf(stderr, "Error while parsing ( av_parser_parse() returned %d)\n", parsed_len);
        return MMIO_ERROR_UNKNOWN;
      }

      // Ok, some was parsed
      pPluginInstance->llInputBufferPos += parsed_len;

      // Let's see if a decodable stuff is now available!
      if ((decode_pos) && (decode_size))
      {
        //printf("[avcdecoder_GetOneFrame] : Could parse it, so decode it.\n"); fflush(stdout);
        /* Now that the frame is parsed, try to decode it */
        switch (pRSInfo->iStreamType)
        {
          case MMIO_STREAMTYPE_VIDEO:
              {
                int decoded_size = llDataBufSize;;
                int decode_len;
                int got_picture = 1;
                AVFrame *frame = avcodec_alloc_frame();

#if 0
                // Stack alignment stuff
                unsigned int localstackpointer=0xDEADBEEF;
                void *pStackAligner = NULL;
                _asm {
                  movzx eax, sp
                  mov localstackpointer, eax
                }

                //fprintf(stderr, "Stack pointer (pre) : 0x%x\n", localstackpointer);

                pStackAligner = alloca(localstackpointer % 0xFF);

                _asm {
                  movzx eax, sp
                  mov localstackpointer, eax
                }

                //fprintf(stderr, "Stack pointer (post): 0x%x\n", localstackpointer);
#endif

                //printf("Decode video (size %d)\n", decode_size); fflush(stdout);
                decode_len = avcodec_decode_video(pPluginInstance->hCodecContext, frame,
                                                  &got_picture,
                                                  decode_pos, decode_size);
                //fprintf(stderr, "decoded (decode_len %d, max is %d, got_picture is %d)\n", decode_len, AVCODEC_MAX_AUDIO_FRAME_SIZE, got_picture);
                if (decode_len < 0)
                {
                  fprintf(stderr, "Error while decoding\n");
                  av_free(frame);
                  return MMIO_ERROR_UNKNOWN;
                }
                
                if (!got_picture)
                {
                  //printf("Got no picture, so going another round.\n"); fflush(stdout);
                  av_free(frame);
                  continue;
                }
                //printf("Decoded video, ok\n"); fflush(stdout);
#if 0
                /* if a frame has been decoded, output it */
                memcpy(*ppDataBuf, frame->data);
#endif
                /* Make pDataDesc from current ptsdiff and decoded bytes */
                pDataDesc->llDataSize = decoded_size;
                pDataDesc->llPTS = pPluginInstance->BufDataDesc.llPTS;
                pDataDesc->StreamInfo.VideoStruct.iFPSCount = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iFPSCount;
                pDataDesc->StreamInfo.VideoStruct.iFPSDenom = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iFPSDenom;
                pDataDesc->StreamInfo.VideoStruct.iWidth = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iWidth;
                pDataDesc->StreamInfo.VideoStruct.iHeight = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iHeight;
                pDataDesc->StreamInfo.VideoStruct.iPixelAspectRatioCount = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iPixelAspectRatioCount;
                pDataDesc->StreamInfo.VideoStruct.iPixelAspectRatioDenom = pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iPixelAspectRatioDenom;
                pDataDesc->iExtraStreamInfo = iExtraStreamInfo;
                av_free(frame);
                //printf("[mpavcod_GetOneFrame]: decoded (%d / %d).\n", pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iFPSCount, pPluginInstance->BufDataDesc.StreamInfo.VideoStruct.iFPSDenom);
                return MMIO_NOERROR;
              }
          case MMIO_STREAMTYPE_AUDIO:
              {
                  int decoded_size = llDataBufSize;;
                  int decode_len;
                  decode_len = avcodec_decode_audio2(pPluginInstance->hCodecContext, (short *)(*ppDataBuf),
                                                     &decoded_size,
                                                     decode_pos, decode_size);
                  //fprintf(stderr, "decoded %d bytes (decode_len %d, max is %d)\n", decoded_size, decode_len, AVCODEC_MAX_AUDIO_FRAME_SIZE);
                  if (decode_len < 0)
                  {
                    fprintf(stderr, "Error while decoding\n");
                    exit(1);
                  }
                  if (decoded_size > 0)
                  {
                    /* if a frame has been decoded, output it */
      
                    /* Make pDataDesc from current ptsdiff and decoded bytes */
                    pDataDesc->llDataSize = decoded_size;
                    pDataDesc->llPTS = pPluginInstance->BufDataDesc.llPTS;
                    pDataDesc->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iSampleRate;
                    pDataDesc->StreamInfo.AudioStruct.iIsSigned = 1;
                    pDataDesc->StreamInfo.AudioStruct.iBits = 16;
                    pDataDesc->StreamInfo.AudioStruct.iChannels = pPluginInstance->BufDataDesc.StreamInfo.AudioStruct.iChannels;
                    pDataDesc->iExtraStreamInfo = iExtraStreamInfo;
          
                    //printf("[mpavcod_GetOneFrame]: decoded.\n");
                    return MMIO_NOERROR;
                  }
                  fprintf(stderr, "Reached the error condition!\n");
                  return MMIO_ERROR_UNKNOWN;
              }
          default:
              fprintf(stderr, "Unknown stream format!\n");
              return MMIO_ERROR_UNKNOWN;
        }
      } else
      {
        //fprintf(stderr, "Parsed it, but no decodable data.\n");
        pPluginInstance->llInputBufferPos = 0;
        pPluginInstance->BufDataDesc.llDataSize = 0;
        continue;
      }
    } else
    {
      //printf("[avcdecoder_GetOneFrame] : Need more data into inputbuffer (type %d, %p %p).\n", pRSInfo->iStreamType, pPluginInstance->pESInstance, pPluginInstance->pESInfo->pESID); fflush(stdout);
      // We need some more data into inputbuffer!
      pPluginInstance->llInputBufferPos = 0;
      pPluginInstance->BufDataDesc.llDataSize = 0;
      do {
        rc = pPluginInstance->pESInfo->mmioes_GetOneFrame(pPluginInstance->pESInstance,
                                                          pPluginInstance->pESInfo->pESID,
                                                          &(pPluginInstance->BufDataDesc),
                                                          pPluginInstance->pInputBuffer,
                                                          pPluginInstance->llInputBufferSize - FF_INPUT_BUFFER_PADDING_SIZE);

        if (rc == MMIO_ERROR_BUFFER_TOO_SMALL)
        {
          /* Try to increase the buffer! */
          if (pPluginInstance->llInputBufferSize<MPAVCOD_MAX_BUFFER_SIZE)
          {
            unsigned char *pNewBuf;

            pNewBuf = MMIOrealloc(pPluginInstance->pInputBuffer, pPluginInstance->llInputBufferSize+2*1024);
            if (!pNewBuf)
            {
              printf("[mpavcod_GetOneFrame] : Out of memory!\n");
              return MMIO_ERROR_OUT_OF_MEMORY;
            }

            /* Buffer size increased! We can retry with new size! */
            pPluginInstance->pInputBuffer = pNewBuf;
            pPluginInstance->llInputBufferSize += 2*1024;
            memset(pNewBuf, 0, pPluginInstance->llInputBufferSize);

          } else
          {
            printf("[mpavcod_GetOneFrame] : Out of memory (buffer over limit)!\n");
            return MMIO_ERROR_OUT_OF_MEMORY;
          }
        } else
        if (rc == MMIO_ERROR_OUT_OF_DATA)
        {
          printf("[mpavcod_GetOneFrame] : Out of data!\n");
          /* End of stream! */
          return MMIO_ERROR_OUT_OF_DATA;
        } else
        if (rc != MMIO_NOERROR)
        {
          /* Some other kind of error! */
          printf("[mpavcod_GetOneFrame] : Other error!\n");
          return MMIO_ERROR_UNKNOWN;
        }
      } while (rc==MMIO_ERROR_BUFFER_TOO_SMALL);
    }
  } while (1);

  /* Execution should never reach this, but who knows... */
  return MMIO_ERROR_UNKNOWN;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_GetStreamLength(void *pInstance, void *pRSID, long long *pllLength)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllLength))
     return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_GetStreamLength(pPluginInstance->pESInstance,
                                                        pPluginInstance->pESInfo->pESID,
                                                        pllLength);
  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_GetTimeBase(void *pInstance, void *pRSID, long long *pllFirstTimeStamp)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
  long rc;

  if ((!pInstance) || (!pllFirstTimeStamp))
     return MMIO_ERROR_INVALID_PARAMETER;

  rc = pPluginInstance->pESInfo->mmioes_GetTimeBase(pPluginInstance->pESInstance,
                                                    pPluginInstance->pESInfo->pESID,
                                                    pllFirstTimeStamp);
  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_SetPosition(void *pInstance, void *pRSID, long long llPos, int iPosType, long long *pllPosFound)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
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


MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_DropFrames(void *pInstance, void *pRSID, long long llAmount, int iPosType, long long *pllDropped)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  snprintf(pchDescBuffer, iDescBufferSize,
           "Multiple Formats Decoder v1.0 (using libavcodec %s)", AV_STRINGIFY(LIBAVCODEC_VERSION) );
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL avcdecoder_Initialize()
{
  //BOOL rc;
  avcdecoderInstance_p pPluginInstance = NULL;

  pPluginInstance = (avcdecoderInstance_p) MMIOmalloc(sizeof(avcdecoderInstance_t));
  if (pPluginInstance == NULL)
  {
    printf("[avcdecoder_Initialize] : Out of memory!\n");
    return NULL;
  }

  if (uiEngineUsageCounter==0)
  {
    /* Initialize libavcodec */
    avcodec_init();
    /* Register codecs */
    avcodec_register_all();
  }
  uiEngineUsageCounter++;

  memset(pPluginInstance, 0, sizeof(avcdecoderInstance_t));

  pPluginInstance->iDirection = MMIO_DIRECTION_STOP;
  pPluginInstance->pESInstance = NULL;
  pPluginInstance->pESInfo = NULL;

  pPluginInstance->hCodec = NULL;
  pPluginInstance->hCodecContext = NULL;
  pPluginInstance->hCodecParser = NULL;

  pPluginInstance->bDidSeek = 0;

  pPluginInstance->llInputBufferSize = 64*1024;
  pPluginInstance->pInputBuffer = MMIOmalloc(pPluginInstance->llInputBufferSize);
  pPluginInstance->llInputBufferPos = 0;

  if (!pPluginInstance->pInputBuffer)
    pPluginInstance->llInputBufferSize = 0;

  return pPluginInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_Uninitialize(void *pInstance)
{
  avcdecoderInstance_p pPluginInstance = pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free all our resources! */
  if (pPluginInstance->hCodecParser)
    av_parser_close(pPluginInstance->hCodecParser);
  if (pPluginInstance->hCodecContext)
  {
    avcodec_close(pPluginInstance->hCodecContext);
    av_free(pPluginInstance->hCodecContext);
  }
  if (pPluginInstance->pInputBuffer)
    MMIOfree(pPluginInstance->pInputBuffer);
  MMIOfree(pPluginInstance);

  /* Uninitialize libavcodec */
  uiEngineUsageCounter--;
  if (uiEngineUsageCounter==0)
  {
    /* Free all "statically" allocated libavcodec memory */
    av_free_static();
  }

  return MMIO_NOERROR;
}

static AVCodec * avcdecoder_GetAVCodecForFormat(char *pchNodeOutputFormat)
{
  unsigned int uiCodecID = CODEC_ID_NONE;
  unsigned int uiFormatTag = 0;
  char achFormat[32];
  int  iFormatPos;
  char *pchNodeFormat;
  AVCodec *pCodec = NULL;

  iFormatPos = 0;
  pchNodeFormat = pchNodeOutputFormat;
  while (pchNodeOutputFormat[iFormatPos])
  {
    if (pchNodeOutputFormat[iFormatPos] == '_')
      pchNodeFormat = pchNodeOutputFormat + iFormatPos + 1;

    iFormatPos++;
  }
  strncpy(achFormat, pchNodeFormat, sizeof(achFormat));
  for (iFormatPos=0; iFormatPos<sizeof(achFormat); iFormatPos++)
  {
    achFormat[iFormatPos] = toupper(achFormat[iFormatPos]);
    if (iFormatPos<4)
      uiFormatTag |= achFormat[iFormatPos] << (iFormatPos*8);
  }

//  uiFormatTag = MKTAG('D', 'X', '5', '0');

  printf("[avcdecoder_GetAVCodecForFormat] : Format is [%s], format tag is [%.4s]\n", achFormat, &uiFormatTag); fflush(stdout);

  /* Find a codec for this format */
  printf("[avcdecoder_GetAVCodecForFormat] : Searching in WAV+BMP tags...\n", achFormat); fflush(stdout);
  uiCodecID = av_codec_get_id((const struct AVCodecTag **) &codec_all_tags, uiFormatTag);

  if (uiCodecID != CODEC_ID_NONE)
  {
    printf("[avcdecoder_GetAVCodecForFormat] : Found codec ID [%d]\n", uiCodecID); fflush(stdout);
    /* Get the codec for this format */
    pCodec = avcodec_find_decoder(uiCodecID);
  }

  if (!pCodec)
  {
    /* Oops, we did not find any codecs that would support this format. */
    printf("[avcdecoder_GetAVCodecForFormat] : Could not find decoder for format [%s]\n", achFormat); fflush(stdout);
  } else
  {
    printf("[avcdecoder_GetAVCodecForFormat] : Will use codec [%s]\n", pCodec->name); fflush(stdout);
  }

  return pCodec;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  AVCodec *pCodec;

  if ((!pInstance) || (!pNode) || (!ppExamineResult) || (pNode->iNodeType!=MMIO_NODETYPE_ELEMENTARYSTREAM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We should examine the ES node here, to know what we can produce from it. */

  /* What we might really do here is to probe the stream, if it's really a decodable one. */
  /* We now leave it for a later version, we only check the format for now. */

  /* If all goes well, we have an elementary stream with format something like es_?_<fmt> */
  /* We try to get the last fmt string, make that lowercased, and check if ffmpeg can handle that. */
  pCodec = avcdecoder_GetAVCodecForFormat(pNode->achNodeOwnerOutputFormat);

  (*ppExamineResult) = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  if ((pCodec) &&
      (
       (pCodec->type == CODEC_TYPE_VIDEO) ||
       (pCodec->type == CODEC_TYPE_AUDIO)
      )
     )
  {
    (*ppExamineResult)->iNumOfEntries = 1;
    (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc((*ppExamineResult)->iNumOfEntries * sizeof(mmioFormatDesc_t));

    /* We produce PCM audio raw stream for audio formats, and
     * YUV420 video raw stream for video formats
     */
    switch (pCodec->type)
    {
      case CODEC_TYPE_VIDEO:
        snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t), "rs_v_YUV420");
        break;
      case CODEC_TYPE_AUDIO:
        snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t), "rs_a_PCM");
        break;
      default:
        /* Should never happen... */
        snprintf(&((*ppExamineResult)->pOutputFormats[0]), sizeof(mmioFormatDesc_t), "rs_u_Unknown");
        break;
    }

    printf("[avcdecoder_Examine] : Examine result is [%s]\n", ((*ppExamineResult)->pOutputFormats[0])); fflush(stdout);
  } else
  {
    (*ppExamineResult)->iNumOfEntries = 0;
    (*ppExamineResult)->pOutputFormats = NULL;
  }

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
{
  /* Check parameters */
  if ((!pInstance) ||
      (!pExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free resources allocated for examination result structure */
  if (pExamineResult->pOutputFormats)
    MMIOfree(pExamineResult->pOutputFormats);
  MMIOfree(pExamineResult);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
  mmioProcessTreeNode_p     pNewRS;
  mmioRSSpecificInfo_p      pRSInfo;
  mmioESSpecificInfo_p      pESInfo;
  AVCodec *pCodec;

  /* Check Params */
  if ((!pInstance) || (!pchNeededOutputFormat) || (!pNode) || (pNode->iNodeType!=MMIO_NODETYPE_ELEMENTARYSTREAM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Get pointer to ES info */
  pESInfo = (mmioESSpecificInfo_p) (pNode->pTypeSpecificInfo);

  /* CHeck again what kind of codec we can use for this node */
  pCodec = avcdecoder_GetAVCodecForFormat(pNode->achNodeOwnerOutputFormat);
  if (!pCodec)
  {
    /* We can not handle this node, so return an error! */
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  /* Now check if the needed output format matches the one we can do here! */
  switch (pCodec->type)
  {
    case CODEC_TYPE_VIDEO:
      if (strcmp(pchNeededOutputFormat, "rs_v_YUV420"))
        return MMIO_ERROR_NOT_SUPPORTED;
      break;
    case CODEC_TYPE_AUDIO:
      if (strcmp(pchNeededOutputFormat, "rs_a_PCM"))
        return MMIO_ERROR_NOT_SUPPORTED;
      break;
    default:
      /* Should never happen... */
      return MMIO_ERROR_NOT_SUPPORTED;
  }

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
  pRSInfo->pRSID = pRSInfo; // The ID is a pointer to the RSInfo structure itself
  strncpy(pRSInfo->achDescriptionText, pESInfo->achDescriptionText, sizeof(pRSInfo->achDescriptionText));
  pRSInfo->mmiors_SendMsg = avcdecoder_SendMsg;
  pRSInfo->mmiors_SetDirection = avcdecoder_SetDirection;
  pRSInfo->mmiors_SetPosition = avcdecoder_SetPosition;
  pRSInfo->mmiors_GetOneFrame = avcdecoder_GetOneFrame;
  pRSInfo->mmiors_ReleaseForeignBuffers = avcdecoder_ReleaseForeignBuffers;
  pRSInfo->mmiors_GetStreamLength = avcdecoder_GetStreamLength;
  pRSInfo->mmiors_DropFrames = avcdecoder_DropFrames;
  pRSInfo->mmiors_GetTimeBase = avcdecoder_GetTimeBase;

  /* Let's see what are our capabilities! */
  /* We support everything our parent (ES) supports, except backward playback! */
  pRSInfo->iRSCapabilities =
    pESInfo->iESCapabilities &
    ~(MMIO_ES_CAPS_DIRECTION_CUSTOMREVERSE | MMIO_ES_CAPS_DIRECTION_REVERSE);

  /* Fill the stream-type specific fields */

  switch (pCodec->type)
  {
    case CODEC_TYPE_VIDEO:
      pRSInfo->iStreamType = MMIO_STREAMTYPE_VIDEO;
    
      /* Our output will probably be this: */
      memcpy(&(pRSInfo->StreamInfo), &(pESInfo->StreamInfo), sizeof(mmioStreamInfo_t));

      /* Avoid division by zero */
      if (pRSInfo->StreamInfo.VideoStruct.iFPSDenom==0)
        pRSInfo->StreamInfo.VideoStruct.iFPSDenom = 1;

      snprintf(pRSInfo->achDescriptiveRSFormat, sizeof(pRSInfo->achDescriptiveRSFormat),
               "YUV420_%dx%d_%.4fFPS",
               pRSInfo->StreamInfo.VideoStruct.iWidth,
               pRSInfo->StreamInfo.VideoStruct.iHeight,
               pRSInfo->StreamInfo.VideoStruct.iFPSCount*1.0/pRSInfo->StreamInfo.VideoStruct.iFPSDenom);

      /* Set required output buffer size, needed for ffmpeg... */
      pPluginInstance->llOutputBufferSize = 65536;
      break;
    case CODEC_TYPE_AUDIO:
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

      /* Set required output buffer size, needed for ffmpeg... */
      pPluginInstance->llOutputBufferSize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
      break;
    default:
      /* Should never happen... */
      MMIOfree(pRSInfo);
      MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);
      return MMIO_ERROR_NOT_SUPPORTED;
  }
  
  /* Save some pointers as shortcuts */
  pPluginInstance->pESInfo = pESInfo;
  pPluginInstance->pESInstance = pNode->pNodeOwnerPluginInstance;

  /* Initialize the libAVCodec stuffs */

  /* Save the codec info */
  pPluginInstance->hCodec = pCodec;

  /* Create a parser context for it */
  pPluginInstance->hCodecParser = av_parser_init(pCodec->id);
  if (!pPluginInstance->hCodecParser)
  {
    printf("[avcdecoder_Link] : Could not create codec parser!\n");

    MMIOfree(pRSInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Allocate codec context */
  pPluginInstance->hCodecContext = avcodec_alloc_context();
  if (!pPluginInstance->hCodecContext)
  {
    printf("[avcdecoder_Link] : Could not create codec context!\n");

    av_parser_close(pPluginInstance->hCodecParser); pPluginInstance->hCodecParser = NULL;
    MMIOfree(pRSInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Open the codec with the codec context */
  if (avcodec_open(pPluginInstance->hCodecContext, pPluginInstance->hCodec) < 0)
  {
    printf("[avcdecoder_Link] : Could not open codec!\n");
    av_free(pPluginInstance->hCodecContext); pPluginInstance->hCodecContext = NULL;
    av_parser_close(pPluginInstance->hCodecParser); pPluginInstance->hCodecParser = NULL;
    MMIOfree(pRSInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNewRS);
    return MMIO_ERROR_UNKNOWN;
  }

  return MMIO_NOERROR;
}

static void avcdecoder_internal_DestroyTree(mmioProcessTreeNode_p pRoot)
{
  if (!pRoot) return;

  if (pRoot->pFirstChild)
    avcdecoder_internal_DestroyTree(pRoot->pFirstChild);

  if (pRoot->pNextBrother)
    avcdecoder_internal_DestroyTree(pRoot->pNextBrother);

  MMIOfree(pRoot->pTypeSpecificInfo);
  MMIOPsUnlinkAndDestroyNodeStruct(pRoot);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avcdecoder_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  avcdecoderInstance_p pPluginInstance = pInstance;
  if ((!pInstance) || (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Uninitialize the codec we used in here */
  if (pPluginInstance->hCodecParser)
  {
    av_parser_close(pPluginInstance->hCodecParser);
    pPluginInstance->hCodecParser = NULL;
  }
  if (pPluginInstance->hCodecContext)
  {
    avcodec_close(pPluginInstance->hCodecContext);
    av_free(pPluginInstance->hCodecContext);
    pPluginInstance->hCodecContext = NULL;
  }
  pPluginInstance->hCodec = NULL;

  /* Destroy all the levels we've created */
  avcdecoder_internal_DestroyTree(pNode);

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
    *ppchInternalName = "avcdecoder";
    *ppchSupportedFormats = "es_?_*";
    *piImportance = 5000;

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
