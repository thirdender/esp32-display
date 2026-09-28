#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCREEN_NONE = 0,
    SCREEN_SLEPT, /* screen just turned off */
    SCREEN_WOKE,  /* screen just turned on; a refresh should happen */
} screen_event_t;

/* Call once after display_init(). Turns the backlight on and sets up the
 * BOOT button as a test/demo trigger. */
void screen_init(void);

/* Drive the screen state machine. Call frequently (e.g. every ~200 ms) from
 * the main task. Returns a transition event when the state changes. */
screen_event_t screen_tick(void);

/* true while the backlight is off. */
bool screen_is_off(void);

/* Reset the idle timer (call after rendering so a fresh screen stays on). */
void screen_mark_active(void);

/* Wake/sleep requests, safe to call from any context (ISR or task). */
void screen_request_wake(void);   /* for a future motion/touch sensor */
void screen_request_sleep(void);  /* for a future motion/touch sensor */
void screen_request_toggle(void); /* BOOT button / debug */

/* Idle timeout before the screen auto-sleeps. */
#define SCREEN_IDLE_TIMEOUT_S 300