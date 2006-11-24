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

#ifndef __TPL_CEVSEM_H__
#define __TPL_CEVSEM_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
#include "tpl_common.h"
/* ------------------------------------------------------------------------ */
/* Counting event semaphore support                                         */

#ifdef __OS2__
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
#define TPL_CEVSEM HEV
#endif

TPLIMPEXP TPL_CEVSEM   TPLCALL tpl_cevsemCreate(int iInitialCount);
TPLIMPEXP void         TPLCALL tpl_cevsemDelete(TPL_CEVSEM hcevSem);
TPLIMPEXP int          TPLCALL tpl_cevsemWaitFor(TPL_CEVSEM hcevSem, int iTimeOut);
TPLIMPEXP int          TPLCALL tpl_cevsemPost(TPL_CEVSEM hcevSem);

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __TPL_CEVSEM_H__ */
