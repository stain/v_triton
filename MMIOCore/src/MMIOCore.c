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
#include <direct.h> /* For mkdir() */
#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"
#include "MMIOMemInternals.h"

#define MMIO_REGISTRY_FILENAME  "MMIOReg.cfg"
#define MMIO_REGISTRY_DEFAULT_HEADER \
  "#\n"\
  "# Multimedia Input Output support (Triton) - Plugin Registry File\n"\
  "#\n"\
  "# Don't edit this file by hand unless you know what you do!\n"\
  "#\n"\
  "# Format of lines:\n"\
  "# <InternalName> <FileName> <Importance> <SupportedFormats>\n"\
  "\n"


#define MMIO_STREAMGROUP_MQ_SIZE 128


typedef struct mmioStreamGroupList_s
{
  mmioStreamGroup_p   pStreamGroup;
  void               *pNext;
} mmioStreamGroupList_t, *mmioStreamGroupList_p;

typedef struct mmioUsablePluginList_s
{
  mmioAvailablePluginList_p  pPlugin;
  void                      *pNext;
} mmioUsablePluginList_t, *mmioUsablePluginList_p;


static int                     bInitialized = 0;
static char                   *pchGlobalHomeDirectory;
static TPL_MTXSEM              hmtxUseLoadedPluginList;
static mmioLoadedPluginList_p  pLoadedPluginListHead;
static TPL_MTXSEM              hmtxUseRootNode;
static mmioProcessTreeNode_p   pRootNode;
static TPL_MTXSEM              hmtxUseStreamGroupList;
static mmioStreamGroupList_p   pStreamGroupListHead;

MMIOIMPEXP mmioResult_t MMIOCALL MMIOInitialize(char *pchHomeDirectory)
{
  int bNeedEnding;
  int bNeedSubDir;
  int iExtraBytes;
  int iLen;

  /* Don't reinitialize! */
  if (bInitialized)
    return MMIO_NOERROR;

  /* Initialize the memory tracking subsystem first */
  MMIOMemInitialize();

  iExtraBytes = 0;
  bNeedEnding = 0;
  bNeedSubDir = 0;

  /* Set default home directory if needed */
  if (!pchHomeDirectory)
  {
    pchHomeDirectory = getenv("HOME");
    if (!pchHomeDirectory)
      pchHomeDirectory = ".";
    else
    {
      iExtraBytes += 6; /* length of ".MMIO\" */
      bNeedSubDir = 1;
    }
  }

  /* Make sure path has the ending */
  iLen = strlen(pchHomeDirectory);
  if (pchHomeDirectory[iLen-1]!=TPL_PATH_SEPARATOR)
  {
    iExtraBytes++;
    bNeedEnding = 1;
  }

  /* Allocate memory for local copy */
  pchGlobalHomeDirectory = MMIOmalloc(iLen+iExtraBytes+1);
  if (!pchGlobalHomeDirectory)
  {
    /* Ooops, out of memory! */
    MMIOMemUninitialize();
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Make the copy and put the slash at the end if needed! */
  strcpy(pchGlobalHomeDirectory, pchHomeDirectory);
  if (bNeedEnding)
  {
    iLen = strlen(pchGlobalHomeDirectory);
    pchGlobalHomeDirectory[iLen] = TPL_PATH_SEPARATOR;
    pchGlobalHomeDirectory[iLen+1] = 0;
  }

  /* Also put there the subdirectory if needed */
  if (bNeedSubDir)
  {
    strcat(pchGlobalHomeDirectory, ".MMIO");
    /* Make sure this subdirectory exists */
    mkdir(pchGlobalHomeDirectory);
    /* Put there the ending path delimiter */
    strcat(pchGlobalHomeDirectory, "\\");
  }

  /* Create the root node */
  hmtxUseRootNode = tpl_mtxsemCreate(0);
  if (!hmtxUseRootNode)
  {
    /* Could not create mutex to protect root node! */
    MMIOfree(pchGlobalHomeDirectory); pchGlobalHomeDirectory = NULL;
    MMIOMemUninitialize();
    return MMIO_ERROR_OUT_OF_RESOURCES;
  }

  pRootNode = MMIOPsCreateNewEmptyNodeStruct();

  if (!pRootNode)
  {
    /* Out of memory! */
    tpl_mtxsemDelete(hmtxUseRootNode);
    MMIOfree(pchGlobalHomeDirectory); pchGlobalHomeDirectory = NULL;
    MMIOMemUninitialize();
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  strncpy(pRootNode->achNodeOwnerOutputFormat, "system", sizeof(pRootNode->achNodeOwnerOutputFormat));
  pRootNode->bPluginWasLoadedBySystem = 0;
  pRootNode->bUnlinkPoint = 0;
  pRootNode->iNodeType = MMIO_NODETYPE_ROOT;
  pRootNode->pTypeSpecificInfo = NULL;

  /* Create default loaded plugin list */
  pLoadedPluginListHead = NULL;
  hmtxUseLoadedPluginList = tpl_mtxsemCreate(0);
  if (!hmtxUseLoadedPluginList)
  {
    /* Could not create mutex to protect plugin list! */
    MMIOPsDestroyNodeStruct(pRootNode); pRootNode = NULL;
    tpl_mtxsemDelete(hmtxUseRootNode);
    MMIOfree(pchGlobalHomeDirectory); pchGlobalHomeDirectory = NULL;
    MMIOMemUninitialize();
    return MMIO_ERROR_OUT_OF_RESOURCES;
  }

  /* Create default stream group list */
  pStreamGroupListHead = NULL;
  hmtxUseStreamGroupList = tpl_mtxsemCreate(0);
  if (!hmtxUseStreamGroupList)
  {
    /* Could not create mutex to protect stream group list! */
    tpl_mtxsemDelete(hmtxUseLoadedPluginList);
    MMIOPsDestroyNodeStruct(pRootNode); pRootNode = NULL;
    tpl_mtxsemDelete(hmtxUseRootNode);
    MMIOfree(pchGlobalHomeDirectory); pchGlobalHomeDirectory = NULL;
    MMIOMemUninitialize();
    return MMIO_ERROR_OUT_OF_RESOURCES;
  }

  /* Initialization done! */
  bInitialized = 1;
  return MMIO_NOERROR;
}

MMIOIMPEXP void         MMIOCALL MMIOUninitialize(int bShowMemoryLeaks)
{
  mmioProcessTreeNode_p pChild, pPrev;
  mmioLoadedPluginList_p hPlugin;
  void *pInstance;

  if (!bInitialized)
    return;

  /* Free allocated resources */

  /* Destroy stream groups */
  while (pStreamGroupListHead)
    MMIODestroyStreamGroup(pStreamGroupListHead->pStreamGroup);

  /* Destroy process-tree */
  tpl_mtxsemRequest(hmtxUseRootNode, TPL_WAIT_FOREVER);

  /* Remove all opened URLs first! */
  pPrev = NULL;
  pChild = pRootNode->pFirstChild;
  while (pChild)
  {
    hPlugin = pChild->pNodeOwnerPluginHandle;
    pInstance = pChild->pNodeOwnerPluginInstance;

    if (pChild->pFirstChild)
      MMIOUnlinkNode(pChild->pFirstChild);

    if (pChild->pNextBrother)
      MMIOUnlinkNode(pChild->pNextBrother);

    hPlugin->mmiomod_Unlink(pInstance, pChild);

    if (((!pPrev) && (pRootNode->pFirstChild == pChild)) ||
        ((pPrev) && (pPrev->pNextBrother == pChild))
       )
    {
      fprintf(stderr, "[MMIOUninitialize] : Warning! Could not unlink a URL node, skipping it!\n");
      pPrev = pChild;
      pChild = pChild->pNextBrother;
    } else
    {
      /* First node successfully uninitialized. */
      /* Go for next one! */

      if (!pPrev)
        pChild = pRootNode->pFirstChild;
      else
        pChild = pPrev->pNextBrother;
    }
  }

  tpl_mtxsemRelease(hmtxUseRootNode);

  /* Destroy the root node then! */
  MMIOPsDestroyNodeStruct(pRootNode); pRootNode = NULL;

  /* Destroy loaded plugin list */
  while (pLoadedPluginListHead)
    MMIOUnloadPlugin(pLoadedPluginListHead);

  /* Global home directory string: */
  MMIOfree(pchGlobalHomeDirectory); pchGlobalHomeDirectory = NULL;

  /* Destroy mutexes */
  tpl_mtxsemDelete(hmtxUseRootNode);
  tpl_mtxsemDelete(hmtxUseLoadedPluginList);
  tpl_mtxsemDelete(hmtxUseStreamGroupList);

  /* Uninitialize memory tracking subsystem */
  if (bShowMemoryLeaks)
  {
    if (MMIOGetCurrentMemUsage()>0)
    {
      fprintf(stderr, "[MMIOUninitialize] : Warning! Some memory is still allocated!\n");
      MMIOShowMemUsage();
    }
  }
  MMIOMemUninitialize();

  /* Everything is uninitialized and cleaned up. */
  bInitialized = 0;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOQueryRegisteredPluginList(mmioAvailablePluginList_p *ppPluginList)
{
  FILE *hFile;
  char *pchFileName;
  char  achLineBuf[256];
  char *pchTemp;
  int   iFound;
  char *pchInternalName;
  char *pchImportance;
  char *pchSupportedFormats;
  mmioAvailablePluginList_p pNew;
  mmioAvailablePluginList_p pLast;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!ppPluginList)
    return MMIO_ERROR_INVALID_PARAMETER;

  *ppPluginList = NULL;
  pLast = NULL;

  pchFileName = (char *) MMIOmalloc(strlen(pchGlobalHomeDirectory)+strlen(MMIO_REGISTRY_FILENAME)+1);
  if (!pchFileName)
    return MMIO_ERROR_OUT_OF_MEMORY;

  sprintf(pchFileName, "%s%s", pchGlobalHomeDirectory, MMIO_REGISTRY_FILENAME);

  hFile = fopen(pchFileName, "rt");
  MMIOfree(pchFileName);

  if (hFile)
  {
    while (fgets(achLineBuf, sizeof(achLineBuf), hFile))
    {
      /* Process this line of config file */
      pchTemp = &(achLineBuf[0]);

      iFound = 0;
      pchInternalName = pchFileName = pchImportance = pchSupportedFormats = NULL;
      while (*pchTemp)
      {
        /* Skip the leading space and tab, if any */
        while ((*pchTemp) &&
               (((*pchTemp)==' ') || ((*pchTemp)=='\t'))
              )
          pchTemp++;

        /* Get out if found a remark */
        if (*pchTemp == '#') break;

        if (*pchTemp == '<')
        {
          /* Ooops, found a starting marker! */
          pchTemp++;
          iFound++;
          switch (iFound)
          {
            case 1:
                pchInternalName = pchTemp;
                break;
            case 2:
                pchFileName = pchTemp;
                break;
            case 3:
                pchImportance = pchTemp;
                break;
            case 4:
                pchSupportedFormats = pchTemp;
                break;
            default:
                break;
          }
          /* Look for end marker */
          while ((*pchTemp) && (*pchTemp != '>'))
            pchTemp++;

          if (*pchTemp)
          {
            *pchTemp = 0;
            pchTemp++;
          }
        } else
          pchTemp++;

      }

      if (iFound >= 4)
      {
        /* It seems that this line contained all the required fields */
        /* so we now have one new plugin entry, if the importance value */
        /* is really a number. */
        if (atoi(pchImportance)>0)
        {
          /* Ok, create a new plugin entry into the list! */
          pNew = (mmioAvailablePluginList_p) MMIOmalloc(sizeof(mmioAvailablePluginList_t));
          if (pNew)
          {
            pNew->pchPluginInternalName = MMIOmalloc(strlen(pchInternalName)+1);
            pNew->pchPluginFileName = MMIOmalloc(strlen(pchFileName)+1);
            pNew->pchSupportedFormats = MMIOmalloc(strlen(pchSupportedFormats)+1);

            if ((pNew->pchPluginInternalName) &&
                (pNew->pchPluginFileName) &&
                (pNew->pchSupportedFormats))
            {
              /* Fine, we could allocate all the memory we requested! */
              /* Fill in the fields */
              strcpy(pNew->pchPluginInternalName, pchInternalName);
              strcpy(pNew->pchPluginFileName, pchFileName);
              strcpy(pNew->pchSupportedFormats, pchSupportedFormats);
              pNew->iPluginImportance = atoi(pchImportance);
              pNew->pNext = NULL;
              /* Link this new element to end of list */
              if (pLast)
              {
                pLast->pNext = pNew;
                pLast = pNew;
              } else
              {
                *ppPluginList = pLast = pNew;
              }
            } else
            {
              /* Ooops, not enough memory! Skip this element then, */
              /* and free all allocated memory */

              if (pNew->pchPluginInternalName)
                MMIOfree(pNew->pchPluginInternalName);
              if (pNew->pchPluginFileName)
                MMIOfree(pNew->pchPluginFileName);
              if (pNew->pchSupportedFormats)
                MMIOfree(pNew->pchSupportedFormats);

              MMIOfree(pNew);
            }
          }
        }
      }
    }
    fclose(hFile);
  }

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreeRegisteredPluginList(mmioAvailablePluginList_p pPluginList)
{
  mmioAvailablePluginList_p pToDelete;

  if (!pPluginList)
    return MMIO_ERROR_INVALID_PARAMETER;

  while (pPluginList)
  {
    pToDelete = pPluginList;
    pPluginList = pPluginList->pNext;

    MMIOfree(pToDelete->pchPluginInternalName);
    MMIOfree(pToDelete->pchPluginFileName);
    MMIOfree(pToDelete->pchSupportedFormats);
    MMIOfree(pToDelete);
  }

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOQueryPluginsOfBinary(char *pchPluginFileName, mmioAvailablePluginList_p *ppPluginList)
{
  TPL_HMODULE hModule;
  char *pchFullPathFileName;
  pfn_MMIOQueryPluginInfoForRegistry pfnQueryInfo = NULL;
  pfn_MMIOFreePluginInfoForRegistry pfnFreeInfo = NULL;
  int iPluginIndex;
  mmioAvailablePluginList_p pNewElement, pLastElement, pFirstElement;
  char *pchInternalName;
  char *pchSupportedFormats;
  int   iImportance;

  if ((!pchPluginFileName) || (!ppPluginList))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Make the filename relative to home directory if it doesn't contain path delimiter */
  pchFullPathFileName = pchPluginFileName;
  while ((*pchFullPathFileName) && (*pchFullPathFileName != TPL_PATH_SEPARATOR))
    pchFullPathFileName++;

  if (*pchFullPathFileName)
  {
    /* Found a path delimiter in the filename, so simply use that as filename */
    hModule = tpl_dloaderOpen(pchPluginFileName);
  } else
  {
    /* There is no path delimiter in filename, so make it relative to home directory! */
    pchFullPathFileName = MMIOmalloc(strlen(pchGlobalHomeDirectory) + strlen(pchPluginFileName) + 1);
    if (!pchFullPathFileName)
    {
      /* There is not enough memory for this, so fall back to old method */
      hModule = tpl_dloaderOpen(pchPluginFileName);
    } else
    {
      /* Create new filename (fully specified) */
      strcpy(pchFullPathFileName, pchGlobalHomeDirectory);
      strcat(pchFullPathFileName, pchPluginFileName);
      /* Load plugin */
      hModule = tpl_dloaderOpen(pchFullPathFileName);
      /* Free allocated memory */
      MMIOfree(pchFullPathFileName); pchFullPathFileName = NULL;
    }
  }

  if (!hModule)
  {
    /* Could not load DLL! */
    return MMIO_ERROR_NOT_FOUND;
  }

  /* DLL loaded, query the special function pointers */
  pfnQueryInfo = tpl_dloaderGetSymAddr(hModule, "MMIOQueryPluginInfoForRegistry");
  pfnFreeInfo = tpl_dloaderGetSymAddr(hModule, "MMIOFreePluginInfoForRegistry");
  if ((!pfnQueryInfo) || (!pfnFreeInfo))
  {
    /* This DLL does not have the required functions exported */
    return MMIO_ERROR_NOT_FOUND;
  }

  pFirstElement = pLastElement = NULL;
  iPluginIndex = 0;
  while ((*pfnQueryInfo)(iPluginIndex, &pchInternalName, &pchSupportedFormats, &iImportance) == MMIO_NOERROR)
  {
    iPluginIndex++;
    pNewElement = (mmioAvailablePluginList_p) MMIOmalloc(sizeof(mmioAvailablePluginList_t));
    if (pNewElement)
    {
      pNewElement->pchPluginFileName = MMIOmalloc(strlen(pchPluginFileName)+1);
      pNewElement->pchPluginInternalName = MMIOmalloc(strlen(pchInternalName)+1);
      pNewElement->pchSupportedFormats = MMIOmalloc(strlen(pchSupportedFormats)+1);
      if ((!pNewElement->pchPluginFileName) ||
          (!pNewElement->pchPluginInternalName) ||
          (!pNewElement->pchSupportedFormats))
      {
        /* Could not allocate some memory! */
        if (pNewElement->pchPluginFileName)
          MMIOfree(pNewElement->pchPluginFileName);
        if (pNewElement->pchPluginInternalName)
          MMIOfree(pNewElement->pchPluginInternalName);
        if (pNewElement->pchSupportedFormats)
          MMIOfree(pNewElement->pchSupportedFormats);

        MMIOfree(pNewElement); pNewElement = NULL;
      } else
      {
        /* Fill the entries */
        strcpy(pNewElement->pchPluginFileName, pchPluginFileName);
        strcpy(pNewElement->pchPluginInternalName, pchInternalName);
        strcpy(pNewElement->pchSupportedFormats, pchSupportedFormats);
        pNewElement->iPluginImportance = iImportance;
        pNewElement->pNext = NULL;

        /* Link it to end of list */
        if (!pFirstElement)
        {
          pFirstElement = pLastElement = pNewElement;
        } else
        {
          pLastElement->pNext = pNewElement;
          pLastElement = pNewElement;
        }
      }
    }

    /* Now let the plugin free all the info it has provided to us */
    (*pfnFreeInfo)(pchInternalName, pchSupportedFormats);
  }

  /* Close the plugin */
  tpl_dloaderClose(hModule);

  *ppPluginList = pFirstElement;
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreePluginsOfBinary(mmioAvailablePluginList_p pPluginList)
{
  /* It's the same as MMIOFreeRegisteredPluginList in the current implementation */
  return MMIOFreeRegisteredPluginList(pPluginList);
}


MMIOIMPEXP mmioResult_t MMIOCALL MMIORegisterPlugin(char *pchPluginInternalName, char *pchPluginFileName, char *pchSupportedFormats, int iImportance)
{
  mmioResult_t result;
  mmioAvailablePluginList_p pPluginList, pPlugin;
  FILE *hFile;
  char *pchFileName;
  int bNewFile;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  /* Check parameters */
  if ((!pchPluginInternalName) ||
      (!pchPluginFileName) ||
      (!pchSupportedFormats) ||
      (iImportance<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Query current plugin list */
  result = MMIOQueryRegisteredPluginList(&pPluginList);
  if (result!=MMIO_NOERROR)
    return result;

  /* Check if this plugin is already in the list or not! */
  pPlugin = pPluginList;
  while (pPlugin)
  {
    if ((!stricmp(pchPluginInternalName, pPlugin->pchPluginInternalName)) &&
        (!stricmp(pchPluginFileName, pPlugin->pchPluginFileName)))
    {
      /* This plugin is already in the list! */
      break;
    }
    pPlugin = pPlugin->pNext;
  }

  MMIOFreeRegisteredPluginList(pPluginList);

  if (pPlugin)
    return MMIO_ERROR_ALREADY_REGISTERED;

  /* Ok, this plugin is not yet registered, so put this to the */
  /* end of the file! */

  pchFileName = (char *) MMIOmalloc(strlen(pchGlobalHomeDirectory)+strlen(MMIO_REGISTRY_FILENAME)+1);
  if (!pchFileName)
    return MMIO_ERROR_OUT_OF_MEMORY;

  sprintf(pchFileName, "%s%s", pchGlobalHomeDirectory, MMIO_REGISTRY_FILENAME);

  hFile = fopen(pchFileName, "rt");
  if (hFile)
  {
    /* The config file already exists, so we'll extend that one! */
    fclose(hFile);
    bNewFile = 0;
  } else
  {
    /* The config file does not yet exist, so we'll create a default one! */
    bNewFile = 1;
  }

  hFile = fopen(pchFileName, "at");
  if (!hFile)
  {
    MMIOfree(pchFileName);
    return MMIO_ERROR_UNKNOWN;
  }

  if (bNewFile)
  {
    /* This is a newly created file, so put there the default header! */
    fprintf(hFile, MMIO_REGISTRY_DEFAULT_HEADER);
  }

  /* The line describing this plugin */
  fprintf(hFile, "<%s> <%s> <%d> <%s>\n",
          pchPluginInternalName,
          pchPluginFileName,
          iImportance,
          pchSupportedFormats);
  fclose(hFile);

  /* Free stuffs and return! */
  MMIOfree(pchFileName);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIODeregisterPlugin(char *pchPluginInternalName, char *pchPluginFileName, char *pchSupportedFormats, int iImportance)
{
  mmioResult_t result;
  mmioAvailablePluginList_p pPluginList, pPlugin, pPluginFound;
  FILE *hFile;
  char *pchFileName;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  /* Check parameters */
  if ((!pchPluginInternalName) ||
      (!pchPluginFileName) ||
      (!pchSupportedFormats) ||
      (iImportance<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Query current plugin list */
  result = MMIOQueryRegisteredPluginList(&pPluginList);
  if (result!=MMIO_NOERROR)
    return result;

  /* Check if this plugin is already in the list or not! */
  pPlugin = pPluginList;
  while (pPlugin)
  {
    if ((!stricmp(pchPluginInternalName, pPlugin->pchPluginInternalName)) &&
        (!stricmp(pchPluginFileName, pPlugin->pchPluginFileName)) &&
        (!stricmp(pchSupportedFormats, pPlugin->pchSupportedFormats)) &&
        (iImportance == pPlugin->iPluginImportance)
       )
    {
      /* This plugin is in the list! */
      break;
    }
    pPlugin = pPlugin->pNext;
  }

  if (!pPlugin)
  {
    MMIOFreeRegisteredPluginList(pPluginList);
    return MMIO_ERROR_NOT_FOUND;
  }

  /* Ok, plugin is really registered, so we have to re-create the */
  /* config file leaving out that plugin! */

  pchFileName = (char *) MMIOmalloc(strlen(pchGlobalHomeDirectory)+strlen(MMIO_REGISTRY_FILENAME)+1);
  if (!pchFileName)
  {
    MMIOFreeRegisteredPluginList(pPluginList);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  hFile = fopen(pchFileName, "wt");
  if (!hFile)
  {
    /* Could not open file! */
    MMIOfree(pchFileName);
    MMIOFreeRegisteredPluginList(pPluginList);
    return MMIO_ERROR_UNKNOWN;
  }

  /* This is a newly created file, so put there the default header! */
  fprintf(hFile, MMIO_REGISTRY_DEFAULT_HEADER);

  pPluginFound = pPlugin;
  pPlugin = pPluginList;
  while (pPlugin)
  {
    if (pPlugin!=pPluginFound)
    {
      fprintf(hFile, "<%s> <%s> <%d> <%s>\n",
              pPlugin->pchPluginInternalName,
              pPlugin->pchPluginFileName,
              pPlugin->iPluginImportance,
              pPlugin->pchSupportedFormats);
    }
    pPlugin = pPlugin->pNext;
  }

  fclose(hFile);

  /* Free stuffs and return! */
  MMIOfree(pchFileName);
  MMIOFreeRegisteredPluginList(pPluginList);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetRootNode(mmioProcessTreeNode_p *ppRoot)
{
  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!ppRoot)
    return MMIO_ERROR_INVALID_PARAMETER;

  *ppRoot = pRootNode;
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOLoadPlugin(char *pchPluginInternalName, char *pchPluginFileName, mmioLoadedPluginList_p *phPlugin)
{
  mmioResult_t result;
  mmioLoadedPluginList_p pPlugin;
  char *pchTemp;
  char *pchFullPathFileName;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pchPluginInternalName) ||
      (!pchPluginFileName) ||
      (!phPlugin))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Grab mutex semaphore */
  if (!tpl_mtxsemRequest(hmtxUseLoadedPluginList, TPL_WAIT_FOREVER))
    return MMIO_ERROR_UNKNOWN;

  /* Check the plugin list if this is already loaded */
  pPlugin = pLoadedPluginListHead;
  while ((pPlugin) &&
         ((stricmp(pchPluginInternalName, pPlugin->pchPluginInternalName)) ||
          (stricmp(pchPluginFileName, pPlugin->pchPluginFileName))))
    pPlugin = pPlugin->pNext;

  if (pPlugin)
  {
    /* Fine, this plugin with this internal name is already loaded,
     * so we only increase the reference counter!
     */
    pPlugin->iRefCount++;

    *phPlugin = pPlugin;
    result = MMIO_NOERROR;
  } else
  {
    /* This plugin is not yet loaded, so try to load it! */

    /* Allocate a work buffer first! */
    /* This is where we'll assemble the internal function name strings. */
    /* An extra storage of 64 characters must be enough for all the public functions. */
    pchTemp = (char *) MMIOmalloc(strlen(pchPluginInternalName) + 64);

    /* Also allocate the plugin descriptor structure */
    pPlugin = (mmioLoadedPluginList_p) MMIOmalloc(sizeof(mmioLoadedPluginList_t));
    if ((!pPlugin) || (!pchTemp))
    {
      /* Out of memory! */
      if (pPlugin)
        MMIOfree(pPlugin);
      if (pchTemp)
        MMIOfree(pchTemp);
      result = MMIO_ERROR_OUT_OF_MEMORY;
    } else
    {
      /* Prepare plugin descriptor structure */
      memset(pPlugin, 0, sizeof(mmioLoadedPluginList_t));
      pPlugin->pchPluginInternalName = (char *) MMIOmalloc(strlen(pchPluginInternalName)+1);
      pPlugin->pchPluginFileName = (char *) MMIOmalloc(strlen(pchPluginFileName)+1);
      if ((!pPlugin->pchPluginInternalName) ||
          (!pPlugin->pchPluginFileName))
      {
        /* Out of memory! */
        if (pPlugin->pchPluginInternalName)
          MMIOfree(pPlugin->pchPluginInternalName);
        if (pPlugin->pchPluginFileName)
          MMIOfree(pPlugin->pchPluginFileName);
        MMIOfree(pPlugin);
        result = MMIO_ERROR_OUT_OF_MEMORY;
      } else
      {
        strcpy(pPlugin->pchPluginInternalName, pchPluginInternalName);
        strcpy(pPlugin->pchPluginFileName, pchPluginFileName);
        pPlugin->iRefCount = 1;

        /* Make the filename relative to home directory if it doesn't contain path delimiter */
        pchFullPathFileName = pchPluginFileName;
        while ((*pchFullPathFileName) && (*pchFullPathFileName != TPL_PATH_SEPARATOR))
          pchFullPathFileName++;

        if (*pchFullPathFileName)
        {
          /* Found a path delimiter in the filename, so simply use that as filename */
          pPlugin->hModule = tpl_dloaderOpen(pchPluginFileName);
        } else
        {
          /* There is no path delimiter in filename, so make it relative to home directory! */
          pchFullPathFileName = MMIOmalloc(strlen(pchGlobalHomeDirectory) + strlen(pchPluginFileName) + 1);
          if (!pchFullPathFileName)
          {
            /* There is not enough memory for this, so fall back to old method */
            pPlugin->hModule = tpl_dloaderOpen(pchPluginFileName);
          } else
          {
            /* Create new filename (fully specified) */
            strcpy(pchFullPathFileName, pchGlobalHomeDirectory);
            strcat(pchFullPathFileName, pchPluginFileName);
            /* Load plugin */
            pPlugin->hModule = tpl_dloaderOpen(pchFullPathFileName);
            /* Free allocated memory */
            MMIOfree(pchFullPathFileName); pchFullPathFileName = NULL;
          }
        }
        if (!(pPlugin->hModule))
        {
          /* Could not load DLL! */
          MMIOfree(pPlugin->pchPluginInternalName);
          MMIOfree(pPlugin->pchPluginFileName);
          MMIOfree(pPlugin);
          result = MMIO_ERROR_NOT_FOUND;
        } else
        {
          /* DLL loaded, query function pointers */

          sprintf(pchTemp, "%s_GetPluginDesc", pchPluginInternalName);
          pPlugin->mmiomod_GetPluginDesc = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_Initialize", pchPluginInternalName);
          pPlugin->mmiomod_Initialize = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_Examine", pchPluginInternalName);
          pPlugin->mmiomod_Examine = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_FreeExamineResult", pchPluginInternalName);
          pPlugin->mmiomod_FreeExamineResult = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_Link", pchPluginInternalName);
          pPlugin->mmiomod_Link = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_Unlink", pchPluginInternalName);
          pPlugin->mmiomod_Unlink = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          sprintf(pchTemp, "%s_Uninitialize", pchPluginInternalName);
          pPlugin->mmiomod_Uninitialize = tpl_dloaderGetSymAddr(pPlugin->hModule, pchTemp);

          if ((!pPlugin->mmiomod_GetPluginDesc) ||
              (!pPlugin->mmiomod_Initialize) ||
              (!pPlugin->mmiomod_Examine) ||
              (!pPlugin->mmiomod_FreeExamineResult) ||
              (!pPlugin->mmiomod_Link) ||
              (!pPlugin->mmiomod_Unlink) ||
              (!pPlugin->mmiomod_Uninitialize) ||
              (pPlugin->mmiomod_GetPluginDesc(pPlugin->achPluginDescription, sizeof(pPlugin->achPluginDescription))!=MMIO_NOERROR)
             )
          {
            /* Some of the functions are not exported by the DLL, or */
            /* could not query plugin description string, so */
            /* it's not a real plugin! */

            fprintf(stderr, "[MMIOLoadPlugin] : Could not load plugin: %s (%s)\n", pchPluginFileName, pchPluginInternalName);

            tpl_dloaderClose(pPlugin->hModule);
            MMIOfree(pPlugin->pchPluginInternalName);
            MMIOfree(pPlugin->pchPluginFileName);
            MMIOfree(pPlugin);
            result = MMIO_ERROR_NOT_PLUGIN;
          } else
          {
            /* Fine, all the public functions are exported! Link this */
            /* plugin to the list of loaded plugins! */

            pPlugin->pNext = pLoadedPluginListHead;
            pLoadedPluginListHead = pPlugin;

            /* Set result to success */
            *phPlugin = pPlugin;
            result = MMIO_NOERROR;
          }
        }
      }

      MMIOfree(pchTemp);
    }
  }

  /* Release plugin list */
  tpl_mtxsemRelease(hmtxUseLoadedPluginList);

  return result;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOUnloadPlugin(mmioLoadedPluginList_p hPlugin)
{
  mmioResult_t result;
  mmioLoadedPluginList_p pPlugin, pPrev;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!hPlugin)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Grab mutex semaphore */
  if (!tpl_mtxsemRequest(hmtxUseLoadedPluginList, TPL_WAIT_FOREVER))
    return MMIO_ERROR_UNKNOWN;

  pPrev = NULL;
  pPlugin = pLoadedPluginListHead;
  while ((pPlugin) && (pPlugin!=hPlugin))
  {
    pPrev = pPlugin;
    pPlugin = pPlugin->pNext;
  }

  if (!pPlugin)
  {
    /* Plugin handle was not found in list of plugins */
    result = MMIO_ERROR_INVALID_PARAMETER;
  } else
  {
    /* Plugin found, so decrease reference counter, and unload if needed! */
    pPlugin->iRefCount--;
    if (pPlugin->iRefCount<=0)
    {
      /* Plugin is not used anymore, so unload it! */

      /* First unlink from list of plugins */
      if (pPrev)
        pPrev->pNext = pPlugin->pNext;
      else
        pLoadedPluginListHead = pPlugin->pNext;

      /* Then free resources */
      tpl_dloaderClose(pPlugin->hModule);
      MMIOfree(pPlugin->pchPluginInternalName);
      MMIOfree(pPlugin->pchPluginFileName);
      MMIOfree(pPlugin);
    }
  }

  tpl_mtxsemRelease(hmtxUseLoadedPluginList);

  return result;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOInitializePlugin(mmioLoadedPluginList_p hPlugin, void **ppInstance)
{
  if ((!hPlugin) || (!ppInstance))
    return MMIO_ERROR_INVALID_PARAMETER;

  *ppInstance = hPlugin->mmiomod_Initialize();

  if (!(*ppInstance))
    return MMIO_ERROR_IN_PLUGIN;

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOUninitializePlugin(mmioLoadedPluginList_p hPlugin, void *pInstance)
{
  if ((!hPlugin) || (!pInstance))
    return MMIO_ERROR_INVALID_PARAMETER;

  return hPlugin->mmiomod_Uninitialize(pInstance);
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOExamineNodeWithPlugin(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                    mmioProcessTreeNode_p pNode,
                                                    mmioNodeExamineResult_p *ppExamineResult)
{
  if ((!hPlugin) || (!pInstance) ||
      (!pNode) || (!ppExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  return hPlugin->mmiomod_Examine(pInstance, pNode, ppExamineResult);
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOFreeExamineResult(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                mmioNodeExamineResult_p pExamineResult)
{
  if ((!hPlugin) || (!pInstance) ||
      (!pExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  return hPlugin->mmiomod_FreeExamineResult(pInstance, pExamineResult);
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOLinkPluginToNode(mmioLoadedPluginList_p hPlugin, void *pInstance,
                                               char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mmioResult_t rc;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!hPlugin) || (!pInstance) ||
      (!pchNeededOutputFormat) ||(!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  rc = hPlugin->mmiomod_Link(pInstance, pchNeededOutputFormat, pNode);

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOUnlinkNode(mmioProcessTreeNode_p pNode)
{
  mmioLoadedPluginList_p hPlugin;
  void *pInstance;
  int bPluginWasLoadedBySystem;
  mmioResult_t result;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pNode) || (pNode == pRootNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  result = MMIO_NOERROR;

  hPlugin = pNode->pNodeOwnerPluginHandle;
  pInstance = pNode->pNodeOwnerPluginInstance;
  bPluginWasLoadedBySystem = pNode->bPluginWasLoadedBySystem;

  if (pNode->pFirstChild)
  {
    result = MMIOUnlinkNode(pNode->pFirstChild);
  }

  if ((result == MMIO_NOERROR) && (pNode->pNextBrother))
  {
    result = MMIOUnlinkNode(pNode->pNextBrother);
  }

  if (pNode->iNodeType == MMIO_NODETYPE_URL)
  {
    /* This is an URL node, so we created it, we have to destroy it. */
    MMIOfree(pNode->pTypeSpecificInfo);
    MMIOPsUnlinkAndDestroyNodeStruct(pNode);
  } else
  {
    /* Not an URL node, so handle it with the plugin which created it */
    if ((result == MMIO_NOERROR) && (pNode->bUnlinkPoint) && (hPlugin))
    {
      result = hPlugin->mmiomod_Unlink(pInstance, pNode);
      if (bPluginWasLoadedBySystem)
      {
        /* Also unload the plugin */
        MMIOUninitializePlugin(hPlugin, pInstance);
        MMIOUnloadPlugin(hPlugin);
      }
    }
    else
      result = MMIO_NOERROR;
  }

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOWalkProcessTree(mmioProcessTreeNode_p pStartNode, pfn_mmiowalkprocesstree_ProcessNode pfnProcessNode, void *pUserData)
{
  mmioProcessTreeNode_p pNode;
  mmioResult_t rc;

  if ((!pStartNode) || (!pfnProcessNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  pNode = pStartNode;
  while (pNode)
  {
    /* Call the callback for the node */
    if (!(*pfnProcessNode)(pNode, pUserData))
      return MMIO_ERROR_WALK_INTERRUPTED;

    if (pNode->pFirstChild)
    {
      /* Go deeper in the tree */
      rc = MMIOWalkProcessTree(pNode->pFirstChild, pfnProcessNode, pUserData);
      if (rc != MMIO_NOERROR)
        return rc;
    }
    /* Go for next brother then */
    pNode = pNode->pNextBrother;
  }

  return MMIO_NOERROR;
}

MMIOIMPEXP void         MMIOCALL MMIOShowProcessTree(mmioProcessTreeNode_p pStartNode)
{
  int iLevel;
  mmioURLSpecificInfo_p pURL;
  mmioMediaSpecificInfo_p pMedia;
  mmioChannelSpecificInfo_p pChannel;
  mmioESSpecificInfo_p pES;
  mmioRSSpecificInfo_p pRS;
  mmioTermSpecificInfo_p pTerm;

  if (pStartNode)
  {
    for (iLevel=0; iLevel<pStartNode->iTreeLevel; iLevel++)
      printf(" ");

    switch (pStartNode->iNodeType)
    {
      case MMIO_NODETYPE_ROOT:
        printf("[%c%c][Root]\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ');
        break;
      case MMIO_NODETYPE_URL:
        pURL = (mmioURLSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][URL ; %s -> %s] : %s\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pURL->achURL);
        break;
      case MMIO_NODETYPE_MEDIUM:
        pMedia = (mmioMediaSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][Medium ; %s -> %s] : (%s)\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription);
        break;
      case MMIO_NODETYPE_CHANNEL:
        pChannel = (mmioChannelSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][Channel ; %s -> %s] : (%s) %s\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription,
               pChannel->achDescriptionText);
        break;
      case MMIO_NODETYPE_ELEMENTARYSTREAM:
        pES = (mmioESSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][ES ; %s -> %s] : (%s) %s ; %s  [Caps %x]\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription,
               pES->achDescriptiveESFormat,
               pES->achDescriptionText,
               pES->iESCapabilities);
        break;
      case MMIO_NODETYPE_RAWSTREAM:
        pRS = (mmioRSSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][RS ; %s -> %s] : (%s) %s ; %s  [Caps %x]\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription,
               pRS->achDescriptiveRSFormat,
               pRS->achDescriptionText,
               pRS->iRSCapabilities);
        break;
      case MMIO_NODETYPE_TERMINATOR:
        pTerm = (mmioTermSpecificInfo_p) (pStartNode->pTypeSpecificInfo);
        printf("[%c%c][Term ; %s -> %s] : (%s) %s  [Caps %x]\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription,
               pTerm->achDescriptionText,
               pTerm->iTermCapabilities);
        break;
      default:
        printf("[%c%c][Unk_%02x ; %s -> %s] : (%s)\n",
               pStartNode->bUnlinkPoint?'U':' ',
               pStartNode->bPluginWasLoadedBySystem?'S':' ',
               pStartNode->iNodeType,
               pStartNode->pParent->achNodeOwnerOutputFormat,
               pStartNode->achNodeOwnerOutputFormat,
               pStartNode->pNodeOwnerPluginHandle->achPluginDescription);
        break;
    }

    if (pStartNode->pFirstChild)
      MMIOShowProcessTree(pStartNode->pFirstChild);
    if (pStartNode->pNextBrother)
      MMIOShowProcessTree(pStartNode->pNextBrother);
  }
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetOneSimpleRawFrame(mmioProcessTreeNode_p pRawStreamNode,
                                                          mmioDataDesc_p pDataDesc,
                                                          void *pDataBuf,
                                                          long long llDataBufSize,
                                                          mmioRSFormatRequest_p pRequestedFormat)
{
  mmioDataDesc_t localDataDesc;
  mmioRSSpecificInfo_p pRSInfo;
  void *pOriginalDataBuf;
  mmioResult_t rc;

  /* This is a utility function to be able to quickly get raw, decoded data from plugins. */

  /* It is the "Simple" method, which does not support plugins which "eat" buffers, so most */
  /* probably it won't work with video decoder plugins supporting B frames, but will work */
  /* with image and audio decoders. */

  /* Check mandatory parameters */
  if ((!pRawStreamNode) ||
      (pRawStreamNode->iNodeType != MMIO_NODETYPE_RAWSTREAM) ||
      (!pDataBuf))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Set optional parameters */
  if (!pDataDesc)
    pDataDesc = &localDataDesc;

  /* Set the playback direction first */
  pRSInfo = (mmioRSSpecificInfo_p) pRawStreamNode->pTypeSpecificInfo;
  rc = pRSInfo->mmiors_SetDirection(pRawStreamNode->pNodeOwnerPluginInstance,
                                    pRSInfo->pRSID,
                                    MMIO_DIRECTION_PLAY);
  if (rc != MMIO_NOERROR)
    return rc;

  /* Decode one frame then */
  pOriginalDataBuf = pDataBuf;
  rc = pRSInfo->mmiors_GetOneFrame(pRawStreamNode->pNodeOwnerPluginInstance,
                                   pRSInfo->pRSID,
                                   pDataDesc,
                                   &pDataBuf,
                                   llDataBufSize,
                                   pRequestedFormat);

  /* Check the result code, and make sure we got back the same buffer we sent */
  if ((rc == MMIO_NOERROR) && (pOriginalDataBuf != pDataBuf))
    rc = MMIO_ERROR_NOT_SUPPORTED;

  if (rc != MMIO_NOERROR)
  {
    /* Oops, an error happened, or the decoder gave back a different buffer! */
    /* "Reset" the decoder buffers then */
    pRSInfo->mmiors_ReleaseForeignBuffers(pRawStreamNode->pNodeOwnerPluginInstance,
                                          pRSInfo->pRSID);
  }

  /* Return with result code */
  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOCreateEmptyStreamGroup(mmioStreamGroup_p *ppNewStreamGroup)
{
  mmioStreamGroup_p pNew;
  mmioStreamGroupList_p pNewListElement;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!ppNewStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Allocate memory for structure */
  pNew = (mmioStreamGroup_p) MMIOmalloc(sizeof(mmioStreamGroup_t));
  if (!pNew)
    return MMIO_ERROR_OUT_OF_MEMORY;

  /* Fill the structure with initial data */
  memset(pNew, 0, sizeof(mmioStreamGroup_t));

  pNew->hmtxUseStreamGroup = tpl_mtxsemCreate(0);
  if (!pNew->hmtxUseStreamGroup)
  {
    /* Could not create mutex semaphore! */
    MMIOfree(pNew);
    return MMIO_ERROR_OUT_OF_RESOURCES;
  }

  pNew->hmqEventMessageQueue = tpl_msgqCreate(MMIO_STREAMGROUP_MQ_SIZE,
                                               sizeof(mmioEventMessageQueueElement_t));
  if (!pNew->hmqEventMessageQueue)
  {
    /* Could not create message queue */
    tpl_mtxsemDelete(pNew->hmtxUseStreamGroup);
    MMIOfree(pNew);
    return MMIO_ERROR_OUT_OF_RESOURCES;
  }

  pNew->iDirection = MMIO_DIRECTION_STOP;
  pNew->pMainStream = NULL;
  pNew->pStreamList = NULL;
  pNew->iTermCapabilities = 0;
  pNew->iSubscribedEvents = 0;

  /* Fine, now add this to the list of streamgroups */

  pNewListElement = (mmioStreamGroupList_p) MMIOmalloc(sizeof(mmioStreamGroupList_t));
  if (!pNewListElement)
  {
    /* Could not create list element structure! */
    tpl_msgqDelete(pNew->hmqEventMessageQueue);
    tpl_mtxsemDelete(pNew->hmtxUseStreamGroup);
    MMIOfree(pNew);
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  if (!tpl_mtxsemRequest(hmtxUseStreamGroupList, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    MMIOfree(pNewListElement);
    tpl_msgqDelete(pNew->hmqEventMessageQueue);
    tpl_mtxsemDelete(pNew->hmtxUseStreamGroup);
    MMIOfree(pNew);
    return MMIO_ERROR_UNKNOWN;
  }

  /* Ok, we grabbed the semaphore and has the structure, so */
  /* fill the structure and add it to the list of stream groups. */
  pNewListElement->pStreamGroup = pNew;
  pNewListElement->pNext = pStreamGroupListHead;
  pStreamGroupListHead = pNewListElement;

  tpl_mtxsemRelease(hmtxUseStreamGroupList);

  *ppNewStreamGroup = pNew;
  return MMIO_NOERROR;
}

static mmioStreamGroupList_p MMIO_internal_GetStreamGroupListEntry(mmioStreamGroup_p pStreamGroup)
{
  mmioStreamGroupList_p pEntry;

  if (!tpl_mtxsemRequest(hmtxUseStreamGroupList, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return NULL;
  }

  /* Look for this stream group in the list */
  pEntry = pStreamGroupListHead;
  while ((pEntry) && (pEntry->pStreamGroup != pStreamGroup))
    pEntry = pEntry->pNext;

  tpl_mtxsemRelease(hmtxUseStreamGroupList);

  return pEntry;
}

static mmioResult_t MMIO_internal_AddStreamsToGroup_core(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pNode)
{
  mmioResult_t rc;
  mmioStreamGroupStreamList_p pNewEntry;
  mmioTermSpecificInfo_p pTerm, pMainTerm;
  long long llPosition;
  mmioSystemTime_t TimeDiff;

  /* Walk in the tree */
  if (pNode->pFirstChild)
  {
    rc = MMIO_internal_AddStreamsToGroup_core(pStreamGroup, pNode->pFirstChild);
    if (rc!=MMIO_NOERROR)
      return rc;
  }

  if (pNode->pNextBrother)
  {
    rc = MMIO_internal_AddStreamsToGroup_core(pStreamGroup, pNode->pNextBrother);
    if (rc!=MMIO_NOERROR)
      return rc;
  }

  /* All the subtree was walked, so let's check this node then! */

  rc = MMIO_NOERROR;

  if ((pNode->iNodeType == MMIO_NODETYPE_TERMINATOR) &&
      (pNode->pOwnerStreamGroup == NULL))
  {
    /* Finally, this is a terminator node which is not yet part */
    /* of any stream groups, so add it to our stream group! */

    pNewEntry = (mmioStreamGroupStreamList_p) MMIOmalloc(sizeof(mmioStreamGroupStreamList_t));
    if (!pNewEntry)
    {
      /* Not enough memory! */
      rc = MMIO_ERROR_OUT_OF_MEMORY;
    } else
    {
      if (tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
      {
        /* Get old position of stream-group */
        llPosition = 0;
        if (pStreamGroup->pMainStream)
        {
          long long llSignedTimeDiff;
          llPosition = pStreamGroup->pMainStream->llLastTimeStamp;

          if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
          {
            TimeDiff = (MMIOPsGetCurrentSystemTime() - pStreamGroup->pMainStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
            if (TimeDiff > pStreamGroup->pMainStream->llLastFrameLength)
              TimeDiff = pStreamGroup->pMainStream->llLastFrameLength;

            llSignedTimeDiff = TimeDiff;
            llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

            llPosition += llSignedTimeDiff;
          }
        }

        pNewEntry->pNode = pNode;
        pNewEntry->LastTimeStampTime = MMIOPsGetCurrentSystemTime();
        pNewEntry->llLastTimeStamp = 0;
        pNewEntry->llLastFrameLength = 0;
        pNewEntry->pNext = pStreamGroup->pStreamList;
        pStreamGroup->pStreamList = pNewEntry;

        pNode->pOwnerStreamGroup = pStreamGroup;

        pTerm = (mmioTermSpecificInfo_p) (pNode->pTypeSpecificInfo);

        /* Re-calculate main stream */
        if (!pStreamGroup->pMainStream)
        {
          /* There is no main-stream yet, so set this as the main one */
          pStreamGroup->pMainStream = pNewEntry;
        } else
        {
          /* There is already a main-stream, so set it to be the main one */
          /* only if it's better than that stream */
          pMainTerm = (mmioTermSpecificInfo_p) (pStreamGroup->pMainStream->pNode->pTypeSpecificInfo);

          if ((pTerm->iStreamType == MMIO_STREAMTYPE_AUDIO) ||
              ((pTerm->iStreamType == MMIO_STREAMTYPE_VIDEO) && (pMainTerm->iStreamType != MMIO_STREAMTYPE_AUDIO))
             )
          {
            /* Use this stream as main stream! */
            pStreamGroup->pMainStream = pNewEntry;
          }
        }

        /* Re-calculate stream-group capabilities */
        pStreamGroup->iTermCapabilities = pTerm->iTermCapabilities;
        pNewEntry = pStreamGroup->pStreamList;
        while (pNewEntry)
        {
          pTerm = (mmioTermSpecificInfo_p) (pNewEntry->pNode->pTypeSpecificInfo);
          pStreamGroup->iTermCapabilities &= pTerm->iTermCapabilities;
          pNewEntry = pNewEntry->pNext;
        }

        /* Set direction of new stream */
        pTerm = (mmioTermSpecificInfo_p) (pNode->pTypeSpecificInfo);
        rc = pTerm->mmioterm_SetDirection(pNode->pNodeOwnerPluginInstance, pTerm->pTermID, pStreamGroup->iDirection);
        if (rc!=MMIO_NOERROR)
        {
          /* Could not set direction to required one, so stop all streams! */
          pNewEntry = pStreamGroup->pStreamList;
          while (pNewEntry)
          {
            pTerm = (mmioTermSpecificInfo_p) (pNewEntry->pNode->pTypeSpecificInfo);
            pTerm->mmioterm_SetDirection(pNode->pNodeOwnerPluginInstance, pTerm->pTermID, MMIO_DIRECTION_STOP);
            pNewEntry = pNewEntry->pNext;
          }
          pStreamGroup->iDirection = MMIO_DIRECTION_STOP;
          rc = MMIO_NOERROR;
        }

        /* Set position of new stream to old position of stream-group*/
        pTerm = (mmioTermSpecificInfo_p) (pNode->pTypeSpecificInfo);
        pTerm->mmioterm_SetPosition(pNode->pNodeOwnerPluginInstance, pTerm->pTermID, llPosition, MMIO_POSTYPE_TIME, &llPosition);

        tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);
      } else
      {
        /* Oops, internal error! */
        rc = MMIO_ERROR_UNKNOWN;
      }

      /* Cleanup in case of problems */
      if (rc!=MMIO_NOERROR)
        MMIOfree(pNewEntry);
    }
  }

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOAddStreamsToGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pStartNode)
{
  mmioResult_t rc;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) || (!pStartNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check if it's a real stream group */
  if (!MMIO_internal_GetStreamGroupListEntry(pStreamGroup))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pStartNode == pRootNode)
  {
    /* Grab mutex semaphore */
    if (!tpl_mtxsemRequest(hmtxUseRootNode, TPL_WAIT_FOREVER))
      return MMIO_ERROR_UNKNOWN;
  }

  rc = MMIO_internal_AddStreamsToGroup_core(pStreamGroup, pStartNode);

  if (pStartNode == pRootNode)
  {
    /* Release mutex semaphore */
    tpl_mtxsemRelease(hmtxUseRootNode);
  }

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIORemoveStreamFromGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pNodeToRemove)
{
  mmioStreamGroupStreamList_p pStream, pPrev;
  mmioTermSpecificInfo_p pTerm, pMainTerm;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) || (!pNodeToRemove))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check if it's a real stream group */
  if (!MMIO_internal_GetStreamGroupListEntry(pStreamGroup))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Oops, internal error! */
    return MMIO_ERROR_UNKNOWN;
  }

  /* First of all, stop its playback! */
  pTerm = (mmioTermSpecificInfo_p) (pNodeToRemove->pTypeSpecificInfo);
  pTerm->mmioterm_SetDirection(pNodeToRemove->pNodeOwnerPluginInstance, pTerm->pTermID, MMIO_DIRECTION_STOP);

  /* Remove link from node */
  tpl_mtxsemRequest(pNodeToRemove->hmtxUseOwnerStreamGroupField, TPL_WAIT_FOREVER);
  pNodeToRemove->pOwnerStreamGroup = NULL;
  tpl_mtxsemRelease(pNodeToRemove->hmtxUseOwnerStreamGroupField);

  /* Remove node from stream group */
  pNodeToRemove->pOwnerStreamGroup = NULL;

  pPrev = NULL;
  pStream = pStreamGroup->pStreamList;
  while ((pStream) && (pStream->pNode!=pNodeToRemove))
  {
    pPrev = pStream;
    pStream = pStream->pNext;
  }

  if (pStream)
  {
    /* So, unlink this stream from stream-group! */
    if (pPrev)
      pPrev->pNext = pStream->pNext;
    else
      pStreamGroup->pStreamList = pStream->pNext;

    /* Set new main-stream, if necessary! */
    if (pStreamGroup->pMainStream == pStream)
    {
      pStreamGroup->pMainStream = NULL;
      if (pStreamGroup->pStreamList)
      {
        /* Ok, time to select the best stream from the available streams! */
        pStream = pStreamGroup->pStreamList;
        while (pStream)
        {
          pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);
          if (!(pStreamGroup->pMainStream))
            pStreamGroup->pMainStream = pStream;
          else
          {
            pMainTerm = (mmioTermSpecificInfo_p) (pStreamGroup->pMainStream->pNode->pTypeSpecificInfo);

            if (pTerm->iStreamType == MMIO_STREAMTYPE_AUDIO)
            {
              /* Let the first available audio stream be the main stream! */
              pStreamGroup->pMainStream = pStream;
              break;
            } else
            if ((pTerm->iStreamType == MMIO_STREAMTYPE_VIDEO) && (pMainTerm->iStreamType != MMIO_STREAMTYPE_AUDIO) && (pMainTerm->iStreamType != MMIO_STREAMTYPE_VIDEO))
            {
              /* Use this stream as main stream, but look for better alternatives! */
              pStreamGroup->pMainStream = pStream;
            }
          }
          pStream = pStream->pNext;
        }
      }
    }

    /* Re-calculate stream-group capabilities */
    if (!(pStreamGroup->pStreamList))
    {
      pStreamGroup->iTermCapabilities = 0;
    } else
    {
      pStream = pStreamGroup->pStreamList;
      pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);
      pStreamGroup->iTermCapabilities = pTerm->iTermCapabilities;
      while (pStream)
      {
        pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);
        pStreamGroup->iTermCapabilities &= pTerm->iTermCapabilities;
        pStream = pStream->pNext;
      }
    }
  }

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetMainStreamOfGroup(mmioStreamGroup_p pStreamGroup, mmioProcessTreeNode_p pNode)
{
  mmioStreamGroupStreamList_p pStream;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) || (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Check if it's a real stream group */
  if (!MMIO_internal_GetStreamGroupListEntry(pStreamGroup))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Oops, internal error! */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Set new main-stream */
  pStream = pStreamGroup->pStreamList;
  while (pStream)
  {
    if (pStream->pNode == pNode)
    {
      pStreamGroup->pMainStream = pStream;
      break;
    }
    pStream = pStream->pNext;
  }

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  if (pStream)
    return MMIO_NOERROR;
  else
    return MMIO_ERROR_NOT_FOUND;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIODestroyStreamGroup(mmioStreamGroup_p pStreamGroup)
{
  mmioResult_t rc;
  mmioStreamGroupStreamList_p pStream, pToDelete;
  mmioStreamGroupList_p pInternalListEntry, pInternalListEntryPrev;
  mmioTermSpecificInfo_p pTerm;
  mmioEventMessageQueueElement_t Event;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Look for this stream in our internal list */
  /* Check if it's a real stream group */

  rc = MMIO_NOERROR;
  if (!tpl_mtxsemRequest(hmtxUseStreamGroupList, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  /* Look for this stream group in the list */
  pInternalListEntryPrev = NULL;
  pInternalListEntry = pStreamGroupListHead;
  while ((pInternalListEntry) && (pInternalListEntry->pStreamGroup != pStreamGroup))
  {
    pInternalListEntryPrev = pInternalListEntry;
    pInternalListEntry = pInternalListEntry->pNext;
  }

  if (!pInternalListEntry)
  {
    /* Could not find in our list! */
    rc = MMIO_ERROR_NOT_FOUND;
  } else
  {
    /* Unlink from list */
    if (pInternalListEntryPrev)
    {
      pInternalListEntryPrev->pNext = pInternalListEntry->pNext;
    } else
    {
      pStreamGroupListHead = pInternalListEntry->pNext;
    }
    /* Free our internal structure */
    MMIOfree(pInternalListEntry);

    /* Ok, this entry was unlinked and freed from our internal list */
  }
  tpl_mtxsemRelease(hmtxUseStreamGroupList);

  /* Now destroy the stream group itself! */
  if (rc == MMIO_NOERROR)
  {
    tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER);

    /* First of all, stop all the playback! */
    pStream = pStreamGroup->pStreamList;
    while (pStream)
    {
      pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);
      pTerm->mmioterm_SetDirection(pStream->pNode->pNodeOwnerPluginInstance, pTerm->pTermID, MMIO_DIRECTION_STOP);
      pStream = pStream->pNext;
    }

    /* Then remove all the nodes from the group */
    pStream = pStreamGroup->pStreamList;
    while (pStream)
    {
      tpl_mtxsemRequest(pStream->pNode->hmtxUseOwnerStreamGroupField, TPL_WAIT_FOREVER);
      pStream->pNode->pOwnerStreamGroup = NULL;
      tpl_mtxsemRelease(pStream->pNode->hmtxUseOwnerStreamGroupField);
      pToDelete = pStream;
      pStream = pStream->pNext;
      MMIOfree(pToDelete);
    }

    /* Remove all the events from the queue */
    while (tpl_msgqReceive(pStreamGroup->hmqEventMessageQueue,
                           &Event,
                           sizeof(Event),
                           250));

    /* Destroy the queue */
    tpl_msgqDelete(pStreamGroup->hmqEventMessageQueue);

    /* Destroy mutex semaphore */
    tpl_mtxsemDelete(pStreamGroup->hmtxUseStreamGroup);

    /* Free structure */
    MMIOfree(pStreamGroup);
  }

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSubscribeEvents(mmioStreamGroup_p pStreamGroup, long long llEventMask)
{
  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  pStreamGroup->iSubscribedEvents = llEventMask;

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetEvent(mmioStreamGroup_p pStreamGroup, int *pbFromMainStream, int *piEventCode, long long *pllEventParm, int iTimeOut)
{
  int rc;
  mmioEventMessageQueueElement_t Event;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) || (!pbFromMainStream) || (!piEventCode) || (!pllEventParm))
    return MMIO_ERROR_INVALID_PARAMETER;

  rc = tpl_msgqReceive(pStreamGroup->hmqEventMessageQueue,
                        &Event,
                        sizeof(Event),
                        iTimeOut);

  if (rc)
  {
    *pbFromMainStream = Event.bFromMainStream;
    *piEventCode = Event.iEventCode;
    *pllEventParm = Event.llEventParm;
    return MMIO_NOERROR;
  }
  else
    return MMIO_ERROR_TIMEOUT;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetDirection(mmioStreamGroup_p pStreamGroup, int iDirection)
{
  mmioResult_t rc;
  int iCapToCheck;
  mmioStreamGroupStreamList_p pStream;
  mmioTermSpecificInfo_p pTerm;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  rc = MMIO_NOERROR;
  switch (iDirection)
  {
    case MMIO_DIRECTION_STOP:
      iCapToCheck = MMIO_TERM_CAPS_DIRECTION_STOP;
      break;
    case MMIO_DIRECTION_PLAY:
      iCapToCheck = MMIO_TERM_CAPS_DIRECTION_PLAY;
      break;
    case MMIO_DIRECTION_REVERSE:
      iCapToCheck = MMIO_TERM_CAPS_DIRECTION_REVERSE;
      break;
    default:
      if (iDirection>0)
        iCapToCheck = MMIO_TERM_CAPS_DIRECTION_CUSTOMPLAY;
      else
        iCapToCheck = MMIO_TERM_CAPS_DIRECTION_CUSTOMREVERSE;
      break;
  }

  if (!(pStreamGroup->iTermCapabilities & iCapToCheck))
    rc = MMIO_ERROR_NOT_SUPPORTED;
  else
  {
    pStreamGroup->iDirection = iDirection;

    pStream = pStreamGroup->pStreamList;
    while (pStream)
    {
      pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);

      if (pTerm->mmioterm_SetDirection(pStream->pNode->pNodeOwnerPluginInstance, pTerm->pTermID, iDirection)!=MMIO_NOERROR)
        rc = MMIO_ERROR_IN_PLUGIN;
      pStream = pStream->pNext;
    }
  }
  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOSetPosition(mmioStreamGroup_p pStreamGroup, long long llPosition)
{
  mmioResult_t rc;
  mmioStreamGroupStreamList_p pStream;
  mmioTermSpecificInfo_p pTerm;
  long long llPosFound;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  rc = MMIO_NOERROR;

  if (!(pStreamGroup->iTermCapabilities & MMIO_TERM_CAPS_SETPOSITION))
    rc = MMIO_ERROR_NOT_SUPPORTED;
  else
  {
    pStream = pStreamGroup->pStreamList;
    while (pStream)
    {
      pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);

      if (pTerm->mmioterm_SetPosition(pStream->pNode->pNodeOwnerPluginInstance, pTerm->pTermID, llPosition, MMIO_POSTYPE_TIME, &llPosFound)!=MMIO_NOERROR)
        rc = MMIO_ERROR_IN_PLUGIN;
      else
      {
        /* Update stream position */
        pStream->llLastTimeStamp = llPosFound;
        pStream->LastTimeStampTime = MMIOPsGetCurrentSystemTime();
      }

      pStream = pStream->pNext;
    }
  }
  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetDirection(mmioStreamGroup_p pStreamGroup, int *piDirection)
{
  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) || (!piDirection))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  *piDirection = pStreamGroup->iDirection;

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetPosition(mmioStreamGroup_p pStreamGroup, long long *pllPosition)
{
  mmioResult_t rc;
  long long llPosition;
  mmioSystemTime_t TimeDiff;
  long long llSignedTimeDiff;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) && (pllPosition))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  if (pStreamGroup->pMainStream)
  {

    llPosition = pStreamGroup->pMainStream->llLastTimeStamp;

    if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
    {
      TimeDiff = (MMIOPsGetCurrentSystemTime() - pStreamGroup->pMainStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
      if (TimeDiff > pStreamGroup->pMainStream->llLastFrameLength)
        TimeDiff = pStreamGroup->pMainStream->llLastFrameLength;

      llSignedTimeDiff = TimeDiff;
      llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

      llPosition += llSignedTimeDiff;
    }

    *pllPosition = llPosition;
    rc = MMIO_NOERROR;
  } else
    rc = MMIO_ERROR_NO_STREAM_IN_GROUP;

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOGetLength(mmioStreamGroup_p pStreamGroup, long long *pllLength)
{
  mmioResult_t rc;
  mmioTermSpecificInfo_p pTerm;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pStreamGroup) && (pllLength))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  if (pStreamGroup->pMainStream)
  {
    pTerm = (mmioTermSpecificInfo_p) (pStreamGroup->pMainStream->pNode->pTypeSpecificInfo);
    rc = pTerm->mmioterm_GetStreamLength(pStreamGroup->pMainStream->pNode->pNodeOwnerPluginInstance, pTerm->pTermID, pllLength);
  } else
    rc = MMIO_ERROR_NO_STREAM_IN_GROUP;

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOShowStreamGroup(mmioStreamGroup_p pStreamGroup)
{
  mmioStreamGroupStreamList_p pStream;
  mmioTermSpecificInfo_p pTerm;
  char *pchType;
  long long llPosition, llMainPosition;
  mmioSystemTime_t TimeDiff;
  long long llSignedTimeDiff;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  printf("-- Stream group %p --\n", pStreamGroup);
  printf("* Direction: %d\n", pStreamGroup->iDirection);
  printf("* TermCaps : %d\n", pStreamGroup->iTermCapabilities);
  printf("* Streams:\n");

  if (pStreamGroup->pMainStream)
  {
    /* Calculate position of main stream */
    llMainPosition = pStreamGroup->pMainStream->llLastTimeStamp;

    if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
    {
      TimeDiff = (MMIOPsGetCurrentSystemTime() - pStreamGroup->pMainStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
      if (TimeDiff > pStreamGroup->pMainStream->llLastFrameLength)
        TimeDiff = pStreamGroup->pMainStream->llLastFrameLength;

      llSignedTimeDiff = TimeDiff;
      llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

      llMainPosition += llSignedTimeDiff;
    }
  } else
    llMainPosition = 0;

  pStream = pStreamGroup->pStreamList;
  while (pStream)
  {
    pTerm = (mmioTermSpecificInfo_p) (pStream->pNode->pTypeSpecificInfo);

    /* Check stream type */
    switch (pTerm->iStreamType)
    {
      case MMIO_STREAMTYPE_AUDIO:
        pchType = "A";
        break;
      case MMIO_STREAMTYPE_VIDEO:
        pchType = "V";
        break;
      case MMIO_STREAMTYPE_SUBTITLE:
        pchType = "S";
        break;
      default:
        pchType = "?";
        break;
    }

    /* Calculate position of this stream */
    llPosition = pStream->llLastTimeStamp;

    if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
    {
      TimeDiff = (MMIOPsGetCurrentSystemTime() - pStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
      if (TimeDiff > pStream->llLastFrameLength)
        TimeDiff = pStream->llLastFrameLength;

      llSignedTimeDiff = TimeDiff;
      llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

      llPosition += llSignedTimeDiff;
    }

    printf("  [%s%s] : Pos: %lld  FrLen: %lld  DiffFromMain: %lld\n",
           (pStream == pStreamGroup->pMainStream)?"M":" ",
           pchType,
           llPosition,
           pStream->llLastFrameLength,
           llMainPosition - llPosition);

    pStream = pStream->pNext;
  }

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioSystemTime_t MMIOCALL MMIOPsGetCurrentSystemTime()
{
  return tpl_hrtimerGetTime();
}

MMIOIMPEXP mmioSystemTime_t MMIOCALL MMIOPsGetOneSecSystemTime()
{
  return tpl_hrtimerGetOneSecTime();
}

MMIOIMPEXP void             MMIOCALL MMIOPsGuessPixelAspectRatio(int iImageWidth, int iImageHeight, int *piPixelAspectRatioCount, int *piPixelAspectRatioDenom)
{
  if ((!piPixelAspectRatioCount) || (!piPixelAspectRatioDenom))
    return;

  /* Default is 1:1 pixel aspect ratio */
  *piPixelAspectRatioCount = 1;
  *piPixelAspectRatioDenom = 1;

  switch (iImageWidth)
  {
    case 720:
    case 704:
    case 480:
      if ((iImageHeight==480) || (iImageHeight==576))
      {
        /*
         720x480 : Seems to be D1 (NTSC) resolution.
         720x576 : Seems to be D1 (PAL) resolution.

         704x480 : Seems to be 1/1 D1 (NTSC) resolution.
         704x576 : Seems to be 1/1 D1 (PAL) resolution.

         480x480 : Seems to be SVCD (NTSC) resolution.
         480x576 : Seems to be SVCD (PAL) resolution.

         So, set pixel ratio for 4:3 screen with this resolution!
         */

        *piPixelAspectRatioCount = iImageWidth*3;
        *piPixelAspectRatioDenom = iImageHeight*4;
      }
      break;

    case 352:
      if ((iImageHeight==480) || (iImageHeight==576) ||
          (iImageHeight==240) || (iImageHeight==288))
      {
        /*
         352x480 : Seems to be China Video Disc (CVD) and SVHS (NTSC) resolution.
         352x576 : Seems to be China Video Disc (CVD) and SVHS (PAL) resolution.

         352x240 : VHS and CIF (NTSC) resolution.
         352x288 : VHS and CIF (PAL) resolution.

         So, set pixel ratio for 4:3 screen with this resolution!
         */

        *piPixelAspectRatioCount = iImageWidth*3;
        *piPixelAspectRatioDenom = iImageHeight*4;
      }
      break;
    case 176:
      if ((iImageHeight==120) || (iImageHeight==144))
      {
        /*
         176x120 : QCIF (NTSC) resolution.
         176x144 : QCIF (PAL) resolution.

         So, set pixel ratio for 4:3 screen with this resolution!
         */

        *piPixelAspectRatioCount = iImageWidth*3;
        *piPixelAspectRatioDenom = iImageHeight*4;
      }
      break;

    default:
      break;
  }
}

MMIOIMPEXP mmioResult_t     MMIOCALL MMIOPsReportPosition(mmioProcessTreeNode_p pNode,
                                                              mmioSystemTime_t      TimeWhenPositionReached,
                                                              long long            llPosition,
                                                              long long            llFrameLength,
                                                              long long           *pllSyncDiff)
{
  mmioResult_t rc;
  mmioStreamGroupStreamList_p pStream;
  mmioStreamGroup_p pStreamGroup;
  long long llThisPosition, llMainPosition;
  mmioSystemTime_t TimeDiff;
  long long llSignedTimeDiff;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pNode)
    return MMIO_ERROR_INVALID_PARAMETER;

  pStreamGroup = pNode->pOwnerStreamGroup;
  if (!pStreamGroup)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (!tpl_mtxsemRequest(pStreamGroup->hmtxUseStreamGroup, TPL_WAIT_FOREVER))
    return MMIO_ERROR_UNKNOWN;

  rc = MMIO_NOERROR;

  /* Set position info */
  pStream = pStreamGroup->pStreamList;
  while ((pStream) && (pStream->pNode != pNode))
    pStream = pStream->pNext;

  if (!pStream)
  {
    rc = MMIO_ERROR_NOT_FOUND;
  } else
  {
    pStream->LastTimeStampTime = TimeWhenPositionReached;
    pStream->llLastTimeStamp = llPosition;
    pStream->llLastFrameLength = llFrameLength;

    /* Calculate position of main stream */
    if (pStreamGroup->pMainStream)
    {
      llMainPosition = pStreamGroup->pMainStream->llLastTimeStamp;

      if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
      {
        TimeDiff = (MMIOPsGetCurrentSystemTime() - pStreamGroup->pMainStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
        if (TimeDiff > pStreamGroup->pMainStream->llLastFrameLength)
          TimeDiff = pStreamGroup->pMainStream->llLastFrameLength;

        llSignedTimeDiff = TimeDiff;
        llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

        llMainPosition += llSignedTimeDiff;
      }
    } else
      llMainPosition = 0;

    /* Calculate position of this stream */
    if (pStream == pStreamGroup->pMainStream)
      llThisPosition = llMainPosition;
    else
    {
      llThisPosition = pStream->llLastTimeStamp;

      if (pStreamGroup->iDirection!=MMIO_DIRECTION_STOP)
      {
        TimeDiff = (MMIOPsGetCurrentSystemTime() - pStream->LastTimeStampTime) * 1000 / MMIOPsGetOneSecSystemTime();
        if (TimeDiff > pStream->llLastFrameLength)
          TimeDiff = pStream->llLastFrameLength;

        llSignedTimeDiff = TimeDiff;
        llSignedTimeDiff = llSignedTimeDiff * pStreamGroup->iDirection / 1000;

        llThisPosition += llSignedTimeDiff;
      }
    }

    /* Report time diff */
    if (pllSyncDiff)
      *pllSyncDiff = llMainPosition - llThisPosition;
  }

  tpl_mtxsemRelease(pStreamGroup->hmtxUseStreamGroup);

  return rc;
}

MMIOIMPEXP mmioResult_t     MMIOCALL MMIOPsReportEvent(mmioProcessTreeNode_p pNode,
                                                           int iEventCode, long long llEventParm)
{
  mmioStreamGroup_p pStreamGroup;
  mmioEventMessageQueueElement_t Event;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pNode)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* This mutex is required here so destroying a stream group will not crash us here */
  if (!tpl_mtxsemRequest(pNode->hmtxUseOwnerStreamGroupField, TPL_WAIT_FOREVER))
  {
    /* Ooops, could not get mutex! */
    return MMIO_ERROR_UNKNOWN;
  }

  pStreamGroup = pNode->pOwnerStreamGroup;
  if (pStreamGroup)
  {
    if (pStreamGroup->iSubscribedEvents & iEventCode)
    {
      if (!(pStreamGroup->pMainStream))
        Event.bFromMainStream = 1;
      else
        Event.bFromMainStream = (pNode == pStreamGroup->pMainStream->pNode);
      Event.iEventCode = iEventCode;
      Event.llEventParm = llEventParm;
      tpl_msgqSend(pStreamGroup->hmqEventMessageQueue, &Event, sizeof(Event));
    }
  }

  tpl_mtxsemRelease(pNode->hmtxUseOwnerStreamGroupField);

  return MMIO_NOERROR;
}

MMIOIMPEXP mmioProcessTreeNode_p MMIOCALL MMIOPsCreateNewEmptyNodeStruct()
{
  mmioProcessTreeNode_p pNode;

  pNode = (mmioProcessTreeNode_p) MMIOmalloc(sizeof(mmioProcessTreeNode_t));
  if (!pNode)
  {
    /* Out of memory */
    return NULL;
  }

  memset(pNode, 0, sizeof(mmioProcessTreeNode_t));

  pNode->hmtxUseOwnerStreamGroupField = tpl_mtxsemCreate(0);
  if (!(pNode->hmtxUseOwnerStreamGroupField))
  {
    /* Out of resources */
    MMIOfree(pNode);
    return NULL;
  }

  return pNode;
}

MMIOIMPEXP mmioProcessTreeNode_p MMIOCALL MMIOPsCreateAndLinkNewNodeStruct(mmioProcessTreeNode_p pParent)
{
  int bURLNode;
  mmioProcessTreeNode_p pNode, pPrev;

  /* Check parameters */
  if (!pParent)
    return NULL;

  /* Create a new empty node */
  pNode = MMIOPsCreateNewEmptyNodeStruct();
  if (!pNode)
    return NULL;

  /* Fill some fields */
  pNode->iTreeLevel = pParent->iTreeLevel+1;
  pNode->pParent = pParent;

  if (pParent == pRootNode)
    bURLNode = 1;
  else
    bURLNode = 0;

  if (bURLNode)
  {
      /* Grab mutex semaphore */
    if (!tpl_mtxsemRequest(hmtxUseRootNode, TPL_WAIT_FOREVER))
    {
      MMIOPsDestroyNodeStruct(pNode);
      return NULL;
    }
  }

  /* Link it to parent's children list */
  if (pParent->pFirstChild == NULL)
    pParent->pFirstChild = pNode;
  else
  {
    pPrev = pParent->pFirstChild;
    while (pPrev->pNextBrother)
      pPrev = pPrev->pNextBrother;

    pPrev->pNextBrother = pNode;
  }

  if (bURLNode)
    tpl_mtxsemRelease(hmtxUseRootNode);

  return pNode;
}

MMIOIMPEXP mmioResult_t     MMIOCALL MMIOPsDestroyNodeStruct(mmioProcessTreeNode_p pNode)
{
  if (!pNode)
    return MMIO_ERROR_INVALID_PARAMETER;

  tpl_mtxsemDelete(pNode->hmtxUseOwnerStreamGroupField); pNode->hmtxUseOwnerStreamGroupField = NULL;
  MMIOfree(pNode);
  return MMIO_NOERROR;
}

MMIOIMPEXP mmioResult_t     MMIOCALL MMIOPsUnlinkAndDestroyNodeStruct(mmioProcessTreeNode_p pNode)
{
  mmioProcessTreeNode_p pPrev;

  if (!pNode)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Unlink from parent first */
  if (pNode->pParent)
  {
    if (pNode->pParent->pFirstChild == pNode)
    {
      /* This is the first child, so easy to unlink. */
      pNode->pParent->pFirstChild = pNode->pNextBrother;
    } else
    {
      /* This is not the first child, so look for it in list */
      pPrev = pNode->pParent->pFirstChild;
      if (pPrev)
      {
        while ((pPrev->pNextBrother) &&
               (pPrev->pNextBrother != pNode))
          pPrev = pPrev->pNextBrother;

        if (pPrev)
        {
          /* Found the previous node, so unlink it from list */
          pPrev->pNextBrother = pNode->pNextBrother;
        }
      }
    }

    pNode->pParent = NULL;
  }
  /* Node is unlinked, now destroy memory structure and free resources */
  return MMIOPsDestroyNodeStruct(pNode);
}

static int MMIO_internal_WildcardMatch(char *pchMask, char *pchValue)
{
  int i;
  int bWildcardFound = 0;
  int bRestart;

  /* This wildcard matching algorithm is based on the work of
   * Alessandro Cantatore. His algorithms are available at
   * http://xoomer.virgilio.it/acantato/dev/wildcard/wildmatch.html
   */

  /* Empty strings do not match anything. This is a special requirement
   * for MMIO.
   */
  if ((!pchMask) || (!pchValue) || (!(*pchMask)) || (!(*pchValue)))
    return 0;

  /* Now do the real matching */
  do {
    bRestart = 0;

    i = 0;
    while ((pchValue[i]) && (!bRestart))
    {
      switch (pchMask[i])
      {
        case '?':
          break;
        case '*':
          bWildcardFound = 1;
          pchMask += i+1; pchValue += i;
          /* Remove extra wildcards */
          while (*pchMask == '*')
            pchMask++;
          /* If this wildcard was the last one of mask, */
          /* then we're done, it matches all the remaining value */
          if (!(*pchMask))
            return 1;
          /* Otherwise we have more job */
          bRestart = 1;
          break;
        default:
          if (pchValue[i] != pchMask[i])
          {
            if (!bWildcardFound)
              return 0;
            pchValue++;
            bRestart = 1;
          }
          break;
      }
      i++;
    }
  } while (bRestart);

  while (pchMask[i] == '*')
    i++;

  return (pchMask[i] == 0);
}

static int MMIO_internal_FormatsMatch(char *pchFormat, char *pchSupportedFormats)
{
  int result;
  char *pchSupportedFormatsCopy;
  char *pchOneSupportedFormat;
  char *pchTemp;

  if ((!pchFormat) || (!pchSupportedFormats))
    return 0;

  /* Create a local copy of pchSupportedFormats because we'll f*ck up the string */
  pchSupportedFormatsCopy = (char *) MMIOmalloc(strlen(pchSupportedFormats)+1);
  if (!pchSupportedFormatsCopy)
    return 0;
  strcpy(pchSupportedFormatsCopy, pchSupportedFormats);

  result = 0;

  pchOneSupportedFormat = pchSupportedFormatsCopy;
  pchTemp = pchSupportedFormatsCopy;
  while ((*pchTemp) && (result == 0))
  {
    if (*pchTemp == ';')
    {
      /* Found a format separator */
      *pchTemp = 0;
      result = MMIO_internal_WildcardMatch(pchOneSupportedFormat, pchFormat);
      if (!result)
        pchOneSupportedFormat = pchTemp+1;
    }
    pchTemp++;
  }

  /* Check the last one too! */
  if (!result)
    result = MMIO_internal_WildcardMatch(pchOneSupportedFormat, pchFormat);

  MMIOfree(pchSupportedFormatsCopy);
  return result;
}

static mmioUsablePluginList_p MMIO_internal_CreateUsablePluginListForFormat(mmioAvailablePluginList_p pPluginList, char *pchFormat)
{
  mmioUsablePluginList_p pResult, pNew, pPrev, pTemp;

  if ((!pPluginList) || (!pchFormat))
    return NULL;

  pResult = NULL;

  while (pPluginList)
  {
    if (MMIO_internal_FormatsMatch(pchFormat, pPluginList->pchSupportedFormats))
    {
      /* This is a plugin which could be used for this node, so put it */
      /* into the list of plugins to try, sorted by importance. */
      pNew = (mmioUsablePluginList_p) MMIOmalloc(sizeof(mmioUsablePluginList_t));
      if (pNew)
      {
        pNew->pPlugin = pPluginList;
        
        /* Find a place for this entry in the sorted list */
        if (!pResult)
        {
          pResult = pNew;
          pNew->pNext = NULL;
        } else
        {
          pPrev = NULL;
          pTemp = pResult;
          while ((pTemp) && (pTemp->pPlugin->iPluginImportance <= pNew->pPlugin->iPluginImportance))
          {
            pPrev = pTemp;
            pTemp = pTemp->pNext;
          }

          if (!pPrev)
          {
            pNew->pNext = pResult;
            pResult = pNew;
          } else
          {
            pNew->pNext = pPrev->pNext;
            pPrev->pNext = pNew;
          }
        }
      }
    }
    pPluginList = pPluginList->pNext;
  }

  return pResult;
}

static void MMIO_internal_DestroyUsablePluginList(mmioUsablePluginList_p pList)
{
  mmioUsablePluginList_p pToDelete;

  while (pList)
  {
    pToDelete = pList;
    pList = pList->pNext;
    MMIOfree(pToDelete);
  }
}

static void MMIO_internal_SetOwnerPluginOfChildrenNodes(mmioProcessTreeNode_p pParentNode,
                                                       mmioLoadedPluginList_p hPlugin, void *pInstance,
                                                       int bPluginWasLoadedBySystem)
{
  mmioProcessTreeNode_p pNode;

  pNode = pParentNode->pFirstChild;
  while (pNode)
  {
    pNode->pNodeOwnerPluginHandle = hPlugin;
    pNode->pNodeOwnerPluginInstance = pInstance;
    pNode->bPluginWasLoadedBySystem = bPluginWasLoadedBySystem;

    if (pNode->pFirstChild)
    {
      /* It has children! */
      MMIO_internal_SetOwnerPluginOfChildrenNodes(pNode,
                                                hPlugin, pInstance,
                                                bPluginWasLoadedBySystem);
    }

    pNode = pNode->pNextBrother;
  }
}

static mmioResult_t MMIO_internal_CreateSortedFormatList(mmioNodeExamineResult_p pExamineResult, mmioProcessTreeNode_p pNode, pfn_mmioopen_SortList pfnSortList, int *piNumEntries, char ***pppchList)
{
  int i, bBad;

  /* Don't sort if there is no sorting function */
  if (!pfnSortList)
    return MMIO_ERROR_UNKNOWN;

  *piNumEntries = pExamineResult->iNumOfEntries;
  *pppchList = (char **) MMIOmalloc(pExamineResult->iNumOfEntries);

  if (!(*pppchList))
    return MMIO_ERROR_OUT_OF_MEMORY;

  bBad = 0;
  for (i=0; i<pExamineResult->iNumOfEntries; i++)
  {
    (*pppchList)[i] = MMIOmalloc(strlen(pExamineResult->pOutputFormats[i])+1);
    if ((*pppchList)[i])
      strcpy((*pppchList)[i], pExamineResult->pOutputFormats[i]);
    else
    {
      bBad = 1;
      break;
    }
  }

  if (bBad)
  {
    /* Ops, there was a problem creating the list! */
    for (i=0; i<pExamineResult->iNumOfEntries; i++)
    {
      if ((*pppchList)[i])
        MMIOfree((*pppchList)[i]);
    }
    MMIOfree(*pppchList);

    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Ok, list was created, now sort it! */
  (*pfnSortList)(MMIO_SORTLIST_LISTTYPE_FORMATLIST, pNode, pExamineResult->iNumOfEntries, *pppchList);

  return MMIO_NOERROR;
}

static void MMIO_internal_DestroySortedFormatList(int iNumEntries, char **ppchList)
{
  int i;

  for (i=0; i<iNumEntries; i++)
  {
    if (ppchList[i])
      MMIOfree(ppchList[i]);
  }

  MMIOfree(ppchList);
}

static mmioResult_t MMIO_internal_CreateSortedPluginList(mmioUsablePluginList_p pUsablePluginList, mmioProcessTreeNode_p pNode, pfn_mmioopen_SortList pfnSortList, int *piNumEntries, mmioAvailablePluginList_p **ppSortedPluginList)
{
  mmioUsablePluginList_p pUsablePlugin;
  int i;

  /* Don't sort if there is no sorting function */
  if (!pfnSortList)
    return MMIO_ERROR_UNKNOWN;

  /* Count the number of entries */
  *piNumEntries = 0;
  pUsablePlugin = pUsablePluginList;
  while (pUsablePlugin)
  {
    (*piNumEntries)++;
    pUsablePlugin = pUsablePlugin->pNext;
  }

  (*ppSortedPluginList) = MMIOmalloc((*piNumEntries) * sizeof(mmioAvailablePluginList_p));
  if ((*ppSortedPluginList) == NULL)
  {
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  i = 0;
  pUsablePlugin = pUsablePluginList;
  while (pUsablePlugin)
  {
    (*ppSortedPluginList)[i] = pUsablePlugin->pPlugin;
    i++;
    pUsablePlugin = pUsablePlugin->pNext;
  }

  /* Ok, list was created, now sort it! */
  (*pfnSortList)(MMIO_SORTLIST_LISTTYPE_PLUGINLIST, pNode, (*piNumEntries), *ppSortedPluginList);

  return MMIO_NOERROR;
}

static void MMIO_internal_DestroySortedPluginList(int iNumEntries, mmioAvailablePluginList_p *pSortedPluginList)
{
  MMIOfree(pSortedPluginList);
}

/* Prototyping, Forward declaration */
static int MMIO_internal_OpenCore(mmioProcessTreeNode_p pParentNode, mmioAvailablePluginList_p pPlugin, int iOpenLevel, mmioAvailablePluginList_p pPluginList, pfn_mmioopen_SortList pfnSortList);

static void MMIO_internal_OpenCoreWalkTree(mmioProcessTreeNode_p pParentNode, int iOpenLevel, mmioAvailablePluginList_p pPluginList, pfn_mmioopen_SortList pfnSortList)
{
  mmioUsablePluginList_p pUsablePluginList, pUsablePlugin;
  mmioProcessTreeNode_p pNode;
  int iNumEntries, i;
  mmioAvailablePluginList_p *pSortedPluginList;

  if (pParentNode->iNodeType>=iOpenLevel)
    return;

  pNode = pParentNode->pFirstChild;
  while (pNode)
  {
    if (pNode->pFirstChild)
    {
      MMIO_internal_OpenCoreWalkTree(pNode, iOpenLevel, pPluginList, pfnSortList);
    }
    else
    {
      /* This is a leaf node, so if it's not a terminator node, then we should try to */
      /* build the tree from here */
      if (pNode->iNodeType!=MMIO_NODETYPE_TERMINATOR)
      {
        /* Look for a plugin which supports this node, and link it here */
        pUsablePluginList = MMIO_internal_CreateUsablePluginListForFormat(pPluginList, pNode->achNodeOwnerOutputFormat);
        if (pUsablePluginList)
        {
          /* Now that we have a list of plugins which support this node, sorted */
          /* by plugin importance, we can go through these and try to build the */
          /* process tree. */

          if (pUsablePluginList)
          {
            /* Ok, there is at least one plugin for this node */

            if (pUsablePluginList->pNext == NULL)
            {
              /* There is only one plugin for this node, so no need for heuristics */
              MMIO_internal_OpenCore(pNode, pUsablePluginList->pPlugin, iOpenLevel, pPluginList, pfnSortList);
            } else
            {
              /* There are more than one plugins for this node, so create a sorted list of plugins and */
              /* try in that order! */
              if (MMIO_internal_CreateSortedPluginList(pUsablePluginList, pNode, pfnSortList, &iNumEntries, &pSortedPluginList) != MMIO_NOERROR)
              {
                /* Oops, could not create sorted plugin list, so use the old way: go through the unsorted list */
                pUsablePlugin = pUsablePluginList;
                while (pUsablePlugin)
                {
                  if (MMIO_internal_OpenCore(pNode, pUsablePlugin->pPlugin, iOpenLevel, pPluginList, pfnSortList))
                  {
                    /* Get out of loop */
                    pUsablePlugin = NULL;
                  } else
                  {
                    pUsablePlugin = pUsablePlugin->pNext;
                  }
                }
              } else
              {
                /* We have a sorted plugin list, go through that one! */
                for (i=0; i<iNumEntries; i++)
                {
                  if (MMIO_internal_OpenCore(pNode, pSortedPluginList[i], iOpenLevel, pPluginList, pfnSortList))
                    break;
                }
                /* Free allocated memory */
                MMIO_internal_DestroySortedPluginList(iNumEntries, pSortedPluginList);
              }
            }
          }
          MMIO_internal_DestroyUsablePluginList(pUsablePluginList); pUsablePluginList = NULL;
        }
      }
    }
    pNode = pNode->pNextBrother;
  }
}

static int MMIO_internal_OpenCore(mmioProcessTreeNode_p pParentNode, mmioAvailablePluginList_p pPlugin, int iOpenLevel, mmioAvailablePluginList_p pPluginList, pfn_mmioopen_SortList pfnSortList)
{
  mmioResult_t rc;
  int i;
  mmioLoadedPluginList_p   hPlugin;
  void                  *pInstance;
  mmioNodeExamineResult_p  pExamineResult;

  /* Load the required plugin, initialize, link to the required node, and */
  /* build more levels of the tree then! */

  if (MMIOLoadPlugin(pPlugin->pchPluginInternalName, pPlugin->pchPluginFileName, &hPlugin) != MMIO_NOERROR)
  {
    /* Could not load required plugin! */
    return 0;
  }

  if (MMIOInitializePlugin(hPlugin, &pInstance)!=MMIO_NOERROR)
  {
    /* Could not initialize plugin */
    MMIOUnloadPlugin(hPlugin);
    return 0;
  }

  if (MMIOExamineNodeWithPlugin(hPlugin, pInstance, pParentNode, &pExamineResult) != MMIO_NOERROR)
  {
    /* Could not examine node! */
    MMIOUninitializePlugin(hPlugin, pInstance);
    MMIOUnloadPlugin(hPlugin);
    return 0;
  }

  if (pExamineResult->iNumOfEntries<=0)
  {
    /* Could not examine node! */
    MMIOFreeExamineResult(hPlugin, pInstance, pExamineResult);
    MMIOUninitializePlugin(hPlugin, pInstance);
    MMIOUnloadPlugin(hPlugin);
    return 0;
  }

  if (pExamineResult->iNumOfEntries <= 0)
    rc = MMIO_ERROR_UNKNOWN;
  else
  if (pExamineResult->iNumOfEntries == 1)
  {
    /* Easy case, there is only one possible output format, so choose that one */
    rc = MMIOLinkPluginToNode(hPlugin, pInstance, pExamineResult->pOutputFormats[0], pParentNode);
  } else
  {
    int iNumEntries;
    char **ppchList;

    /* Not so easy case: there are more than one possible output formats. */
    /* We will sort that list with the external sorting algorithm, and go through */
    /* that list in that sorted order, trying to use that given format. */
    rc = MMIO_internal_CreateSortedFormatList(pExamineResult, pParentNode, pfnSortList, &iNumEntries, &ppchList);
    if (rc != NO_ERROR)
    {
      /* Could not create a sorted list, so we'll go through the original unsorted list */
      for (i=0; i<pExamineResult->iNumOfEntries; i++)
      {
        rc = MMIOLinkPluginToNode(hPlugin, pInstance, pExamineResult->pOutputFormats[i], pParentNode);
        if (rc == MMIO_NOERROR)
          break;
      }
    } else
    {
      /* Fine, we have a sorted list, so go through that sorted list */
      for (i=0; i<iNumEntries; i++)
      {
        rc = MMIOLinkPluginToNode(hPlugin, pInstance, ppchList[i], pParentNode);
        if (rc == MMIO_NOERROR)
          break;
      }
      /* Clean up */
      MMIO_internal_DestroySortedFormatList(iNumEntries, ppchList);
    }

  }

  /* Free examination result */
  MMIOFreeExamineResult(hPlugin, pInstance, pExamineResult);

  if (rc == MMIO_NOERROR)
  {
    /* Fine, we could link this plugin */

    /* Set stuffs in new levels! */
    MMIO_internal_SetOwnerPluginOfChildrenNodes(pParentNode,
                                              hPlugin, pInstance, 1);

    /* Build more levels if required */
    MMIO_internal_OpenCoreWalkTree(pParentNode, iOpenLevel, pPluginList, pfnSortList);

    return 1;
  } else
  {
    /* We could not link this plugin. */
    /* Clean up and return error. */

    MMIOUninitializePlugin(hPlugin, pInstance);
    MMIOUnloadPlugin(hPlugin);
    return 0;
  }
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOOpen(char *pchURL, int iOpenLevel, pfn_mmioopen_SortList pfnSortList, mmioProcessTreeNode_p *ppURL)
{
  mmioResult_t rc;
  mmioProcessTreeNode_p pResult;
  mmioURLSpecificInfo_p pURLSpecific;
  mmioUsablePluginList_p pUsablePluginList, pUsablePlugin;
  mmioAvailablePluginList_p pPluginList;
  int iLen;
  char *pchCopy;
  char *pchTemp;

  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if ((!pchURL) || (!ppURL))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* URL has to be at least:
   * "a://b"
   * so at least 5 characters
   */
  iLen = strlen(pchURL);
  if (iLen<5)
    return MMIO_ERROR_WRONG_URL;

  /* Make a local copy */
  pchCopy = (char *) MMIOmalloc(iLen+1);
  if (!pchCopy)
  {
    /* Out of memory! */
    return MMIO_ERROR_OUT_OF_MEMORY;
  }
  strcpy(pchCopy, pchURL);

  /* Search for the first ':' character */
  pchTemp = pchCopy;
  while ((*pchTemp) && (*pchTemp!=':'))
    pchTemp++;

  /* We assume that the evaluating of this 'if'
   * sentence is using the boolean shortcut stuff
   */
  if ((pchTemp[0]!=':') ||
      (pchTemp[1]!='/') ||
      (pchTemp[2]!='/'))
  {
    MMIOfree(pchCopy);
    return MMIO_ERROR_WRONG_URL;
  }

  /* Ok, it seems to be a good URL */
  /* pchCopy will now contain the internal plugin name */
  pchTemp[0] = 0;

  /* Let's look for a plugin which supports URL and has */
  /* this internal name! */
  rc = MMIOQueryRegisteredPluginList(&pPluginList);
  if (rc!=NO_ERROR)
  {
    MMIOfree(pchCopy);
    return rc;
  }
  pUsablePluginList = MMIO_internal_CreateUsablePluginListForFormat(pPluginList, "URL");
  if (!pUsablePluginList)
  {
    /* There is no plugin which supports URLs...*/
    rc = MMIO_ERROR_NO_SUCH_MEDIAHANDLER;
  } else
  {
    /* Now that we have a list of plugins which support URLs, sorted */
    /* by plugin importance, we can go through these and try to build the */
    /* process tree. */

    /* The default result code is that we did not find any matching media handler */
    rc = MMIO_ERROR_NO_SUCH_MEDIAHANDLER;

    pUsablePlugin = pUsablePluginList;
    while ((pUsablePlugin) && (rc == MMIO_ERROR_NO_SUCH_MEDIAHANDLER))
    {
      /* One extra step at this level (Media handler) is to */
      /* check for internal name of plugin, to match the one */
      /* given in the URL. */
      if (!stricmp(pUsablePlugin->pPlugin->pchPluginInternalName, pchCopy))
      {
        /* Okay, here is a media handler with the required internal name. */
        /* Let's try to build a process tree with it. If it won't work, then */
        /* go for another plugin. */

        /* Create an URL node */
        pResult = MMIOPsCreateAndLinkNewNodeStruct(pRootNode);
        if (pResult)
        {
          pResult->iNodeType = MMIO_NODETYPE_URL;
          strcpy(pResult->achNodeOwnerOutputFormat, "URL");
          pResult->bUnlinkPoint = 1;

          pResult->pTypeSpecificInfo = pURLSpecific = MMIOmalloc(sizeof(mmioURLSpecificInfo_t));
          if (pResult->pTypeSpecificInfo)
          {
            strncpy(pURLSpecific->achURL, pchURL, sizeof(pURLSpecific->achURL));

            if (MMIO_internal_OpenCore(pResult, pUsablePlugin->pPlugin, iOpenLevel, pPluginList, pfnSortList))
            {
              /* Good, we're done! */
              *ppURL = pResult;
              rc = MMIO_NOERROR;
            }
          }

          if (rc != MMIO_NOERROR)
          {
            /* Hm, could not build process tree */
            if (pResult->pTypeSpecificInfo)
            {
              MMIOfree(pResult->pTypeSpecificInfo);
              pResult->pTypeSpecificInfo = NULL;
            }
            MMIOPsUnlinkAndDestroyNodeStruct(pResult); pResult = NULL;
          }
        } else
        {
          /* Could not create new URL node! */
          rc = MMIO_ERROR_OUT_OF_MEMORY;
        }

      }
      pUsablePlugin = pUsablePlugin->pNext;
    }

    MMIO_internal_DestroyUsablePluginList(pUsablePluginList); pUsablePluginList = NULL;
  }

  MMIOFreeRegisteredPluginList(pPluginList);
  MMIOfree(pchCopy);
  return rc;
}

MMIOIMPEXP mmioResult_t MMIOCALL MMIOClose(mmioProcessTreeNode_p pURL)
{
  if (!bInitialized)
    return MMIO_ERROR_NOT_INITIALIZED;

  if (!pURL)
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pURL->iNodeType != MMIO_NODETYPE_URL)
    return MMIO_ERROR_INVALID_PARAMETER;

  return MMIOUnlinkNode(pURL);
}
