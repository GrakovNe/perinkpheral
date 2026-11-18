// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdatomic.h>

typedef enum {
    WAITING, RECEIVING, CONFIRM, ACCEPTED, WRITING, CANCELLED, FAILED, RETRYING, RESTART
} recovery_state_t;

enum { RECOVERY_STATE_BITS = 4, RECOVERY_STATE_MASK = 15 };
typedef struct { atomic_uint ticket; } recovery_control_t;

static inline recovery_state_t recovery_control_state(const recovery_control_t *control)
{
    return atomic_load(&control->ticket) & RECOVERY_STATE_MASK;
}

/* Generation and state change together: an old key sample must not approve
 * another request which has reached the same state (ABA). */
static inline bool recovery_control_claim_ticket(recovery_control_t *control,
                                                  unsigned before, recovery_state_t next)
{
    unsigned state = before & RECOVERY_STATE_MASK;
    if (state != WAITING && state != FAILED) { return false; }
    unsigned after = ((before & ~RECOVERY_STATE_MASK) + (1u << RECOVERY_STATE_BITS)) | next;
    return atomic_compare_exchange_strong(&control->ticket, &before, after);
}

static inline bool recovery_control_claim(recovery_control_t *control, recovery_state_t next)
{
    return recovery_control_claim_ticket(control, atomic_load(&control->ticket), next);
}

static inline bool recovery_control_change_ticket(recovery_control_t *control,
                                                   unsigned before, recovery_state_t next)
{
    unsigned after = (before & ~RECOVERY_STATE_MASK) | next;
    return atomic_compare_exchange_strong(&control->ticket, &before, after);
}

/* Only the owner of a claimed operation may publish its next state. */
static inline void recovery_control_set(recovery_control_t *control, recovery_state_t next)
{
    unsigned before = atomic_load(&control->ticket);
    while (!recovery_control_change_ticket(control, before, next)) {
        before = atomic_load(&control->ticket);
    }
}

static inline bool recovery_control_change(recovery_control_t *control,
                                           recovery_state_t before, recovery_state_t after)
{
    unsigned ticket = atomic_load(&control->ticket);
    return (ticket & RECOVERY_STATE_MASK) == (unsigned)before &&
           recovery_control_change_ticket(control, ticket, after);
}

typedef struct {
    unsigned previous_keys, stable_keys, generation;
    recovery_state_t state;
    bool initialized, confirmation_ready;
} recovery_keys_t;

/* Two equal samples debounce an edge. Every new request must first be shown
 * and then observed with both keys released: an old/held press is no consent. */
static inline unsigned recovery_keys_pressed(recovery_keys_t *keys, unsigned sample,
                                             recovery_state_t state, unsigned generation,
                                             bool prompt_shown)
{
    if (!keys->initialized || keys->state != state || keys->generation != generation) {
        keys->initialized = true;
        keys->state = state;
        keys->generation = generation;
        keys->previous_keys = keys->stable_keys = sample;
        keys->confirmation_ready = false;
        return 0;
    }
    if (state == CONFIRM && (!prompt_shown || !keys->confirmation_ready)) {
        keys->confirmation_ready = prompt_shown && sample == 0;
        keys->previous_keys = keys->stable_keys = sample;
        return 0;
    }
    unsigned pressed = 0;
    if (sample == keys->previous_keys && sample != keys->stable_keys) {
        pressed = sample & ~keys->stable_keys;
        keys->stable_keys = sample;
    }
    keys->previous_keys = sample;
    return pressed;
}

