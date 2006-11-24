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
#include <fcntl.h>
#include <io.h>
#include <string.h>
#include <ctype.h>

#define MMIO_TRACK_MEMORY_USAGE
#include "MMIO.h"
#include "MMIOMem.h"

typedef struct mmioPluginInstance_s
{
  int hFile;
} mmioPluginInstance_t, *mmioPluginInstance_p;


MMIOPLUGINEXPORT long         MMIOCALL file_SendMsg(void *pInstance, long lCommandWord, void *pParam1, void *pParam2)
{
  return MMIO_ERROR_NOT_SUPPORTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Read(void *pInstance, void *pBuffer, long lBytes)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;
  long lTemp;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Try to read the required number of bytes */
  lTemp = read(pPluginInstance->hFile, pBuffer, lBytes);
  if (lTemp==-1)
  {
    /* -1 means error, so we could read zero bytes */
    lTemp = 0;
  }

  /* If we could not read enough, return failure, and go back in the file! */
  if (lTemp<lBytes)
  {
    _lseeki64(pPluginInstance->hFile, -lTemp, SEEK_CUR);
    return MMIO_ERROR_UNKNOWN;
  }

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Seek(void *pInstance, long long llPosition, int iBase)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;
  int iTranslatedBase;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Translate seek base from PE to native */
  switch (iBase)
  {
    default:
    case MMIO_MEDIA_SEEK_SET:
      iTranslatedBase = SEEK_SET;
      break;
    case MMIO_MEDIA_SEEK_CUR:
      iTranslatedBase = SEEK_CUR;
      break;
    case MMIO_MEDIA_SEEK_END:
      iTranslatedBase = SEEK_END;
      break;
  }

  /* lseek returns non-(-1) for success */
  if (_lseeki64(pPluginInstance->hFile, llPosition, iTranslatedBase) != -1I64)
    return MMIO_NOERROR;
  else
    return MMIO_ERROR_UNKNOWN;
}

MMIOPLUGINEXPORT long long    MMIOCALL file_Tell(void *pInstance)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  return _telli64(pPluginInstance->hFile);
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_ReadPacket(void *pInstance, void *pBuffer, long lBytes, long *plRead)
{
  return MMIO_ERROR_NOT_IMPLEMENTED;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_GetPluginDesc(char *pchDescBuffer, int iDescBufferSize)
{
  if ((!pchDescBuffer) || (iDescBufferSize<=0))
    return MMIO_ERROR_INVALID_PARAMETER;

  strncpy(pchDescBuffer, "Local File Reader v1.1", iDescBufferSize);
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT void *       MMIOCALL file_Initialize(void)
{
  mmioPluginInstance_p pInstance;

  pInstance = (mmioPluginInstance_p) MMIOmalloc(sizeof(mmioPluginInstance_t));
  if (pInstance)
  {
    pInstance->hFile = 0;
  }

  return pInstance;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Uninitialize(void *pInstance)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if (!pInstance)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Free resources allocated for instance structure */
  if (pPluginInstance->hFile)
  {
    close(pPluginInstance->hFile); pPluginInstance->hFile = 0;
  }
  MMIOfree(pInstance);

  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Examine(void *pInstance, mmioProcessTreeNode_p pNode, mmioNodeExamineResult_p *ppExamineResult)
{
  mmioURLSpecificInfo_p pURLInfo;
  char *pchFileName;
  char *pchExt;
  int hFile;

  /* Check parameters */
  if ((!pInstance) ||
      (!pNode) ||
      (!ppExamineResult))
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Media handlers only support URL nodes */
  if (pNode->iNodeType != MMIO_NODETYPE_URL)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Examine the URL, if we can handle it or not! */

  pURLInfo = pNode->pTypeSpecificInfo;
  if (!pURLInfo)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Extract filename. It starts from 7th byte of URL, because of
   * the 'file://' preamble.
   */
  pchFileName = &(pURLInfo->achURL[7]);

  /* Try to open the file */
  hFile = open(pchFileName,
               O_RDONLY | O_BINARY);
  if (hFile == -1)
  {
    /* File could not be opened, so we cannot do anything with this URL */
    return MMIO_ERROR_NOT_FOUND;
  }

  /* If the file could be opened, then fine, we can handle this URL */
  /* Close the filehandle then and report what we can do from it */
  close(hFile);

  *ppExamineResult = (mmioNodeExamineResult_p) MMIOmalloc(sizeof(mmioNodeExamineResult_t));
  if (!(*ppExamineResult))
    return MMIO_ERROR_OUT_OF_MEMORY;

  (*ppExamineResult)->iNumOfEntries = 1;
  (*ppExamineResult)->pOutputFormats = (mmioFormatDesc_t *) MMIOmalloc(sizeof(mmioFormatDesc_t) * (*ppExamineResult)->iNumOfEntries);

  if (!((*ppExamineResult)->pOutputFormats))
  {
    /* Could not allocate memory for possible output formats! */
    MMIOfree(*ppExamineResult); *ppExamineResult = NULL;
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Look for extension of file */
  pchExt = pchFileName;
  while (*pchFileName)
  {
    if (*pchFileName == '.')
      pchExt = pchFileName+1;

    pchFileName++;
  }

  /* Copy extension to format we handle */
  snprintf(&((*ppExamineResult)->pOutputFormats[0]),
           sizeof(mmioFormatDesc_t),
           "cont_%s", pchExt);

  /* Make the extension uppercased in the copy */
  pchExt = &((*ppExamineResult)->pOutputFormats[0]);
  pchExt += 5; /* Skip "cont_" string */

  while (*pchExt)
  {
    *pchExt = toupper(*pchExt);
    pchExt++;
  }

  /* Done! */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_FreeExamineResult(void *pInstance, mmioNodeExamineResult_p pExamineResult)
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

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Link(void *pInstance, char *pchNeededOutputFormat, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;
  mmioURLSpecificInfo_p pURLInfo;
  mmioMediaSpecificInfo_p pMediaInfo;
  char *pchFileName;
  mmioProcessTreeNode_p pNewNode;

  /* Check parameters */
  if ((!pInstance) ||
      (!pchNeededOutputFormat) ||
      (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pNode->iNodeType!=MMIO_NODETYPE_URL)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Open the file in the URL */
  pURLInfo = pNode->pTypeSpecificInfo;
  if (!pURLInfo)
    return MMIO_ERROR_INVALID_PARAMETER;

  /* Extract filename. It starts from 7th byte of URL, because of
   * the 'file://' preamble.
   */
  pchFileName = &(pURLInfo->achURL[7]);

  /* Try to open the file */
  pPluginInstance->hFile = open(pchFileName,
                                O_RDONLY | O_BINARY);
  if (pPluginInstance->hFile == -1)
  {
    /* File could not be opened, so we cannot do anything with this URL */
    return MMIO_ERROR_NOT_FOUND;
  }

  /* Ok, the file was opened! */

  /* Build the new node(s) then! */
  pNewNode = MMIOPsCreateAndLinkNewNodeStruct(pNode);
  if (!pNewNode)
  {
    /* Could not create new node for some reason! */
    close(pPluginInstance->hFile); pPluginInstance->hFile = 0;
    return MMIO_ERROR_UNKNOWN;
  }
  /* Fill some fields of the new node */
  strncpy(pNewNode->achNodeOwnerOutputFormat, pchNeededOutputFormat, sizeof(pNewNode->achNodeOwnerOutputFormat));
  pNewNode->bUnlinkPoint = 1;
  pNewNode->iNodeType = MMIO_NODETYPE_MEDIUM;
  pNewNode->pTypeSpecificInfo = (mmioMediaSpecificInfo_p) MMIOmalloc(sizeof(mmioMediaSpecificInfo_t));
  if (!(pNewNode->pTypeSpecificInfo))
  {
    /* Out of memory! */
    MMIOPsUnlinkAndDestroyNodeStruct(pNewNode);
    close(pPluginInstance->hFile); pPluginInstance->hFile = 0;
    return MMIO_ERROR_OUT_OF_MEMORY;
  }

  /* Fill the type-specific structure */
  pMediaInfo = (mmioMediaSpecificInfo_p) (pNewNode->pTypeSpecificInfo);
  pMediaInfo->iMediaCapabilities =
    MMIO_MEDIA_CAPS_READ |
    MMIO_MEDIA_CAPS_SEEK |
    MMIO_MEDIA_CAPS_TELL;
  pMediaInfo->mmiomedia_SendMsg    = file_SendMsg;
  pMediaInfo->mmiomedia_Read       = file_Read;
  pMediaInfo->mmiomedia_Seek       = file_Seek;
  pMediaInfo->mmiomedia_Tell       = file_Tell;
  pMediaInfo->mmiomedia_ReadPacket = file_ReadPacket;

  /* All done, new node is ready! */
  return MMIO_NOERROR;
}

MMIOPLUGINEXPORT mmioResult_t MMIOCALL file_Unlink(void *pInstance, mmioProcessTreeNode_p pNode)
{
  mmioPluginInstance_p pPluginInstance = (mmioPluginInstance_p) pInstance;

  /* Check parameters */
  if ((!pInstance) ||
      (!pNode))
    return MMIO_ERROR_INVALID_PARAMETER;

  if (pNode->iNodeType != MMIO_NODETYPE_MEDIUM)
    return MMIO_ERROR_NOT_SUPPORTED;

  /* Free type-specific structure and resources */
  MMIOfree(pNode->pTypeSpecificInfo);
  /* Destroy node we've created at link time */
  MMIOPsUnlinkAndDestroyNodeStruct(pNode);

  /* Close file handle and do some clean up */
  close(pPluginInstance->hFile); pPluginInstance->hFile = 0;

  /* Unlink is done */
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
    *ppchInternalName = "file";
    *ppchSupportedFormats = "URL"; /* URL nodes */
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
