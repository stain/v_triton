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

/* ------------------------------------------------------------------------ */
#include <stdlib.h>
#include <string.h> /* For memcpy() */
#include "tpl_common.h"
#include "tpl_msgq.h"
#include "tpl_cevsem.h"
#include "tpl_mtxsem.h"

/* ------------------------------------------------------------------------ */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
/* ------------------------------------------------------------------------ */
/* Definitions for message queue handling                                   */

typedef struct TPLMsgQElement_s
{
  void *pData;
  int   iDataSize;

  void *pNext;

} TPLMsgQElement_t, *TPLMsgQElement_p;

typedef struct TPLMsgQ_s
{
  TPL_MTXSEM hmtxUseQueue;
  TPL_CEVSEM hcevDataInQueue;

  int iMsgQSize;
  int iSizeOfMQElement;

  int iNumOfElementsInQueue;

  TPLMsgQElement_p pHead;
  TPLMsgQElement_p pTail;

} TPLMsgQ_t, *TPLMsgQ_p;


TPLIMPEXP TPL_MSGQ     TPLCALL tpl_msgqCreate(int iMsgQSize, int iSizeOfMQElement)
{
  TPLMsgQ_p pResult;

  /* Check parameters */
  if ((iMsgQSize<1) || (iSizeOfMQElement<1))
    return NULL;

  /* Create MQ descriptor structure */
  pResult = (TPLMsgQ_p) malloc(sizeof(TPLMsgQ_t));
  if (!pResult)
    return NULL;

  pResult->iMsgQSize = iMsgQSize;
  pResult->iSizeOfMQElement = iSizeOfMQElement;
  pResult->iNumOfElementsInQueue = 0;
  pResult->pHead = NULL;
  pResult->pTail = NULL;

  pResult->hmtxUseQueue = tpl_mtxsemCreate(0);
  if (!pResult->hmtxUseQueue)
  {
    free(pResult);
    return NULL;
  }

  pResult->hcevDataInQueue = tpl_cevsemCreate(0);
  if (!pResult->hcevDataInQueue)
  {
    tpl_mtxsemDelete(pResult->hmtxUseQueue);
    free(pResult);
    return NULL;
  }

  /* Ok, queue structure is ready! */
  return (TPL_MSGQ) pResult;
}

TPLIMPEXP void         TPLCALL tpl_msgqDelete(TPL_MSGQ hmqMsgQueue)
{
  TPLMsgQ_p pQ = (TPLMsgQ_p) hmqMsgQueue;
  TPLMsgQElement_p pElement, pToDelete;

  if (!hmqMsgQueue)
    return;

  tpl_mtxsemRequest(pQ->hmtxUseQueue, TPL_WAIT_FOREVER);

  pElement = pQ->pHead;
  while (pElement)
  {
    pToDelete = pElement;
    pElement = pElement->pNext;

    free(pToDelete->pData);
    free(pToDelete);
  }
  pQ->pHead = pQ->pTail = NULL;

  tpl_cevsemDelete(pQ->hcevDataInQueue);
  tpl_mtxsemDelete(pQ->hmtxUseQueue);

  free(pQ);
}

TPLIMPEXP int          TPLCALL tpl_msgqReceive(TPL_MSGQ hmqMsgQueue,
                                                  void *pBuffer,
                                                  int  iBufferSize,
                                                  int  iTimeOut)
{
  TPLMsgQ_p pQ = (TPLMsgQ_p) hmqMsgQueue;
  TPLMsgQElement_p pElement;
  int iResult;

  /* Check parameters */
  if ((!hmqMsgQueue) || (!pBuffer))
    return 0;

  iResult = tpl_cevsemWaitFor(pQ->hcevDataInQueue, iTimeOut);
  if (!iResult)
    return 0;


  iResult = 0;
  /* Ok, it seems that there is something in queue! */
  /* Take it out from there */
  tpl_mtxsemRequest(pQ->hmtxUseQueue, TPL_WAIT_FOREVER);

  if (pQ->pHead)
  {
    pElement = pQ->pHead;
    if (pElement->iDataSize>iBufferSize)
    {
      /* Buffer is too small! */
      /* Don't take the element out from queue,
       * and re-increase the event semaphore counter! */
      tpl_cevsemPost(pQ->hcevDataInQueue);
    } else
    {
      /* Buffer is big enough, so copy out the data and
       * take the element out of the queue! */

      memcpy(pBuffer, pElement->pData, pElement->iDataSize);

      pQ->pHead = pElement->pNext;
      if (!(pQ->pHead))
        pQ->pTail = NULL;
      pQ->iNumOfElementsInQueue--;

      free(pElement->pData);
      free(pElement);

      iResult = 1;
    }
  }

  tpl_mtxsemRelease(pQ->hmtxUseQueue);

  return iResult;

}

TPLIMPEXP int          TPLCALL tpl_msgqSend(TPL_MSGQ hmqMsgQueue,
                                               void *pBuffer,
                                               int  iBufferSize)
{
  TPLMsgQ_p pQ = (TPLMsgQ_p) hmqMsgQueue;
  TPLMsgQElement_p pNewElement;
  int iResult;

  /* Check parameters */
  if ((!hmqMsgQueue) || (!pBuffer) || (iBufferSize<1) || (iBufferSize>pQ->iSizeOfMQElement))
    return 0;

  iResult = 0;
  tpl_mtxsemRequest(pQ->hmtxUseQueue, TPL_WAIT_FOREVER);

  if (pQ->iNumOfElementsInQueue<pQ->iMsgQSize)
  {
    /* Ok, there is space in queue. */
    pNewElement = (TPLMsgQElement_p) malloc(sizeof(TPLMsgQElement_t));
    if (pNewElement)
    {
      /* New queue element allocated */
      pNewElement->pNext = NULL;
      pNewElement->iDataSize = iBufferSize;
      pNewElement->pData = malloc(iBufferSize);
      if (!(pNewElement->pData))
      {
        /* Oops, not enough memory! */
        free(pNewElement);
      } else
      {
        /* Fine, memory allocated. Now
         * copy there the data.
         */
        memcpy(pNewElement->pData, pBuffer, iBufferSize);

        /* Now link this element into list of elements */
        if (pQ->pTail)
        {
          pQ->pTail->pNext = pNewElement;
          pQ->pTail = pNewElement;
        } else
        {
          pQ->pHead = pQ->pTail = pNewElement;
        }
        pQ->iNumOfElementsInQueue++;

        /* Notify waiting threads about new data in queue */
        tpl_cevsemPost(pQ->hcevDataInQueue);

        iResult = 1;
      }
    }
  }

  tpl_mtxsemRelease(pQ->hmtxUseQueue);

  return iResult;
}

