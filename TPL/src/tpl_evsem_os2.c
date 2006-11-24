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
#include "tpl_evsem.h"

/* ------------------------------------------------------------------------ */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
/* ------------------------------------------------------------------------ */
/* Event semaphore support                                                  */

TPLIMPEXP TPL_EVSEM   TPLCALL tpl_evsemCreate(int bPosted)
{
  APIRET rc;
  HEV hevResult;

  rc = DosCreateEventSem(NULL,
                         &hevResult,
                         0L,
                         bPosted);
  if (rc!=NO_ERROR)
  {
    /* Could not create event semaphore! */
    hevResult = 0;
  }

  return hevResult;
}

TPLIMPEXP void        TPLCALL tpl_evsemDelete(TPL_EVSEM hevSem)
{
  if (hevSem)
    DosCloseEventSem(hevSem);
}

TPLIMPEXP int         TPLCALL tpl_evsemReset(TPL_EVSEM hevSem)
{
  APIRET rc;
  ULONG ulPostCount;

  /* Check parameter(s) */
  if (!hevSem)
    return 0;

  rc = DosResetEventSem(hevSem, &ulPostCount);

  return (rc == NO_ERROR);
}

TPLIMPEXP int         TPLCALL tpl_evsemWaitFor(TPL_EVSEM hevSem, int iTimeOut)
{
  int iOSTimeOut;
  APIRET rc;

  /* Check parameter(s) */
  if (!hevSem)
    return 0;

  /* Convert TimeOut value to OS-specific value */
  if (iTimeOut == TPL_WAIT_FOREVER)
    iOSTimeOut = SEM_INDEFINITE_WAIT;
  else
    iOSTimeOut = iTimeOut;

  rc = DosWaitEventSem(hevSem, iOSTimeOut);
  return (rc == NO_ERROR);
}

TPLIMPEXP int         TPLCALL tpl_evsemPost(TPL_EVSEM hevSem)
{
  APIRET rc;

  /* Check parameter(s) */
  if (!hevSem)
    return 0;

  rc = DosPostEventSem(hevSem);
  return (rc == NO_ERROR);
}
