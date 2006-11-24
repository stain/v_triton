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
#include "MMIOMem.h"
#include "tpl.h"

/* --------------------- Memory allocation tracking ----------------------- */

/* Some configuration defines: */
//#define MMIO_DEBUG_MEMORY_OVERWRITE
//#define MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE  512


typedef struct MemList_s {

  void *pPtr;
  unsigned int uiSize;

  char *pchSourceFile;
  int iLineNum;

  void *pNext;
} MemList_t, *MemList_p; 

static TPL_MTXSEM hmtxUseMemList;
static MemList_p pMemListHead = NULL;
static unsigned long ulAllocedMemorySize = 0;
static unsigned long ulAllocedMemorySizeMax = 0;

/* Some internal stuffs, called by MMIOInitialize() and MMIOUninitialize() */
void MMIOMemInitialize(void)
{
  pMemListHead = NULL;
  ulAllocedMemorySize = 0;
  ulAllocedMemorySizeMax = 0;
  hmtxUseMemList = tpl_mtxsemCreate(0);
}

void MMIOMemUninitialize(void)
{
  /* Free all the allocated memory */
  while (pMemListHead)
    MMIOfree(pMemListHead->pPtr);

  pMemListHead = NULL;
  ulAllocedMemorySize = 0;
  ulAllocedMemorySizeMax = 0;
  tpl_mtxsemDelete(hmtxUseMemList);
}

MMIOIMPEXP unsigned long MMIOCALL MMIOGetCurrentMemUsage(void)
{
  return ulAllocedMemorySize;
}

MMIOIMPEXP void   MMIOCALL MMIOShowMemUsage(void)
{
  MemList_p pTemp = pMemListHead;
  unsigned long ulSum = 0;
  int i, iLen;
  char *pchTemp;

  tpl_mtxsemRequest(hmtxUseMemList, TPL_WAIT_FOREVER);

  fprintf(stdout, " --- MMIO Memory Usage Status --- BEGIN ---\n");
  while (pTemp)
  {
    if (pTemp->pchSourceFile)
      fprintf(stdout, "%8d @%p [%s line %d]\n", pTemp->uiSize, pTemp->pPtr, pTemp->pchSourceFile, pTemp->iLineNum);
    else
      fprintf(stdout, "%8d @%p [Unknown file, line %d]\n", pTemp->uiSize, pTemp->pPtr, pTemp->pchSourceFile, pTemp->iLineNum);
    /* Memory dump */
    iLen = 12;
    if (pTemp->uiSize<iLen) iLen = pTemp->uiSize;
    pchTemp = pTemp->pPtr;
    fprintf(stdout, "        ");
    for (i=0; i<iLen; i++)
    {
      fprintf(stdout, "0x%02x ", pchTemp[i]);
    }
    if (pTemp->uiSize>iLen)
      fprintf(stdout, "...");
    fprintf(stdout, "\n");

    ulSum+=pTemp->uiSize;

    pTemp = pTemp->pNext;
  }
  fprintf(stdout, " --- SUM: %lu bytes = %lu KBytes ---\n", ulSum, ulSum/1024);
  fprintf(stdout, " --- Accounted SUM: %lu bytes = %lu KBytes ---\n",
          ulAllocedMemorySize,
          ulAllocedMemorySize/1024);
  fprintf(stdout, " --- Max peak was: %lu bytes = %lu KBytes ---\n",
          ulAllocedMemorySizeMax,
          ulAllocedMemorySizeMax/1024);
  fprintf(stdout, " --- MMIO Memory Usage Status ---- END ----\n");

  tpl_mtxsemRelease(hmtxUseMemList);
}

MMIOIMPEXP void * MMIOCALL MMIOmalloc_core(unsigned int numbytes, char *pchSourceFile, int iLineNum)
{
  MemList_p pTemp;

  pTemp = (MemList_p) malloc(sizeof(MemList_t));
  if (pTemp)
  {
#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
    pTemp->pPtr = malloc(numbytes + MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE);
#else
    pTemp->pPtr = malloc(numbytes);
#endif
    if (pTemp->pPtr)
    {
      pTemp->uiSize = numbytes;
      pTemp->iLineNum = iLineNum;
      pTemp->pchSourceFile = malloc(strlen(pchSourceFile)+1);
      if (pTemp->pchSourceFile)
        strcpy(pTemp->pchSourceFile, pchSourceFile);

      tpl_mtxsemRequest(hmtxUseMemList, TPL_WAIT_FOREVER);

      pTemp->pNext = pMemListHead;
      pMemListHead = pTemp;

      ulAllocedMemorySize += numbytes;
      if (ulAllocedMemorySize>ulAllocedMemorySizeMax)
        ulAllocedMemorySizeMax = ulAllocedMemorySize;

      tpl_mtxsemRelease(hmtxUseMemList);

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
      // Store known bytes in safe area!
      {
        int i;
        char *pchSafeArea;

        pchSafeArea = (char *) pTemp->pPtr;
        pchSafeArea += pTemp->uiSize;
        for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
          pchSafeArea[i] = (char) i;
      }
#endif

      return pTemp->pPtr;
    } else
    {
      fprintf(stderr, "[MMIOmalloc] : Out of memory! (To allocate %d bytes)\n", numbytes);
      fprintf(stderr, "               Called from %s line %d\n", pchSourceFile, iLineNum);
      return NULL;
    }
  } else
  {
    fprintf(stderr, "[MMIOmalloc] : Out of memory! (To allocate %d bytes)\n", numbytes);
    fprintf(stderr, "               Called from %s line %d\n", pchSourceFile, iLineNum);
    return NULL;
  }
  
}

MMIOIMPEXP void * MMIOCALL MMIOcalloc_core(unsigned int nelements, unsigned int elsize, char *pchSourceFile, int iLineNum)
{
  MemList_p pTemp;

  pTemp = (MemList_p) malloc(sizeof(MemList_t));
  if (pTemp)
  {
#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
    pTemp->pPtr = calloc(nelements + (MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE/elsize) + 1, elsize);
#else
    pTemp->pPtr = calloc(nelements, elsize);
#endif
    if (pTemp->pPtr)
    {
      pTemp->uiSize = nelements*elsize;
      pTemp->iLineNum = iLineNum;
      pTemp->pchSourceFile = malloc(strlen(pchSourceFile)+1);
      if (pTemp->pchSourceFile)
        strcpy(pTemp->pchSourceFile, pchSourceFile);

      tpl_mtxsemRequest(hmtxUseMemList, TPL_WAIT_FOREVER);

      pTemp->pNext = pMemListHead;
      pMemListHead = pTemp;

      ulAllocedMemorySize += nelements*elsize;
      if (ulAllocedMemorySize>ulAllocedMemorySizeMax)
        ulAllocedMemorySizeMax = ulAllocedMemorySize;

      tpl_mtxsemRelease(hmtxUseMemList);

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
      // Store known bytes in safe area!
      {
        int i;
        char *pchSafeArea;

        pchSafeArea = (char *) pTemp->pPtr;
        pchSafeArea += pTemp->uiSize;
        for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
          pchSafeArea[i] = (char) i;
      }
#endif
      return pTemp->pPtr;
    } else
    {
      fprintf(stderr, "[MMIOcalloc] : Out of memory! (To allocate %d bytes)\n", nelements*elsize);
      fprintf(stderr, "               Called from %s line %d\n", pchSourceFile, iLineNum);
      return NULL;
    }
  } else
  {
    fprintf(stderr, "[MMIOcalloc] : Out of memory! (To allocate %d bytes)\n", nelements*elsize);
    fprintf(stderr, "              Called from %s line %d\n", pchSourceFile, iLineNum);
    return NULL;
  }
  
}

MMIOIMPEXP void   MMIOCALL MMIOfree_core(void *ptr, char *pchSourceFile, int iLineNum)
{
  MemList_p pTemp, pPrev;

  tpl_mtxsemRequest(hmtxUseMemList, TPL_WAIT_FOREVER);

  pPrev = NULL;
  pTemp = pMemListHead;
  while ((pTemp) && ((pTemp->pPtr) != ptr)) 
  {
    pPrev = pTemp;
    pTemp = pTemp->pNext;
  }

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
  if (pTemp)
  {
    // Found block in list, so it's alloc'd via us.
    // Let's check for memory overwrite!
    int i;
    char *pchSafeArea;

    pchSafeArea = (char *) pTemp->pPtr;
    pchSafeArea += pTemp->uiSize;
    for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
      if (pchSafeArea[i] != (char) i)
      {
        fprintf(stderr, "[MMIOfree] : Warning, memory block (%p) overwrite detected!\n", ptr);
        fprintf(stderr, "             Overwritten in SafeArea, at byte pos %d\n", i);
        fprintf(stderr, "             Block allocated at %s line %d\n", pTemp->pchSourceFile, pTemp->iLineNum);
        fprintf(stderr, "             MMIOfree() was called from %s line %d\n", pchSourceFile, iLineNum);
        break;
      }
  }
#endif

  free(ptr);

  if (!pTemp) 
  {
    fprintf(stderr, "[MMIOfree] : Warning, memory block (%p) cannot be found in list!\n", ptr);
    fprintf(stderr, "             Called from %s line %d\n", pchSourceFile, iLineNum);
  } else
  {
    if (ulAllocedMemorySize < pTemp->uiSize)
    {
      fprintf(stderr, "[MMIOfree] : Warning, internal pool housekeeping error!\n");
      ulAllocedMemorySize = 0;
    } else
      ulAllocedMemorySize-=pTemp->uiSize;

    if (pPrev)
    {
      pPrev->pNext = pTemp->pNext;
    } else
    {
      pMemListHead = pTemp->pNext;
    }
    if (pTemp->pchSourceFile)
      free(pTemp->pchSourceFile);

    free(pTemp);
  }

  tpl_mtxsemRelease(hmtxUseMemList);
}

MMIOIMPEXP void * MMIOCALL MMIOrealloc_core(void *ptr, int inewsize, char *pchSourceFile, int iLineNum)
{
  MemList_p pTemp;
  void *newptr;

  if (ptr==NULL)
  {
    // By definition
    return MMIOmalloc_core(inewsize, pchSourceFile, iLineNum);
  }

  if (inewsize==0)
  {
    // By definition
    MMIOfree_core(ptr, pchSourceFile, iLineNum);
    return NULL;
  }

  tpl_mtxsemRequest(hmtxUseMemList, TPL_WAIT_FOREVER);

  // Find the pointer in our list
  pTemp = pMemListHead;
  while ((pTemp) && ((pTemp->pPtr) != ptr)) 
    pTemp = pTemp->pNext;

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
  if (pTemp)
  {
    // Found block in list, so it's alloc'd via us.
    // Let's check for memory overwrite, before reallocating!
    int i;
    char *pchSafeArea;

    pchSafeArea = (char *) pTemp->pPtr;
    pchSafeArea += pTemp->uiSize;
    for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
      if (pchSafeArea[i] != (char) i)
      {
        fprintf(stderr, "[MMIOrealloc] : Warning, memory block (%p) overwrite detected!\n", ptr);
        fprintf(stderr, "                Overwritten in SafeArea, at byte pos %d\n", i);
        fprintf(stderr, "                Block allocated at %s line %d\n", pTemp->pchSourceFile, pTemp->iLineNum);
        fprintf(stderr, "                MMIOrealoc() was called from %s line %d\n", pchSourceFile, iLineNum);
        break;
      }
  }
  newptr = realloc(ptr, inewsize + MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE);
#else
  newptr = realloc(ptr, inewsize);
#endif

  if (newptr!=NULL)
  {
    // If realloc successful
    if (!pTemp)
    {
      fprintf(stderr, "[MMIOrealloc] : Warning, memory block (%p) cannot be found in list!\n", ptr);
      fprintf(stderr, "                Called from %s line %d\n", pchSourceFile, iLineNum);
      // Add it to list
      pTemp = (MemList_p) malloc(sizeof(MemList_t));
      if (pTemp)
      {
        pTemp->pPtr = newptr;
        pTemp->uiSize = inewsize;
        pTemp->iLineNum = iLineNum;
        if (pTemp->pchSourceFile)
          free(pTemp->pchSourceFile);
        pTemp->pchSourceFile = malloc(strlen(pchSourceFile)+1);
        if (pTemp->pchSourceFile)
          strcpy(pTemp->pchSourceFile, pchSourceFile);

        pTemp->pNext = pMemListHead;
        pMemListHead = pTemp;
  
        ulAllocedMemorySize += inewsize;
        if (ulAllocedMemorySize>ulAllocedMemorySizeMax)
          ulAllocedMemorySizeMax = ulAllocedMemorySize;

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
        // Store known bytes in safe area!
        {
          int i;
          char *pchSafeArea;
  
          pchSafeArea = (char *) pTemp->pPtr;
          pchSafeArea += pTemp->uiSize;
          for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
            pchSafeArea[i] = (char) i;
        }
#endif

        tpl_mtxsemRelease(hmtxUseMemList);
        return newptr;
      } else
      {
        tpl_mtxsemRelease(hmtxUseMemList);
        fprintf(stderr, "[MMIOrealloc] : Out of memory!  (To reallocate to %d bytes)\n", inewsize);
        fprintf(stderr, "                Called from %s line %d\n", pchSourceFile, iLineNum);
        return NULL;
      }
    } else
    {
      if (ulAllocedMemorySize < pTemp->uiSize)
      {
        fprintf(stderr, "[MMIOrealloc] : Warning, internal pool housekeeping error!\n");
        ulAllocedMemorySize = 0;
      } else
        ulAllocedMemorySize-=pTemp->uiSize;

      ulAllocedMemorySize+=inewsize;
      if (ulAllocedMemorySize>ulAllocedMemorySizeMax)
          ulAllocedMemorySizeMax = ulAllocedMemorySize;

      pTemp->uiSize=inewsize;
      pTemp->pPtr = newptr;
      pTemp->iLineNum = iLineNum;
      if (pTemp->pchSourceFile)
        free(pTemp->pchSourceFile);
      pTemp->pchSourceFile = malloc(strlen(pchSourceFile)+1);
      if (pTemp->pchSourceFile)
        strcpy(pTemp->pchSourceFile, pchSourceFile);

#ifdef MMIO_DEBUG_MEMORY_OVERWRITE
      // Store known bytes in safe area!
      {
        int i;
        char *pchSafeArea;

        pchSafeArea = (char *) pTemp->pPtr;
        pchSafeArea += pTemp->uiSize;
        for (i=0; i<MMIO_DEBUG_MEMORY_OVERWRITE__SAFEAREA_SIZE; i++)
          pchSafeArea[i] = (char) i;
      }
#endif
      tpl_mtxsemRelease(hmtxUseMemList);
      return newptr;
    }
  }
  tpl_mtxsemRelease(hmtxUseMemList);
  return newptr;
}

MMIOIMPEXP char * MMIOCALL MMIOstrdup_core(char *ptr, char *pchSourceFile, int iLineNum)
{
  char *newptr;
  int len;

  if (!ptr)
  {
    fprintf(stderr, "[MMIOstrdup] : Warning, trying to strdup() a NULL pointer!\n");
    fprintf(stderr, "               Called from %s line %d\n", pchSourceFile, iLineNum);
    return NULL;
  }

  len = strlen(ptr) + 1;
  newptr = MMIOmalloc_core(len, pchSourceFile, iLineNum);
  if (newptr)
  {
    memcpy(newptr, ptr, len);
  }
  return newptr;
}
