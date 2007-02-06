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
#include <conio.h>
#include <string.h>
#include "MMIO.h"
#include "MMIOMem.h"
#include "tpl.h"

/* Plugin-specific stuffs: */
#include "MPMP3Dem\mpmp3dem.h"


int  MMIOCALL NodeProcessFunc(mmioProcessTreeNode_p pNode, void *pUserData)
{
  if (pNode->iNodeType == MMIO_NODETYPE_ELEMENTARYSTREAM)
  {
    mmioESSpecificInfo_p pElementaryStreamInfo;
    static int iStreamNo = 0;

    /* We have found the raw stream node, so print info about it! */
    iStreamNo++;

    printf("Stream-%02d :\n", iStreamNo);

    /* Get the stream info structure */
    pElementaryStreamInfo = pNode->pTypeSpecificInfo;
    if (!pElementaryStreamInfo)
    {
      printf("  No information about the stream.\n");
    } else
    {
      printf("  Stream format:      [%s]\n", pElementaryStreamInfo->achDescriptiveESFormat);
      printf("  Stream description: [%s]\n", pElementaryStreamInfo->achDescriptionText);

      switch (pElementaryStreamInfo->iStreamType)
      {
        case MMIO_STREAMTYPE_AUDIO:
          printf("  Audio format: %d Hz, %d bits, %d channel(s)\n",
                 pElementaryStreamInfo->StreamInfo.AudioStruct.iSampleRate,
                 pElementaryStreamInfo->StreamInfo.AudioStruct.iBits,
                 pElementaryStreamInfo->StreamInfo.AudioStruct.iChannels);
          break;
        case MMIO_STREAMTYPE_VIDEO:
          printf("  Image resolution is: %dx%d\n",
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iWidth,
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iHeight);
          printf("  Playback speed: %.2f FPS (%d / %d)\n",
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iFPSCount * 1.0 /
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iFPSDenom,
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iFPSCount,
                 pElementaryStreamInfo->StreamInfo.VideoStruct.iFPSDenom);
          break;
        default:
          break;
      }

      /* Some extra, format-specific info might also be got */
      if (strcmp(pNode->pNodeOwnerPluginHandle->pchPluginInternalName, "mp3demux")==0)
      {
        mp3demuxMP3TAG_t *pMp3Tag;
        /* This node is handled by the mp3 demuxer plugin, so we can query extra info like */
        /* mp3 tags */

        if (pElementaryStreamInfo->mmioes_SendMsg(pNode->pNodeOwnerPluginInstance,
                                                  MMIO_MP3DEMUX_MSG_GETTAG,
                                                  &pMp3Tag, NULL) == MMIO_NOERROR)
        {
          printf("  MP3 Tag:\n");
          printf("    Title  : [%s]\n", pMp3Tag->achTitle);
          printf("    Artist : [%s]\n", pMp3Tag->achArtist);
          printf("    Album  : [%s]\n", pMp3Tag->achAlbum);
          printf("    Year   : [%s]\n", pMp3Tag->achYear);
          printf("    Comment: [%s]\n", pMp3Tag->achComment);
        }
      }
    }
  }

  return 1; /* Returning 1 means go and walk more in the tree */
}

int main(int argc, char *argv[])
{
  mmioResult_t rc;
  mmioProcessTreeNode_p pURL;

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  /* Process command line parameters */
  if (argc<2)
  {
    printf("Usage: urlinfo.exe <url>\n");
    return 1;
  }

  rc = MMIOInitialize(".\\.MMIO");
  if (rc!=MMIO_NOERROR)
  {
    printf("- Error initializing MMIO, rc is %d\n", rc);
    return 1;
  }

  /* Open the URL, but build the process tree only 'till the ES nodes! */
  printf("* Opening URL [%s]\n", argv[1]);
  rc = MMIOOpen(argv[1], MMIO_NODETYPE_RAWSTREAM, NULL, &pURL);
  if (rc!=MMIO_NOERROR)
  {
    printf("- Error opening URL, rc is %d\n", rc);
    pURL = NULL;
  }

  if (pURL)
  {
    /* Walk the tree to find the raw stream nodes, and print info about them */
    MMIOWalkProcessTree(pURL, NodeProcessFunc, NULL);

    /* Close the URL, destroy the Process Tree */
    rc = MMIOClose(pURL);
    if (rc!=MMIO_NOERROR)
      printf("- Error closing URL, rc is %d\n", rc);
  }

  MMIOUninitialize(1);
  
  return 0;
}
