/*
 * game.c - Core game implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the central game logic that ties together all subsystems.
 * It implements the game initialization, update loop, and level design.
 */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "entity.h"
#include "collision.h"
#include "physics.h"
#include "viewport.h"
#include "input.h"

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
                /* Pivot points are handled separately */
                break;
            default:
                /* Handle any other cases or CELL_MAX */
                break;
        }
    }
}

/*
 * Initialize the game state - this sets up the example level
 */
void init_game(GameState* game) {
    /* Clear the grid */
    memset(game->grid, CELL_EMPTY, GRID_SIZE);
    
    /* Initialize viewport position (centered vertically) */
    game->viewport->offset_x = 0;
    game->viewport->offset_y = VIEWPORT_OFFSET_Y;
    
    /* Initialize cache tracking */
    game->grid_state->cache_valid = false;
    
    /* Initialize entity system */
    init_entity_system(game);
    
    /* Initialize collision system */
    init_collision_system(game->collision);
    
    /* Build the level walls */
    
    /* Outer walls around the viewport */
    for (int i = 0; i < VIEWPORT_WIDTH; i++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Top wall */
        viewport_to_grid(game, i, 0, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Bottom wall */
        viewport_to_grid(game, i, VIEWPORT_HEIGHT - 1, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Side walls of the viewport */
    for (int j = 0; j < VIEWPORT_HEIGHT; j++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Left wall */
        viewport_to_grid(game, 0, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
        
        /* Right wall */
        viewport_to_grid(game, VIEWPORT_WIDTH - 1, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        set_collision_cell(game, grid_x, grid_y, CMAP_WALL, true);
    }
    
    /* Add pivot points for entity movement */
    int left_pivot_x, left_pivot_y;
    int right_pivot_x, right_pivot_y;
    
    /* Left side pivot */
    viewport_to_grid(game, 10, VIEWPORT_HEIGHT/2, &left_pivot_x, &left_pivot_y);
    set_cell(game, left_pivot_x, left_pivot_y, CELL_PIVOT);
    
    /* Set pivot in collision map with directions */
    set_pivot_point(game, left_pivot_x, left_pivot_y, PIVOT_LEFT | PIVOT_RIGHT);
    
    /* Right side pivot */
    viewport_to_grid(game, VIEWPORT_WIDTH - 11, VIEWPORT_HEIGHT/2, &right_pivot_x, &right_pivot_y);
    set_cell(game, right_pivot_x, right_pivot_y, CELL_PIVOT);
    
    /* Set pivot in collision map with directions */
    set_pivot_point(game, right_pivot_x, right_pivot_y, PIVOT_LEFT | PIVOT_RIGHT);
    
    /* Add a patrol entity that moves between the pivots */
    add_entity(game, left_pivot_x, left_pivot_y, DIR_RIGHT, ENTITY_PATROL);
    
    /* Add some items for collection */
    for (int i = 0; i < 20; i++) {
        /* Generate random viewport coordinates (away from walls) */
        int viewport_x = 5 + (rand() % (VIEWPORT_WIDTH - 10));
        int viewport_y = 5 + (rand() % (VIEWPORT_HEIGHT - 10));
        
        /* Convert to grid coordinates */
        int grid_x, grid_y;
        viewport_to_grid(game, viewport_x, viewport_y, &grid_x, &grid_y);
        
        /* Only place items in empty spaces */
        if (get_cell(game, grid_x, grid_y) == CELL_EMPTY &&
            !is_entity_at_position(game, grid_x, grid_y)) {
            set_cell(game, grid_x, grid_y, CELL_ITEM);
            set_collision_cell(game, grid_x, grid_y, CMAP_ITEM, true);
        }
    }
    
    /* Initialize player in center of bottom half */
    int player_viewport_x = VIEWPORT_WIDTH / 2;
    int player_viewport_y = 3 * VIEWPORT_HEIGHT / 4;
    
    /* Convert to grid coordinates */
    int player_grid_x, player_grid_y;
    viewport_to_grid(game, player_viewport_x, player_viewport_y, &player_grid_x, &player_grid_y);
    
    /* Set player position */
    game->player->pos_x = player_grid_x;
    game->player->pos_y = player_grid_y;
    game->player->target_x = player_grid_x;
    game->player->target_y = player_grid_y;
    game->player->direction = DIR_NONE;
    game->player->is_moving = false;
    game->player->just_started = false;
    game->player->move_frame = 0;
    
    /* Add player to collision map */
    set_collision_cell(game, player_grid_x, player_grid_y, CMAP_PLAYER, true);
    
    /* Reset input state */
    game->input->key_states = 0;
    game->input->current_dir = DIR_NONE;
    game->input->buffered_dir = DIR_NONE;
    
    /* Update viewport cache */
    update_viewport_cache(game);
    
    /* Mark grid as updated */
    game->grid_state->cells_changed = false;
    game->grid_state->last_frame_updated = game->frame_count;
    
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
        init_game(game);
        set_restart_requested(input, false);
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
