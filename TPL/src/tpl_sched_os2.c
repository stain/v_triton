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
#include "tpl_sched.h"

/* ------------------------------------------------------------------------ */
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_TYPES
#include <os2.h>
/* ------------------------------------------------------------------------ */
/* Scheduling support                                                       */

/* Configuration define(s) for this module: */
#define USE_TRICKY_DELAY


TPLIMPEXP void        TPLCALL tpl_schedYield(void)
{
  DosSleep(0);
}

#ifndef USE_TRICKY_DELAY

TPLIMPEXP void        TPLCALL tpl_schedDelay(int iDelaymsec)
{
  DosSleep(iDelaymsec);
}

#else

TPLIMPEXP void        TPLCALL tpl_schedDelay(int iDelaymsec)
{
  /* High resolution sleep, originally made by Ilya Zakharevich */

  /* This is similar to DosSleep(), but has 8ms granularity in time-critical
   * threads even on Warp3.
   */

  HEV     hevEvent1     = 0;   /* Event semaphore handle     */
  HTIMER  htimerEvent1  = 0;   /* Timer handle               */
  APIRET  rc            = NO_ERROR;  /* Return code          */
  int     ret = 1;
  ULONG   priority = 0, nesting;   /* Shut down the warnings */
  PPIB pib;
  PTIB tib;
  char *e = NULL;
  APIRET badrc;
  int switch_priority = 50;

  DosCreateEventSem(NULL,      /* Unnamed */
                    &hevEvent1,  /* Handle of semaphore returned */
                    DC_SEM_SHARED, /* Shared needed for DosAsyncTimer */
                    FALSE);      /* Semaphore is in RESET state  */

  if (iDelaymsec >= switch_priority)
    switch_priority = 0;
  if (switch_priority)
  {
    if (DosGetInfoBlocks(&tib, &pib)!=NO_ERROR)
      switch_priority = 0;
    else
    {
      /* In Warp3, to switch scheduling to 8ms step, one needs to do
       * DosAsyncTimer() in time-critical thread.  On laters versions,
       * more and more cases of wait-for-something are covered.
       *
       * It turns out that on Warp3fp42 it is the priority at the time
       * of DosAsyncTimer() which matters.  Let's hope that this works
       * with later versions too...  XXXX
       */

      priority = (tib->tib_ptib2->tib2_ulpri);
      if ((priority & 0xFF00) == 0x0300) /* already time-critical */
        switch_priority = 0;

      /* Make us time-critical.  Just modifying TIB is not enough... */

      /* We do not want to run at high priority if a signal causes us
       * to longjmp() out of this section... */
      if (DosEnterMustComplete(&nesting))
        switch_priority = 0;
      else
        DosSetPriority(PRTYS_THREAD, PRTYC_TIMECRITICAL, 0, 0);
    }
  }

  if ((badrc = DosAsyncTimer(iDelaymsec,
                             (HSEM) hevEvent1, /* Semaphore to post        */
                             &htimerEvent1))) /* Timer handler (returned) */
    e = "DosAsyncTimer";

  if (switch_priority && tib->tib_ptib2->tib2_ulpri == 0x0300)
  {
    /* Nobody switched priority while we slept...  Ignore errors... */

    if (!(rc = DosSetPriority(PRTYS_THREAD, (priority>>8) & 0xFF, 0, 0)))
      rc = DosSetPriority(PRTYS_THREAD, 0, priority & 0xFF, 0);
  }

  if (switch_priority)
    rc = DosExitMustComplete(&nesting); /* Ignore errors */

  /* The actual blocking call is made with "normal" priority.  This way we
     should not bother with DosSleep(0) etc. to compensate for us interrupting
     higher-priority threads.  The goal is to prohibit the system spending too
     much time halt()ing, not to run us "no matter what". */

  if (!e)     /* Wait for AsyncTimer event */
    badrc = DosWaitEventSem(hevEvent1, SEM_INDEFINITE_WAIT);

  if (e) ;    /* Do nothing */
  else if (badrc == ERROR_INTERRUPT)
    ret = 0;
  else if (badrc)
    e = "DosWaitEventSem";
  if ((rc = DosCloseEventSem(hevEvent1)) && !e) { /* Get rid of semaphore */
    e = "DosCloseEventSem";
    badrc = rc;
  }

  /*
   if (e)
   {
     printf("[tpl_schedDelay] : Had error in %s(), rc is 0x%x\n", e, badrc);
   }
   */
}
#endif

