/*
 * physics.h - Movement declarations
 *
 * This header defines player movement and visual interpolation.
 */

#ifndef PHYSICS_H
#define PHYSICS_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Movement state - 8 bytes */
typedef struct MovementState {
    uint8_t pos_x;            /* Current X (0-63) */
    uint8_t pos_y;            /* Current Y (0-63) */
    uint8_t target_x;         /* Target X (0-63) */
    uint8_t target_y;         /* Target Y (0-63) */
    uint8_t direction;        /* Current direction (0-3, 255 for none) */
    uint8_t is_moving;        /* Boolean: 1 if moving, 0 if not */
    uint8_t just_started;     /* Boolean: 1 if just started, 0 if not */
    uint8_t move_frame;       /* Current frame (0-11) */
} MovementState;

/* Movement functions */
bool are_directions_opposite(Direction dir1, Direction dir2);
bool start_movement(GameState* game, Direction dir);
void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y);
void complete_movement(GameState* game);
Direction get_direction(const MovementState* movement);
void set_direction(MovementState* movement, Direction dir);

#endif /* PHYSICS_H */
