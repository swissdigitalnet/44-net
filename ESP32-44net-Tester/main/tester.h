#pragma once
#include "esp_err.h"

/* Starts the passive target: the status server on port 80, the UDP listener,
   and the arrivals ring they both feed.

   The device answers; it never probes. Everything it reports is something that
   arrived at it, which is what makes it usable as the "something on the 44net
   segment to aim at" that docs/testing.md calls for. */
esp_err_t tester_start(void);

/* Runs in the background. Marks the running image valid only once WiFi is up
   and the listener has held for the given settle time, so a firmware that
   cannot rejoin is rolled back by the bootloader instead of stranding the
   device. No-op unless the image is pending verification. */
void tester_confirm_after(int settle_s);
