/*
 * game.c - Game logic implementation for the Bit-Twiddled Game Engine
 *
 * This file contains the core game mechanics, including:
 * - Grid management
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
 * Optimized Grid Functions
 */

/* Get cell with direct array access for better cache performance */
CellType get_cell(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts (still efficient for power-of-two sizes) */
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
    
    /* Add walls for a simple maze */
    
    /* Outer walls */
    for (int i = 0; i < GRID_WIDTH; i++) {
        set_cell(game, i, 0, CELL_WALL);              /* Top wall */
        set_cell(game, i, GRID_HEIGHT - 1, CELL_WALL); /* Bottom wall */
    }
    
    /* Inner walls for testing */
    for (int x = 10; x < 20; x++) {
        set_cell(game, x, 10, CELL_WALL);
        set_cell(game, x + 20, 15, CELL_WALL);
    }
    
    /* Add corner testing area */
    for (int y = 20; y < 25; y++) {
        set_cell(game, 10, y, CELL_WALL);
        set_cell(game, 20, y, CELL_WALL);
    }
    for (int x = 11; x < 20; x++) {
        set_cell(game, x, 20, CELL_WALL);
    }
    
    /* Add some items for collection */
    for (int i = 0; i < 40; i++) {
        int x = rand() & GRID_WIDTH_MASK;  /* Random x (0-63) */
        int y = rand() & GRID_HEIGHT_MASK; /* Random y (0-31) */
        if (get_cell(game, x, y) == CELL_EMPTY) {
            set_cell(game, x, y, CELL_ITEM);
        }
    }
    
    /* Initialize player in center of grid */
    game->player.pos_x = GRID_WIDTH >> 1;   /* Center X (32) */
    game->player.pos_y = GRID_HEIGHT >> 1;  /* Center Y (16) */
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
    
    /* Early exit if no input and not moving */
    if (input->current_dir == DIR_NONE && !movement->is_moving) {
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
