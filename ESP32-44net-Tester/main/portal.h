#pragma once
#include <stdbool.h>

/* Raises the open provisioning AP, hijacks DNS so any lookup lands on the
   config page, and serves it.

   Returns true  - credentials were saved (caller should reboot into station mode)
           false - the window expired with nothing saved

   Bounded on purpose: see CONFIG_TESTER_PORTAL_TIMEOUT_S. A transient AP
   outage must not park the device in AP mode forever at a remote site. */
bool portal_run(int timeout_s);
