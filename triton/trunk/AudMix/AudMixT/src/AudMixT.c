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
#include <math.h>
#include <limits.h>
#include "tpl.h"
#include "AudMixer.h"

#ifndef M_PI
#define M_PI 3.1415
#endif

int main(int argc, char *argv[])
{
  audmixInfoBuffer_t AudMixInfo;
  int rc;
  int iCounter;
  audmixClient_p hAudMixClient;
  void *pBuffer;
  unsigned int uiBufferSize;
  audmixBufferFormat_t fmtBufferFormat;
  double dPos, dStep;
  unsigned long long ullPlaybackPos;


  rc = AudMixGetInformation(&AudMixInfo, sizeof(AudMixInfo));
  if (!rc)
  {
    printf("* Error querying Audio Mixer Information!\n");
    return 1;
  }

  printf("Using mixer: %s\n", AudMixInfo.pchVersion);

  if (AudMixInfo.iVersion<AUDMIX_VERSION_INT)
  {
    printf("* Audio Mixer is too old! Wanted v%d.%02d, found v%d.%02d\n",
           AUDMIX_VERSION_INT/10, AUDMIX_VERSION_INT%10,
           AudMixInfo.iVersion/10, AudMixInfo.iVersion%10);
    return 1;
  }

  if (!AudMixInfo.bIsDaemonRunning)
  {
    printf("* Audio Mixer Daemon is not running!\n");
    return 1;
  }

  fmtBufferFormat.uiSampleRate = AudMixInfo.uiPrimarySampleRate;
  fmtBufferFormat.uiChannels = AudMixInfo.uiPrimaryChannels;;
  fmtBufferFormat.uchBits = AudMixInfo.uchPrimaryBits;
  fmtBufferFormat.uchSigned = AudMixInfo.uchPrimarySigned;
  fmtBufferFormat.uchDoReversePlay = 0;
  fmtBufferFormat.uchReserved1 = 0;
  fmtBufferFormat.uiVolume = 256;


  printf("* Creating new AudMix client.\n");
  hAudMixClient = AudMixCreateClient(AudMixInfo.uiPrimaryNumBuffers*2, 16384);
  rc = (hAudMixClient!=NULL);
  printf("  rc is %d\n", rc);

  if (!rc)
    return 1;

  printf("* Filling buffers.\n");
  dPos = 0;
  dStep = 440.0*360.0/44100.0*M_PI/180;

  ullPlaybackPos = 0;
  for (iCounter = 0; iCounter<200; iCounter++)
  {
    rc = AudMixGetEmptyBuffer(hAudMixClient,
                              &pBuffer,
                              &uiBufferSize, 250);
    if (rc)
    {
      int i;
      signed short int *piBuffer;
      /*
      unsigned long long ullTimeStamp;
      tplTime_t timTimeStampTime;
      tplTime_t timTimeStampUpdateInterval;
      */

      printf("* Got an empty buffer 0x%p\n", pBuffer);

      /*
      if (iCounter == 100)
      {
        printf("* Resizing a buffer\n");
        rc = AudMixResizeBuffer(hAudMixClient, &pBuffer, uiBufferSize*2);
        printf("* Resizing was done, rc is %d\n", rc);
      }

      if (iCounter == 120)
      {
        printf("* Adding new buffer\n");
        rc = AudMixAddNewBuffer(hAudMixClient, uiBufferSize);
        printf("* New buffer was added, rc is %d\n", rc);
      }

      AudMixGetTimestampInfo(hAudMixClient,
                             &ullTimeStamp,
                             &timTimeStampTime,
                             &timTimeStampUpdateInterval);
      printf("* Daemon Playback is currently at %d msec\n", (int) ullTimeStamp);
      */


      uiBufferSize = 4608;

      piBuffer = (short int *) pBuffer;
      i=0;

      while (i<uiBufferSize/4)
      {
        *piBuffer = (short int) (SHRT_MAX*sin(dPos));
        piBuffer++;
        *piBuffer = (short int) (SHRT_MAX*sin(dPos));
        piBuffer++;
        dPos += dStep;
        i++;
      }

      fmtBufferFormat.uiVolume = 256-iCounter;
      fmtBufferFormat.uiSampleRate -= iCounter*3;
      if (fmtBufferFormat.uiSampleRate < 1000)
        fmtBufferFormat.uiSampleRate = 1000;

      ullPlaybackPos +=
        1000 *
        uiBufferSize /
        (fmtBufferFormat.uiSampleRate*((fmtBufferFormat.uchBits+7)/8)*fmtBufferFormat.uiChannels)
        ;

      printf("* Sending a full buffer (size is %d)\n", uiBufferSize);


      rc = AudMixPutFullBuffer(hAudMixClient,
                               pBuffer,
                               uiBufferSize,
                               ullPlaybackPos, 1000,
                               &fmtBufferFormat);
      printf("* Sent a full buffer (rc is %d), pos %d\n", rc, (int) ullPlaybackPos);

    }
    else
      printf("* Did not get an empty buffer.\n");

  }

  printf("* Waiting 5 secs\n");
  DosSleep(5*1000);

#if 0
  /* Forget to destroy client, to test cleanup stuff of AudMixer.dll */
  printf("* Destroying AudMix client\n");
  rc = AudMixDestroyClient(hAudMixClient);
  printf("  rc is %d\n", rc);
#endif

  printf("* All done, bye!\n");
  return 0;
}
