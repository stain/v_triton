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

#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include "AudMixer.h"

#define INCL_DOS
#include <os2.h>


int bShutdown;

void sig_handler(int signal_number)
{
  /* Send shutdown signal to AudMixDaemonProc() */
  printf("* Got a signal (%d)! Shutting down.\n", signal_number);
  bShutdown = 1;
}

int main(int argc, char *argv[])
{
  int rc;
  audmixInfoBuffer_t AudMixInfo;

  /* Initialize signal handler */

  signal(SIGABRT, sig_handler);
  signal(SIGBREAK, sig_handler);
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  rc = AudMixGetInformation(&AudMixInfo, sizeof(AudMixInfo));
  if (!rc)
  {
    printf("* Error querying Audio Mixer Information!\n");
    printf("  Daemon not started!\n");
  } else
  {
    printf(AudMixInfo.pchVersion);

    printf("\n* Starting Daemon thread\n");

    rc = AudMixStartDaemonThread(2, 4096);
    printf("* Daemon thread started, rc is %d\n", rc);

    bShutdown = 0;
    while (!bShutdown)
      DosSleep(100);

    printf("* Stopping Daemon thread\n");
    rc = AudMixStopDaemonThread();
    printf("* Daemon thread stopped, rc is %d\n", rc);
    printf("* All done, bye!\n");
  }
  return 0;
}
