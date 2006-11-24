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
#include "tpl_thread.h"

/* ------------------------------------------------------------------------ */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
/* ------------------------------------------------------------------------ */
/* Threads/Tasks support                                                    */

/* Thread creation is in tpl_thread_statics.h ! */

TPLIMPEXP int       TPLCALL tpl_threadSetPriority(TPL_TID tidThreadID, int iPriority)
{
  APIRET rc;
  ULONG  ulClass;
  LONG   lDelta;

  switch (iPriority)
  {
    case TPL_THREAD_PRIORITY_IDLE:
        ulClass = PRTYC_IDLETIME;
        lDelta = 0;
        break;
    case TPL_THREAD_PRIORITY_LOW:
        ulClass = PRTYC_REGULAR;
        lDelta = -16;
        break;
    case TPL_THREAD_PRIORITY_REGULAR:
    default:
        ulClass = PRTYC_REGULAR;
        lDelta = 0;
        break;
    case TPL_THREAD_PRIORITY_HIGH:
        ulClass = PRTYC_FOREGROUNDSERVER;
        lDelta = 0;
        break;
    case TPL_THREAD_PRIORITY_REALTIME:
        ulClass = PRTYC_TIMECRITICAL;
        lDelta = 0;
        break;
  }

  rc = DosSetPriority(PRTYS_THREAD,
                      ulClass,
                      lDelta,
                      tidThreadID);
  return (rc == NO_ERROR);
}

TPLIMPEXP void      TPLCALL tpl_threadDelete(TPL_TID tidThreadID)
{
  if (tidThreadID)
    DosKillThread(tidThreadID);
}

TPLIMPEXP int       TPLCALL tpl_threadWaitForDie(TPL_TID tidThreadID, int iTimeOut)
{
  int iOSTimeOut;
  APIRET rc;
  int iWaited;
  TID tidTemp;

  /* Check parameter(s) */
  if (!tidThreadID)
    return 0;

  /* Convert TimeOut value to OS-specific value */
  if (iTimeOut == TPL_WAIT_FOREVER)
  {
    tidTemp = tidThreadID;
    rc = DosWaitThread(&tidTemp, DCWW_WAIT);
  }
  else
  {
    iOSTimeOut = iTimeOut;

    iWaited = 0;
    do {
      tidTemp = tidThreadID;
      rc = DosWaitThread(&tidTemp, 32);
      iWaited += 32;
    } while ((iWaited<iTimeOut) && (rc != NO_ERROR));
  }

  return (rc == NO_ERROR);
}

