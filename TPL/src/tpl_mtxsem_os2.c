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
#include "tpl_common.h"
#include "tpl_mtxsem.h"

/* ------------------------------------------------------------------------ */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
/* ------------------------------------------------------------------------ */
/* Mutex semaphore support                                                  */

TPLIMPEXP TPL_MTXSEM   TPLCALL tpl_mtxsemCreate(int bRequested)
{
  APIRET rc;
  HMTX hmtxResult;

  rc = DosCreateMutexSem(NULL,
                         &hmtxResult,
                         0L,
                         bRequested);
  if (rc!=NO_ERROR)
  {
    /* Could not create mutex semaphore! */
    hmtxResult = 0;
  }

  return hmtxResult;
}

TPLIMPEXP void         TPLCALL tpl_mtxsemDelete(TPL_MTXSEM hmtxSem)
{
  if (hmtxSem)
    DosCloseEventSem(hmtxSem);
}

TPLIMPEXP int          TPLCALL tpl_mtxsemRequest(TPL_MTXSEM hmtxSem, int iTimeOut)
{
  int iOSTimeOut;
  APIRET rc;

  /* Check parameter(s) */
  if (!hmtxSem)
    return 0;

  /* Convert TimeOut value to OS-specific value */
  if (iTimeOut == TPL_WAIT_FOREVER)
    iOSTimeOut = SEM_INDEFINITE_WAIT;
  else
    iOSTimeOut = iTimeOut;

  rc = DosRequestMutexSem(hmtxSem, iOSTimeOut);
  return (rc == NO_ERROR);
}

TPLIMPEXP int          TPLCALL tpl_mtxsemRelease(TPL_MTXSEM hmtxSem)
{
  APIRET rc;

  /* Check parameter(s) */
  if (!hmtxSem)
    return 0;

  rc = DosReleaseMutexSem(hmtxSem);
  return (rc == NO_ERROR);
}
