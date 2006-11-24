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
#include "MMIO.h"
#include "MMIOMem.h"
#include "tpl.h"
#include "tpl_threadstatics.h"

int bEndOfStream;
int bShowPosition;
int bTrickyPlay;
mmioSystemTime_t timStartTime;
int bEventThreadShutdownRequest;
TPL_TID tidEventThread;

void TPLCALL EventThreadFunc(void *param)
{
  mmioStreamGroup_p pStreamGroup = (mmioStreamGroup_p) param;

  int bFromMainStream;
  int iEventCode;
  long long llEventParm;

  printf("* Event thread started\n");
  while (!bEventThreadShutdownRequest)
  {
    if (MMIOGetEvent(pStreamGroup, &bFromMainStream, &iEventCode, &llEventParm, 100) == MMIO_NOERROR)
    {
      switch (iEventCode)
      {
        case MMIO_STREAMGROUP_EVENT_OUT_OF_DATA:
        case MMIO_STREAMGROUP_EVENT_ERROR_IN_STREAM:
          printf("! Out Of Data or Error In Stream event\n");
          bEndOfStream = 1;
          printf("  (Setting Stream-Group direction to STOP in event processor thread)\n");
          MMIOSetDirection(pStreamGroup, MMIO_DIRECTION_STOP);
          break;

        case MMIO_STREAMGROUP_EVENT_POSITION_INFO:
          if ((bShowPosition) && (bFromMainStream))
          {
            long long llLength = 0;
            int iDirection = 0;
            mmioSystemTime_t timNow;

            timNow = MMIOPsGetCurrentSystemTime();
            MMIOGetLength(pStreamGroup, &llLength);
            MMIOGetDirection(pStreamGroup, &iDirection);
            printf("Position: %Ld (of %Ld), Elapsed time: %Ld, Direction: %d        \r",
                   llEventParm, llLength,
                   (timNow - timStartTime) * 1000LL / MMIOPsGetOneSecSystemTime(),
                   iDirection);
          }

          if (bTrickyPlay)
          {
            if (llEventParm>1000)
            {
              if (llEventParm<50000)
                MMIOSetDirection(pStreamGroup, 1000+(llEventParm/30));
              else
                if (llEventParm<110000)
                  MMIOSetDirection(pStreamGroup, 1000 + (100000 - llEventParm)/30);
                else
                  MMIOSetDirection(pStreamGroup, 1000);
            }
          }
          break;
        default:
          break;
      }
    } else
      tpl_schedDelay(100);
  }
  printf("* Event thread stopped\n");
}

void MMIOCALL ListSorter(int iListType, mmioProcessTreeNode_p pNode, int iNumEntries, void *pList)
{
  int i;
  mmioAvailablePluginList_p *pPluginList;
  char **pFormatList;

  printf("ListSorter was called for node %d (format %s)\n", pNode->iNodeType, pNode->achNodeOwnerOutputFormat);
  switch (iListType)
  {
    case MMIO_SORTLIST_LISTTYPE_PLUGINLIST:
      pPluginList = pList;
      printf("  Plugin list:\n");
      for (i=0; i<iNumEntries; i++)
        printf("    - %s (%s)\n", pPluginList[i]->pchPluginInternalName, pPluginList[i]->pchPluginFileName);
      break;
    case MMIO_SORTLIST_LISTTYPE_FORMATLIST:
      pFormatList = pList;
      printf("  Format list:\n");
      for (i=0; i<iNumEntries; i++)
        printf("    - %s\n", pFormatList[i]);
      break;
    default:
      printf("  Unknown list type: %d\n", iListType);
      break;
  }
}

int main(int argc, char *argv[])
{
  mmioResult_t rc;
  mmioAvailablePluginList_p pPluginList, pPluginListEntry;
  mmioProcessTreeNode_p pURL, pRoot;
  mmioStreamGroup_p pStreamGroup;

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  /* Process command line parameters */
  if (argc<2)
  {
    printf("Usage: mmiotest.exe <url> [pos] [trick]\n");
    return 1;
  }

  bShowPosition = 0;
  bTrickyPlay = 0;

  if (argc>2)
  {
    int i;
    for (i=2; i<argc; i++)
    {
      if (argv[i][0] == 'p')
        bShowPosition = 1;
      if (argv[i][0] == 't')
        bTrickyPlay = 1;
    }
  }

  printf("Parameters: ShowPosition=%d  TrickPlay = %d\n", bShowPosition, bTrickyPlay);
  printf("\n");
  printf("* Usable keypress commands when running:\n");
  printf("  ESC : Stop testing\n");
  printf("  >   : Seek forward 10 secs\n");
  printf("  <   : Seek backward 10 secs\n");
  printf("  f   : Set playback direction to forward\n");
  printf("  s   : Set playback direction to stop\n");
  printf("  b   : Set playback direction to backward\n");
  printf("  +   : Increase playback direction by 100\n");
  printf("  -   : Decrease playback direction by 100\n");
  printf("\n");

  printf("* Initializing MMIO\n");
  rc = MMIOInitialize(".\\.MMIO");
  if (rc!=MMIO_NOERROR)
    printf("- Error initializing MMIO, rc is %d\n", rc);

  /*
  printf("* Registering file handler plugin\n");
  rc = MMIORegisterPlugin("file", "pefile.dll", "URL", 1000);
  if (rc!=MMIO_NOERROR)
      printf("- Error registering plugin, rc is %d\n", rc);
  */

  printf("* Available plugins:\n");
  rc = MMIOQueryRegisteredPluginList(&pPluginList);
  if (rc != MMIO_NOERROR)
    printf("- Error querying plugin list, rc is %d\n", rc);
  else
  {
    pPluginListEntry = pPluginList;
    while (pPluginListEntry)
    {
      printf("  + Plugin: <%s> <%s> <%s> <%d>\n",
             pPluginListEntry->pchPluginInternalName,
             pPluginListEntry->pchPluginFileName,
             pPluginListEntry->pchSupportedFormats,
             pPluginListEntry->iPluginImportance);

      pPluginListEntry = pPluginListEntry->pNext;
    }
    /* Then free memory */
    MMIOFreeRegisteredPluginList(pPluginList);
  }

  printf("* Trying to open URL [%s]\n", argv[1]);
  rc = MMIOOpen(argv[1], MMIO_NODETYPE_TERMINATOR, ListSorter, &pURL);
  if (rc!=MMIO_NOERROR)
  {
    printf("- Error opening URL, rc is %d\n", rc);
    pURL = NULL;
  }

  if (pURL)
  {
    printf("* Showing URL\n");
    MMIOShowProcessTree(pURL);

    printf("* Creating Empty Stream-Group\n");
    MMIOCreateEmptyStreamGroup(&pStreamGroup);
    printf("* Adding streams to Stream-Group\n");
    MMIOAddStreamsToGroup(pStreamGroup, pURL);
    printf("* Showing Stream-Group\n");
    MMIOShowStreamGroup(pStreamGroup);

    printf("* Subscribing for events\n");
    MMIOSubscribeEvents(pStreamGroup, MMIO_EVENTCODE_ALL);

    /* Start event receiver thread so we can handle console events from this main thread */
    printf("* Starting event thread\n");
    bEventThreadShutdownRequest = 0;
    tidEventThread = tpl_threadCreate(16384,
                                      EventThreadFunc,
                                      pStreamGroup);

    bEndOfStream = 0;

    printf("* Setting Stream-Group direction to PLAY\n");
    rc = MMIOSetDirection(pStreamGroup, MMIO_DIRECTION_PLAY);

    if (rc==MMIO_NOERROR)
    {
      timStartTime = MMIOPsGetCurrentSystemTime();
      printf("* Waiting for end of stream condition...\n");
      while (!bEndOfStream)
      {
        while (kbhit())
        {
          int keystroke = getch();
          long long llPosition;
          int iDirection;

          switch (keystroke)
          {
            case 27: // ESC
              bEndOfStream = 1;
              break;
            case '>':
              printf("\n* Seeking forward 10 secs\n");
              MMIOGetPosition(pStreamGroup, &llPosition);
              MMIOSetPosition(pStreamGroup,
                              llPosition + 10 * 1000);
              break;
            case '<':
              printf("\n* Seeking backward 10 secs\n");
              MMIOGetPosition(pStreamGroup, &llPosition);
              MMIOSetPosition(pStreamGroup,
                              llPosition - 10 * 1000);
              break;
            case 'f':
              printf("\n* Setting playback direction to forward\n");
              MMIOSetDirection(pStreamGroup,
                               MMIO_DIRECTION_PLAY);
              break;
            case 'b':
              printf("\n* Setting playback direction to backward\n");
              MMIOSetDirection(pStreamGroup,
                               MMIO_DIRECTION_REVERSE);
              break;
            case 's':
              printf("\n* Setting playback direction to stop\n");
              MMIOSetDirection(pStreamGroup,
                               MMIO_DIRECTION_STOP);
              break;
            case '+':
              printf("\n* Increasing playback direction by 100\n");
              MMIOGetDirection(pStreamGroup, &iDirection);
              MMIOSetDirection(pStreamGroup,
                               iDirection + 100);
              break;
            case '-':
              printf("\n* Decreasing playback direction by 100\n");
              MMIOGetDirection(pStreamGroup, &iDirection);
              MMIOSetDirection(pStreamGroup,
                               iDirection - 100);
              break;
            default:
              printf("Unknown keypress command: %c\n", keystroke);
              break;
          }
        }
        tpl_schedDelay(100);
      }

      printf("* Setting Stream-Group direction to STOP\n");
      MMIOSetDirection(pStreamGroup, MMIO_DIRECTION_STOP);
    } else
      printf("- Error setting direction, rc is %d\n", rc);

    printf("* Waiting for event thread to stop\n");
    bEventThreadShutdownRequest = 1;
    tpl_threadWaitForDie(tidEventThread, TPL_WAIT_FOREVER);

    printf("* Destroying Stream-Group\n");
    MMIODestroyStreamGroup(pStreamGroup);

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
