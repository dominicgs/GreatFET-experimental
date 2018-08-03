/*
 * This file is part of GreatFET
 *
 * GlitchKit common functionality
 */

#include <stdbool.h>
#include "glitchkit.h"

#include <greatfet_core.h>

#include <gpio.h>
#include <gpio_lpc.h>
#include <gpio_scu.h>

#include <libopencm3/lpc43xx/scu.h>
#include <libopencm3/lpc43xx/cgu.h>

// FIXME: make synchronization explicit using atomic operations :)
// [even though these are currently all atomic due to bus configuration]

/**
 * Structure that coalesces the state of the GlitchKit trigger/stimulus system.
 */
struct glitchkit_state {

	// Start off with glitchkit functionality disabled.
	bool enabled;

	// Start off with no trigger set up.
	struct gpio_t trigger_gpio;

	// FIXME: temporary, replace me with a timer!
	bool triggered;

	// Structure that stores which events are currently active.
	glitchkit_event_t active_events;

	// Stores any events for which we've recieved deferred notifications.
	// See the glitchkit_notify_event_deferred method for details.
	glitchkit_event_t deferred_events;

	// Stores the name of an event (or events) that we're blocking on.
	// For use by glitchkit_wait_for_events().
	glitchkit_event_t blocking_on;

	// Stores a set of events necessary to enable a target clock,
	// if relevant.
	glitchkit_event_t enable_clock_on;

	// Stores the source of the target clock. Should match a CGU_SRC_*
	// constant.
	uint32_t target_clock_source;


};
typedef struct glitchkit_state glitchkit_state_t;


// Store the state of the system's GlitchKit modules.
static volatile glitchkit_state_t glitchkit = {
	.enabled = false,
	.trigger_gpio = GPIO(5, 14),
	.triggered = false,
	.active_events = 0,
	.deferred_events = 0
};



/**
 * Enables GlitchKit functionality. This should be called by any GlitchKit
 * module before it sees used. This is idempotent, and can be called multiple times
 * without ill effect.
 */
void glitchkit_enable(void) {

	// If GlitchKit's already been set up, bail out.
	if (glitchkit.enabled) {
		return;
	}

	// Mark GlitchKit as enabled.
	glitchkit.enabled = true;

	// Initialize our state.
	glitchkit.active_events   = 0;
	glitchkit.deferred_events = 0;

	// Set up our default trigger GPIO, if none has been set up already.
	// TODO: Allow this to be customizable, instead of fixed to Indigo?
	
	// FIXME: abstract
	scu_grp_pin_t scu_pin = get_scu_pin_for_gpio(5, 14);
	uint32_t scu_func    = get_scu_func_for_gpio(5, 14);
	scu_pinmux(scu_pin, SCU_GPIO_NOPULL | scu_func);

	// FIXME: abstract
	gpio_output((gpio_t)&glitchkit.trigger_gpio);
	gpio_write((gpio_t)&glitchkit.trigger_gpio, false);
}


/**
 * Disables GlitchKit functionality, but does not disable any GlitchKit modueles.
 * Disable them before calling this function.
 */
void glitchkit_disable(void) {
	if (!glitchkit.enabled)
		return;

	glitchkit.enabled = false;
}

// TODO: Use sync_fetch_and_and/or to make these explicit?

/**
 * Requests that GlitchKit issue a trigger to the ChipWhisperer when
 * the given event happens.
 */
void glitchkit_enable_trigger_on(glitchkit_event_t event)
{
	glitchkit.active_events |= event;
}

/**
 * Requests that GlitchKit no longer issue a trigger to the ChipWhisperer
 * when the given event happens.
 */
void glitchkit_disable_trigger_on(glitchkit_event_t event)
{
	glitchkit.active_events &= ~event;
}


/**
 * Notify GlitchKit of a GlitchKit-observable event.
 *
 * @param event The type of event(s) observed. An OR'ing of multiple
 *     events is acceptable, as well.
 */
void glitchkit_notify_event(glitchkit_event_t event)
{
	// If we're watching for this event, trigger.
	if(glitchkit.active_events & event) {
		glitchkit_trigger();
	}

	// If we're waiting for this event to enable the clock, enable it.
	if(glitchkit.enable_clock_on & event) {
		glitchkit_provide_target_clock(glitchkit.target_clock_source, glitchkit.enable_clock_on & ~event);
	}

	// Clear the event from the list of events we're waiting for,
	// allowing the event to unblock glitchkit_wait_for_events.
	glitchkit.blocking_on &= ~event;
}


/**
 * Adds a GlitchKit event to a list of events that have occurred,
 * but does not issue any relevant tirggers until glitchkit_apply_defferred_events()
 * is called.
 *
 * This is good if events occur outside of a context with reproducible timing
 * (e.g. there's some skew in when a given event happen). The
 * glitchkit_apply_defferred_events method can be called from the next 
 * synchronized context.
 *
 * @param event The type of event(s) observed. An OR'ing of multiple
 *     events is acceptable, as well.
 */
void glitchkit_notify_event_deferred(glitchkit_event_t event)
{
	glitchkit.deferred_events |= event;
}


/**
 * Issues a trigger if any of the events in the allowed_events mask have
 * been previously deferred. See the documentation if glitchkit_notify_event_deferred.
 *
 * @param allowed_events The bitwise OR of all events of interest.
 */
void glitchkit_apply_deferred_events(glitchkit_event_t allowed_events)
{
	// Figure out (and trigger) any relevant deferred events...
	uint32_t relevant_events = (allowed_events & glitchkit.deferred_events);
	glitchkit_notify_event(relevant_events);

	// ... and clear the deferred events.
	glitchkit.deferred_events &= ~relevant_events;
}


/**
 * Specifies to use a given type of event for synchronization, but does not
 * block. This will be used by	
 *
 * @param event_to_sync_on 
 */
void glitchkit_use_event_for_synchronization(glitchkit_event_t event_to_sync_on)
{
	glitchkit.blocking_on = event_to_sync_on;
}


/**
 * Waits for an event or events that should occur in an interrupt context.
 *
 * @param event_to_wait_for The event or events to wait for. If zero,
 *		this method will use the values previously set by used_event_for_synchronization.
 */
void glitchkit_wait_for_events(glitchkit_event_t event_to_wait_for)
{
	// If we have an event to wait for, use it as our source of blocking events.
	if (event_to_wait_for) {
			glitchkit.blocking_on = event_to_wait_for;
	}

	// Wait for us to have any events left to wait on.
	while (glitchkit.blocking_on);

	// FIXME: Remove when we're no longer debugging.
	led_toggle(LED3);
}



/**
 * Function that causes the GlitchKit trigger signal to rise, triggering the
 * ChipWhisperer to e.g. induce a glitch.
 */
void glitchkit_trigger() {

		// FIXME: Remove when we're no longer debugging.
		led_toggle(LED4);

		// Set the GPIO pin high, immediately...
		gpio_write((gpio_t)&glitchkit.trigger_gpio, true);

		//... and then schedule it to turn off later.
		// FIXME: This should really be on a timer.
		glitchkit.triggered = true;
}

/**
 * Main loop service routine for GlitchKit.
 */
void service_glitchkit() {

		if(!glitchkit.enabled) {
				return;
		}

		// Temporary implementation: hold the trigger high for >1ms.
		// FIXME: Replace me with a timer!
		if(glitchkit.triggered) {

			// Wait 1 ms...
			delay(1000);

			// ... and then de-assert the trigger.
			gpio_write((gpio_t)&glitchkit.trigger_gpio, false);
			glitchkit.triggered = false;
		}
}


/**
 * Sets up GlitchKit to provide the output clock.
 *
 * @param source The clock to be provided. Should match one of the CGU_SRC_*
 *		constants from cgu.h; see table 147 in the datasheet.
 * @param requirements If nonzero, enabling of the clock will be deferred until
 *		the given events occur.
 */
void glitchkit_provide_target_clock(uint32_t source, glitchkit_event_t requirements)
{
		source &= 0x1F;

		// If this is a deferred enable, store the requirements and then bail.
		if(requirements) {
			glitchkit.enable_clock_on = requirements;
			glitchkit.target_clock_source = source;
			return;
		}

		// Set the clock source for the target clock...
		// FIXME: uncomment
		CGU_BASE_OUT_CLK = CGU_BASE_OUT_CLK_AUTOBLOCK(1)
				| CGU_BASE_OUT_CLK_CLK_SEL(source) | CGU_BASE_OUT_CLK_PD(0);

		// Set up CLK0 (provided on the GreatFET one) to drive a strong clock output.
		scu_pinmux(CLK0, SCU_CLK_OUT | SCU_CONF_FUNCTION1);
}


