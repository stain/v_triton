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

#ifndef __AUDMIXER_H__
#define __AUDMIXER_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Include files                                                            */

#include "tpl.h"

/* ------------------------------------------------------------------------ */
/* Set structure packing to 4 byte boundaries                               */
#pragma pack(4)

/* ------------------------------------------------------------------------ */
/* Calling convention and import/export defines                             */

#define AUDMIXCALL _System

#ifdef BUILD_AUDMIXER
#define AUDMIXIMPEXP __declspec(dllexport)
#else
#define AUDMIXIMPEXP
#endif

/* ------------------------------------------------------------------------ */

/* Current version: v1.00 */
#define AUDMIX_VERSION_INT     100
#define AUDMIX_VERSION_STR   "1.00"

typedef struct audmixClient_s audmixClient_t, *audmixClient_p;

typedef struct audmixBufferFormat_s
{
  unsigned int  uiSampleRate;
  unsigned int  uiChannels;
  unsigned char uchBits;
  unsigned char uchSigned;
  unsigned char uchDoReversePlay;
  unsigned char uchReserved1;
  unsigned int  uiVolume;     /* 0..256, 0 is Mute, 256 is full volume */

} audmixBufferFormat_t, *audmixBufferFormat_p;

typedef struct audmixInfoBuffer_s
{
  int   iVersion;
  char *pchVersion;

  int   bIsDaemonRunning;

  unsigned int  uiPrimarySampleRate;
  unsigned int  uiPrimaryChannels;
  unsigned char uchPrimaryBits;
  unsigned char uchPrimarySigned;

  unsigned int  uiPrimaryNumBuffers;
  unsigned int  uiPrimaryBufSize;

  /* Might be extended later */
} audmixInfoBuffer_t, *audmixInfoBuffer_p;


AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetInformation(audmixInfoBuffer_p pInfoBuffer, unsigned int uiInfoBufferSize);

AUDMIXIMPEXP int            AUDMIXCALL  AudMixStartDaemonThread(unsigned int uiNumBuffers, unsigned int uiBufSize);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixStopDaemonThread();

AUDMIXIMPEXP audmixClient_p AUDMIXCALL  AudMixCreateClient(unsigned int uiNumBuffers, unsigned int uiBufSize);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixDestroyClient(audmixClient_p hClient);

AUDMIXIMPEXP int            AUDMIXCALL  AudMixResizeBuffer(audmixClient_p hClient, void **ppBuffer, unsigned int uiNewBufSize);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixAddNewBuffer(audmixClient_p hClient, unsigned int uiBufSize);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixDestroyBuffer(audmixClient_p hClient, void *pBuffer);
 
AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetEmptyBuffer(audmixClient_p hClient, void **ppBuffer, unsigned int *puiBufferSize, int iTimeOut);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixPutFullBuffer(audmixClient_p hClient, void *pBuffer, unsigned int uiDataInside, unsigned long long ullTimeStamp, unsigned long long ullTimeStampIncPerSec, audmixBufferFormat_p pfmtFormat);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixPutEmptyBuffer(audmixClient_p hClient, void *pBuffer);

AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetNumOfBuffers(audmixClient_p hClient, unsigned int *puiNumAllBufs, unsigned int *puiNumInSystem);
 
AUDMIXIMPEXP int            AUDMIXCALL  AudMixWaitForTimestampUpdate(int iTimeOut);
AUDMIXIMPEXP int            AUDMIXCALL  AudMixGetTimestampInfo(audmixClient_p hClient, unsigned long long *pullTimeStamp, tplTime_t *ptimTimeStampTime, tplTime_t *ptimTimeStampUpdateInterval);

/* ------------------------------------------------------------------------ */
/* Restore structure packing to compiler default                            */
#pragma pack()

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __AUDMIXER_H__ */
