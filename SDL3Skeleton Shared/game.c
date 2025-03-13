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
    
    /* Initialize glitch state */
    init_glitch_state(game->glitch);
    
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
    
    /* Initialize glitch state if needed */
    if (game->frame_count == 0) {
        init_glitch_state(game->glitch);
    }
    
    /* If glitch is active, update it and skip normal game logic */
    if (is_glitch_active(game)) {
        update_glitch_effect(game);
        return;
    }
    
    /* If game is paused and not in glitch, don't update game logic */
    if (game->glitch->paused) {
        return;
    }
    
    /* Check for restart request */
    if (is_restart_requested(input)) {
        /* Start the glitch effect instead of immediately restarting */
        start_glitch_effect(game, GLITCH_RESTART);
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
        /* Start glitch effect instead of immediate reset */
        start_glitch_effect(game, GLITCH_RESTART);
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

/*
 * Initialize the glitch effect state
 */
void init_glitch_state(GlitchState* glitch) {
    memset(glitch, 0, sizeof(GlitchState));
    glitch->active = false;
    glitch->type = GLITCH_RESTART;
    glitch->frame_count = 0;
    glitch->expansion_radius = 0.0f;
    glitch->paused = false;
    glitch->pending_pause_toggle = false;
    glitch->target_pause_state = false;
}

static void get_spawn_position(GameState* game, int* spawn_x, int* spawn_y) {
    /* The level system assigns the player position during level loading,
     so we can use the current level number to determine the approximate spawn position.
     This is a simplified version - ideally we would add a proper function to level.c */
    
    int viewport_x, viewport_y;
    int level_num = get_current_level(game);
    
    /* Use the same logic as in build_level0 and build_level1 */
    if (level_num == 0) {
        /* Level 0: Player starts in the center of bottom half */
        viewport_x = VIEWPORT_WIDTH / 2;
        viewport_y = 3 * VIEWPORT_HEIGHT / 4;
    } else {
        /* Level 1: Player starts in a corner */
        viewport_x = 3;
        viewport_y = 3;
    }
    
    /* Convert to grid coordinates */
    int grid_x, grid_y;
    viewport_to_grid(game, viewport_x, viewport_y, &grid_x, &grid_y);
    
    /* Return the spawn position */
    *spawn_x = grid_x;
    *spawn_y = grid_y;
}

/*
 * Start the glitch effect (called when player dies, restart requested, or pause/unpause)
 */
void start_glitch_effect(GameState* game, GlitchType type) {
    GlitchState* glitch = game->glitch;
    
    /* Don't restart if already in a glitch */
    if (glitch->active) {
        return;
    }
    
    /* Get position for expansion highlight based on glitch type */
    int highlight_grid_x, highlight_grid_y;
    
    if (type == GLITCH_RESTART) {
        /* For restart: use spawn position */
        get_spawn_position(game, &highlight_grid_x, &highlight_grid_y);
    } else {
        /* For pause/unpause: use current player position */
        MovementState* player = game->player;
        float visual_x, visual_y;
        get_visual_position(player, &visual_x, &visual_y);
        highlight_grid_x = (int)visual_x;
        highlight_grid_y = (int)visual_y;
    }
    
    /* Convert to viewport coordinates for the visual effect */
    int viewport_x, viewport_y;
    grid_to_viewport(game, highlight_grid_x, highlight_grid_y, &viewport_x, &viewport_y);
    
    /* Store highlight position in screen pixels */
    glitch->highlight_pos_x = viewport_x * PIXEL_SCALE + PIXEL_SCALE/2;
    glitch->highlight_pos_y = viewport_y * PIXEL_SCALE + PIXEL_SCALE/2;
    
    /* Reset glitch parameters */
    glitch->active = true;
    glitch->type = type;
    glitch->frame_count = 0;
    glitch->expansion_radius = 0.0f;
    
    /* Set pause toggle information for PAUSE/UNPAUSE glitches */
    if (type == GLITCH_PAUSE || type == GLITCH_UNPAUSE) {
        glitch->pending_pause_toggle = true;
        glitch->target_pause_state = (type == GLITCH_PAUSE);
        SDL_Log("Setting target pause state to: %s",
                glitch->target_pause_state ? "PAUSED" : "UNPAUSED");
    }
    
    /* Randomize the number of scanlines for this glitch */
    glitch->scanline_count = 2 + (rand() % (GLITCH_SCANLINES - 1));
    
    /* Randomize scanline positions, heights, and shifts */
    for (int i = 0; i < glitch->scanline_count; i++) {
        /* Random position within the viewport */
        glitch->scanline_y[i] = rand() % WINDOW_HEIGHT;
        
        /* Random height between 2 and 8 pixels */
        glitch->scanline_height[i] = 2 + (rand() % 7);
        
        /* Random shift amount */
        if ((float)rand()/RAND_MAX < GLITCH_SHIFT_PROB) {
            glitch->scanline_shifts[i] = (rand() % (GLITCH_SHIFT_MAX * 2)) - GLITCH_SHIFT_MAX;
        } else {
            glitch->scanline_shifts[i] = 0;
        }
    }
    
    /* Randomize color shifting */
    if ((float)rand()/RAND_MAX < GLITCH_COLOR_PROB) {
        glitch->color_shift_r = rand() % 100;
        glitch->color_shift_g = rand() % 100;
        glitch->color_shift_b = rand() % 100;
    } else {
        glitch->color_shift_r = 0;
        glitch->color_shift_g = 0;
        glitch->color_shift_b = 0;
    }
    
    /* Force a render on next frame */
    SDL_Log("Glitch effect started - Type: %s",
            (type == GLITCH_RESTART) ? "Restart" :
            (type == GLITCH_PAUSE) ? "Pause" : "Unpause");
}

/*
 * Update glitch effect state
 */
void update_glitch_effect(GameState* game) {
    GlitchState* glitch = game->glitch;
    
    /* If not active, nothing to update */
    if (!glitch->active) {
        return;
    }
    
    /* Update glitch frame counter */
    glitch->frame_count++;
    
    /* Update expansion radius */
    glitch->expansion_radius += GLITCH_EXPAND_SPEED;
    
    /* Every 2-3 frames, update the random elements of the glitch */
    if (glitch->frame_count % 3 == 0) {
        /* Randomize scanline positions and shifts again for more chaos */
        for (int i = 0; i < glitch->scanline_count; i++) {
            if ((float)rand()/RAND_MAX < GLITCH_SHIFT_PROB) {
                glitch->scanline_y[i] = rand() % WINDOW_HEIGHT;
                glitch->scanline_shifts[i] = (rand() % (GLITCH_SHIFT_MAX * 2)) - GLITCH_SHIFT_MAX;
            }
        }
        
        /* Randomly change color shift */
        if ((float)rand()/RAND_MAX < GLITCH_COLOR_PROB) {
            glitch->color_shift_r = rand() % 100;
            glitch->color_shift_g = rand() % 100;
            glitch->color_shift_b = rand() % 100;
        }
    }
    
    /* Check if glitch effect is complete */
    if (glitch->frame_count >= GLITCH_FRAMES) {
        glitch->active = false;
        glitch->frame_count = 0;
        glitch->expansion_radius = 0.0f;
        
        /* Different actions based on glitch type */
        if (glitch->type == GLITCH_RESTART) {
            /* Actually perform the level restart */
            load_level(game, get_current_level(game));
        }
        else if (glitch->pending_pause_toggle) {
            /* Apply the target pause state now that the effect is complete */
            glitch->paused = glitch->target_pause_state;
            glitch->pending_pause_toggle = false;
            SDL_Log("Game %s after glitch effect completed",
                    glitch->paused ? "paused" : "unpaused");
        }
    }
}

/*
 * Check if a glitch effect is currently active
 */
bool is_glitch_active(const GameState* game) {
    return game->glitch->active;
}
