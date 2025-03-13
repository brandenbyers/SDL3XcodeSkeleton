/*
 * physics.c - Movement implementation
 *
 * This file contains player movement functions and visual interpolation.
 */

#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#include "game.h"
#include "physics.h"
#include "collision.h"
#include "entity.h"
#include "input.h"

/*
 * Movement Functions
 */

/* Get current direction */
Direction get_direction(const MovementState* movement) {
    return (Direction)movement->direction;
}

/* Set current direction */
void set_direction(MovementState* movement, Direction dir) {
    movement->direction = dir;
}

/* Check if directions are opposite */
bool are_directions_opposite(Direction dir1, Direction dir2) {
    /* If either direction is NONE, they're not opposite */
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) return false;
    
    /* Directions are opposite if they differ by 2 (when 2-bit values) */
    return ((dir1 ^ dir2) == 2);  /* XOR with 2 checks if bits flipped */
}

/* Start Movement */
bool start_movement(GameState* game, Direction dir) {
    MovementState* movement = game->player;
    
    /* Check if direction is valid */
    if (dir == DIR_NONE) return false;
    
    /* Calculate target position with bit masking for wrapping */
    int target_x = (movement->pos_x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    int target_y = (movement->pos_y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
    
    /* Check if the move is valid using collision map */
    if (!is_move_valid(game, movement->pos_x, movement->pos_y, dir)) {
        return false;
    }
    
    /* Update movement state */
    movement->target_x = target_x;
    movement->target_y = target_y;
    set_direction(movement, dir);
    movement->is_moving = true;
    movement->just_started = true;
    movement->move_frame = 0;
    
    return true;
}

/* Calculate visual position based on movement state */
void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y) {
    if (!movement->is_moving || movement->move_frame >= FRAMES_PER_TILE) {
        /* Not moving or movement complete - use exact grid position */
        *visual_x = (float)movement->pos_x;
        *visual_y = (float)movement->pos_y;
        return;
    }
    
    /* Calculate progress (0.0 to 1.0) */
    float progress = (float)movement->move_frame / FRAMES_PER_TILE;
    
    /* Get start and target positions */
    float start_x = (float)movement->pos_x;
    float start_y = (float)movement->pos_y;
    float target_x = (float)movement->target_x;
    float target_y = (float)movement->target_y;
    
    /* Check if wrapping horizontally */
    int dx = abs((int)movement->target_x - (int)movement->pos_x);
    if (dx > GRID_WIDTH/2) {
        /* We're wrapping around the edge */
        if (movement->target_x < movement->pos_x) {
            /* Moving right to left across the edge */
            target_x += GRID_WIDTH;
        } else {
            /* Moving left to right across the edge */
            start_x += GRID_WIDTH;
        }
    }
    
    /* Check if wrapping vertically */
    int dy = abs((int)movement->target_y - (int)movement->pos_y);
    if (dy > GRID_HEIGHT/2) {
        /* We're wrapping around the edge */
        if (movement->target_y < movement->pos_y) {
            /* Moving bottom to top across the edge */
            target_y += GRID_HEIGHT;
        } else {
            /* Moving top to bottom across the edge */
            start_y += GRID_HEIGHT;
        }
    }
    
    /* Linear interpolation between start and target */
    *visual_x = start_x + (target_x - start_x) * progress;
    *visual_y = start_y + (target_y - start_y) * progress;
    
    /* Normalize coordinates to grid range */
    *visual_x = fmodf(*visual_x, GRID_WIDTH);
    *visual_y = fmodf(*visual_y, GRID_HEIGHT);
    
    /* Handle negative coordinates from wrapping */
    if (*visual_x < 0) *visual_x += GRID_WIDTH;
    if (*visual_y < 0) *visual_y += GRID_HEIGHT;
}

/* Handle movement completion and start next movement if needed */
void complete_movement(GameState* game) {
    MovementState* movement = game->player;
    InputState* input = game->input;
    
    /* Update position to target */
    movement->pos_x = movement->target_x;
    movement->pos_y = movement->target_y;
    movement->move_frame = 0;
    
    /* Check if target has an item */
    if (get_collision_cell(game, movement->pos_x, movement->pos_y) & CMAP_ITEM) {
        /* Collect the item */
        set_cell(game, movement->pos_x, movement->pos_y, CELL_EMPTY);
        set_collision_cell(game, movement->pos_x, movement->pos_y, CMAP_ITEM, false);
    }
    
    /* Check if we should continue moving */
    Direction next_dir = DIR_NONE;
    
    /* Priority 1: Use buffered direction if valid */
    if (has_buffered_dir(input) &&
        is_move_valid(game, movement->pos_x, movement->pos_y, input->buffered_dir)) {
        next_dir = input->buffered_dir;
        set_has_buffered(input, false);
    }
    /* Priority 2: Continue in same direction if key still held */
    else {
        Direction current_dir = get_direction(movement);
        if (is_key_pressed(input, current_dir) &&
            is_move_valid(game, movement->pos_x, movement->pos_y, current_dir)) {
            next_dir = current_dir;
        }
        /* Priority 3: Check for any held direction key */
        else {
            /* Use standard bit check instead of bit scan for compatibility */
            uint8_t keys = input->key_states & 0x0F; /* Get just direction bits */
            for (int i = 0; i < DIR_COUNT; i++) {
                if ((keys & (1 << i)) && is_move_valid(game, movement->pos_x, movement->pos_y, i)) {
                    next_dir = i;
                    break;
                }
            }
        }
    }
    
    /* Start next movement or stop */
    if (next_dir != DIR_NONE) {
        start_movement(game, next_dir);
    } else {
        movement->is_moving = false;
        set_direction(movement, DIR_NONE);
    }
}
