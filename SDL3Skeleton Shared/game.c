/*
 * game.c - Core game implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the central game logic that ties together all subsystems.
 * It implements the game initialization, update loop, and core mechanics.
 */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "entity.h"
#include "collision.h"
#include "viewport.h"
#include "input.h"
#include "level.h"

/*
 * Get cell value from grid with direct array access
 */
CellType get_cell(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts (efficient for power-of-two sizes) */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Direct array access - much more cache friendly */
    return (CellType)game->grid[idx];
}

/*
 * Set cell value in grid with direct array access
 */
void set_cell(GameState* game, int x, int y, CellType type) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Mark grid as changed if the cell value is different */
    if (game->grid[idx] != type) {
        game->grid[idx] = type;
        game->grid_state->cells_changed = true;
        
        /* Invalidate cache since grid changed */
        game->grid_state->cache_valid = false;
        
        /* If cell is in viewport, also update cache directly for write-through caching */
        if (is_in_viewport(game, x, y)) {
            int viewport_x, viewport_y;
            grid_to_viewport(game, x, y, &viewport_x, &viewport_y);
            int cache_idx = viewport_y * VIEWPORT_WIDTH + viewport_x;
            game->viewport->cache[cache_idx] = type;
        }
        
        /* Update collision map to match visual grid */
        uint8_t cell_flags = get_collision_cell(game, x, y);
        
        switch (type) {
            case CELL_WALL:
                set_collision_cell(game, x, y, CMAP_WALL, true);
                break;
            case CELL_ITEM:
                set_collision_cell(game, x, y, CMAP_ITEM, true);
                break;
            case CELL_EMPTY:
                /* Clear all but entity and player flags */
                cell_flags &= (CMAP_ENTITY | CMAP_PLAYER);
                game->collision->map[(y << GRID_WIDTH_SHIFT) | x] = cell_flags;
                break;
            case CELL_PIVOT:
                /* Pivot points are handled separately by set_pivot_point function */
                break;
            default:
                /* Handle any other cases or CELL_MAX */
                break;
        }
    }
}

/*
 * Initialize the game state
 */
void init_game(GameState* game) {
    /* Initialize viewport position (centered vertically) */
    game->viewport->offset_x = 0;
    game->viewport->offset_y = VIEWPORT_OFFSET_Y;
    
    /* Initialize cache tracking */
    game->grid_state->cache_valid = false;
    
    /* Initialize level system - this loads the first level */
    init_level_system(game);
    
    /* Reset timing */
    game->last_tick_time = SDL_GetTicks();
    game->accumulated_time = 0;
    game->frame_count = 0;
}

/*
 * Update the game logic at a fixed time step (60 FPS)
 */
void update_game_logic_fixed_step(GameState* game) {
    MovementState* movement = game->player;
    InputState* input = game->input;
    
    /* Check for restart request */
    if (is_restart_requested(input)) {
        /* Reload the current level */
        load_level(game, get_current_level(game));
        set_restart_requested(input, false);
        return;
    }
    
    /* Process any special actions (like level cycling) */
    process_actions(game);
    
    /* Check if level is completed */
    if (is_level_completed(game)) {
        /* Load the next level */
        next_level(game);
        return;
    }
    
    /* Increment frame counter */
    game->frame_count++;
    
    /* Update collision map with current positions */
    update_collision_map(game);
    
    /* Update entity positions based on frame-based movement */
    update_entities(game);
    
    /* If player is not moving, check for direction input to start movement */
    if (!movement->is_moving) {
        /* Try to move in current input direction */
        if (input->current_dir != DIR_NONE) {
            if (start_movement(game, input->current_dir)) {
                /* Movement started */
                movement->just_started = true;
            }
        }
    }
    /* If player is currently moving, handle movement progression */
    else {
        /* Check for buffered direction change near the end of current movement */
        if (movement->move_frame >= (FRAMES_PER_TILE - CORNER_BUFFER_FRAMES)) {
            /* Near the end of current movement, can buffer a turn */
            Direction current_dir = get_direction(movement);
            if (input->current_dir != DIR_NONE &&
                input->current_dir != current_dir &&
                !are_directions_opposite(input->current_dir, current_dir)) {
                
                /* Buffer this direction for the next intersection */
                input->buffered_dir = input->current_dir;
                set_has_buffered(input, true);
            }
        }
        
        /* Increment movement frame counter */
        movement->move_frame++;
        
        /* Check if movement is complete */
        if (movement->move_frame >= FRAMES_PER_TILE) {
            /* Update old position in collision map */
            set_collision_cell(game, movement->pos_x, movement->pos_y, CMAP_PLAYER, false);
            
            /* Complete the movement */
            complete_movement(game);
            
            /* Update new position in collision map */
            set_collision_cell(game, movement->pos_x, movement->pos_y, CMAP_PLAYER, true);
        }
    }
    
    /* Check for collision with entities */
    if (check_player_entity_collision(game)) {
        /* Collision detected - reset level */
        set_restart_requested(game->input, true);
        return;
    }
    
    /* Check if viewport cache needs updating */
    if (!game->grid_state->cache_valid) {
        update_viewport_cache(game);
    }
    
    /* Clear the just started moving flag after first frame */
    if (movement->just_started) {
        movement->just_started = false;
    }
}
