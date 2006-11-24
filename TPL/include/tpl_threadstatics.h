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

#ifndef __TPL_THREADSTATICS_H__
#define __TPL_THREADSTATICS_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
#include "tpl_common.h"
#include "tpl_thread.h"
/* ------------------------------------------------------------------------ */
/* This file contains the static functions that may be needed for some
 * platforms to safely implement thread support.
 *
 * This include file should be included only in source code which really uses
 * the TPL threading functions. Otherwise you'll have unreferenced symbols.
 */

#ifdef __OS2__

#ifdef __WATCOMC__
#include <process.h>
#endif
#ifdef __EMX__
#include <stdlib.h>
#endif

typedef struct TPLThreadStartParms_s
{
  void                        *args;
  pfn_tpl_ThreadFn            pfnThreadFn;
} TPLThreadStartParms_t, *TPLThreadStartParms_p;

static void              internal_tpl_ThreadFunc(void *pParm)
{
  TPLThreadStartParms_p pThreadParms = pParm;
  pfn_tpl_ThreadFn pfnThreadFunc = pThreadParms->pfnThreadFn;

  /* Call the thread function with the right calling convention! */
  (*pfnThreadFunc)(pThreadParms->args);

  /*
   * Get the current endthread we have to use, and
   * free the descriptor memory structure
   */
  if (pThreadParms)
    free(pThreadParms);

  /* Call endthread! */
  _endthread();
}

static TPL_TID   TPLCALL tpl_threadCreate(int               iStackSize,
                                          pfn_tpl_ThreadFn  pfnMainFunction,
                                          void             *pThreadParameter)
{
  TPLThreadStartParms_p pThreadParms = malloc(sizeof(TPLThreadStartParms_t));
  TPL_TID tidResult;

  if (!pThreadParms)
  {
    /* Out of memory! */
    return NULL;
  }

  /* Save the thread function */
  pThreadParms->pfnThreadFn = pfnMainFunction;
  /* Also save the real parameters we have to pass to thread function */
  pThreadParms->args = pThreadParameter;

  /* Start the thread using the runtime library of calling app! */
  tidResult = _beginthread(internal_tpl_ThreadFunc, NULL, iStackSize, pThreadParms);

  if (tidResult==0)
  {
    /* Could not create thread! */
    free(pThreadParms);
    return NULL;
  }

  return tidResult;
}

#endif /* __OS2__ */

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __TPL_THREADSTATICS_H__ */
