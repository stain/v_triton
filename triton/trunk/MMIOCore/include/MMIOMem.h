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

#ifndef __MMIOMEM_H__
#define __MMIOMEM_H__

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Include the callig convention and function exporting defines             */
#include "MMIO.h"

/* ------------------------------------------------------------------------ */

/*
 * Define the following in your code before including this header file
 * if you want to keep track of ttn_malloc'd memories:
 *    #define MMIO_TRACK_MEMORY_USAGE
 */
#if defined(MMIO_TRACK_MEMORY_USAGE) || defined(BUILD_MMIO)

MMIOIMPEXP void * MMIOCALL MMIOmalloc_core(unsigned int numbytes, char *pchSourceFile, int iLineNum);
MMIOIMPEXP void * MMIOCALL MMIOcalloc_core(unsigned int nelements, unsigned int elsize, char *pchSourceFile, int iLineNum);
MMIOIMPEXP void   MMIOCALL MMIOfree_core(void *ptr, char *pchSourceFile, int iLineNum);
MMIOIMPEXP void * MMIOCALL MMIOrealloc_core(void *ptr, int inewsize, char *pchSourceFile, int iLineNum);
MMIOIMPEXP char * MMIOCALL MMIOstrdup_core(char *ptr, char *pchSourceFile, int iLineNum);
#define MMIOmalloc(a) MMIOmalloc_core(a,__FILE__,__LINE__)
#define MMIOcalloc(a,b) MMIOcalloc_core(a,b,__FILE__,__LINE__)
#define MMIOfree(a) MMIOfree_core(a,__FILE__,__LINE__)
#define MMIOrealloc(a,b) MMIOrealloc_core(a,b,__FILE__,__LINE__)
#define MMIOstrdup(a) MMIOstrdup_core(a,__FILE__,__LINE__)
#else

#define MMIOmalloc(a) malloc(a)
#define MMIOcalloc(a,b) calloc(a)
#define MMIOfree(a) free(a)
#define MMIOrealloc(a,b) realloc(a,b)
#define MMIOstrdup(a) strdup(a,__FILE__,__LINE__)

#endif

MMIOIMPEXP unsigned long MMIOCALL MMIOGetCurrentMemUsage(void);
MMIOIMPEXP void          MMIOCALL MMIOShowMemUsage(void);

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

/* ------------------------------------------------------------------------ */
#endif /* __MMIOMEM_H__ */
