/*
 * input.h - Input subsystem declarations
 *
 * This header defines input processing structures and functions.
 */

#ifndef INPUT_H
#define INPUT_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_gamepad.h>
#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Input state flags */
#define KEY_RIGHT           0x01
#define KEY_UP              0x02
#define KEY_LEFT            0x04
#define KEY_DOWN            0x08
#define HAS_BUFFERED        0x10
#define RESTART_REQ         0x20

/* Input state structure */
typedef struct InputState {
    uint8_t key_states;        /* Bit 0-3: direction keys, 4: has_buffered, 5: restart */
    uint8_t current_dir;       /* Current direction (0-3, 255 for none) */
    uint8_t buffered_dir;      /* Buffered direction (0-3, 255 for none) */
    uint8_t actions;           /* Bit flags for special actions */
} InputState;

/* Input processing functions */
void process_key_event(InputState* input, SDL_Scancode key, bool pressed);
void process_gamepad_button_event(InputState* input, Uint8 button, bool pressed);
void process_gamepad_axis_event(InputState* input, Uint8 axis, Sint16 value);
void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad);
void process_actions(GameState* game);
bool is_key_pressed(const InputState* input, Direction dir);
void set_key_state(InputState* input, Direction dir, bool pressed);
bool has_buffered_dir(const InputState* input);
void set_has_buffered(InputState* input, bool has_buffered);
bool is_restart_requested(const InputState* input);
void set_restart_requested(InputState* input, bool requested);
bool is_action_requested(const InputState* input, uint8_t action);
void set_action_requested(InputState* input, uint8_t action, bool requested);
void clear_actions(InputState* input);
void initialize_gamepad(AppState* app);

#endif /* INPUT_H */
