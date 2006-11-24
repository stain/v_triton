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

int  MMIOCALL NodeProcessFunc(mmioProcessTreeNode_p pNode, void *pUserData)
{
  printf("  . NodeProcessFunc: called for node '%s'\n",
         pNode->achNodeOwnerOutputFormat);
  if ((pNode->iNodeType == MMIO_NODETYPE_RAWSTREAM) &&
      (!strcmp(pNode->achNodeOwnerOutputFormat, "rs_v_RGBA")))
  {
    /* We have found the raw stream node we're looking for, so save the */
    /* node pointer and stop the walking (return 0) */
    mmioProcessTreeNode_p *pResult = pUserData;
    *pResult = pNode;
    return 0;
  }

  return 1; /* Returning 1 means go and walk more in the tree */
}

int main(int argc, char *argv[])
{
  mmioResult_t rc;
  mmioProcessTreeNode_p pURL, pRoot, pRawImageNode;

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  /* Process command line parameters */
  if (argc<2)
  {
    printf("Usage: imagetest.exe <url>\n");
    return 1;
  }


  printf("* Initializing MMIO\n");
  rc = MMIOInitialize(".\\.MMIO");
  if (rc!=MMIO_NOERROR)
    printf("- Error initializing MMIO, rc is %d\n", rc);

  printf("* Trying to open URL [%s]\n", argv[1]);
  rc = MMIOOpen(argv[1], MMIO_NODETYPE_RAWSTREAM, NULL, &pURL);
  if (rc!=MMIO_NOERROR)
  {
    printf("- Error opening URL, rc is %d\n", rc);
    pURL = NULL;
  }

  if (pURL)
  {
    printf("* Showing URL\n");
    MMIOShowProcessTree(pURL);

    printf("* Walking the tree to find the RGBA raw stream\n");
    pRawImageNode = NULL;
    rc = MMIOWalkProcessTree(pURL, NodeProcessFunc, &pRawImageNode);
    if ((rc == MMIO_ERROR_WALK_INTERRUPTED) && (pRawImageNode))
    {
      mmioRSSpecificInfo_p pRawStreamInfo;
      int   iImageNum;
      char *pchDecodedImageBuffer;
      int   iDecodedImageBufferSize;

      printf("* Found the RGBA node\n");

      /* Get the stream info structure */
      pRawStreamInfo = pRawImageNode->pTypeSpecificInfo;
      printf("  Image resolution is: %dx%d\n",
             pRawStreamInfo->StreamInfo.VideoStruct.iWidth,
             pRawStreamInfo->StreamInfo.VideoStruct.iHeight);

      printf("* Decode and save image(s)\n");

      /* Allocate a buffer where the decoded image will be put */
      iDecodedImageBufferSize =
        pRawStreamInfo->StreamInfo.VideoStruct.iWidth *
        pRawStreamInfo->StreamInfo.VideoStruct.iHeight * 4;
      pchDecodedImageBuffer = MMIOmalloc(iDecodedImageBufferSize);

      if (pchDecodedImageBuffer)
      {
        /* Set the fields of decodeFormatRequest to tell exactly what */
        /* RGBA pixel format we want */
        iImageNum = 0;
        while (MMIOGetOneSimpleRawFrame(pRawImageNode,
                                        NULL, /* We're not interested in the format of decoded image now */
                                        pchDecodedImageBuffer,
                                        iDecodedImageBufferSize,
                                        NULL) == MMIO_NOERROR) /* The default decoded format will be ok for now */
        {
          char achFileName[64];
          FILE *hFile;

          snprintf(achFileName, sizeof(achFileName), "img_%02d.raw", iImageNum);

          printf("- Saving decoded frame to '%s'\n", achFileName);

          hFile = fopen(achFileName, "wb");
          if (hFile)
          {
            fwrite(pchDecodedImageBuffer,
                   iDecodedImageBufferSize,
                   1,
                   hFile);
            fclose(hFile);
          }
          iImageNum++;
        }
        MMIOfree(pchDecodedImageBuffer); pchDecodedImageBuffer = NULL;
      } else
      {
        printf("! Out of memory, could not allocate image buffer!\n");
      }


    } else
    {
      printf("* The RGBA node was not found (rc is %d)\n", rc);
    }

    printf("* Close URL\n");
    rc = MMIOClose(pURL);
    if (rc!=MMIO_NOERROR)
      printf("- Error closing URL, rc is %d\n", rc);
  }

  printf("* Showing root URL\n");
  MMIOGetRootNode(&pRoot);
  MMIOShowProcessTree(pRoot);

  printf("* Uninitializing MMIO\n");
  MMIOUninitialize(1);
  
  printf("* All done, bye!\n");
  return 0;
}
