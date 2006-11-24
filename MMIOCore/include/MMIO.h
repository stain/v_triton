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

#ifndef __MMIO_H__
#define __MMIO_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Include the porting layer defines                                        */

#ifdef BUILD_MMIO
/* Also define BUILD_TPL so we get the build-time defines for TPLIMPEXP     */
#define BUILD_TPL
#endif

#include "tpl.h"

/* ------------------------------------------------------------------------ */
/* Set structure packing to 4 byte boundaries                               */
#pragma pack(4)

/* ------------------------------------------------------------------------ */
/* Take calling convention and function exporting from Porting Layer        */

#define MMIOCALL          TPLCALL
#define MMIOIMPEXP        TPLIMPEXP

#define MMIOPLUGINEXPORT  TPLEXPORT

/* ------------------------------------------------------ Error codes ----- */
typedef int mmioResult_t;

#define MMIO_NOERROR                               0
#define MMIO_ERROR_UNKNOWN                         1
#define MMIO_ERROR_NOT_INITIALIZED                 2
#define MMIO_ERROR_INVALID_PARAMETER               3
#define MMIO_ERROR_OUT_OF_MEMORY                   4
#define MMIO_ERROR_OUT_OF_RESOURCES                5
#define MMIO_ERROR_NOT_IMPLEMENTED                 6
#define MMIO_ERROR_NOT_SUPPORTED                   7
#define MMIO_ERROR_ALREADY_REGISTERED              8
#define MMIO_ERROR_NOT_FOUND                       9
#define MMIO_ERROR_NOT_PLUGIN                     10
#define MMIO_ERROR_IN_PLUGIN                      11
#define MMIO_ERROR_TIMEOUT                        12
#define MMIO_ERROR_NO_STREAM_IN_GROUP             13
#define MMIO_ERROR_WRONG_URL                      14
#define MMIO_ERROR_NO_SUCH_MEDIAHANDLER           15
#define MMIO_ERROR_IN_MEDIA_HANDLER               16
#define MMIO_ERROR_OUT_OF_DATA                    17
#define MMIO_ERROR_BUFFER_TOO_SMALL               18
#define MMIO_ERROR_NEED_MORE_BUFFERS              19
#define MMIO_ERROR_BUFFER_NOT_USED                20
#define MMIO_ERROR_REQUESTED_FORMAT_NOT_SUPPORTED 21
#define MMIO_ERROR_WALK_INTERRUPTED               22

/* ------------------------------------------------------ Node types ------ */
#define MMIO_NODETYPE_ROOT                         0
#define MMIO_NODETYPE_URL                          1
#define MMIO_NODETYPE_MEDIUM                       2
#define MMIO_NODETYPE_CHANNEL                      3
#define MMIO_NODETYPE_ELEMENTARYSTREAM             4
#define MMIO_NODETYPE_RAWSTREAM                    5
#define MMIO_NODETYPE_TERMINATOR                   6

/* ------------------------------------ Some common stream directions ----- */
#define MMIO_DIRECTION_REVERSE_DOUBLESPEED     -2000
#define MMIO_DIRECTION_REVERSE                 -1000
#define MMIO_DIRECTION_REVERSE_HALFSPEED        -500
#define MMIO_DIRECTION_STOP                        0
#define MMIO_DIRECTION_PLAY_HALFSPEED            500
#define MMIO_DIRECTION_PLAY                     1000
#define MMIO_DIRECTION_PLAY_DOUBLESPEED         2000

/* ---------------------------------------------- Stream group events ----- */
#define MMIO_STREAMGROUP_EVENT_ALL                   -1
#define MMIO_STREAMGROUP_EVENT_NOP                    0
#define MMIO_STREAMGROUP_EVENT_DIRECTION_CHANGED      1
#define MMIO_STREAMGROUP_EVENT_POSSIBLE_FLICKER       2
#define MMIO_STREAMGROUP_EVENT_FRAMES_DROPPED         4
#define MMIO_STREAMGROUP_EVENT_POSITION_INFO          8
#define MMIO_STREAMGROUP_EVENT_OUT_OF_DATA           16
#define MMIO_STREAMGROUP_EVENT_ERROR_IN_STREAM       32


/* ------------------------------------------------------------------------ */

/* MMIO Global System Time type: */
typedef unsigned long long mmioSystemTime_t;

/* MMIO Format Description type: */
#define MMIO_FORMATDESC_LEN_MAX   255
typedef char mmioFormatDesc_t[MMIO_FORMATDESC_LEN_MAX+1];

/* MMIO Plugin Description type: */
#define MMIO_PLUGINDESC_LEN_MAX   255
typedef char mmioPluginDescription_t[MMIO_PLUGINDESC_LEN_MAX+1];

/* Forward definitions of some structures: */
typedef struct mmioProcessTreeNode_s mmioProcessTreeNode_t, *mmioProcessTreeNode_p;
typedef struct mmioStreamGroup_s mmioStreamGroup_t, *mmioStreamGroup_p;

/* Structure returned by the Examine function of plugins: */
typedef struct mmioNodeExamineResult_s
{
  int               iNumOfEntries;
  mmioFormatDesc_t *pOutputFormats;

} mmioNodeExamineResult_t, *mmioNodeExamineResult_p;

/* Plugin functions exported by all MMIO Plugins: */
typedef mmioResult_t MMIOCALL (*pfn_MMIOQueryPluginInfoForRegistry)(int iPluginIndex, char **ppchInternalName, char **ppchSupportedFormats, int *piImportance);
typedef mmioResult_t MMIOCALL (*pfn_MMIOFreePluginInfoForRegistry)(char *pchInternalName, char *pchSupportedFormats);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_GetPluginDesc)(char *pchDescBuffer, int iDescBufferSize);
typedef void *       MMIOCALL (*pfn_mmiomod_Initialize)(void);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_Examine)(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_FreeExamineResult)(void *pInstance, mmioNodeExamineResult_p pExamineResult);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_Link)(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_Unlink)(void *pInstance, mmioProcessTreeNode_p pNode);
typedef mmioResult_t MMIOCALL (*pfn_mmiomod_Uninitialize)(void *pInstance);

/* Loaded plugin descriptor (and part of a list of all loaded plugins) */
/* Used mostly by LoadPlugin() and UnloadPlugin() functions */
typedef struct mmioLoadedPluginList_s
{
  char                          *pchPluginFileName;      /* Filename of plugin as it was used to load it */
  char                          *pchPluginInternalName;  /* Internal name of plugin, from plugin registry file */
  int                            iRefCount;              /* Reference counter for the plugin (number of opens) */

  mmioPluginDescription_t        achPluginDescription;   /* Short description string about the plugin (name, version) */

  TPL_HMODULE                    hModule;                /* Internal plugin handle used by the Porting Layer to later unload it from memory */

  pfn_mmiomod_GetPluginDesc      mmiomod_GetPluginDesc;  /* Common plugin functions */
  pfn_mmiomod_Initialize         mmiomod_Initialize;
  pfn_mmiomod_Examine            mmiomod_Examine;
  pfn_mmiomod_FreeExamineResult  mmiomod_FreeExamineResult;
  pfn_mmiomod_Link               mmiomod_Link;
  pfn_mmiomod_Unlink             mmiomod_Unlink;
  pfn_mmiomod_Uninitialize       mmiomod_Uninitialize;

  void                          *pNext;                  /* Next plugin in list (for internal use only!) */

} mmioLoadedPluginList_t, *mmioLoadedPluginList_p;

/* The Process Tree Node descriptor structure */
typedef struct mmioProcessTreeNode_s
{
  int                      iTreeLevel;                   /* Zero-based value, incrementing with each new level */

  mmioLoadedPluginList_p   pNodeOwnerPluginHandle;       /* The plugin who handles this node */
  void                    *pNodeOwnerPluginInstance;     /* Instance of the owner plugin for this node */
  mmioFormatDesc_t         achNodeOwnerOutputFormat;     /* Output format of this node */

  int                      bUnlinkPoint;                 /* Is this node an unlink-point? */
  int                      bPluginWasLoadedBySystem;     /* Was the owner plugin for this node loaded by the system or the user? */

  int                      iNodeType;                    /* Node type (See MMIO_NODETYPE_* defines) */
  void                    *pTypeSpecificInfo;            /* Type-specific structure pointer */

  TPL_MTXSEM               hmtxUseOwnerStreamGroupField; /* Mutex to serialize access to pOwnerStreamGroup field */
  mmioStreamGroup_p        pOwnerStreamGroup;            /* The owner StreamGroup, or NULL if the node is not part of a StreamGroup */

  mmioProcessTreeNode_p    pParent;                      /* Parent node */
  mmioProcessTreeNode_p    pFirstChild;                  /* Head of children linked list */
  mmioProcessTreeNode_p    pNextBrother;                 /* Next brother (next pointer of children linked list of parent) */
} mmioProcessTreeNode_t;

/* List of available plugins and their supported formats */
/* Used by QueryRegisteredPluginList() */
typedef struct mmioAvailablePluginList_s
{
  char *pchPluginInternalName;                          /* Plugin internal name */
  char *pchPluginFileName;                              /* Filename used to load the plugin */
  char *pchSupportedFormats;                            /* The supported format or formats */
  int   iPluginImportance;                              /* Importance value for the plugin (the smaller is the more important, 1000 is the default) */

  void *pNext;                                          /* Linked list Next pointer*/
} mmioAvailablePluginList_t, *mmioAvailablePluginList_p;

/* Number of maximum events in message queue: */
#define MMIO_EVENT_MESSAGE_QUEUE_SIZE       128

/* Event mask to get all events */
#define MMIO_EVENTCODE_ALL                   -1
/* Direction change event: */
#define MMIO_EVENTCODE_DIRECTION_CHANGED      1
/* Flicker event: */
#define MMIO_EVENTCODE_POSSIBLE_FLICKER       2
/* Frames dropped: */
#define MMIO_EVENTCODE_FRAMES_DROPPED         4
/* Direction change event: */
#define MMIO_EVENTCODE_POSITION_INFO          8
/* Out of data: */
#define MMIO_EVENTCODE_OUT_OF_DATA           16
/* Error in stream: */
#define MMIO_EVENTCODE_ERROR_IN_STREAM       32


/* Element of the EventMessageQueue. Used internally. */
typedef struct mmioEventMessageQueueElement_s
{
  int                           iEventCode;
  int                           bFromMainStream;
  long long                     llEventParm;
} mmioEventMessageQueueElement_t, *mmioEventMessageQueueElement_p;

/* List of streams of a StreamGroup: */
typedef struct mmioStreamGroupStreamList_s
{
  mmioProcessTreeNode_p         pNode;             /* The Terminator node, representing the stream itself */

  mmioSystemTime_t              LastTimeStampTime; /* Time in system-time when the last timestamp (llLastTimeStamp field) was reported by the stream */
  long long                     llLastTimeStamp;   /* Last timestamp of stream, in milliseconds */
  long long                     llLastFrameLength; /* Length in milliseconds of the last frame/buffer */

  void                         *pNext;            /* Pointer to next element (stream) of linked list (stream group) */
} mmioStreamGroupStreamList_t, *mmioStreamGroupStreamList_p;

/* The StreamGroup descriptor structure: */
typedef struct mmioStreamGroup_s
{
  TPL_MTXSEM                    hmtxUseStreamGroup;   /* Mutex semaphore to serialize access to stream group internals */

  int                           iDirection;           /* Current playback direction of streams of this stream group */

  mmioStreamGroupStreamList_p   pMainStream;          /* Pointer to one of the stream group streams which is treated as the main stream, to which all the others are synchronizing */
  mmioStreamGroupStreamList_p   pStreamList;          /* Linked list of streams of the stream group (also contains pMainStream) */

  int                           iTermCapabilities;    /* Contains the combined capabilities of all streams inside. */

  TPL_MSGQ                      hmqEventMessageQueue; /* Message queue for stream group events */
  int                           iSubscribedEvents;    /* The events which should be posted into the queue (see MMIO_EVENTCODE_* defines) */
} mmioStreamGroup_t, *mmioStreamGroup_p;

/* ======================================================================== */
/* ------------------------------------------ Common MMIO Defines --------- */
/* ======================================================================== */


/* ES and RS stream types: */
#define MMIO_STREAMTYPE_AUDIO       0
#define MMIO_STREAMTYPE_VIDEO       1
#define MMIO_STREAMTYPE_SUBTITLE    2
#define MMIO_STREAMTYPE_OTHER     255

/* Description maximum lengths: */
#define MMIO_NODE_DESCRIPTION_LEN_MAX  511
#define MMIO_URL_LEN_MAX               511

/* SetPosition parameters: */
#define MMIO_POSTYPE_TIME        0
#define MMIO_POSTYPE_BYTE        1

/* Extra Stream Info defines for GetOneFrame(): */
/* (Make sure that these can be OR'd together!) */
#define MMIO_EXTRASTREAMINFO_NOTHING                    0
#define MMIO_EXTRASTREAMINFO_STREAM_DISCONTINUITY       1

/* General Stream Info structure, telling what kind of stream it is: */
/* (Currently only supports Audio and Video streams ) */
typedef union
{
  /* Audio frame, StreamInfo structure: */
  struct
  {
    int           iSampleRate; /* Audio Sample Rate (in Hz) */
    int           iIsSigned;   /* Are audio samples Signed or Unsigned integers? */
    int           iBits;       /* Bit depth of one audio sample (usually 16) */
    int           iChannels;   /* Number of audio channels (2 for Stereo) */
  } AudioStruct;

  /* Video frame, StreamInfo structure: */
  struct
  {
    int           iFPSCount;   /* Number of Frames Per Second. The final value can be calculated this way: */
    int           iFPSDenom;   /* iFPS = iFPSCount / iFPSDenom; */
    int           iWidth;      /* Width of one video frame in pixels */
    int           iHeight;     /* Height of one video frame in pixels */
    int           iPixelAspectRatioCount; /* Counter of pixel aspect ratio */
    int           iPixelAspectRatioDenom; /* Denominator of pixel aspect ratio */
  } VideoStruct;
} mmioStreamInfo_t;
typedef mmioStreamInfo_t *mmioStreamInfo_p;

/* DataDescriptor structure, going together with every frame of every stream: */
typedef struct mmioDataDesc_s
{
  long long        llPTS;            /* Presentation Time Stamp of data, if available, or -1 if PTS not available */
  long long        llDataSize;       /* Number of bytes actually put into the buffer by the GetOneFrame() function */

  mmioStreamInfo_t StreamInfo;       /* Describes the parameters of the ES/RS data in the buffer.
                                      * Note, that these parameters can change dynamically, for example,
                                      * audio data can change from 44KHz to 22KHz, or video frame resolution can change on the fly!
                                      * To detect these changes, one should always check if these parameters
                                      * are the same as the parameters of the previous data chunk!
                                      */
  int              iExtraStreamInfo; /* Information about the elementary stream. See MMIO_EXTRASTREAMINFO_* defines! */
} mmioDataDesc_t, *mmioDataDesc_p;


/* ======================================================================== */
/* ----------------------- Type-specific node descriptor structures ------- */
/* ======================================================================== */

/* --------------------------------------------- URL node ----------------- */

/* Type-specific info structure: */
typedef struct mmioURLSpecificInfo_s
{
  char  achURL[MMIO_URL_LEN_MAX+1];
} mmioURLSpecificInfo_t, *mmioURLSpecificInfo_p;

/* --------------------------------------------- Media node --------------- */

/* Type-specific functions: */
typedef  long         MMIOCALL (*pfn_mmiomedia_SendMsg)(void *pInstance, long lCommandWord, void *pParam1, void *pParam2);
typedef  mmioResult_t MMIOCALL (*pfn_mmiomedia_Read)(void *pInstance, void *pBuffer, long lBytes);
typedef  mmioResult_t MMIOCALL (*pfn_mmiomedia_Seek)(void *pInstance, long long llPosition, int iBase);
typedef  long long    MMIOCALL (*pfn_mmiomedia_Tell)(void *pInstance);
typedef  mmioResult_t MMIOCALL (*pfn_mmiomedia_ReadPacket)(void *pInstance, void *pBuffer, long lBytes, long *plRead);

/* Defines for the mmiomedia_Seek() function: */
#define MMIO_MEDIA_SEEK_SET    0
#define MMIO_MEDIA_SEEK_CUR    1
#define MMIO_MEDIA_SEEK_END    2

/* Media node capability flags: */
#define MMIO_MEDIA_CAPS_READ          1
#define MMIO_MEDIA_CAPS_SEEK          2
#define MMIO_MEDIA_CAPS_TELL          4
#define MMIO_MEDIA_CAPS_READPACKET    8

/* Type-specific info structure: */
typedef struct mmioMediaSpecificInfo_s
{
  int                        iMediaCapabilities;

  pfn_mmiomedia_SendMsg      mmiomedia_SendMsg;
  pfn_mmiomedia_Read         mmiomedia_Read;
  pfn_mmiomedia_Seek         mmiomedia_Seek;
  pfn_mmiomedia_Tell         mmiomedia_Tell;
  pfn_mmiomedia_ReadPacket   mmiomedia_ReadPacket;

} mmioMediaSpecificInfo_t, *mmioMediaSpecificInfo_p;

/* --------------------------------------------- Channel node ------------- */

/* Type-specific functions: */
typedef  long MMIOCALL (*pfn_mmiochannel_SendMsg)(void *pInstance, long lCommandWord, void *pParam1, void *pParam2);

/* Type-specific info structure: */
typedef struct mmioChannelSpecificInfo_s
{
  void    *pChannelID;              /* Unique ID of channel */
  char    achDescriptionText[MMIO_NODE_DESCRIPTION_LEN_MAX+1]; /* Name of channel, or similar */
  pfn_mmiochannel_SendMsg   mmiochannel_SendMsg;

} mmioChannelSpecificInfo_t, *mmioChannelSpecificInfo_p;

/* --------------------------------------------- ES node ------------------ */

/* Type-specific functions: */
typedef  long         MMIOCALL (*pfn_mmioes_SendMsg)(void *pInstance, long lCommandWord, void *pParam1, void *pParam2);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_SetDirection)(void *pInstance, void *pESID, int iDirection);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_SetPosition)(void *pInstance, void *pESID, long long llPos, int iPosType, long long *pllPosFound);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_GetOneFrame)(void *pInstance, void *pESID, mmioDataDesc_p pDataDesc, void *pDataBuf, long long llDataBufSize);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_GetStreamLength)(void *pInstance, void *pESID, long long *pllLength);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_DropFrames)(void *pInstance, void *pESID, long long llAmount, int iPosType, long long *pllDropped);
typedef  mmioResult_t MMIOCALL (*pfn_mmioes_GetTimeBase)(void *pInstance, void *pESID, long long *pllFirstTimeStamp);

/* Elementary Stream node capability flags: */
#define MMIO_ES_CAPS_PTS                         1
#define MMIO_ES_CAPS_DIRECTION_CUSTOMREVERSE     2
#define MMIO_ES_CAPS_DIRECTION_REVERSE           4
#define MMIO_ES_CAPS_DIRECTION_STOP              8
#define MMIO_ES_CAPS_DIRECTION_PLAY             16
#define MMIO_ES_CAPS_DIRECTION_CUSTOMPLAY       32
#define MMIO_ES_CAPS_SETPOSITION                64
#define MMIO_ES_CAPS_DROPFRAMES                128

/* Type-specific info structure: */
typedef struct mmioESSpecificInfo_s
{
  void              *pESID;           /* Unique ID of elementary stream */
  mmioFormatDesc_t   achDescriptiveESFormat; /* Descriptive format string of elementary stream format, like this: PCM_44100_U16_2CH */
  char               achDescriptionText[MMIO_NODE_DESCRIPTION_LEN_MAX+1]; /* Language or similar info */

  int                iStreamType;     /* Type of this stream. See MMIO_STREAMTYPE_* defines for valid stream types */
  mmioStreamInfo_t   StreamInfo;      /* Describes the general parameters of this stream */

  int                iESCapabilities; /* Capabilities of the Elementary Stream Node */

  pfn_mmioes_SendMsg                mmioes_SendMsg;
  pfn_mmioes_SetDirection           mmioes_SetDirection;
  pfn_mmioes_SetPosition            mmioes_SetPosition;
  pfn_mmioes_GetOneFrame            mmioes_GetOneFrame;
  pfn_mmioes_GetStreamLength        mmioes_GetStreamLength;
  pfn_mmioes_DropFrames             mmioes_DropFrames;
  pfn_mmioes_GetTimeBase            mmioes_GetTimeBase;

} mmioESSpecificInfo_t, *mmioESSpecificInfo_p;

/* --------------------------------------------- RS node ------------------ */

/* Raw Stream, raw frame format request structure: */
/*  This is used for video frames, where the format string itself is not descriptive enough.*/
/*  Here is an example: The video overlay hardware usually supports YUV420 video frames.    */
/*                      However, the scanline width and some more details are HW-specific,  */
/*                      so this structure can be used to ask the decoder to decode in the   */
/*                      given requested format at once, if possible.                        */
typedef union
{
  /* Audio frame, format request: Not used yet. */
  /*
   struct {
   int           iReserved;
  } AudioStruct;
  */

  /* Video frame, format request: */
  #define MMIO_RS_FORMATREQUEST_VIDEO_NUMFIELDS_MAX 4
  /* The order is either YUV or RGBA */
  struct
  {
    int  aiFieldStartOffset[MMIO_RS_FORMATREQUEST_VIDEO_NUMFIELDS_MAX]; /* Field start offset from base pointer */
    int  aiFieldNextPixel[MMIO_RS_FORMATREQUEST_VIDEO_NUMFIELDS_MAX];   /* Number of bytes to skip to get to next pixel in same field */
    int  aiFieldPitch[MMIO_RS_FORMATREQUEST_VIDEO_NUMFIELDS_MAX];       /* Number of bytes to skip to get to bottom pixel (next line) in the same field */
  } VideoStruct;

} mmioRSFormatRequest_t;
typedef mmioRSFormatRequest_t *mmioRSFormatRequest_p;

/* Type-specific functions: */
typedef  long         MMIOCALL (*pfn_mmiors_SendMsg)(void *pInstance, long lCommandWord, void *pParam1, void *pParam2);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_SetDirection)(void *pInstance, void *pRSID, int iDirection);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_SetPosition)(void *pInstance, void *pRSID, long long llPos, int iPosType, long long *pllPosFound);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_GetOneFrame)(void *pInstance, void *pRSID, mmioDataDesc_p pDataDesc, void **ppDataBuf, long long llDataBufSize, mmioRSFormatRequest_p pRequestedFormat);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_ReleaseForeignBuffers)(void *pInstance, void *pRSID);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_GetStreamLength)(void *pInstance, void *pRSID, long long *pllLength);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_DropFrames)(void *pInstance, void *pRSID, long long llAmount, int iPosType, long long *pllDropped);
typedef  mmioResult_t MMIOCALL (*pfn_mmiors_GetTimeBase)(void *pInstance, void *pRSID, long long *pllFirstTimeStamp);

/* Raw Stream node capability flags: */
#define MMIO_RS_CAPS_PTS                         1
#define MMIO_RS_CAPS_DIRECTION_CUSTOMREVERSE     2
#define MMIO_RS_CAPS_DIRECTION_REVERSE           4
#define MMIO_RS_CAPS_DIRECTION_STOP              8
#define MMIO_RS_CAPS_DIRECTION_PLAY             16
#define MMIO_RS_CAPS_DIRECTION_CUSTOMPLAY       32
#define MMIO_RS_CAPS_SETPOSITION                64
#define MMIO_RS_CAPS_DROPFRAMES                128

/* Type-specific info structure: */
typedef struct mmioRSSpecificInfo_s
{
  void             *pRSID;       /* Unique ID of raw stream */
  mmioFormatDesc_t  achDescriptiveRSFormat; /* Descriptive format string of raw stream format, like this: PCM_44100_U16_2CH */
  char              achDescriptionText[MMIO_NODE_DESCRIPTION_LEN_MAX+1]; /* Language or similar info */

  int               iStreamType; /* Type of this stream. See MMIO_STREAMTYPE_* defines for valid stream types */
  mmioStreamInfo_t  StreamInfo;  /* Describes the general parameters of this stream */

  int               iRSCapabilities; /* Capabilities of the Raw Stream */

  pfn_mmiors_SendMsg                mmiors_SendMsg;
  pfn_mmiors_SetDirection           mmiors_SetDirection;
  pfn_mmiors_SetPosition            mmiors_SetPosition;
  pfn_mmiors_GetOneFrame            mmiors_GetOneFrame;
  pfn_mmiors_ReleaseForeignBuffers  mmiors_ReleaseForeignBuffers;
  pfn_mmiors_GetStreamLength        mmiors_GetStreamLength;
  pfn_mmiors_DropFrames             mmiors_DropFrames;
  pfn_mmiors_GetTimeBase            mmiors_GetTimeBase;

} mmioRSSpecificInfo_t, *mmioRSSpecificInfo_p;

/* --------------------------------------------- Terminator node ---------- */

/* Type-specific functions: */
typedef  long         MMIOCALL (*pfn_mmioterm_SendMsg)(void *pInstance, long lCommandWord, void *pParam1, void *pParam2);
typedef  mmioResult_t MMIOCALL (*pfn_mmioterm_SetDirection)(void *pInstance, void *pTermID, int iDirection);
typedef  mmioResult_t MMIOCALL (*pfn_mmioterm_SetPosition)(void *pInstance, void *pTermID, long long llPos, int iPosType, long long *pllPosFound);
typedef  mmioResult_t MMIOCALL (*pfn_mmioterm_GetStreamLength)(void *pInstance, void *pTermID, long long *pllLength);
typedef  mmioResult_t MMIOCALL (*pfn_mmioterm_SetTimeOffset)(void *pInstance, void *pTermID, long long llTimeOffset);

/* Terminator node capability flags: */
#define MMIO_TERM_CAPS_PTS                         1
#define MMIO_TERM_CAPS_DIRECTION_CUSTOMREVERSE     2
#define MMIO_TERM_CAPS_DIRECTION_REVERSE           4
#define MMIO_TERM_CAPS_DIRECTION_STOP              8
#define MMIO_TERM_CAPS_DIRECTION_PLAY             16
#define MMIO_TERM_CAPS_DIRECTION_CUSTOMPLAY       32
#define MMIO_TERM_CAPS_SETPOSITION                64

/* Type-specific info structure: */
typedef struct mmioTermSpecificInfo_s
{
  void             *pTermID;     /* Unique ID of terminator node / stream. */
  char              achDescriptionText[MMIO_NODE_DESCRIPTION_LEN_MAX+1]; /* Language or similar info */

  int               iStreamType; /* Type of this stream. See MMIO_STREAMTYPE_* defines for valid stream types */
  mmioStreamInfo_t  StreamInfo;  /* Describes the general parameters of this stream */

  int               iTermCapabilities; /* Capabilities of the Terminator node */

  pfn_mmioterm_SendMsg                mmioterm_SendMsg;
  pfn_mmioterm_SetDirection           mmioterm_SetDirection;
  pfn_mmioterm_SetPosition            mmioterm_SetPosition;
  pfn_mmioterm_GetStreamLength        mmioterm_GetStreamLength;
  pfn_mmioterm_SetTimeOffset          mmioterm_SetTimeOffset;

} mmioTermSpecificInfo_t, *mmioTermSpecificInfo_p;


/* ======================================================================== */
/* --------------------------------------------- MMIO Functions ----------- */
/* ======================================================================== */

/* The ListSorter callback function of the MMIOOpen() API: */
#define MMIO_SORTLIST_LISTTYPE_PLUGINLIST   0
#define MMIO_SORTLIST_LISTTYPE_FORMATLIST   1
typedef void MMIOCALL (*pfn_mmioopen_SortList)(int iListType, mmioProcessTreeNode_p pNode, int iNumEntries, void *pList);

/* The Node processor callback function of the MMIOWalkProcessTree() API: */
typedef int  MMIOCALL (*pfn_mmiowalkprocesstree_ProcessNode)(mmioProcessTreeNode_p pNode, void *pUserData);

/* --------------------------------------------- High-level API ----------- */

MMIOIMPEXP mmioResult_t MMIOCALL MMIOInitialize(char *pchHomeDirectory);
MMIOIMPEXP void         MMIOCALL MMIOUninitialize(int bShowMemoryLeaks);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOOpen(char *pchURL, int iOpenLevel, pfn_mmioopen_SortList pfnSortList, mmioProcessTreeNode_p *ppURL);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOClose(mmioProcessTreeNode_p pURL);

/* --------------------------------------------- Plugin-list handling ----- */

MMIOIMPEXP mmioResult_t MMIOCALL MMIOQueryRegisteredPluginList(mmioAvailablePluginList_p *ppPluginList);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreeRegisteredPluginList(mmioAvailablePluginList_p pPluginList);
MMIOIMPEXP mmioResult_t MMIOCALL MMIORegisterPlugin(char *pchPluginInternalName, char *pchPluginFileName, char *pchSupportedFormats, int iImportance);
MMIOIMPEXP mmioResult_t MMIOCALL MMIODeregisterPlugin(char *pchPluginInternalName, char *pchPluginFileName, char *pchSupportedFormats, int iImportance);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOQueryPluginsOfBinary(char *pchPluginFileName, mmioAvailablePluginList_p *ppPluginList);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreePluginsOfBinary(mmioAvailablePluginList_p pPluginList);

/* --------------------------------------------- Low-level API ------------ */

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetRootNode(mmioProcessTreeNode_p *ppRoot);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOLoadPlugin(char *pchPluginInternalName, char *pchPluginFileName, mmioLoadedPluginList_p *phPlugin);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOUnloadPlugin(mmioLoadedPluginList_p hPlugin);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOInitializePlugin(mmioLoadedPluginList_p hPlugin, void **ppInstance);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOUninitializePlugin(mmioLoadedPluginList_p hPlugin, void *pInstance);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOExamineNodeWithPlugin(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                           mmioProcessTreeNode_p pNode,
                                                           mmioNodeExamineResult_p *ppExamineResult);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreeExamineResult(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                       mmioNodeExamineResult_p pExamineResult);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOLinkPluginToNode(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                      char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOUnlinkNode(mmioProcessTreeNode_p pNode);

/* --------------------------------------------- Utilities ---------------- */

MMIOIMPEXP void         MMIOCALL MMIOShowProcessTree(mmioProcessTreeNode_p pStartNode);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOWalkProcessTree(mmioProcessTreeNode_p pStartNode, pfn_mmiowalkprocesstree_ProcessNode pfnProcessNode, void *pUserData);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetOneSimpleRawFrame(mmioProcessTreeNode_p pRawStreamNode,
                                                          mmioDataDesc_p pDataDesc,
                                                          void *pDataBuf,
                                                          long long llDataBufSize,
                                                          mmioRSFormatRequest_p pRequestedFormat);

/* --------------------------------------------- StreamGroup routines ----- */

MMIOIMPEXP mmioResult_t MMIOCALL MMIOCreateEmptyStreamGroup(mmioStreamGroup_p *ppNewStreamGroup);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOAddStreamsToGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pStartNode);
MMIOIMPEXP mmioResult_t MMIOCALL MMIORemoveStreamFromGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pNodeToRemove);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetMainStreamOfGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pNode);
MMIOIMPEXP mmioResult_t MMIOCALL MMIODestroyStreamGroup(mmioStreamGroup_p pStreamGroup);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetDirection(mmioStreamGroup_p pStreamGroup, int iDirection);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetPosition(mmioStreamGroup_p pStreamGroup, long long llPosition);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetDirection(mmioStreamGroup_p pStreamGroup, int *piDirection);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetPosition(mmioStreamGroup_p pStreamGroup, long long *pllPosition);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetLength(mmioStreamGroup_p pStreamGroup, long long *pllLength);

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSubscribeEvents(mmioStreamGroup_p pStreamGroup, long long llEventMask);
MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetEvent(mmioStreamGroup_p pStreamGroup, int *pbFromMainStream, int *piEventCode, long long *pllEventParm, int iTimeOut);

/* --------------------------------------------- StreamGroup Utilities ---- */

MMIOIMPEXP mmioResult_t MMIOCALL MMIOShowStreamGroup(mmioStreamGroup_p pStreamGroup);


/* ======================================================================== */
/* -------------------- Plugin Support Routines / Plugin Helper API ------- */
/* ======================================================================== */

MMIOIMPEXP mmioSystemTime_t      MMIOCALL MMIOPsGetCurrentSystemTime();
MMIOIMPEXP mmioSystemTime_t      MMIOCALL MMIOPsGetOneSecSystemTime();

MMIOIMPEXP mmioProcessTreeNode_p MMIOCALL MMIOPsCreateNewEmptyNodeStruct();
MMIOIMPEXP mmioProcessTreeNode_p MMIOCALL MMIOPsCreateAndLinkNewNodeStruct(mmioProcessTreeNode_p pParent);
MMIOIMPEXP mmioResult_t          MMIOCALL MMIOPsDestroyNodeStruct(mmioProcessTreeNode_p pNode);
MMIOIMPEXP mmioResult_t          MMIOCALL MMIOPsUnlinkAndDestroyNodeStruct(mmioProcessTreeNode_p pNode);

MMIOIMPEXP void                  MMIOCALL MMIOPsGuessPixelAspectRatio(int iImageWidth, int iImageHeight, int *piPixelAspectRatioCount, int *piPixelAspectRatioDenom);

MMIOIMPEXP mmioResult_t          MMIOCALL MMIOPsReportPosition(mmioProcessTreeNode_p pNode,
                                                               mmioSystemTime_t      timPositionReached,
                                                               long long            llPosition,
                                                               long long            llFrameLength,
                                                               long long           *pllSyncDiff);
MMIOIMPEXP mmioResult_t          MMIOCALL MMIOPsReportEvent(mmioProcessTreeNode_p pNode,
                                                            int                  iEventCode,
                                                            long long            llEventParm);

/* ------------------------------------------------------------------------ */
/* Restore structure packing to compiler default                            */
#pragma pack()

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __MMIO_H__ */
