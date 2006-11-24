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

#ifndef __TPL_THREAD_H__
#define __TPL_THREAD_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
#include "tpl_common.h"
/* ------------------------------------------------------------------------ */
/* Definition of handling Threads/Tasks                                     */

/* Thread priorities                                                        */

#define TPL_THREAD_PRIORITY_IDLE        0
#define TPL_THREAD_PRIORITY_LOW       100
#define TPL_THREAD_PRIORITY_REGULAR   200
#define TPL_THREAD_PRIORITY_HIGH      300
#define TPL_THREAD_PRIORITY_REALTIME  400

/* Thread function definition                                               */

typedef void TPLCALL (*pfn_tpl_ThreadFn)(void *pParm);

/* Per-platform defines and includes                                        */

#ifdef __OS2__
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>

#define TPL_TID               TID

#define TPL_IDLE_TIMESLICE_MSEC       32
#define TPL_LOW_TIMESLICE_MSEC        32
#define TPL_REGULAR_TIMESLICE_MSEC    32
#define TPL_HIGH_TIMESLICE_MSEC       32
#define TPL_REALTIME_TIMESLICE_MSEC    8
#endif /* __OS2__ */

/* Implementation                                                           */

/* The following one is in tpl_threadstatics.h :
static    TPL_TID   TPLCALL tpl_threadCreate(int               iStackSize,
                                             pfn_tpl_ThreadFn  pfnMainFunction,
                                             void             *pThreadParameter);
*/

TPLIMPEXP int       TPLCALL tpl_threadSetPriority(TPL_TID tidThreadID, int iPriority);
TPLIMPEXP void      TPLCALL tpl_threadDelete(TPL_TID tidThreadID);
TPLIMPEXP int       TPLCALL tpl_threadWaitForDie(TPL_TID tidThreadID, int iTimeOut);

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __TPL_THREAD_H__ */
