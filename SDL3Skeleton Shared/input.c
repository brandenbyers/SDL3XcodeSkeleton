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

/* Process gamepad button press/release */
void process_gamepad_button_event(InputState* input, Uint8 button, bool pressed) {
    Direction dir = DIR_NONE;
    
    /* Map gamepad buttons to directions */
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            dir = DIR_RIGHT;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            dir = DIR_UP;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            dir = DIR_LEFT;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            dir = DIR_DOWN;
            break;
        case SDL_GAMEPAD_BUTTON_START:
            set_restart_requested(input, pressed);
            return;
    }
    
    /* Update direction key state if valid direction */
    if (dir != DIR_NONE) {
        set_key_state(input, dir, pressed);
        
        /* If button was pressed, update current direction */
        if (pressed) {
            input->current_dir = dir;
        }
        /* If button was released and it was the current direction, find new current direction */
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

/* Process gamepad axis movement */
void process_gamepad_axis_event(InputState* input, Uint8 axis, Sint16 value) {
    const float deadzone = 0.5f;
    float normalized = value / 32767.0f;
    Direction dir = DIR_NONE;
    bool pressed = false;
    
    /* Check which axis and direction */
    if (axis == SDL_GAMEPAD_AXIS_LEFTX) {
        if (normalized > deadzone) {
            dir = DIR_RIGHT;
            pressed = true;
        } else if (normalized < -deadzone) {
            dir = DIR_LEFT;
            pressed = true;
        } else {
            /* Release both left and right if in deadzone */
            set_key_state(input, DIR_RIGHT, false);
            set_key_state(input, DIR_LEFT, false);
        }
    } else if (axis == SDL_GAMEPAD_AXIS_LEFTY) {
        if (normalized > deadzone) {
            dir = DIR_DOWN;
            pressed = true;
        } else if (normalized < -deadzone) {
            dir = DIR_UP;
            pressed = true;
        } else {
            /* Release both up and down if in deadzone */
            set_key_state(input, DIR_UP, false);
            set_key_state(input, DIR_DOWN, false);
        }
    }
    
    /* Update key state if a direction was determined */
    if (dir != DIR_NONE) {
        set_key_state(input, dir, pressed);
        
        if (pressed) {
            input->current_dir = dir;
        } else if (dir == input->current_dir) {
            /* If this axis was controlling the current direction, find a new direction */
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
    SDL_Log("Initializing gamepad subsystem...");
    
    /* SDL3 doesn't need separate initialization - it's done in SDL_Init */
    
    /* Check for connected gamepads */
    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    
    if (!gamepads || count < 1) {
        SDL_Log("No gamepads found");
        app->gamepad = NULL;
        app->gamepad_id = 0;
        if (gamepads) SDL_free(gamepads);
        return;
    }
    
    /* Open the first gamepad */
    SDL_JoystickID device_id = gamepads[0];
    app->gamepad = SDL_OpenGamepad(device_id);
    if (app->gamepad) {
        app->gamepad_id = device_id;
        const char* name = SDL_GetGamepadName(app->gamepad);
        SDL_Log("Gamepad connected: %s", name ? name : "Unknown");
    } else {
        SDL_Log("Failed to open gamepad: %s", SDL_GetError());
    }
    
    SDL_free(gamepads);
}
