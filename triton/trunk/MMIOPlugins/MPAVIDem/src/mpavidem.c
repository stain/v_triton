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
 * AVI Demuxer plugin
 *
 * Current limitations:
 * - Only full avi files with index table are supported
 * - AVI v2.0 (ODML) index tables are not yet supported
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tpl.h"

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"


//#define DEBUG_BUILD
//#define PRINT_AVI_STRUCTURE

#define AVIDEMUX_INDEXTABLE_INCREASE_STEP            128
#define AVIDEMUX_NUM_INDEX_ENTRIES_TO_PREREAD      32768
#define AVIDEMUX_NUM_INDEX_ENTRIES_TO_READ_DIVIDER     8

#define AVIDEMUX_AVIHEADERFLAG_TRUSTCKTYPE    0x00000800
#define AVIDEMUX_AVIHEADERFLAG_ISINTERLEAVED  0x00000100

#define AVIDEMUX_AVIINDEXFLAG_LIST     0x00000001
#define AVIDEMUX_AVIINDEXFLAG_TWOCC    0x00000002
#define AVIDEMUX_AVIINDEXFLAG_KEYFRAME 0x00000010
#define AVIDEMUX_AVIINDEXFLAG_NOTIME   0x00000100
#define AVIDEMUX_AVIINDEXFLAG_COMPUSE  0x0fff0000

#define AVIDEMUX_FRAMEFLAGS_KEYFRAME   0x01

typedef struct AviDemuxFormatTable_s
{
  char *       pchFormatName;
  unsigned int uiFormatNum;
} AviDemuxFormatTable_t, *AviDemuxFormatTable_p;


static AviDemuxFormatTable_t aviDemuxAudioFormatTable[] =
{
  {"UNKNOWN",           0x0000}, /* Unknown Format */
  {"PCM",               0x0001}, /* PCM */
  {"ADPCM",             0x0002}, /* Microsoft ADPCM Format */
  {"IEEE_FLOAT",        0x0003}, /* IEEE Float */
  {"VSELP",             0x0004}, /* Compaq Computer's VSELP */
  {"IBM_CSVD",          0x0005}, /* IBM CVSD */
  {"ALAW",              0x0006}, /* ALAW */
  {"MULAW",             0x0007}, /* MULAW */
  {"DTS_MS",            0x0008}, /* Microsoft DTS? */
  {"WMAS",              0x000A}, /* WMA 9 Speech */
  {"OKI_ADPCM",         0x0010}, /* OKI ADPCM */
  {"IMA_ADPCM",         0x0011}, /* Intel's DVI ADPCM */
  {"MEDIASPACE_ADPCM",  0x0012}, /* Videologic's MediaSpace ADPCM */
  {"SIERRA_ADPCM",      0x0013}, /* Sierra ADPCM */
  {"G723_ADPCM",        0x0014}, /* G.723 ADPCM */
  {"DIGISTD",           0x0015}, /* DSP Solution's DIGISTD */
  {"DIGIFIX",           0x0016}, /* DSP Solution's DIGIFIX */
  {"DIALOGIC_OKI_ADPCM",0x0017}, /* Dialogic OKI ADPCM */
  {"MEDIAVISION_ADPCM", 0x0018}, /* MediaVision ADPCM */
  {"CU_CODEC",          0x0019}, /* HP CU */
  {"YAMAHA_ADPCM",      0x0020}, /* Yamaha ADPCM */
  {"SONARC",            0x0021}, /* Speech Compression's Sonarc */
  {"TRUESPEECH",        0x0022}, /* DSP Group's True Speech */
  {"ECHOSC1",           0x0023}, /* Echo Speech's EchoSC1 */
  {"AUDIOFILE_AF36",    0x0024}, /* Audiofile AF36 */
  {"APTX",              0x0025}, /* APTX */
  {"AUDIOFILE_AF10",    0x0026}, /* AudioFile AF10 */
  {"PROSODY_1612",      0x0027}, /* Prosody 1612 */
  {"LRC",               0x0028}, /* LRC */
  {"AC2",               0x0030}, /* Dolby AC2 */
  {"GSM610",            0x0031}, /* GSM610 */
  {"MSNAUDIO",          0x0032}, /* MSNAudio */
  {"ANTEX_ADPCME",      0x0033}, /* Antex ADPCME */
  {"CONTROL_RES_VQLPC", 0x0034}, /* Control Res VQLPC */
  {"DIGIREAL",          0x0035}, /* Digireal */
  {"DIGIADPCM",         0x0036}, /* DigiADPCM */
  {"CONTROL_RES_CR10",  0x0037}, /* Control Res CR10 */
  {"VBXADPCM",          0x0038}, /* NMS VBXADPCM */
  {"ROLAND_RDAC",       0x0039}, /* Roland RDAC */
  {"ECHOSC3",           0x003A}, /* EchoSC3 */
  {"ROCKWELL_ADPCM",    0x003B}, /* Rockwell ADPCM */
  {"ROCKWELL_DIGITALK", 0x003C}, /* Rockwell Digit LK */
  {"XEBEC",             0x003D}, /* Xebec */
  {"G721_ADPCM",        0x0040}, /* Antex Electronics G.721 */
  {"G728_CELP",         0x0041}, /* G.728 CELP */
  {"MSG723",            0x0042}, /* MSG723 */
  {"G726",              0x0045}, /* ITU Standard */
  {"MPA",               0x0050}, /* MPEG Layer 1,2 */
  {"RT24",              0x0051}, /* RT24 */
  {"PAC",               0x0051}, /* PAC */
  {"MP3",               0x0055}, /* MPEG Layer 3 */
  {"CIRRUS",            0x0059}, /* Cirrus */
  {"DK3",               0x0061}, /* DK3 */
  {"DK4",               0x0062}, /* DK4 */
  {"CANOPUS_ATRAC",     0x0063}, /* Canopus Atrac */
  {"G726_ADPCM",        0x0064}, /* G.726 ADPCM */
  {"G722_ADPCM",        0x0065}, /* G.722 ADPCM */
  {"DSAT",              0x0066}, /* DSAT */
  {"DSAT_DISPLAY",      0x0067}, /* DSAT Display */
  {"VOXWARE_BYTE_ALIGNED", 0x0069}, /* Voxware Byte Aligned (obsolete) */
  {"VOXWARE_AC8",       0x0070}, /* Voxware AC8 (obsolete) */
  {"VOXWARE_AC10",      0x0071}, /* Voxware AC10 (obsolete) */
  {"VOXWARE_AC16",      0x0072}, /* Voxware AC16 (obsolete) */
  {"VOXWARE_AC20",      0x0073}, /* Voxware AC20 (obsolete) */
  {"VOXWARE_RT24",      0x0074}, /* Voxware MetaVoice (obsolete) */
  {"VOXWARE_RT29",      0x0075}, /* Voxware MetaSound (obsolete) */
  {"VOXWARE_RT29HW",    0x0076}, /* Voxware RT29HW (obsolete) */
  {"VOXWARE_VR12",      0x0077}, /* Voxware VR12 (obsolete) */
  {"VOXWARE_VR18",      0x0078}, /* Voxware VR18 (obsolete) */
  {"VOXWARE_TQ40",      0x0079}, /* Voxware TQ40 (obsolete) */
  {"SOFTSOUND",         0x0080}, /* Softsound */
  {"VOXWARE_TQ60",      0x0081}, /* Voxware TQ60 (obsolete) */
  {"MSRT24",            0x0082}, /* MSRT24 */
  {"G729A",             0x0083}, /* G.729A */
  {"MVI_MV12",          0x0084}, /* MVI MV12 */
  {"DF_G726",           0x0085}, /* DF G.726 */
  {"DF_GSM610",         0x0086}, /* DF GSM610 */
  {"ISIAUDIO",          0x0088}, /* ISIAudio */
  {"ONLIVE",            0x0089}, /* Onlive */
  {"SBC24",             0x0091}, /* SBC24 */
  {"DOLBY_AC3_SPDIF",   0x0092}, /* Dolby AC3 SPDIF */
  {"ZYXEL_ADPCM",       0x0097}, /* ZyXEL ADPCM */
  {"PHILIPS_LPCBB",     0x0098}, /* Philips LPCBB */
  {"PACKED",            0x0099}, /* Packed */
  {"AAC",               0x00FF}, /* AAC */
  {"RHETOREX_ADPCM",    0x0100}, /* Rhetorex ADPCM */
  {"IBM_MULAW",         0x0101}, /* IBM MULAW? */
  {"IBM_ALAW",          0x0102}, /* IBM ALAW? */
  {"IBM_ADPCM",         0x0103}, /* IBM ADPCM? */
  {"VIVO_G723",         0x0111}, /* Vivo G.723 */
  {"VIVO_SIREN",        0x0112}, /* Vivo Siren */
  {"DIGITAL_G723",      0x0123}, /* Digital G.723 */
  {"WMA1",              0x0160}, /* WMA version 1 */
  {"WMA2",              0x0161}, /* WMA (v2) 7, 8, 9 series */
  {"WMAP",              0x0162}, /* WMA 9 Professional */
  {"WMAL",              0x0163}, /* WMA 9 Lossless */
  {"CREATIVE_ADPCM",    0x0200}, /* Creative ADPCM */
  {"CREATIVE_FASTSPEECH8", 0x0202}, /* Creative FastSpeech8 */
  {"CREATIVE_FASTSPEECH10", 0x0203}, /* Creative FastSpeech10 */
  {"QUARTERDECK",       0x0220}, /* Quarterdeck */
  {"FM_TOWNS_SND",      0x0300}, /* FM Towns Snd */
  {"BTV_DIGITAL",       0x0400}, /* BTV Digital */
  {"VME_VMPCM",         0x0680}, /* VME VMPCM */
  {"OLIGSM",            0x1000}, /* OLIGSM */
  {"OLIADPCM",          0x1001}, /* OLIADPCM */
  {"OLICELP",           0x1002}, /* OLICELP */
  {"OLISBC",            0x1003}, /* OLISBC */
  {"OLIOPR",            0x1004}, /* OLIOPR */
  {"LH_CODEC",          0x1100}, /* LH Codec */
  {"NORRIS",            0x1400}, /* Norris */
  {"ISIAUDIO",          0x1401}, /* ISIAudio */
  {"SOUNDSPACE_MUSICOMPRESS", 0x1500}, /* Soundspace Music Compression */
  {"A52",               0x2000}, /* A52 */
  {"DTS",               0x2001}, /* DTS */
  {"DIVIO_AAC",         0x4143}, /* No info */
  {"VORB_1",            0x674F}, /* Vorbis 1 */
  {"VORB_1PLUS",        0x676F}, /* Vorbis 1+ */
  {"VORB_2",            0x6750}, /* Vorbis 2 */
  {"VORB_2PLUS",        0x6770}, /* Vorbis 2+ */
  {"VORB_3",            0x6751}, /* Vorbis 3 */
  {"VORB_3PLUS",        0x6771}, /* Vorbis 3+ */
  {"EXTENSIBLE",        0xFFFE}, /* SubFormat */
  {"DEVELOPMENT",       0xFFFF}  /* Development */
};

typedef struct AviChunkHeader_s
{
  char         achChunkId[4];
  unsigned int uiChunkSize;
} AviChunkHeader_t, *AviChunkHeader_p;

typedef struct AviHeader_s
{
  unsigned int uiMicroSecPerFrame;
  unsigned int uiMaxBytesPerSec;
  unsigned int uiReserved1;
  unsigned int uiFlags;
  unsigned int uiTotalFrames;
  unsigned int uiInitialFrames;
  unsigned int uiStreams;
  unsigned int uiSuggestedBufferSize;
  unsigned int uiWidth;
  unsigned int uiHeight;
  unsigned int uiScale;
  unsigned int uiRate;
  unsigned int uiStart;
  unsigned int uiLength;
} AviHeader_t, *AviHeader_p;

typedef struct AviStreamHeader_s
{
  char           achType[4];
  char           achHandler[4];
  unsigned int   uiFlags;
  unsigned short usPriority;
  unsigned short usLanguage;
  unsigned int   uiInitialFrames;
  unsigned int   uiScale;
  unsigned int   uiRate;
  unsigned int   uiStart;
  unsigned int   uiLength;
  unsigned int   uiSuggestedBufferSize;
  unsigned int   uiQuality;
  unsigned int   uiSampleSize;
  unsigned short usRectX1;
  unsigned short usRectY1;
  unsigned short usRectX2;
  unsigned short usRectY2;
} AviStreamHeader_t, *AviStreamHeader_p;

typedef struct AviIndex1Entry_s
{
  unsigned long uiChunkId;
  unsigned long uiFlags;
  unsigned long uiChunkOffset;
  unsigned long uiChunkLength;
} AviIndex1Entry_t, *AviIndex1Entry_p;

typedef struct AviVideoStreamFormat_s
{
  unsigned int   uiSize;
  unsigned int   uiWidth;
  unsigned int   uiHeight;
  unsigned short usNumPlanes;
  unsigned short usBitCount;
  unsigned int   uiCompression;
  unsigned int   uiImageSize;
  unsigned int   uiXPelsPerMeter;
  unsigned int   uiYPelsPerMeter;
  unsigned int   uiClrUsed;
  unsigned int   uiClrImportant;
} AviVideoStreamFormat_t, *AviVideoStreamFormat_p;

typedef struct AviAudioStreamFormat_s
{
  unsigned short usFormatTag;
  unsigned short usNumChannels;
  unsigned int   uiSamplesPerSec;
  unsigned int   uiAvgBytesPerSec;
  unsigned short usBlockAlign;
  unsigned short usBitsPerSample;
} AviAudioStreamFormat_t, *AviAudioStreamFormat_p;


typedef struct AviDemuxStreamIndexEntry_s
{
  unsigned long long ullStreamPosInBytes;
  unsigned long long ullFileOffset;
  unsigned long      ulFrameSize;
  unsigned char      uchFlags;
} AviDemuxStreamIndexEntry_t, *AviDemuxStreamIndexEntry_p;

typedef struct AviDemuxStreamDesc_s
{
  int                        iStreamNum;

  int                        bAviStreamHeaderFound;
  AviStreamHeader_t          aviStreamHeader;

  int                        bAviStreamFormatFound;
  union {
    AviAudioStreamFormat_t audioFormat;
    AviVideoStreamFormat_t videoFormat;
  } formatSpecific;
  mmioStreamInfo_t           streamInfo;

  char                      *pchName;

  int                        iNumMaxIndexTableEntries;
  int                        iNumIndexTableEntries;
  AviDemuxStreamIndexEntry_p pIndexTable;

  unsigned long long         ullStreamLengthInBytes;
  unsigned int               uiSkippedFrames; /* Should be equal to uiInitialFrames at the end */

  /* Stuffs for playback */
  int                        iPlaybackDirection;
  int                        iLastIndexTablePosition;
  int                        bDidSeek;

  void *pNext;
} AviDemuxStreamDesc_t, *AviDemuxStreamDesc_p;

typedef struct AviDemuxInstance_s
{
  int                   bAviHeaderFound;
  AviHeader_t           aviHeader;
  unsigned int          uiNumStreams;
  AviDemuxStreamDesc_p  pCurrStreamDesc;
  AviDemuxStreamDesc_p  pStreamDescriptions;
  int                   bVeryFirstChunk;
  unsigned long long    ullMoviChunkStartPos;

  TPL_MTXSEM               hmtxUseSource;
  mmioMediaSpecificInfo_p  pSource;
  void                    *pSourceInstance;

} AviDemuxInstance_t, *AviDemuxInstance_p;


static inline int IsFourCC(char *pchChunkId, char *pchToCompare)
{
  return (pchChunkId[0] == pchToCompare[0]) &&
    (pchChunkId[1] == pchToCompare[1]) &&
    (pchChunkId[2] == pchToCompare[2]) &&
    (pchChunkId[3] == pchToCompare[3]);
}

static int ParseAvihChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  unsigned int uiToRead;

  /* Padding to word boundary! */
  if (uiChunkSize%2)
    uiChunkSize++;

  uiToRead = sizeof(AviHeader_t);
  memset(&(pPluginInstance->aviHeader), 0, sizeof(AviHeader_t));
  if (uiToRead>uiChunkSize)
    uiToRead = uiChunkSize;

  if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                               &(pPluginInstance->aviHeader),
                                               uiToRead) == MMIO_NOERROR)
  {
    pPluginInstance->bAviHeaderFound = 1;
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("AVI Header:\n");
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiMicroSecPerFrame: %d\n", pPluginInstance->aviHeader.uiMicroSecPerFrame);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiMaxBytesPerSec  : %d\n", pPluginInstance->aviHeader.uiMaxBytesPerSec);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiReserved1       : %d\n", pPluginInstance->aviHeader.uiReserved1);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiFlags           : %08x\n", pPluginInstance->aviHeader.uiFlags);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiTotalFrames     : %d\n", pPluginInstance->aviHeader.uiTotalFrames);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiInitialFrames   : %d\n", pPluginInstance->aviHeader.uiInitialFrames);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiStreams         : %d\n", pPluginInstance->aviHeader.uiStreams);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiSuggestedBufferSize: %d\n", pPluginInstance->aviHeader.uiSuggestedBufferSize);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiWidth           : %d\n", pPluginInstance->aviHeader.uiWidth);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiHeight          : %d\n", pPluginInstance->aviHeader.uiHeight);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiScale           : %d\n", pPluginInstance->aviHeader.uiScale);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiRate            : %d\n", pPluginInstance->aviHeader.uiRate);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiStart           : %d\n", pPluginInstance->aviHeader.uiStart);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiLength          : %d\n", pPluginInstance->aviHeader.uiLength);
#endif

    /* Skip remaining parts, if any exists */
    if (uiToRead<uiChunkSize)
      pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiChunkSize-uiToRead, MMIO_MEDIA_SEEK_CUR);

    return 1;
  }
  /* Could not parse it otherwise */
  return 0;

}

static int ParseStrhChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  unsigned int uiToRead;

  if (!pPluginInstance->pCurrStreamDesc)
  {
    /* Ouch, we're not in 'strl' chunk, this kind of chunk is not expected! */
    return 0;
  }

  /* Padding to word boundary! */
  if (uiChunkSize%2)
    uiChunkSize++;

  uiToRead = sizeof(AviStreamHeader_t);
  memset(&(pPluginInstance->pCurrStreamDesc->aviStreamHeader), 0, sizeof(AviStreamHeader_t));
  if (uiToRead>uiChunkSize)
    uiToRead = uiChunkSize;

  if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                               &(pPluginInstance->pCurrStreamDesc->aviStreamHeader),
                                               uiToRead) == MMIO_NOERROR)
  {
    pPluginInstance->pCurrStreamDesc->bAviStreamHeaderFound = 1;
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("StreamHeader for stream %02d:\n", pPluginInstance->pCurrStreamDesc->iStreamNum);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  fccType           : 0x%08x [%.4s]\n", *((unsigned int*) &(pPluginInstance->pCurrStreamDesc->aviStreamHeader.achType[0])), pPluginInstance->pCurrStreamDesc->aviStreamHeader.achType);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  fccHandler        : 0x%08x [%.4s]\n", *((unsigned int*) &(pPluginInstance->pCurrStreamDesc->aviStreamHeader.achHandler[0])), pPluginInstance->pCurrStreamDesc->aviStreamHeader.achHandler);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiFlags           : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiFlags);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  usPriority        : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.usPriority);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  usLanguage        : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.usLanguage);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiInitialFrames   : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiInitialFrames);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiScale           : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiScale);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiRate            : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiRate);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiStart           : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiStart);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiLength          : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiLength);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiSuggestedBufferSize: %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiSuggestedBufferSize);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiQuality         : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiQuality);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  uiSampleSize      : %d\n", pPluginInstance->pCurrStreamDesc->aviStreamHeader.uiSampleSize);
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("  Rect: %x;%d -> %d;%d\n",
           pPluginInstance->pCurrStreamDesc->aviStreamHeader.usRectX1,
           pPluginInstance->pCurrStreamDesc->aviStreamHeader.usRectY1,
           pPluginInstance->pCurrStreamDesc->aviStreamHeader.usRectX2,
           pPluginInstance->pCurrStreamDesc->aviStreamHeader.usRectY2);
#endif

    /* Skip remaining parts, if any exists */
    if (uiToRead<uiChunkSize)
      pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiChunkSize-uiToRead, MMIO_MEDIA_SEEK_CUR);

    return 1;
  }
  /* Could not parse it otherwise */
  return 0;
}

static int ParseStrfChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  unsigned int uiToRead;

  if (!pPluginInstance->pCurrStreamDesc)
  {
    /* Ouch, we're not in 'strl' chunk, this kind of chunk is not expected! */
    return 0;
  }

  if (!pPluginInstance->pCurrStreamDesc->bAviStreamHeaderFound)
  {
    /* We don't know what kind of stream we're talking about, so */
    /* this kind of chunk is not expected! */
    return 0;
  }

  /* Padding to word boundary! */
  if (uiChunkSize%2)
    uiChunkSize++;

  if (IsFourCC(pPluginInstance->pCurrStreamDesc->aviStreamHeader.achType, "auds"))
  {
    /* We have to deal with an audio stream! */

    uiToRead = sizeof(AviAudioStreamFormat_t);
    memset(&(pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat), 0, sizeof(AviAudioStreamFormat_t));
    if (uiToRead>uiChunkSize)
      uiToRead = uiChunkSize;

    if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                 &(pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat),
                                                 uiToRead) == MMIO_NOERROR)
    {
      pPluginInstance->pCurrStreamDesc->bAviStreamFormatFound = 1;

      /* Prepare MMIO-specific stream-info structure */
      pPluginInstance->pCurrStreamDesc->streamInfo.AudioStruct.iSampleRate = pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.uiSamplesPerSec;
      pPluginInstance->pCurrStreamDesc->streamInfo.AudioStruct.iIsSigned = 1;
      pPluginInstance->pCurrStreamDesc->streamInfo.AudioStruct.iBits = pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usBitsPerSample;
      pPluginInstance->pCurrStreamDesc->streamInfo.AudioStruct.iChannels = pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usNumChannels;

#ifdef PRINT_AVI_STRUCTURE
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("Audio Stream format for stream %02d:\n", pPluginInstance->pCurrStreamDesc->iStreamNum);

      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  usFormatTag       : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usFormatTag);
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  usNumChannels     : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usNumChannels);
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  uiSamplesPerSec   : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.uiSamplesPerSec);
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  uiAvgBytesPerSec  : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.uiAvgBytesPerSec);
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  usBlockAlign      : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usBlockAlign);
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("  usBitsPerSample   : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.audioFormat.usBitsPerSample);
#endif

      /* Skip remaining parts, if any exists */
      if (uiChunkSize > sizeof(AviAudioStreamFormat_t))
        pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiChunkSize - sizeof(AviAudioStreamFormat_t), MMIO_MEDIA_SEEK_CUR);
      return 1;
    } else
      return 0;
  } else
    if (IsFourCC(pPluginInstance->pCurrStreamDesc->aviStreamHeader.achType, "vids"))
    {
      /* We have to deal with a video stream! */

      uiToRead = sizeof(AviVideoStreamFormat_t);
      memset(&(pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat), 0, sizeof(AviVideoStreamFormat_t));
      if (uiToRead>uiChunkSize)
        uiToRead = uiChunkSize;

      if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                   &(pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat),
                                                   uiToRead) == MMIO_NOERROR)
      {
        pPluginInstance->pCurrStreamDesc->bAviStreamFormatFound = 1;

        /* Prepare MMIO-specific stream-info structure */
        pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iFPSCount = pPluginInstance->aviHeader.uiRate;
        pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iFPSDenom = pPluginInstance->aviHeader.uiScale;
        pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iWidth = pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiWidth;
        pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iHeight = pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiHeight;
        MMIOPsGuessPixelAspectRatio(pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iWidth,
                                    pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iHeight,
                                    &(pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iPixelAspectRatioCount),
                                    &(pPluginInstance->pCurrStreamDesc->streamInfo.VideoStruct.iPixelAspectRatioDenom));

#ifdef PRINT_AVI_STRUCTURE
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("Video Stream format for stream %02d:\n", pPluginInstance->pCurrStreamDesc->iStreamNum);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiSize            : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiSize);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiWidth           : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiWidth);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiHeight          : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiHeight);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  usNumPlanes       : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.usNumPlanes);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  usBitCount        : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.usBitCount);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiCompression     : 0x%08x [%.4s]\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiCompression, (char *)(&pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiCompression));
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiImageSize       : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiImageSize);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiXPelsPerMeter   : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiXPelsPerMeter);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiYPelsPerMeter   : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiYPelsPerMeter);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiClrUsed         : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiClrUsed);
        { int i; for (i=0; i<iDepth; i++) printf(" "); }
        printf("  uiClrImportant    : %d\n", pPluginInstance->pCurrStreamDesc->formatSpecific.videoFormat.uiClrImportant);
#endif

        /* Skip remaining parts, if any exists */
        if (uiChunkSize > sizeof(AviVideoStreamFormat_t))
          pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiChunkSize - sizeof(AviVideoStreamFormat_t), MMIO_MEDIA_SEEK_CUR);

        return 1;
      } else
        return 0;
    } else
      /* Could not parse it otherwise */
      return 0;
}

static int ParseStrnChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  if (!pPluginInstance->pCurrStreamDesc)
  {
    /* Ouch, we're not in 'strl' chunk, this kind of chunk is not expected! */
    return 0;
  }

  if (!pPluginInstance->pCurrStreamDesc->bAviStreamHeaderFound)
  {
    /* We don't know what kind of stream we're talking about, so */
    /* this kind of chunk is not expected! */
    return 0;
  }

  /* Padding to word boundary! */
  if (uiChunkSize%2)
    uiChunkSize++;

  if (pPluginInstance->pCurrStreamDesc->pchName)
    MMIOfree(pPluginInstance->pCurrStreamDesc->pchName);

  pPluginInstance->pCurrStreamDesc->pchName = (char *) MMIOmalloc(uiChunkSize+1);
  if (!pPluginInstance->pCurrStreamDesc->pchName)
  {
    /* Oops, could not allocate memory! */
    return 0;
  }
  pPluginInstance->pCurrStreamDesc->pchName[uiChunkSize] = 0;

  if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                               pPluginInstance->pCurrStreamDesc->pchName,
                                               uiChunkSize) != MMIO_NOERROR)
  {
    /* Read error! */
    MMIOfree(pPluginInstance->pCurrStreamDesc->pchName);
    pPluginInstance->pCurrStreamDesc->pchName = NULL;

    return 0;
  } else
  {
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("Stream name for stream %02d: [%s]\n", pPluginInstance->pCurrStreamDesc->iStreamNum, pPluginInstance->pCurrStreamDesc->pchName);
#endif
  }
  return 1;
}

static void AddNewIndexEntry(AviDemuxInstance_p pPluginInstance, AviIndex1Entry_p pAviIndexEntry, unsigned int uiApproximateNumOfIndexEntries)
{
  int iStreamNum;
  int bThereIsSpaceInTable;
  AviDemuxStreamIndexEntry_p pIndexEntry;

  /* Extract stream number */
  iStreamNum =
    (((char *) &(pAviIndexEntry->uiChunkId))[0] - '0') * 10 +
    ((char *) &(pAviIndexEntry->uiChunkId))[1] - '0';

  if (iStreamNum<pPluginInstance->uiNumStreams)
  {
    /* Ok, now we know which stream's index table we have to increase */

    /* Skip initial frames if the AVI is interleaved! */
    if ((pPluginInstance->aviHeader.uiFlags & AVIDEMUX_AVIHEADERFLAG_ISINTERLEAVED) &&
        (pPluginInstance->pStreamDescriptions[iStreamNum].uiSkippedFrames <
         pPluginInstance->pStreamDescriptions[iStreamNum].aviStreamHeader.uiInitialFrames))
    {
      /* Skip this frame, it's still initial frame! */
      pPluginInstance->pStreamDescriptions[iStreamNum].uiSkippedFrames++;
    } else
    {
      /* Don't skip this frame, we need it! */

      /* Check if there is space in that table or not! */
      bThereIsSpaceInTable = 1;
      if (pPluginInstance->pStreamDescriptions[iStreamNum].iNumIndexTableEntries >=
          pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries)
      {
        /* We have to make space in the table */

        if (pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable == NULL)
        {
          pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries = uiApproximateNumOfIndexEntries;
          if (pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries < AVIDEMUX_INDEXTABLE_INCREASE_STEP)
            pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries = AVIDEMUX_INDEXTABLE_INCREASE_STEP;

          pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable = (AviDemuxStreamIndexEntry_p) MMIOmalloc(sizeof(AviDemuxStreamIndexEntry_t) * pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries);
          bThereIsSpaceInTable = (pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable != NULL);

          if (!bThereIsSpaceInTable)
          {
            /* Could not allocate space! */
            pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries = 0;
          }
        }
        else
        {
          AviDemuxStreamIndexEntry_p pNew;

          pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries += AVIDEMUX_INDEXTABLE_INCREASE_STEP;
          pNew = (AviDemuxStreamIndexEntry_p) MMIOrealloc(pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable, sizeof(AviDemuxStreamIndexEntry_t) * pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries);

          if (pNew)
          {
            pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable = pNew;
            bThereIsSpaceInTable = 1;
          } else
          {
            /* Could not increase it! */
            pPluginInstance->pStreamDescriptions[iStreamNum].iNumMaxIndexTableEntries -= AVIDEMUX_INDEXTABLE_INCREASE_STEP;
            bThereIsSpaceInTable = 0;
          }
        }
      }

      if (bThereIsSpaceInTable)
      {
        /* Get the new index entry we have to fill */
        pIndexEntry =
          pPluginInstance->pStreamDescriptions[iStreamNum].pIndexTable +
          pPluginInstance->pStreamDescriptions[iStreamNum].iNumIndexTableEntries;

        /* Increase number of index entries */
        pPluginInstance->pStreamDescriptions[iStreamNum].iNumIndexTableEntries++;

        pIndexEntry->ullFileOffset = pPluginInstance->ullMoviChunkStartPos + pAviIndexEntry->uiChunkOffset + 8;
        pIndexEntry->ulFrameSize = pAviIndexEntry->uiChunkLength;
        pIndexEntry->uchFlags = 0;

        if (pPluginInstance->aviHeader.uiFlags & AVIDEMUX_AVIHEADERFLAG_TRUSTCKTYPE)
        {
          /* AVI Header said that we should trust the chunk type to guess if */
          /* the given frame is a keyframe or not. */
          if (pPluginInstance->pStreamDescriptions[iStreamNum].aviStreamHeader.achType[0] == 'v')
          {
            /* For video streams, guess if this frame is key or not from the */
            /* chunk type */
            if (((pAviIndexEntry->uiChunkId & 0xFF000000) >> 24) == 'b')
            {
              /* 00db (Data Bitmap), so uncompressed, so treat it as keyframe! */
              pIndexEntry->uchFlags |= AVIDEMUX_FRAMEFLAGS_KEYFRAME;
            }
            /* else 00dc (Data Compressed), so not a keyframe. */
          } else
          {
            /* Either audio stream or other, we treat every frame as keyframe here. */
            pIndexEntry->uchFlags |= AVIDEMUX_FRAMEFLAGS_KEYFRAME;
          }
        } else
        {
          /* AVI Header said we should trust the index table entry flag to guess if */
          /* the given frame is a keyframe of not. */
          if (pAviIndexEntry->uiFlags & AVIDEMUX_AVIINDEXFLAG_KEYFRAME)
            pIndexEntry->uchFlags |= AVIDEMUX_FRAMEFLAGS_KEYFRAME;

        }

        /* Small hack: if this is the very first index table entry for the stream, */
        /* then force to set it to be a keyframe! */
        if (pPluginInstance->pStreamDescriptions[iStreamNum].iNumIndexTableEntries==1)
        {
          pIndexEntry->ullStreamPosInBytes = 0;
          pIndexEntry->uchFlags |= AVIDEMUX_FRAMEFLAGS_KEYFRAME;
        } else
        {
          pIndexEntry->ullStreamPosInBytes =
            (pIndexEntry-1)->ullStreamPosInBytes + (pIndexEntry-1)->ulFrameSize;
        }

        /* Calculate stream length */
        pPluginInstance->pStreamDescriptions[iStreamNum].ullStreamLengthInBytes += pAviIndexEntry->uiChunkLength;
      }
    }
  }
}

static int ParseIdx1ChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  AviIndex1Entry_p pAviIndexEntries;
  unsigned int uiNumIndexEntriesToRead;
  unsigned int uiEntryToTry;
  unsigned int uiRead;
  int iIndexNum;
  long long llStartingFilePosition;
  char *pchChunkId;
  unsigned int uiApproximateNumOfIndexEntries;

  /* There seems to be a problem with index tables, that they sometimes */
  /* have a length which is different from the reported chunk size.     */
  /* For this reason, we don't use the uiChunkSize value here, only for */
  /* approximating the number of index table entries, but read the file */
  /* as long as it seems to be an index table. */

  /* However, reading the whole index table by 16 bytes isn't the fastest */
  /* thing one can do. To solve this, we read it in bigger pieces, parse  */
  /* it in memory, and then read more, or go back some.                   */

  /* Calculate the approximate number of index table entries */
  uiApproximateNumOfIndexEntries = uiChunkSize / sizeof(AviIndex1Entry_t);

  /* Allocate buffer to read big chunks of file */
  pAviIndexEntries = (AviIndex1Entry_p) MMIOmalloc(sizeof(AviIndex1Entry_t) * AVIDEMUX_NUM_INDEX_ENTRIES_TO_PREREAD);
  if (!pAviIndexEntries)
  {
    /* Eeek, out of memory! */
    return 0;
  }

  /* Get the starting file position */
  llStartingFilePosition = pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance);

  /* Initialize counters */
  uiNumIndexEntriesToRead = AVIDEMUX_NUM_INDEX_ENTRIES_TO_PREREAD;
  iIndexNum = 0;
  uiRead = 0;

  /* Read, parse, work... */
  while (uiNumIndexEntriesToRead>0)
  {
    while(pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                   pAviIndexEntries,
                                                   sizeof(AviIndex1Entry_t) * uiNumIndexEntriesToRead) == MMIO_NOERROR)
    {
      /* Ok, a big number of data could be read from the file, let's try to parse it! */
      pchChunkId = (char *) &(pAviIndexEntries[0].uiChunkId);

      for (uiEntryToTry = 0; uiEntryToTry<uiNumIndexEntriesToRead; uiEntryToTry++)
      {
        if (
            (pchChunkId[0] >= '0') &&
            (pchChunkId[0] <= '9') &&
            (pchChunkId[1] >= '0') &&
            (pchChunkId[1] <= '9')
           )
        {
          /* Seems to be a valid index table entry! */
          uiRead += sizeof(AviIndex1Entry_t);
          iIndexNum++;

          AddNewIndexEntry(pPluginInstance, pAviIndexEntries+uiEntryToTry, uiApproximateNumOfIndexEntries);

          /*
          if (iIndexNum<10)
          {
            { int i; for (i=0; i<iDepth; i++) printf(" "); }
            printf("  Index-%02d:\n", iIndexNum);
            { int i; for (i=0; i<iDepth; i++) printf(" "); }
            printf("    uiChunkId     : 0x%08x [%.4s]\n", indexEntry.uiChunkId, (char *) &(indexEntry.uiChunkId));
            { int i; for (i=0; i<iDepth; i++) printf(" "); }
            printf("    uiFlags       : 0x%08x\n", indexEntry.uiFlags);
            { int i; for (i=0; i<iDepth; i++) printf(" "); }
            printf("    uiChunkOffset : 0x%08x  (%d)\n", indexEntry.uiChunkOffset, indexEntry.uiChunkOffset);
            { int i; for (i=0; i<iDepth; i++) printf(" "); }
            printf("    uiChunkLength : 0x%08x  (%d)\n", indexEntry.uiChunkLength, indexEntry.uiChunkLength);
          }
          */
        } else
        if ((pchChunkId[0] == 'r') &&
            (pchChunkId[1] == 'e') &&
            (pchChunkId[2] == 'c') &&
            (pchChunkId[3] == ' '))
        {
          /* Seems to be a 'rec ' field, skip it! */
        } else
        {
          /* Doesn't look to be an index table entry */
          /* Go back, and return! */
          pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance,
                                                   llStartingFilePosition + uiRead, MMIO_MEDIA_SEEK_SET);
          MMIOfree(pAviIndexEntries);
          return 1;
        }

        /* Go for chunk id of the next index entry */
        pchChunkId += sizeof(AviIndex1Entry_t);
      }
    }

    /* Could not read more big chunks, let's try to read smaller chunks then, */
    /* or set read counter to 0 to indicate end-of-loop condition.            */
    if (uiNumIndexEntriesToRead>1)
    {
      uiNumIndexEntriesToRead /= AVIDEMUX_NUM_INDEX_ENTRIES_TO_READ_DIVIDER;
      if (uiNumIndexEntriesToRead<1)
        uiNumIndexEntriesToRead = 1;
    } else
    {
      uiNumIndexEntriesToRead = 0;
    }
  }

  /* Went through all the file, everything has been parsed! */
  MMIOfree(pAviIndexEntries);
  return 1;
}


static int ParseListChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  char achListType[4];

  if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                               achListType,
                                               sizeof(achListType)) == MMIO_NOERROR)
  {
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("LIST listtype is [%.4s]\n", achListType);
#endif

    if (IsFourCC(achListType, "movi"))
    {
      /* Found the chunk which contains the data itself, so don't parse it! */
      pPluginInstance->ullMoviChunkStartPos = pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance)-4;

      if (uiChunkSize%2)
        uiChunkSize++;
      pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiChunkSize-4, MMIO_MEDIA_SEEK_CUR);
      return 1;
    } else
    if (IsFourCC(achListType, "strl"))
    {
      int rc;

      /* Found the chunk which describes a stream in AVI file! */

      /* Are we in a stream definition already? */
      if (pPluginInstance->pCurrStreamDesc)
      {
        /* Yes, we're already parsing a stream description, should not meet */
        /* a new stream description again! Error! */
        return 0;
      }

      /* Create a new structure! */
      pPluginInstance->uiNumStreams++;

      if (pPluginInstance->pStreamDescriptions)
      {
        AviDemuxStreamDesc_p pNew;

        pNew = (AviDemuxStreamDesc_p) MMIOrealloc(pPluginInstance->pStreamDescriptions, pPluginInstance->uiNumStreams * sizeof(AviDemuxStreamDesc_t));
        if (!pNew)
        {
          /* Not enough memory to parse this stream info! */
          pPluginInstance->uiNumStreams--;
          return 0;
        }

        pPluginInstance->pStreamDescriptions = pNew;
      } else
      {
        pPluginInstance->pStreamDescriptions = (AviDemuxStreamDesc_p) MMIOmalloc(pPluginInstance->uiNumStreams * sizeof(AviDemuxStreamDesc_t));
        if (!pPluginInstance->pStreamDescriptions)
        {
          /* Not enough memory to parse this stream info! */
          pPluginInstance->uiNumStreams++;
          return 0;
        }
      }

      memset(pPluginInstance->pStreamDescriptions + (pPluginInstance->uiNumStreams-1), 0, sizeof(AviDemuxStreamDesc_t));

      pPluginInstance->pCurrStreamDesc = pPluginInstance->pStreamDescriptions + (pPluginInstance->uiNumStreams-1);

      /* Set up some per-stream variables */
      pPluginInstance->pCurrStreamDesc->iStreamNum = pPluginInstance->uiNumStreams-1;
      pPluginInstance->pCurrStreamDesc->iPlaybackDirection = MMIO_DIRECTION_STOP;
      pPluginInstance->pCurrStreamDesc->iLastIndexTablePosition = -1;
      pPluginInstance->pCurrStreamDesc->bDidSeek = 0;

      rc = ParseChunks(pPluginInstance, uiChunkSize-4, iDepth);
      pPluginInstance->pCurrStreamDesc = NULL;
      if (!rc)
      {
        /* Error while parsing stream data! */
        /* Clean up */
        if (pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pchName)
          MMIOfree(pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pchName);
        if (pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pIndexTable)
          MMIOfree(pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pIndexTable);
        /* No need to resize the array again... */
        /* Decrease num of streams */
        pPluginInstance->uiNumStreams--;
        return 0;
      } else
      {
        /* Ok, we could parse it! */
        /* Check if we got all the mandatory stuffs for it! */
        if ((pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].bAviStreamHeaderFound) &&
            (pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].bAviStreamFormatFound))
        {
          /* Yep, found everything, so return with success! */
          return 1;
        } else
        {
          /* No, something is missing, don't use this stream! */
          /* Clean up */
          if (pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pchName)
            MMIOfree(pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pchName);
          if (pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pIndexTable)
            MMIOfree(pPluginInstance->pStreamDescriptions[pPluginInstance->uiNumStreams-1].pIndexTable);
          /* No need to resize the array again... */
          /* Decrease num of streams */
          pPluginInstance->uiNumStreams--;
          return 0;
        }
      }
    } else
    {
      /* Found some other kind of chunk, parse it! */
      return ParseChunks(pPluginInstance, uiChunkSize-4, iDepth);
    }
  }
  /* Could not parse it otherwise */
  return 0;
}

static int ParseRIFFChunkData(AviDemuxInstance_p pPluginInstance, unsigned int uiChunkSize, int iDepth)
{
  char achFileType[4];

  if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                               achFileType,
                                               sizeof(achFileType)) == MMIO_NOERROR)
  {
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("RIFF filetype is [%.4s]\n", achFileType);
#endif

    if (IsFourCC(achFileType, "AVI "))
    {
      /* This is an AVI chunk, which contains more chunks. Parse them! */
      return ParseChunks(pPluginInstance, uiChunkSize-4, iDepth);
    }
  }
  /* Could not parse it otherwise */
  return 0;
}

static int ParseChunks(AviDemuxInstance_p pPluginInstance, unsigned int uiMaxDataToParse, int iDepth)
{
  AviChunkHeader_t chunkHeader;
  unsigned int uiLastPos, uiCurrPos;
  int bParsed;

  uiCurrPos = pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance);
  if (uiCurrPos%2)
  {
    pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, 1, MMIO_MEDIA_SEEK_CUR);
    uiCurrPos++;
    uiMaxDataToParse--;
  }
  uiLastPos = uiCurrPos + uiMaxDataToParse;

  while (((uiMaxDataToParse==0) || (uiCurrPos<uiLastPos)) &&
         (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                   &chunkHeader,
                                                   sizeof(chunkHeader)) == MMIO_NOERROR))
  {
#ifdef PRINT_AVI_STRUCTURE
    { int i; for (i=0; i<iDepth; i++) printf(" "); }
    printf("Chunk [%.4s] size %d (end %d)\n", chunkHeader.achChunkId, chunkHeader.uiChunkSize, uiCurrPos+sizeof(chunkHeader)+chunkHeader.uiChunkSize);
#endif

    if (pPluginInstance->bVeryFirstChunk)
    {
      if (!IsFourCC(chunkHeader.achChunkId, "RIFF"))
      {
#ifdef PRINT_AVI_STRUCTURE
        printf("Doesn't seem to be an AVI file!\n");
#endif
        return 0;
      }
      pPluginInstance->bVeryFirstChunk = 0;
    }

    bParsed = 0;
    if (IsFourCC(chunkHeader.achChunkId, "RIFF"))
      bParsed = ParseRIFFChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "LIST"))
      bParsed = ParseListChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "avih"))
      bParsed = ParseAvihChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "strh"))
      bParsed = ParseStrhChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "strf"))
      bParsed = ParseStrfChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "strn"))
      bParsed = ParseStrnChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);
    else
    if (IsFourCC(chunkHeader.achChunkId, "idx1"))
      bParsed = ParseIdx1ChunkData(pPluginInstance, chunkHeader.uiChunkSize, iDepth+1);

    if (!bParsed)
    {
#ifdef PRINT_AVI_STRUCTURE
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("=== UNPROCESSED CHUNK ===\n");
#endif

      /* Go to end of this chunk */
      if (chunkHeader.uiChunkSize%2)
        chunkHeader.uiChunkSize++;
#ifdef DEBUG_BUILD
      pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, chunkHeader.uiChunkSize, MMIO_MEDIA_SEEK_CUR);
#else
      pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance, uiCurrPos + sizeof(chunkHeader) + chunkHeader.uiChunkSize, MMIO_MEDIA_SEEK_SET);
#endif
    }
#ifdef DEBUG_BUILD
    if (uiCurrPos + sizeof(chunkHeader) + chunkHeader.uiChunkSize != pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance))
    {
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("===== Yikes! diff is %d\n", uiCurrPos + sizeof(chunkHeader) + chunkHeader.uiChunkSize - pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance));
      { int i; for (i=0; i<iDepth; i++) printf(" "); }
      printf("===== Should be: %d, but it is %d\n", uiCurrPos + sizeof(chunkHeader) + chunkHeader.uiChunkSize, pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance));
    }
#endif
    uiCurrPos = pPluginInstance->pSource->mmiomedia_Tell(pPluginInstance->pSourceInstance);
  }
  return 1;
}

static void TranslateVideoFormatToString(unsigned int uiFormat, char *pchFormat, unsigned int uiFormatBufSize)
{
  snprintf(pchFormat, uiFormatBufSize, "%.4s", &uiFormat);
}

static void TranslateAudioFormatToString(unsigned int uiFormat, char *pchFormat, unsigned int uiFormatBufSize)
{
  int i;

  for (i=0; i<sizeof(aviDemuxAudioFormatTable); i++)
  {
    if (aviDemuxAudioFormatTable[i].uiFormatNum == uiFormat)
    {
      snprintf(pchFormat, uiFormatBufSize, "%s", aviDemuxAudioFormatTable[i].pchFormatName);
      return;
    }
  }
  /* Oops, did not find in table, so create a special string from it */
  snprintf(pchFormat, uiFormatBufSize, "%04x", uiFormat);
}

#ifdef PRINT_AVI_STRUCTURE
static void ShowAviFileDescStruct(AviDemuxInstance_p pPluginInstance)
{
  int i;
  int j, iNumKeys;
  char achCodec[128];
  unsigned int uiLength;

  printf("Important AVI flags:");
  if (pPluginInstance->aviHeader.uiFlags & AVIDEMUX_AVIHEADERFLAG_ISINTERLEAVED)
    printf(" INTERLEAVED");
  if (pPluginInstance->aviHeader.uiFlags & AVIDEMUX_AVIHEADERFLAG_TRUSTCKTYPE)
    printf(" TRUSTCKTYPE");
  printf("\n");
  printf("Number of streams: %d\n", pPluginInstance->uiNumStreams);
  for (i=0; i<pPluginInstance->uiNumStreams; i++)
  {
    printf("Stream-%02d :\n", i);
    printf("  NumOfIndexTableEntries: %d", pPluginInstance->pStreamDescriptions[i].iNumIndexTableEntries);

    iNumKeys=0;
    for (j=0; j<pPluginInstance->pStreamDescriptions[i].iNumIndexTableEntries; j++)
    {
      if (pPluginInstance->pStreamDescriptions[i].pIndexTable[j].uchFlags)
        iNumKeys++;
    }

    printf(" (Key frames: %d = %06.2f%%)\n", iNumKeys, (100.0*iNumKeys)/pPluginInstance->pStreamDescriptions[i].iNumIndexTableEntries);

    if (pPluginInstance->pStreamDescriptions[i].aviStreamHeader.achType[0] == 'v')
    {
      /* Video stream */
      printf("  Video Stream Info:\n");
      uiLength =
        pPluginInstance->pStreamDescriptions[i].iNumIndexTableEntries *
        1000LL *
        pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiScale /
        pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiRate;

      printf("    Length      : %d msec (%02d:%02d.%03d)\n",
             uiLength,
             uiLength / 1000 / 60,
             uiLength / 1000 % 60,
             uiLength % 1000);
      TranslateVideoFormatToString(pPluginInstance->pStreamDescriptions[i].formatSpecific.videoFormat.uiCompression,
                                   achCodec,
                                   sizeof(achCodec));
      printf("    CODEC       : [%s]\n", achCodec);
      printf("    Resolution  : %d x %d\n",
             pPluginInstance->pStreamDescriptions[i].formatSpecific.videoFormat.uiWidth,
             pPluginInstance->pStreamDescriptions[i].formatSpecific.videoFormat.uiHeight);
      printf("    FramePerSec : %9.3f (%d/%d)\n",
             1.0 *
             pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiRate /
             pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiScale,
             pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiRate,
             pPluginInstance->pStreamDescriptions[i].aviStreamHeader.uiScale);
    }
    if (pPluginInstance->pStreamDescriptions[i].aviStreamHeader.achType[0] == 'a')
    {
      /* Audio stream */
      printf("  Audio Stream Info:\n");
      uiLength = pPluginInstance->pStreamDescriptions[i].ullStreamLengthInBytes *
        1000LL /
        pPluginInstance->pStreamDescriptions[i].formatSpecific.audioFormat.uiAvgBytesPerSec;

      printf("    Length      : %d msec (%02d:%02d.%03d)\n",
             uiLength,
             uiLength / 1000 / 60,
             uiLength / 1000 % 60,
             uiLength % 1000);
      TranslateAudioFormatToString(pPluginInstance->pStreamDescriptions[i].formatSpecific.audioFormat.usFormatTag,
                                   achCodec,
                                   sizeof(achCodec));
      printf("    CODEC       : [%s]\n", achCodec);
      printf("    Sample rate : %d Hz\n", pPluginInstance->pStreamDescriptions[i].formatSpecific.audioFormat.uiSamplesPerSec);
      printf("    NumChannels : %d\n", pPluginInstance->pStreamDescriptions[i].formatSpecific.audioFormat.usNumChannels);
      printf("    Data rate   : %d KBits/sec\n", pPluginInstance->pStreamDescriptions[i].formatSpecific.audioFormat.uiAvgBytesPerSec * 8 / 1000);
    }
  }
}
#endif

static long long CalculateTimeStamp(AviDemuxStreamDesc_p pStream,
                                    int iStreamPosition)
{
  if (pStream->aviStreamHeader.achType[0] == 'v')
  {
    /* Video stream */
    return
      iStreamPosition *
      1000LL *
      pStream->aviStreamHeader.uiScale /
      pStream->aviStreamHeader.uiRate;
  }
  if (pStream->aviStreamHeader.achType[0] == 'a')
  {
    /* Audio stream */
    return
      pStream->pIndexTable[iStreamPosition].ullStreamPosInBytes *
      1000LL /
      pStream->formatSpecific.audioFormat.uiAvgBytesPerSec;
  }
  /* Otherwise unsupported stream type. */
  return 0LL;
}

static void FillDataDescFields(mmioDataDesc_p pDataDesc,
                               AviDemuxStreamDesc_p pStream,
                               int iStreamPosition,
                               long long llDataSize,
                               int iExtraStreamInfo)
{
  pDataDesc->llDataSize = llDataSize;
  pDataDesc->iExtraStreamInfo = iExtraStreamInfo;
  memcpy(&(pDataDesc->StreamInfo), &(pStream->streamInfo), sizeof(mmioStreamInfo_t));
  pDataDesc->llPTS = CalculateTimeStamp(pStream, iStreamPosition);
}

MMIOPLUGINEXPORT long         MMIOCALL avidemux_es_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  /* Message handler for both channels and elementary streams */
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT long         MMIOCALL avidemux_ch_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return avidemux_es_SendMsg(pInstance, lCommandWord, pParam1, pParam2);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_SetDirection(void *pInstance, void *pESID, int iDirection)
{
  AviDemuxStreamDesc_p pStream = pESID;

  if ((!pInstance) || (!pESID))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We do support every direction value! */
  pStream->iPlaybackDirection = iDirection;

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_GetOneFrame(void *pInstance, void *pESID, mmioDataDesc_p pDataDesc, void *pDataBuf, long long llDataBufSize)
{
  AviDemuxInstance_p pPluginInstance = pInstance;
  AviDemuxStreamDesc_p pStream = pESID;
  int iNextPos;

  if ((!pInstance) || (!pESID) || (!pDataDesc) || (!pDataBuf))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pStream->iPlaybackDirection<0)
  {
    /* Playing backwards: go backward but take only the keyframes which need no previous frame to be decoded! */

    /* Check if we've already given back the very first frame */
    if (pStream->iLastIndexTablePosition<=0)
    {
      /* No more frames! */
      return MMIO_ERROR_OUT_OF_DATA;
    }

    iNextPos = pStream->iLastIndexTablePosition-1;
    /* Okay, find a previous key frame then */
    while ((iNextPos>=0) && (!(pStream->pIndexTable[iNextPos].uchFlags & AVIDEMUX_FRAMEFLAGS_KEYFRAME)))
      iNextPos--;

    if (iNextPos<0)
    {
      /* No more frames! */
      return MMIO_ERROR_OUT_OF_DATA;
    }

    /* Otherwise we've found a keyframe */

    /* Check if we have a big enough buffer */
    if (pStream->pIndexTable[iNextPos].ulFrameSize > llDataBufSize)
      return MMIO_ERROR_BUFFER_TOO_SMALL;

    /* Read the frame into the buffer */
    tpl_mtxsemRequest(pPluginInstance->hmtxUseSource, TPL_WAIT_FOREVER);
    if (pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance,
                                                 pStream->pIndexTable[iNextPos].ullFileOffset,
                                                 MMIO_MEDIA_SEEK_SET) != MMIO_NOERROR)
    {
      tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);
      return MMIO_ERROR_IN_MEDIA_HANDLER;
    }
    if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                 pDataBuf,
                                                 pStream->pIndexTable[iNextPos].ulFrameSize) != MMIO_NOERROR)
    {
      tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);
      return MMIO_ERROR_IN_MEDIA_HANDLER;
    }
    tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);

    /* Fill in the datadesc structure */
    FillDataDescFields(pDataDesc,
                       pStream,
                       pStream->iLastIndexTablePosition,
                       pStream->pIndexTable[iNextPos].ulFrameSize,
                       ((pStream->iLastIndexTablePosition-iNextPos>1) || (pStream->bDidSeek)) ? MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY : 0);

    /* Set new stream position */
    pStream->iLastIndexTablePosition = iNextPos;
    pStream->bDidSeek = 0;

    /* And now return with success! */
    return MMIO_NOERROR;
  } else
  {
    /* Playing forward: go forward, take only keyframes if speed is more than or equal to double speed! */

    /* Check if we've already given back the very last frame */
    if (pStream->iLastIndexTablePosition >= pStream->iNumIndexTableEntries-1)
    {
      /* No more frames! */
      return MMIO_ERROR_OUT_OF_DATA;
    }

    iNextPos = pStream->iLastIndexTablePosition+1;
    if (pStream->iPlaybackDirection >= MMIO_DIRECTION_PLAY_DOUBLESPEED)
    {
      /* Okay, find anext key frame then */
      while ((iNextPos<=pStream->iNumIndexTableEntries-1) && (!(pStream->pIndexTable[iNextPos].uchFlags & AVIDEMUX_FRAMEFLAGS_KEYFRAME)))
        iNextPos++;
    }

    if (iNextPos>=pStream->iNumIndexTableEntries)
    {
      /* No more frames! */
      return MMIO_ERROR_OUT_OF_DATA;
    }

    /* Otherwise we've found a good frame */

    /* Check if we have a big enough buffer */
    if (pStream->pIndexTable[iNextPos].ulFrameSize > llDataBufSize)
      return MMIO_ERROR_BUFFER_TOO_SMALL;

    /* Read the frame into the buffer */
    tpl_mtxsemRequest(pPluginInstance->hmtxUseSource, TPL_WAIT_FOREVER);
    if (pPluginInstance->pSource->mmiomedia_Seek(pPluginInstance->pSourceInstance,
                                                 pStream->pIndexTable[iNextPos].ullFileOffset,
                                                 MMIO_MEDIA_SEEK_SET) != MMIO_NOERROR)
    {
      tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);
      return MMIO_ERROR_IN_MEDIA_HANDLER;
    }
    if (pPluginInstance->pSource->mmiomedia_Read(pPluginInstance->pSourceInstance,
                                                 pDataBuf,
                                                 pStream->pIndexTable[iNextPos].ulFrameSize) != MMIO_NOERROR)
    {
      tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);
      return MMIO_ERROR_IN_MEDIA_HANDLER;
    }
    tpl_mtxsemRelease(pPluginInstance->hmtxUseSource);

    /* Fill in the datadesc structure */
    FillDataDescFields(pDataDesc,
                       pStream,
                       pStream->iLastIndexTablePosition,
                       pStream->pIndexTable[iNextPos].ulFrameSize,
                       ((iNextPos-pStream->iLastIndexTablePosition>1) || (pStream->bDidSeek)) ? MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY : 0);

    /* Set new stream position */
    pStream->iLastIndexTablePosition = iNextPos;
    pStream->bDidSeek = 0;

    /* And now return with success! */
    return MMIO_NOERROR;
  }
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_GetStreamLength(void *pInstance, void *pESID, long long *pllLength)
{
  AviDemuxStreamDesc_p pStream = pESID;

  if ((!pInstance) || (!pESID) || (!pllLength))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pStream->aviStreamHeader.achType[0] == 'v')
  {
    /* Video stream */

    *pllLength =
      pStream->iNumIndexTableEntries *
      1000LL *
      pStream->aviStreamHeader.uiScale /
      pStream->aviStreamHeader.uiRate;

    return MMIO_NOERROR;
  } else
  if (pStream->aviStreamHeader.achType[0] == 'a')
  {
    /* Audio stream */

    *pllLength =
      pStream->ullStreamLengthInBytes *
      1000LL /
      pStream->formatSpecific.audioFormat.uiAvgBytesPerSec;

    return MMIO_NOERROR;
  } else
  {
    /* Unknown stream type, dunno how to calculate stream length... */
    /* So, guessing, and using the general method. */

    *pllLength =
      pStream->iNumIndexTableEntries *
      1000LL *
      pStream->aviStreamHeader.uiScale /
      pStream->aviStreamHeader.uiRate;

    return MMIO_NOERROR;
  }
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_GetTimeBase(void *pInstance, void *pESID, long long *pllFirstTimeStamp)
{
  /* The AVI file format has no starting timestamp information, only with */
  /* the ODML extenstion.                                                 */
  /* As I've not yet met an AVI file with that, it's not supported, so    */
  /* every stream of the AVI file will start with a timestamp of zero.    */

  if (!pllFirstTimeStamp)
    return MMIO_ERROR_INVALID_PARAMETER;

  *pllFirstTimeStamp = 0;

  return MMIO_NOERROR;
}

static long long GetFramePosition(AviDemuxStreamDesc_p pStream, int iPosition, int iPosType)
{
  if (iPosType == MMIO_POSTYPE_TIME)
    return CalculateTimeStamp(pStream, iPosition);
  if (iPosType == MMIO_POSTYPE_BYTE)
    return pStream->pIndexTable[iPosition].ullStreamPosInBytes;

  /* Unknown position type otherwise! */
  return 0;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_SetPosition(void *pInstance, void *pESID, long long llPos, int iPosType, long long *pllPosFound)
{
  long long llFirst, llLast, llMiddle;
  int iFirstPos, iLastPos, iMiddlePos;
  AviDemuxStreamDesc_p pStream = pESID;

  if ((!pInstance) || (!pESID) || (!pllPosFound))
    return MMIO_ERROR_INVALID_PARAMETER;

  if ((iPosType != MMIO_POSTYPE_TIME) && (iPosType != MMIO_POSTYPE_BYTE))
    return MMIO_ERROR_INVALID_PARAMETER;

  iFirstPos = 0;
  llFirst = 0;
  iLastPos = pStream->iNumIndexTableEntries-1;
  llLast = GetFramePosition(pStream, iLastPos, iPosType);

  /* Check if we're about to seek beyond first */
  if (llPos<=llFirst)
  {
    if (pStream->iPlaybackDirection>=0)
      pStream->iLastIndexTablePosition = iFirstPos-1;
    else
      pStream->iLastIndexTablePosition = iFirstPos+1;

    *pllPosFound = GetFramePosition(pStream, iFirstPos, iPosType);
    pStream->bDidSeek = 1;
    return MMIO_NOERROR;
  }
  /* Check if we're about to seek beyond last */
  if (llPos<=llFirst)
  {
    if (pStream->iPlaybackDirection>=0)
      pStream->iLastIndexTablePosition = iLastPos-1;
    else
      pStream->iLastIndexTablePosition = iLastPos+1;

    *pllPosFound = GetFramePosition(pStream, iLastPos, iPosType);
    pStream->bDidSeek = 1;
    return MMIO_NOERROR;
  }

  /* Find the closest keyframe! */
  while (abs(iFirstPos-iLastPos)>1)
  {
    iMiddlePos = (iLastPos + iFirstPos)/2;
    llMiddle = GetFramePosition(pStream, iMiddlePos, iPosType);
    if (llMiddle == llPos)
    {
      /* Just found it! */
      break;
    } else
    if (llMiddle<llPos)
    {
      iFirstPos = iMiddlePos+1;
    } else
    {
      iLastPos = iMiddlePos-1;
    }
  }

  /* Now find the nearest keyframe! */
  pStream->iLastIndexTablePosition = iMiddlePos;
  if (pStream->iPlaybackDirection>=0)
  {
    while ((pStream->iLastIndexTablePosition>0) &&
           (pStream->pIndexTable[pStream->iLastIndexTablePosition].uchFlags & AVIDEMUX_FRAMEFLAGS_KEYFRAME == 0))
      pStream->iLastIndexTablePosition--;

    *pllPosFound = GetFramePosition(pStream, pStream->iLastIndexTablePosition, iPosType);
    pStream->iLastIndexTablePosition--;
  } else
  {
    while ((pStream->iLastIndexTablePosition<pStream->iNumIndexTableEntries-1) &&
           (pStream->pIndexTable[pStream->iLastIndexTablePosition].uchFlags & AVIDEMUX_FRAMEFLAGS_KEYFRAME == 0))
      pStream->iLastIndexTablePosition++;

    *pllPosFound = GetFramePosition(pStream, pStream->iLastIndexTablePosition, iPosType);
    pStream->iLastIndexTablePosition++;
  }

  pStream->bDidSeek = 1;
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_DropFrames(void *pInstance, void *pESID, long long llAmount, int iPosType, long long *pllDropped)
{
  long long llOldPos;
  mmioResult_t rc;
  AviDemuxStreamDesc_p pStream = pESID;

  if ((!pInstance) || (!pESID) || (!pllDropped))
    return MMIO_ERROR_INVALID_PARAMETER;

  if ((iPosType != MMIO_POSTYPE_TIME) && (iPosType != MMIO_POSTYPE_BYTE))
    return MMIO_ERROR_INVALID_PARAMETER;

  llOldPos = GetFramePosition(pStream, pStream->iLastIndexTablePosition, iPosType);

  rc = avidemux_SetPosition(pInstance, pESID, llOldPos + llAmount, iPosType, pllDropped);
  if (rc == MMIO_NOERROR)
    *pllDropped -= llOldPos;

  return rc;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "AVI Demuxer v1.0", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL avidemux_Initialize()
{
  AviDemuxInstance_p pPluginInstance = NULL;

  pPluginInstance = (AviDemuxInstance_p) MMIOmalloc(sizeof(AviDemuxInstance_t));
  if (pPluginInstance == NULL)
    return NULL;

  memset(pPluginInstance, 0, sizeof(AviDemuxInstance_t));
  pPluginInstance->bAviHeaderFound = 0;
  pPluginInstance->uiNumStreams = 0;
  pPluginInstance->pCurrStreamDesc = NULL;
  pPluginInstance->pStreamDescriptions = NULL;
  pPluginInstance->bVeryFirstChunk = 1;

  /* Create mutex semaphore to handle concurrent access to source */
  pPluginInstance->hmtxUseSource = tpl_mtxsemCreate(0);

  return pPluginInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_Uninitialize(void *pInstance)
{
  AviDemuxInstance_p pPluginInstance = pInstance;
  unsigned int uiTemp;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free all our resources! */
  tpl_mtxsemDelete(pPluginInstance->hmtxUseSource);
  if (pPluginInstance->pStreamDescriptions)
  {
    for (uiTemp=0; uiTemp<pPluginInstance->uiNumStreams; uiTemp++)
    {
      if (pPluginInstance->pStreamDescriptions[uiTemp].pchName)
        MMIOfree(pPluginInstance->pStreamDescriptions[uiTemp].pchName);
      if (pPluginInstance->pStreamDescriptions[uiTemp].pIndexTable)
        MMIOfree(pPluginInstance->pStreamDescriptions[uiTemp].pIndexTable);
    }
    MMIOfree(pPluginInstance->pStreamDescriptions);
  }

  MMIOfree(pPluginInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  AviDemuxInstance_p pPluginInstance = pInstance;
  mmioMediaSpecificInfo_p pMediaInfo;
  unsigned int uiTemp;

  if ((!pInstance) || (!pNode) || (!ppExamineResult) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* We should examine the file here, to know what we can produce from it. */

  /* For the examination of AVI files, we need a media which can read, seek and tell. */
  pMediaInfo = pNode->pTypeSpecificInfo;
  if ((!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_READ)) ||
      (!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_SEEK)) ||
      (!(pMediaInfo->iMediaCapabilities & MMIO_MEDIA_CAPS_TELL)))
  {
    printf("[avidemux_Examine] : Media cannot be read!\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  /* So, let's try to parse the AVI file! */
  pPluginInstance->pSourceInstance = pNode->pNodeOwnerPluginInstance;
  pPluginInstance->pSource = pMediaInfo;

  if (
      (!ParseChunks(pPluginInstance, 0, 0)) ||
      (!pPluginInstance->bAviHeaderFound) ||
      (!pPluginInstance->uiNumStreams)
     )

  {
    printf("[avidemux_Examine] : ERROR: Could not parse AVI file.\n");
    return MMIO_ERROR_NOT_SUPPORTED;
  }

  /* We need index table for every stream! */
  for (uiTemp=0; uiTemp<pPluginInstance->uiNumStreams; uiTemp++)
    if (!pPluginInstance->pStreamDescriptions[uiTemp].iNumIndexTableEntries)
    {
      printf("[avidemux_Examine] : ERROR: No index table found for at least one of the streams in AVI file.\n");
      return MMIO_ERROR_NOT_SUPPORTED;
    }

#ifdef PRINT_AVI_STRUCTURE
  printf("\n[avidemux_Examine] : AVI File parsed successfully.\n\n");
  ShowAviFileDescStruct(pPluginInstance);
#endif

  (*ppExamineResult) = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc((*ppExamineResult)->iNumOfEntries * sizeof(mmioFormatDesc_t));

  /* Assemble the format specified from stream types we found! */
  memset(&((*ppExamineResult)->pOutputFormats[0]), 0, sizeof(mmioFormatDesc_t));
  for (uiTemp=0; uiTemp<pPluginInstance->uiNumStreams; uiTemp++)
  {
    mmioFormatDesc_t achTemp;
    char chType;
    char achFormat[64];

    /* Get stream type and format */
    switch (pPluginInstance->pStreamDescriptions[uiTemp].aviStreamHeader.achType[0])
    {
      case 'a':
        /* 'auds' */
        chType = 'a';
        TranslateAudioFormatToString(pPluginInstance->pStreamDescriptions[uiTemp].formatSpecific.audioFormat.usFormatTag,
                                     achFormat,
                                     sizeof(achFormat));

        break;
      case 'v':
        /* 'vids' */
        chType = 'v';
        TranslateVideoFormatToString(pPluginInstance->pStreamDescriptions[uiTemp].formatSpecific.videoFormat.uiCompression,
                                     achFormat,
                                     sizeof(achFormat));
        break;
      case 'm':
        /* 'midi' */
        chType = 'm';
        strcpy(achFormat, "MIDI");
        break;
      case 't':
        /* 'text' */
        chType = 's'; /* AVI Subtitle? */
        strcpy(achFormat, "AVI");
        break;
      default:
        chType = 'u'; /* Unknown */
        snprintf(achFormat, sizeof(achFormat),
                 "AVI_%.4s",
                 pPluginInstance->pStreamDescriptions[uiTemp].aviStreamHeader.achType);
        break;
    }

    snprintf(achTemp, sizeof(achTemp),
             "%ses_%c_%s",
             (uiTemp>0)?";":"",
             chType,
             achFormat);

    strlcat(&((*ppExamineResult)->pOutputFormats[0]), achTemp, sizeof(mmioFormatDesc_t));
  }

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  AviDemuxInstance_p pPluginInstance = pInstance;
  mmioProcessTreeNode_p     pNewCh, pNewES;
  mmioChannelSpecificInfo_p pChannelInfo;
  mmioESSpecificInfo_p      pESInfo;
  unsigned int              uiESNum;

  /* Check Params*/
  if ((!pInstance) || (!pchNeededOutputFormat) || (!pNode) || (pNode->iNodeType!=MMIO_NODETYPE_MEDIUM))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Might check the needed output format, if it matches the one we've created before (in Examine()), */
  /* but we trust it for now. :) */

  /* We'll create one channel with channel info, and some ES with ES info: */
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
  strncpy(pNewCh->achNodeOwnerOutputFormat, "ch_AVI", sizeof(pNewCh->achNodeOwnerOutputFormat));
  pNewCh->bUnlinkPoint = 1;
  pNewCh->iNodeType = MMIO_NODETYPE_CHANNEL;
  pNewCh->pTypeSpecificInfo = pChannelInfo;
  pChannelInfo->pChannelID = NULL;
  strncpy(pChannelInfo->achDescriptionText, "AVI File channel", sizeof(pChannelInfo->achDescriptionText));
  pChannelInfo->mmiochannel_SendMsg = avidemux_ch_SendMsg;

  /* Create the ES nodes then! */
  for (uiESNum = 0; uiESNum<pPluginInstance->uiNumStreams; uiESNum++)
  {
    pESInfo = MMIOmalloc(sizeof(mmioESSpecificInfo_t));
    pNewES = MMIOPsCreateAndLinkNewNodeStruct(pNewCh);
    if ((!pESInfo) || (!pNewES))
    {
      /* Out of memory */
      /* Could not create new node! */
      if (pESInfo)
        MMIOfree(pESInfo);
      if (pNewES)
        MMIOPsUnlinkAndDestroyNodeStruct(pNewES);

      while (pNewCh->pFirstChild)
      {
        MMIOfree(pNewCh->pFirstChild->pTypeSpecificInfo);
        MMIOPsUnlinkAndDestroyNodeStruct(pNewCh->pFirstChild);
      }
      MMIOfree(pChannelInfo);
      MMIOPsUnlinkAndDestroyNodeStruct(pNewCh);
      return MMIO_ERROR_OUT_OF_MEMORY;
    }

    /* Ok, fill the new nodee with information */

    /* Then the ES node */
    pNewES->bUnlinkPoint = 0;
    pNewES->iNodeType = MMIO_NODETYPE_ELEMENTARYSTREAM;
    pNewES->pTypeSpecificInfo = pESInfo;
    pESInfo->pESID = pPluginInstance->pStreamDescriptions+uiESNum;

    if (pPluginInstance->pStreamDescriptions[uiESNum].pchName)
      strncpy(pESInfo->achDescriptionText, pPluginInstance->pStreamDescriptions[uiESNum].pchName, sizeof(pESInfo->achDescriptionText));
    else
      snprintf(pESInfo->achDescriptionText, sizeof(pESInfo->achDescriptionText), "AVI Stream [%02d]", uiESNum);

    pESInfo->mmioes_SendMsg = avidemux_es_SendMsg;
    pESInfo->mmioes_SetDirection = avidemux_SetDirection;
    pESInfo->mmioes_SetPosition = avidemux_SetPosition;
    pESInfo->mmioes_GetOneFrame = avidemux_GetOneFrame;
    pESInfo->mmioes_GetStreamLength = avidemux_GetStreamLength;
    pESInfo->mmioes_DropFrames = avidemux_DropFrames;
    pESInfo->mmioes_GetTimeBase = avidemux_GetTimeBase;

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

    /* Set format-specific stuffs */
    switch (pPluginInstance->pStreamDescriptions[uiESNum].aviStreamHeader.achType[0])
    {
      case 'a':
        pESInfo->iStreamType = MMIO_STREAMTYPE_AUDIO;
        /* No problem what stream we give here, as everything depends on what the */
        /* audio decoder produces. So, we give here something. */
        pESInfo->StreamInfo.AudioStruct.iBits = 16;
        pESInfo->StreamInfo.AudioStruct.iChannels = pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.audioFormat.usNumChannels;
        pESInfo->StreamInfo.AudioStruct.iSampleRate = pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.audioFormat.uiSamplesPerSec;
        pESInfo->StreamInfo.AudioStruct.iIsSigned = 1;
        TranslateAudioFormatToString(pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.audioFormat.usFormatTag,
                                     pESInfo->achDescriptiveESFormat,
                                     sizeof(pESInfo->achDescriptiveESFormat));
        snprintf(pNewES->achNodeOwnerOutputFormat, sizeof(pNewES->achNodeOwnerOutputFormat),
                 "es_a_%s",
                 pESInfo->achDescriptiveESFormat);
        break;

      case 'v':
        pESInfo->iStreamType = MMIO_STREAMTYPE_VIDEO;
        /* No problem what stream we give here, as everything depends on what the */
        /* video decoder produces. So, we give here something. */
        pESInfo->StreamInfo.VideoStruct.iFPSCount = pPluginInstance->aviHeader.uiRate;
        pESInfo->StreamInfo.VideoStruct.iFPSDenom = pPluginInstance->aviHeader.uiScale;
        pESInfo->StreamInfo.VideoStruct.iWidth = pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.videoFormat.uiWidth;
        pESInfo->StreamInfo.VideoStruct.iHeight = pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.videoFormat.uiHeight;
        MMIOPsGuessPixelAspectRatio(pESInfo->StreamInfo.VideoStruct.iWidth,
                                    pESInfo->StreamInfo.VideoStruct.iHeight,
                                    &(pESInfo->StreamInfo.VideoStruct.iPixelAspectRatioCount),
                                    &(pESInfo->StreamInfo.VideoStruct.iPixelAspectRatioDenom));
        TranslateVideoFormatToString(pPluginInstance->pStreamDescriptions[uiESNum].formatSpecific.videoFormat.uiCompression,
                                     pESInfo->achDescriptiveESFormat,
                                     sizeof(pESInfo->achDescriptiveESFormat));
        snprintf(pNewES->achNodeOwnerOutputFormat, sizeof(pNewES->achNodeOwnerOutputFormat),
                 "es_v_%s",
                 pESInfo->achDescriptiveESFormat);
        break;
      default:
        pESInfo->iStreamType = MMIO_STREAMTYPE_OTHER;
        snprintf(pNewES->achNodeOwnerOutputFormat, sizeof(pNewES->achNodeOwnerOutputFormat),
                 "es_u_%s",
                 "UNKNOWN");
        break;
    }
  }

  return MMIO_NOERROR;
}

static void avidemux_internal_DestroyTree(mmioProcessTreeNode_p pRoot)
{
  if (!pRoot) return;

  if (pRoot->pFirstChild)
    avidemux_internal_DestroyTree(pRoot->pFirstChild);

  if (pRoot->pNextBrother)
    avidemux_internal_DestroyTree(pRoot->pNextBrother);

  MMIOfree(pRoot->pTypeSpecificInfo);
  MMIOPsUnlinkAndDestroyNodeStruct(pRoot);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL avidemux_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mmioResult_t iResult = MMIO_NOERROR;

  if ((!pInstance) || (!pNode))
    iResult = MMIO_ERROR_INVALID_PARAMETER;
  else
  {
    /* Free resources allocated at avidemux_Link: */
    /*   Nothing to do for this plugin here */

    /* Destroy all the levels we've created */
    avidemux_internal_DestroyTree(pNode);
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
    *ppchInternalName = "avidemux";
    *ppchSupportedFormats = "cont_AVI"; /* AVI file containers */
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
