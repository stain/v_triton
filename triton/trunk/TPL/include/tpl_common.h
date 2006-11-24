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

#ifndef __TPL_COMMON_H__
#define __TPL_COMMON_H__
/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/*                             Common defines                               */

/* ------------------------------------------------------------------------ */
/* Special define for timeouts:                                             */

#define TPL_WAIT_FOREVER  -1

/* ------------------------------------------------------------------------ */
/* Path-separator                                                           */

#ifdef __OS2__
/* The path separator in OS/2 is the backslash                              */
#define TPL_PATH_SEPARATOR '\\'
#endif

#ifndef TPL_PATH_SEPARATOR
/* Default is the slash                                                     */
#define TPL_PATH_SEPARATOR '/'
#endif

/* ------------------------------------------------------------------------ */
/* Definition of calling convention of exported TPL functions               */

#ifdef __OS2__
/* For OS/2 we'll use the System calling convention                         */
#define TPLCALL _System
#endif

#ifndef TPLCALL
/* Default is to have nothing special:                                      */
#define TPLCALL
#endif

/* ------------------------------------------------------------------------ */
/* Definition of exporting symbols in header files                          */

#ifdef __OS2__
/* For OS/2 we'll use dllexport                                             */
#define TPLEXPORT __declspec(dllexport)
#endif

#ifndef TPLEXPORT
/* Default is to have nothing special:                                      */
#define TPLEXPORT
#endif

/* ------------------------------------------------------------------------ */
/* Definition of conditional importing/exporting of symbols in header files */

#ifdef __OS2__
/* For OS/2 we'll use dllexport or dllimport                                */
#ifdef BUILD_TPL
#define TPLIMPEXP __declspec(dllexport)
#else
#define TPLIMPEXP
#endif
#endif

#ifndef TPLIMPEXP
/* Default is to have nothing special:                                      */
#define TPLIMPEXP
#endif

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __TPL_COMMON_H__ */
