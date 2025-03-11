/*
 * input.c - Input handling for the Bit-Twiddled Game Engine
 *
 * This file contains functions for processing keyboard and gamepad input,
 * managing input state, and handling input mapping.
 */

#include "main.h"

/*
 * Input State Functions
 */

/* Check if a direction key is pressed */
bool is_key_pressed(const InputState* input, Direction dir) {
    return (input->key_states & (1 << dir)) != 0;
}

/* Set key state */
void set_key_state(InputState* input, Direction dir, bool pressed) {
    input->key_states = (input->key_states & ~(1 << dir)) | (pressed << dir);
}

/* Check if buffered direction is set */
bool has_buffered_dir(const InputState* input) {
    return (input->key_states & HAS_BUFFERED) != 0;
}

/* Set buffered direction flag */
void set_has_buffered(InputState* input, bool has_buffered) {
    input->key_states = (input->key_states & ~HAS_BUFFERED) | (has_buffered ? HAS_BUFFERED : 0);
}

/* Check if restart is requested */
bool is_restart_requested(const InputState* input) {
    return (input->key_states & RESTART_REQ) != 0;
}

/* Set restart requested flag */
void set_restart_requested(InputState* input, bool requested) {
    input->key_states = (input->key_states & ~RESTART_REQ) | (requested ? RESTART_REQ : 0);
}

/* Process key press/release with throttling for repeats */
void process_key_event(InputState* input, SDL_Scancode key, bool pressed) {
    Direction dir = DIR_NONE;
    
    /* Map keyboard to direction */
    dir = (key == SDL_SCANCODE_RIGHT) ? DIR_RIGHT :
    (key == SDL_SCANCODE_UP)    ? DIR_UP :
    (key == SDL_SCANCODE_LEFT)  ? DIR_LEFT :
    (key == SDL_SCANCODE_DOWN)  ? DIR_DOWN : DIR_NONE;
    
    /* Handle restart key */
    if (key == SDL_SCANCODE_R) {
        set_restart_requested(input, pressed);
        return;
    }
    
    /* Update direction key state if valid direction */
    if (dir != DIR_NONE) {
        set_key_state(input, dir, pressed);
        
        /* If key was pressed, update current direction */
        if (pressed) {
            input->current_dir = dir;
        }
        /* If key was released and it was the current direction, find new current direction */
        else if (dir == input->current_dir) {
            /* Use standard bit check instead of bit scan for compatibility */
            uint8_t keys = input->key_states & 0x0F; /* Get just direction bits */
            input->current_dir = DIR_NONE;
            for (int i = 0; i < DIR_COUNT; i++) {
                if (keys & (1 << i)) {
                    input->current_dir = i;
                    break;
                }
            }
        }
    }
}

/* Efficient gamepad state polling with throttling */
void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad) {
    if (!gamepad) return;
    
    /* Create bit masks for different input sources */
    uint8_t new_state = 0;
    
    /* Check D-pad states and set appropriate bits */
    new_state |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? KEY_RIGHT : 0;
    new_state |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)    ? KEY_UP    : 0;
    new_state |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  ? KEY_LEFT  : 0;
    new_state |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  ? KEY_DOWN  : 0;
    
    /* Check analog stick (with deadzone) */
    float x_axis = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y_axis = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    
    const float deadzone = 0.5f;
    new_state |= (x_axis > deadzone)  ? KEY_RIGHT : 0;
    new_state |= (x_axis < -deadzone) ? KEY_LEFT  : 0;
    new_state |= (y_axis > deadzone)  ? KEY_DOWN  : 0;
    new_state |= (y_axis < -deadzone) ? KEY_UP    : 0;
    
    /* Update input state for directions */
    uint8_t old_state = input->key_states & 0x0F;
    uint8_t changed_bits = old_state ^ new_state;
    
    /* Only process if anything changed */
    if (changed_bits) {
        /* Update the direction bits in key_states */
        input->key_states = (input->key_states & ~0x0F) | new_state;
        
        /* If any new bits are set, update current direction */
        uint8_t new_pressed = changed_bits & new_state;
        if (new_pressed) {
            /* Find first new direction bit */
            for (int i = 0; i < DIR_COUNT; i++) {
                if (new_pressed & (1 << i)) {
                    input->current_dir = i;
                    break;
                }
            }
        }
        /* If current direction was released, find new one */
        else if (!(new_state & (1 << input->current_dir))) {
            input->current_dir = DIR_NONE;
            for (int i = 0; i < DIR_COUNT; i++) {
                if (new_state & (1 << i)) {
                    input->current_dir = i;
                    break;
                }
            }
        }
    }
    
    /* Check if restart button is pressed */
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START)) {
        set_restart_requested(input, true);
    }
}

/* Initialize gamepad */
void initialize_gamepad(AppState* app) {
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    
    if (!gamepads || count < 1) {
        app->gamepad = NULL;
        app->gamepad_id = 0;
        SDL_free(gamepads);
        return;
    }
    
    SDL_JoystickID device_id = gamepads[0];
    app->gamepad = SDL_OpenGamepad(device_id);
    if (app->gamepad) {
        app->gamepad_id = device_id;
        const char* name = SDL_GetGamepadName(app->gamepad);
        SDL_Log("Controller connected: %s", name ? name : "Unknown");
    }
    
    SDL_free(gamepads);
}

/*
 * Touch Input Functions
 */

/* Check if touch is active */
bool is_touch_active(const InputState* input) {
    return (input->key_states & TOUCH_ACTIVE) != 0;
}

/* Set touch active state */
void set_touch_active(InputState* input, bool active) {
    input->key_states = (input->key_states & ~TOUCH_ACTIVE) | (active ? TOUCH_ACTIVE : 0);
}

/* Process touch down event */
void process_touch_down(InputState* input, float x, float y, uint32_t finger_id) {
    /* Store initial touch position as joystick center */
    input->touch_start_x = x;
    input->touch_start_y = y;
    input->touch_current_x = x;
    input->touch_current_y = y;
    input->touch_finger_id = finger_id;
    
    /* Set touch as active */
    set_touch_active(input, true);
    
    /* No direction yet on initial touch */
    /* We'll wait for motion to determine direction */
}

/* Process touch motion event */
void process_touch_motion(InputState* input, float x, float y, uint32_t finger_id) {
    /* Ignore if not the active finger */
    if (!is_touch_active(input) || input->touch_finger_id != finger_id) {
        return;
    }
    
    /* Update current position */
    input->touch_current_x = x;
    input->touch_current_y = y;
    
    /* Calculate delta from start */
    float dx = x - input->touch_start_x;
    float dy = y - input->touch_start_y;
    
    /* Constrain to maximum distance for D-pad (tighter control) */
    const float max_distance = 60.0f;  /* Maximum distance for full activation */
    float distance = sqrtf(dx*dx + dy*dy);
    
    if (distance > max_distance) {
        /* Normalize to max distance to create a "hard edge" feeling */
        dx = dx * max_distance / distance;
        dy = dy * max_distance / distance;
    }
    
    /* Convert to direction based on greatest displacement (digital control) */
    /* Using a small deadzone to prevent accidental movements */
    const float deadzone = 8.0f;  /* Smaller deadzone for more responsive control */
    
    /* Clear existing direction keys from touch */
    input->key_states &= ~0x0F;  /* Clear direction bits */
    
    /* Determine the dominant direction */
    if (fabsf(dx) > deadzone || fabsf(dy) > deadzone) {
        /* Determine if horizontal or vertical movement dominates */
        if (fabsf(dx) > fabsf(dy)) {
            /* Horizontal movement */
            if (dx > 0) {
                /* Right */
                set_key_state(input, DIR_RIGHT, true);
                input->current_dir = DIR_RIGHT;
            } else {
                /* Left */
                set_key_state(input, DIR_LEFT, true);
                input->current_dir = DIR_LEFT;
            }
        } else {
            /* Vertical movement */
            if (dy > 0) {
                /* Down (SDL coordinates have Y increasing downward) */
                set_key_state(input, DIR_DOWN, true);
                input->current_dir = DIR_DOWN;
            } else {
                /* Up */
                set_key_state(input, DIR_UP, true);
                input->current_dir = DIR_UP;
            }
        }
    }
}

/* Process touch up event */
void process_touch_up(InputState* input, uint32_t finger_id) {
    /* Ignore if not the active finger */
    if (!is_touch_active(input) || input->touch_finger_id != finger_id) {
        return;
    }
    
    /* Clear direction keys that were set by touch */
    input->key_states &= ~0x0F;  /* Clear direction bits */
    
    /* Set touch as inactive */
    set_touch_active(input, false);
    
    /* Reset current direction if it was set by touch */
    input->current_dir = DIR_NONE;
}

/* Get the current touch direction with tighter control and stronger diagonal resistance */
Direction get_touch_direction(const InputState* input) {
    if (!is_touch_active(input)) {
        return DIR_NONE;
    }
    
    /* Calculate delta from start */
    float dx = input->touch_current_x - input->touch_start_x;
    float dy = input->touch_current_y - input->touch_start_y;
    
    /* Apply deadzone */
    const float deadzone = 8.0f;
    
    if (fabsf(dx) <= deadzone && fabsf(dy) <= deadzone) {
        return DIR_NONE;
    }
    
    /* Enhanced diagonal resistance - require a stronger bias to register direction */
    /* This makes it easier to get clean cardinal directions */
    if (fabsf(dx) > fabsf(dy) * 1.2f) {
        /* Horizontal movement with bias multiplier */
        return (dx > 0) ? DIR_RIGHT : DIR_LEFT;
    } else if (fabsf(dy) > fabsf(dx) * 1.2f) {
        /* Vertical movement with bias multiplier */
        return (dy > 0) ? DIR_DOWN : DIR_UP;
    } else {
        /* In the diagonal deadzone, use the slightly stronger direction */
        return (fabsf(dx) > fabsf(dy)) ?
        ((dx > 0) ? DIR_RIGHT : DIR_LEFT) :
        ((dy > 0) ? DIR_DOWN : DIR_UP);
    }
}
