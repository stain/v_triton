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
#include <io.h>
#include "MMIO.h"
#include "MMIOMem.h"
#include "tpl.h"

int main(int argc, char *argv[])
{
  mmioResult_t rc;
  mmioAvailablePluginList_p pPluginList, pPluginListEntry;
  struct _finddata_t fileinfo;
  long findhandle;
  int findrc;

  setbuf(stdout, NULL);
  setbuf(stderr, NULL);

  printf("* Initializing MMIO\n");
  rc = MMIOInitialize(".\\.MMIO");
  if (rc!=MMIO_NOERROR)
    printf("- Error initializing MMIO, rc is %d\n", rc);
  else
  {
    printf("* Registering file handler plugin by hand\n");
    rc = MMIORegisterPlugin("file", "mpfile.dll", "URL", 1000);
    if (rc!=MMIO_NOERROR)
      printf("- Error registering plugin, rc is %d\n", rc);

    printf("* Auto-registering all DLL files in .MMIO\n");
    findrc = findhandle = _findfirst(".MMIO\\*.dll", &fileinfo);
    while (findrc!=-1)
    {
      printf(" * Querying list of MMIO plugins inside file %s\n", fileinfo.name);
      rc = MMIOQueryPluginsOfBinary(fileinfo.name, &pPluginList);
      if (rc!=MMIO_NOERROR)
        printf(" - Error querying list of MMIO plugins, rc is %d\n", rc);
      else
      {
        pPluginListEntry = pPluginList;
        while (pPluginListEntry)
        {
          printf("  * Registering '%s' plugin of '%s'\n", pPluginListEntry->pchPluginInternalName, pPluginListEntry->pchPluginFileName);
          rc = MMIORegisterPlugin(pPluginListEntry->pchPluginInternalName,
                                  pPluginListEntry->pchPluginFileName,
                                  pPluginListEntry->pchSupportedFormats,
                                  pPluginListEntry->iPluginImportance);
          if (rc!=MMIO_NOERROR)
            printf("  - Error registering plugin, rc is %d\n", rc);

          pPluginListEntry = pPluginListEntry->pNext;
        }
        MMIOFreePluginsOfBinary(pPluginList);
      }

      /* Look for next DLL in there */
      findrc = _findnext(findhandle, &fileinfo);
    }
    _findclose(findhandle);

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

    printf("* Uninitializing MMIO\n");
    MMIOUninitialize(1);
  }

  printf("* All done, bye!\n");
  return 0;
}
