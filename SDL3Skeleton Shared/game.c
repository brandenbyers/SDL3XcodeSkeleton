/*
 * game.c - Game logic implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the core game mechanics, including:
 * - Grid management
 * - Viewport handling
 * - Movement logic
 * - Game state initialization and updates
 *
 * Optimized for minimal CPU usage using event-driven design.
 */

#include "main.h"

/* Pre-computed lookup tables for movement */
const int8_t DIR_OFFSET_X[4] = {1, 0, -1, 0};   /* RIGHT, UP, LEFT, DOWN */
const int8_t DIR_OFFSET_Y[4] = {0, -1, 0, 1};   /* RIGHT, UP, LEFT, DOWN */

/*
 * Viewport/Grid Conversion Functions
 */

/* Check if a grid coordinate is within the visible viewport */
bool is_in_viewport(const GameState* game, int x, int y) {
    /* Convert to viewport-relative coordinates with wrapping */
    int rel_x = (x - game->viewport.offset_x) & GRID_WIDTH_MASK;
    int rel_y = (y - game->viewport.offset_y) & GRID_HEIGHT_MASK;
    
    /* Check if within viewport bounds */
    return (rel_x < VIEWPORT_WIDTH && rel_y < VIEWPORT_HEIGHT);
}

/* Convert grid coordinates to viewport coordinates */
void grid_to_viewport(const GameState* game, int grid_x, int grid_y, int* viewport_x, int* viewport_y) {
    /* Apply viewport offset with wrapping */
    *viewport_x = (grid_x - game->viewport.offset_x) & GRID_WIDTH_MASK;
    *viewport_y = (grid_y - game->viewport.offset_y) & GRID_HEIGHT_MASK;
}

/* Convert viewport coordinates to grid coordinates */
void viewport_to_grid(const GameState* game, int viewport_x, int viewport_y, int* grid_x, int* grid_y) {
    /* Apply viewport offset with wrapping */
    *grid_x = (viewport_x + game->viewport.offset_x) & GRID_WIDTH_MASK;
    *grid_y = (viewport_y + game->viewport.offset_y) & GRID_HEIGHT_MASK;
}

/* Update the viewport cache for better CPU cache locality */
void update_viewport_cache(GameState* game) {
    /* Fill the viewport cache with current grid data */
    for (int vy = 0; vy < VIEWPORT_HEIGHT; vy++) {
        for (int vx = 0; vx < VIEWPORT_WIDTH; vx++) {
            /* Get grid coordinates */
            int grid_x, grid_y;
            viewport_to_grid(game, vx, vy, &grid_x, &grid_y);
            
            /* Get cell from main grid */
            CellType cell = get_cell(game, grid_x, grid_y);
            
            /* Store in cache using linear indexing for contiguous memory access */
            int cache_idx = vy * VIEWPORT_WIDTH + vx;
            game->viewport.cache[cache_idx] = cell;
        }
    }
    
    /* Mark cache as valid */
    game->grid_state.cache_valid = true;
}

/* Get cell from viewport cache for efficient rendering */
CellType get_cell_from_cache(const GameState* game, int viewport_x, int viewport_y) {
    /* Bounds check */
    if (viewport_x < 0 || viewport_x >= VIEWPORT_WIDTH ||
        viewport_y < 0 || viewport_y >= VIEWPORT_HEIGHT) {
        return CELL_EMPTY;
    }
    
    /* Direct access to the cache with linear indexing */
    int cache_idx = viewport_y * VIEWPORT_WIDTH + viewport_x;
    return (CellType)game->viewport.cache[cache_idx];
}

/*
 * Optimized Grid Functions
 */

/* Get cell with direct array access for better cache performance */
CellType get_cell(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts (efficient for power-of-two sizes) */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Direct array access - much more cache friendly */
    return (CellType)game->grid[idx];
}

/* Set cell with direct array access */
void set_cell(GameState* game, int x, int y, CellType type) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Mark grid as changed if the cell value is different */
    if (game->grid[idx] != type) {
        game->grid[idx] = type;
        game->grid_state.cells_changed = true;
        
        /* Invalidate cache since grid changed */
        game->grid_state.cache_valid = false;
        
        /* If cell is in viewport, also update cache directly for write-through caching */
        if (is_in_viewport(game, x, y)) {
            int viewport_x, viewport_y;
            grid_to_viewport(game, x, y, &viewport_x, &viewport_y);
            int cache_idx = viewport_y * VIEWPORT_WIDTH + viewport_x;
            game->viewport.cache[cache_idx] = type;
        }
    }
}

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

/* Check if a move is valid */
bool is_valid_move(const GameState* game, int x, int y, Direction dir) {
    /* Quick check if direction is valid */
    if (dir >= DIR_COUNT) return false;
    
    /* Calculate target position - bit magic to handle wrapping */
    int target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    int target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
    
    /* Check if target cell is empty or an item */
    CellType target_cell = get_cell(game, target_x, target_y);
    
    /* Return true if not a wall */
    return target_cell != CELL_WALL;
}

/* Get target position using lookup table */
void get_target_position(int x, int y, Direction dir, int* target_x, int* target_y) {
    *target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    *target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
}

/* Check if directions are opposite */
bool are_directions_opposite(Direction dir1, Direction dir2) {
    /* If either direction is NONE, they're not opposite */
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) return false;
    
    /* Directions are opposite if they differ by 2 (when 2-bit values) */
    return ((dir1 ^ dir2) == 2);
}

/* Start Movement */
bool start_movement(GameState* game, Direction dir) {
    MovementState* movement = &game->player;
    
    /* Check if direction is valid */
    if (dir == DIR_NONE) return false;
    
    /* Check if the move is valid */
    int target_x, target_y;
    get_target_position(movement->pos_x, movement->pos_y, dir, &target_x, &target_y);
    
    CellType target_cell = get_cell(game, target_x, target_y);
    bool can_move = (target_cell != CELL_WALL);
    
    /* If we can't move, return false */
    if (!can_move) return false;
    
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
    
    /* Calculate interpolation factor (0.0 to 1.0) */
    float t = (float)movement->move_frame / FRAMES_PER_TILE;
    
    /* Start position */
    float start_x = (float)movement->pos_x;
    float start_y = (float)movement->pos_y;
    
    /* Target position */
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
    *visual_x = start_x + (target_x - start_x) * t;
    *visual_y = start_y + (target_y - start_y) * t;
    
    /* Normalize coordinates to grid range */
    *visual_x = fmodf(*visual_x, GRID_WIDTH);
    *visual_y = fmodf(*visual_y, GRID_HEIGHT);
    
    /* Handle negative coordinates from wrapping */
    if (*visual_x < 0) *visual_x += GRID_WIDTH;
    if (*visual_y < 0) *visual_y += GRID_HEIGHT;
}

/* Handle movement completion and start next movement if needed */
void complete_movement(GameState* game) {
    MovementState* movement = &game->player;
    InputState* input = &game->input;
    
    /* Update position to target */
    movement->pos_x = movement->target_x;
    movement->pos_y = movement->target_y;
    movement->move_frame = 0;
    
    /* Check if target has an item */
    if (get_cell(game, movement->pos_x, movement->pos_y) == CELL_ITEM) {
        /* Collect the item */
        set_cell(game, movement->pos_x, movement->pos_y, CELL_EMPTY);
    }
    
    /* Check if we should continue moving */
    Direction next_dir = DIR_NONE;
    
    /* Priority 1: Use buffered direction if valid */
    if (has_buffered_dir(input) &&
        is_valid_move(game, movement->pos_x, movement->pos_y, input->buffered_dir)) {
        next_dir = input->buffered_dir;
        set_has_buffered(input, false);
    }
    /* Priority 2: Continue in same direction if key still held */
    else {
        Direction current_dir = get_direction(movement);
        if (is_key_pressed(input, current_dir) &&
            is_valid_move(game, movement->pos_x, movement->pos_y, current_dir)) {
            next_dir = current_dir;
        }
        /* Priority 3: Check for any held direction key */
        else {
            /* Use standard bit check instead of bit scan for compatibility */
            uint8_t keys = input->key_states & 0x0F; /* Get just direction bits */
            for (int i = 0; i < DIR_COUNT; i++) {
                if ((keys & (1 << i)) && is_valid_move(game, movement->pos_x, movement->pos_y, i)) {
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

/* Initialize the game state */
void init_game(GameState* game) {
    /* Clear the grid */
    memset(game->grid, CELL_EMPTY, GRID_SIZE);
    
    /* Initialize viewport position (centered vertically) */
    game->viewport.offset_x = 0;
    game->viewport.offset_y = VIEWPORT_OFFSET_Y;
    
    /* Initialize cache tracking */
    game->grid_state.cache_valid = false;
    
    /* Initialize entity system */
    init_entity_system(game);
    
    /* Add walls for a simple maze - now using the full 64x64 grid */
    
    /* Outer walls around the viewport */
    for (int i = 0; i < VIEWPORT_WIDTH; i++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Top wall */
        viewport_to_grid(game, i, 0, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        
        /* Bottom wall */
        viewport_to_grid(game, i, VIEWPORT_HEIGHT - 1, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
    }
    
    /* Side walls of the viewport */
    for (int j = 0; j < VIEWPORT_HEIGHT; j++) {
        /* Get actual grid coordinates for viewport positions */
        int grid_x, grid_y;
        
        /* Left wall */
        viewport_to_grid(game, 0, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        
        /* Right wall */
        viewport_to_grid(game, VIEWPORT_WIDTH - 1, j, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
    }
    
    /* Add pivot points near walls for entity movement */
    int pivot_x, pivot_y;
    
    /* Left side pivot */
    viewport_to_grid(game, 4, VIEWPORT_HEIGHT/2, &pivot_x, &pivot_y);
    set_pivot_point(game, pivot_x, pivot_y, PIVOT_REVERSE);
    
    /* Right side pivot */
    viewport_to_grid(game, VIEWPORT_WIDTH - 5, VIEWPORT_HEIGHT/2, &pivot_x, &pivot_y);
    set_pivot_point(game, pivot_x, pivot_y, PIVOT_REVERSE);
    
    /* Add a patrol entity */
    add_entity(game, pivot_x, pivot_y, DIR_LEFT, ENTITY_PATROL);
    
    /* Inner walls for testing - convert from viewport to grid coordinates */
    for (int x = 10; x < 20; x++) {
        int grid_x, grid_y;
        
        /* First horizontal wall */
        viewport_to_grid(game, x, 10, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        
        /* Second horizontal wall */
        viewport_to_grid(game, x + 20, 15, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
    }
    
    /* Add corner testing area */
    for (int y = 20; y < 25; y++) {
        int grid_x, grid_y;
        
        /* Left vertical wall */
        viewport_to_grid(game, 10, y, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
        
        /* Right vertical wall */
        viewport_to_grid(game, 20, y, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
    }
    
    /* Horizontal wall for corner area */
    for (int x = 11; x < 20; x++) {
        int grid_x, grid_y;
        viewport_to_grid(game, x, 20, &grid_x, &grid_y);
        set_cell(game, grid_x, grid_y, CELL_WALL);
    }
    
    /* Add some items for collection - scatter throughout the viewport area */
    for (int i = 0; i < 40; i++) {
        /* Generate random viewport coordinates */
        int viewport_x = rand() % VIEWPORT_WIDTH;
        int viewport_y = rand() % VIEWPORT_HEIGHT;
        
        /* Convert to grid coordinates */
        int grid_x, grid_y;
        viewport_to_grid(game, viewport_x, viewport_y, &grid_x, &grid_y);
        
        if (get_cell(game, grid_x, grid_y) == CELL_EMPTY) {
            set_cell(game, grid_x, grid_y, CELL_ITEM);
        }
    }
    
    /* Initialize player in center of viewport */
    int viewport_center_x = VIEWPORT_WIDTH >> 1;
    int viewport_center_y = VIEWPORT_HEIGHT >> 1;
    
    /* Convert to grid coordinates */
    int grid_center_x, grid_center_y;
    viewport_to_grid(game, viewport_center_x, viewport_center_y, &grid_center_x, &grid_center_y);
    
    /* Set player position to grid coordinates */
    game->player.pos_x = grid_center_x;
    game->player.pos_y = grid_center_y;
    game->player.target_x = game->player.pos_x;
    game->player.target_y = game->player.pos_y;
    game->player.direction = DIR_NONE;
    game->player.is_moving = false;
    game->player.just_started = false;
    game->player.move_frame = 0;
    
    /* Reset input state */
    game->input.key_states = 0;
    game->input.current_dir = DIR_NONE;
    game->input.buffered_dir = DIR_NONE;
    
    /* Reset grid state */
    game->grid_state.cells_changed = true;
    game->grid_state.last_frame_updated = 0;
    
    /* Reset timing */
    game->last_tick_time = SDL_GetTicks();
    game->accumulated_time = 0;
    game->frame_count = 0;
}

/* Update game logic with fixed time step - optimized for minimal processing */
void update_game_logic_fixed_step(GameState* game) {
    MovementState* movement = &game->player;
    InputState* input = &game->input;
    
    /* Check for restart request */
    if (is_restart_requested(input)) {
        init_game(game);
        set_restart_requested(input, false);
        return;
    }
    
    /* Only increment frame counter when something is happening */
    game->frame_count++;
    
    /* Update entity system */
    update_entity_system(game, game->last_tick_time);
    
    /* Early exit if no input and not moving and no entity update needed */
    if (input->current_dir == DIR_NONE && !movement->is_moving && !game->entities.needs_update) {
        return; /* No update needed */
    }
    
    /* If not moving, check for direction input to start movement */
    if (!movement->is_moving) {
        /* Try to move in current input direction */
        if (input->current_dir != DIR_NONE) {
            if (start_movement(game, input->current_dir)) {
                /* Movement started, mark for rendering */
                return;
            }
        }
    }
    /* If currently moving, handle movement progression */
    else {
        /* Check for buffered direction change */
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
            /* Check for item collection before completing move */
            bool collected_item = (get_cell(game, movement->target_x, movement->target_y) == CELL_ITEM);
            
            /* Complete the movement */
            complete_movement(game);
            
            /* If an item was collected, mark the grid as changed */
            if (collected_item) {
                game->grid_state.cells_changed = true;
            }
        }
    }
    
    /* Clear the just started moving flag after first frame */
    if (movement->just_started) {
        movement->just_started = false;
    }
}
