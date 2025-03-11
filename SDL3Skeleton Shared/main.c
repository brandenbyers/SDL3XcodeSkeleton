/*
 * Bit-Twiddled Classic Game Engine (FIXED)
 *
 * This implementation embraces classic game optimization techniques used in 8-bit
 * and 16-bit era games, while ensuring:
 * 1. Proper display of the full 64×32 grid
 * 2. Smooth, consistent animation between tiles
 * 3. Correct screen wrapping calculations
 *
 * Key techniques implemented:
 * 1. Bit-packed grid (2 bits per cell = 4 cells per byte)
 * 2. Power-of-two dimensions (64×32) for shift operations instead of multiplication
 * 3. Input state packed into individual bits
 * 4. Movement state using bit flags instead of separate booleans
 * 5. Fixed time step with frame counting for deterministic animation
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include "main.h"

/*
 * Grid Configuration With Power-of-Two Dimensions
 */
#define GRID_WIDTH          64      /* Must be power of 2 for bit shifts */
#define GRID_HEIGHT         32      /* Must be power of 2 for bit shifts */
#define GRID_WIDTH_SHIFT    6       /* log2(64) = 6, used for shifting */
#define GRID_HEIGHT_MASK    0x1F    /* 2^5 - 1 = 31, masks lower 5 bits */
#define GRID_WIDTH_MASK     0x3F    /* 2^6 - 1 = 63, masks lower 6 bits */
#define GRID_SIZE           (GRID_WIDTH * GRID_HEIGHT)
#define CELLS_PER_BYTE      4       /* 4 cells (2 bits each) per byte */
#define GRID_BYTES          (GRID_SIZE / CELLS_PER_BYTE)  /* 512 bytes total */

/* Display configuration (showing full grid) */
#define PIXEL_SCALE         12      /* Screen pixels per grid cell */
#define WINDOW_WIDTH        (GRID_WIDTH * PIXEL_SCALE)    /* Show full grid width */
#define WINDOW_HEIGHT       (GRID_HEIGHT * PIXEL_SCALE)   /* Show full grid height */

/* Game timing configuration */
#define LOGIC_TICK_RATE     60      /* Game logic updates per second */
#define FRAMES_PER_TILE     3      /* Frames to move one tile */
#define LOGIC_TICK_MS       (1000 / LOGIC_TICK_RATE)
#define CORNER_BUFFER_FRAMES 2      /* Frames before tile end to accept corner input */

/* Cell types stored in 2 bits per cell */
typedef enum {
    CELL_EMPTY = 0,  /* 00 binary */
    CELL_WALL  = 1,  /* 01 binary */
    CELL_ITEM  = 2,  /* 10 binary */
    CELL_MAX   = 3   /* 11 binary - Unused, but needed for 2-bit mask */
} CellType;

/* Movement direction encoding */
typedef enum {
    DIR_NONE  = 0xFF, /* No direction - using 0xFF instead of -1 for unsigned math */
    DIR_RIGHT = 0,    /* 00 binary */
    DIR_UP    = 1,    /* 01 binary */
    DIR_LEFT  = 2,    /* 10 binary */
    DIR_DOWN  = 3,    /* 11 binary */
    DIR_COUNT = 4
} Direction;

/* Bit flags for input state */
#define KEY_RIGHT       0x01
#define KEY_UP          0x02
#define KEY_LEFT        0x04
#define KEY_DOWN        0x08
#define HAS_BUFFERED    0x10
#define RESTART_REQ     0x20

/* Bit flags for movement state */
#define MOVE_IS_MOVING     0x01
#define MOVE_JUST_STARTED  0x02
#define MOVE_DIR_MASK      0x1C    /* Bits 2-4 for direction (0-4) */
#define MOVE_DIR_SHIFT     2       /* Shift amount to get direction */

/* Bit-packed input state (3 bytes) */
typedef struct {
    uint8_t key_states;        /* Bit 0-3: direction keys, 4: has_buffered, 5: restart */
    uint8_t current_dir;       /* Current direction (0-3, 255 for none) */
    uint8_t buffered_dir;      /* Buffered direction (0-3, 255 for none) */
} InputState;

/* Bit-packed movement state (6 bytes) */
typedef struct {
    uint8_t pos_x;            /* Current X (0-63) */
    uint8_t pos_y;            /* Current Y (0-31) */
    uint8_t target_x;         /* Target X (0-63) */
    uint8_t target_y;         /* Target Y (0-31) */
    uint8_t state_flags;      /* Bit 0: is_moving, 1: just_started, 2-4: direction */
    uint8_t move_frame;       /* Current frame (0-11) */
} MovementState;

/* Game State */
typedef struct {
    uint8_t grid[GRID_BYTES];      /* Bit-packed grid: 2 bits per cell, 4 cells per byte */
    MovementState player;          /* Player movement state */
    InputState input;              /* Input state */
    uint32_t frame_count;          /* Total frames executed (32-bit counter) */
    uint16_t accumulated_time;     /* Accumulated time since last tick (ms) */
    uint64_t last_tick_time;       /* Time of last logic tick */
} GameState;

/* Application State */
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Gamepad* gamepad;
    SDL_JoystickID gamepad_id;
    GameState game;
    uint8_t app_flags;             /* Bit 0: fullscreen, 1-2: time scale */
} AppState;

/* Pre-computed lookup tables for movement */
static const int8_t DIR_OFFSET_X[4] = {1, 0, -1, 0};   /* RIGHT, UP, LEFT, DOWN */
static const int8_t DIR_OFFSET_Y[4] = {0, -1, 0, 1};   /* RIGHT, UP, LEFT, DOWN */

/* Function declarations */
static void init_game(GameState* game);
static void update_game_logic_fixed_step(GameState* game);
static void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad);
static void process_key_event(InputState* input, SDL_Scancode key, bool pressed);
static void render_game(AppState* app);

/*
 * Bit-Manipulating Grid Functions
 */

/* Get cell with bit operations */
static CellType get_cell_bit(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Find byte and position within byte */
    int byte_idx = idx >> 2;               /* Divide by 4 (cells per byte) */
    int bit_pos = (idx & 3) << 1;          /* Position within byte (multiply by 2 bits per cell) */
    
    /* Extract and return the 2-bit cell value */
    return (CellType)((game->grid[byte_idx] >> bit_pos) & 0x3);
}

/* Set cell with bit operations */
static void set_cell_bit(GameState* game, int x, int y, CellType type) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Find byte and position within byte */
    int byte_idx = idx >> 2;               /* Divide by 4 (cells per byte) */
    int bit_pos = (idx & 3) << 1;          /* Position within byte (2 bits per cell) */
    
    /* Clear the 2 bits for this cell */
    uint8_t mask = ~(0x3 << bit_pos);      /* Create mask to clear the bits */
    
    /* Set the new cell value */
    game->grid[byte_idx] = (game->grid[byte_idx] & mask) | ((type & 0x3) << bit_pos);
}

/*
 * Input State Functions with Bit Operations
 */

/* Check if a direction key is pressed using bit operations */
static inline bool is_key_pressed(const InputState* input, Direction dir) {
    return (input->key_states & (1 << dir)) != 0;
}

/* Set key state with bit operations */
static inline void set_key_state(InputState* input, Direction dir, bool pressed) {
    input->key_states = (input->key_states & ~(1 << dir)) | (pressed << dir);
}

/* Check if buffered direction is set */
static inline bool has_buffered_dir(const InputState* input) {
    return (input->key_states & HAS_BUFFERED) != 0;
}

/* Set buffered direction flag */
static inline void set_has_buffered(InputState* input, bool has_buffered) {
    input->key_states = (input->key_states & ~HAS_BUFFERED) | (has_buffered ? HAS_BUFFERED : 0);
}

/* Check if restart is requested */
static inline bool is_restart_requested(const InputState* input) {
    return (input->key_states & RESTART_REQ) != 0;
}

/* Set restart requested flag */
static inline void set_restart_requested(InputState* input, bool requested) {
    input->key_states = (input->key_states & ~RESTART_REQ) | (requested ? RESTART_REQ : 0);
}

/*
 * Movement State Functions with Bit Operations
 */

/* Check if entity is moving */
static inline bool is_moving(const MovementState* movement) {
    return (movement->state_flags & MOVE_IS_MOVING) != 0;
}

/* Set moving state */
static inline void set_moving(MovementState* movement, bool moving) {
    movement->state_flags = (movement->state_flags & ~MOVE_IS_MOVING) | (moving ? MOVE_IS_MOVING : 0);
}

/* Check if movement just started */
static inline bool just_started_moving(const MovementState* movement) {
    return (movement->state_flags & MOVE_JUST_STARTED) != 0;
}

/* Set just started moving flag */
static inline void set_just_started(MovementState* movement, bool just_started) {
    movement->state_flags = (movement->state_flags & ~MOVE_JUST_STARTED) |
    (just_started ? MOVE_JUST_STARTED : 0);
}

/* Get current direction */
static inline Direction get_direction(const MovementState* movement) {
    uint8_t dir_bits = (movement->state_flags & MOVE_DIR_MASK) >> MOVE_DIR_SHIFT;
    return dir_bits ? (Direction)(dir_bits - 1) : DIR_NONE;
}

/* Set current direction */
static inline void set_direction(MovementState* movement, Direction dir) {
    movement->state_flags = (movement->state_flags & ~MOVE_DIR_MASK) |
    ((dir == DIR_NONE ? 0 : dir + 1) << MOVE_DIR_SHIFT);
}

/*
 * Movement Functions
 */

/* Check if a move is valid */
static bool is_valid_move(const GameState* game, int x, int y, Direction dir) {
    /* Quick check if direction is valid */
    if (dir >= DIR_COUNT) return false;
    
    /* Calculate target position - bit magic to handle wrapping */
    int target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    int target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
    
    /* Check if target cell is empty or an item */
    CellType target_cell = get_cell_bit(game, target_x, target_y);
    
    /* Return true if not a wall */
    return target_cell != CELL_WALL;
}

/* Get target position using lookup table */
static void get_target_position(int x, int y, Direction dir, int* target_x, int* target_y) {
    *target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    *target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
}

/* Check if directions are opposite using bit operations */
static bool are_directions_opposite(Direction dir1, Direction dir2) {
    /* If either direction is NONE, they're not opposite */
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) return false;
    
    /* Directions are opposite if they differ by 2 (when 2-bit values) */
    return ((dir1 ^ dir2) == 2);
}

/* Start Movement Using Bit Operations */
static bool start_movement(GameState* game, Direction dir) {
    MovementState* movement = &game->player;
    
    /* Check if direction is valid */
    if (dir == DIR_NONE) return false;
    
    /* Check if the move is valid */
    int target_x, target_y;
    get_target_position(movement->pos_x, movement->pos_y, dir, &target_x, &target_y);
    
    CellType target_cell = get_cell_bit(game, target_x, target_y);
    bool can_move = (target_cell != CELL_WALL);
    
    /* If we can't move, return false */
    if (!can_move) return false;
    
    /* Update movement state */
    movement->target_x = target_x;
    movement->target_y = target_y;
    set_direction(movement, dir);
    set_moving(movement, true);
    set_just_started(movement, true);
    movement->move_frame = 0;
    
    return true;
}

/* Calculate visual position based on movement state - FIXED WRAPPING CALCULATION */
static void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y) {
    if (!is_moving(movement) || movement->move_frame >= FRAMES_PER_TILE) {
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
    
    /* FIXED: Use proper distance check for wrapping detection */
    
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
static void complete_movement(GameState* game) {
    MovementState* movement = &game->player;
    InputState* input = &game->input;
    
    /* Update position to target */
    movement->pos_x = movement->target_x;
    movement->pos_y = movement->target_y;
    movement->move_frame = 0;
    
    /* Check if target has an item */
    if (get_cell_bit(game, movement->pos_x, movement->pos_y) == CELL_ITEM) {
        /* Collect the item */
        set_cell_bit(game, movement->pos_x, movement->pos_y, CELL_EMPTY);
    }
    
    /* Check if we should continue moving, using bit operations where possible */
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
        set_moving(movement, false);
        set_direction(movement, DIR_NONE);
    }
}

/*
 * Game Logic Functions
 */

/* Initialize the game state with bit operations */
static void init_game(GameState* game) {
    /* Zero out the grid with 64-bit operations for speed */
    uint64_t* grid_64 = (uint64_t*)game->grid;
    for (size_t i = 0; i < GRID_BYTES / 8; i++) {
        grid_64[i] = 0ULL;
    }
    
    /* Add walls for a simple maze using bit operations */
    
    /* Outer walls */
    for (int i = 0; i < GRID_WIDTH; i++) {
        set_cell_bit(game, i, 0, CELL_WALL);              /* Top wall */
        set_cell_bit(game, i, GRID_HEIGHT - 1, CELL_WALL); /* Bottom wall */
    }
    
//    for (int i = 0; i < GRID_HEIGHT; i++) {
//        set_cell_bit(game, 0, i, CELL_WALL);              /* Left wall */
//        set_cell_bit(game, GRID_WIDTH - 1, i, CELL_WALL); /* Right wall */
//    }
    
    /* Inner walls for testing */
    for (int x = 10; x < 20; x++) {
        set_cell_bit(game, x, 10, CELL_WALL);
        set_cell_bit(game, x + 20, 15, CELL_WALL);
    }
    
    /* Add corner testing area */
    for (int y = 20; y < 25; y++) {
        set_cell_bit(game, 10, y, CELL_WALL);
        set_cell_bit(game, 20, y, CELL_WALL);
    }
    for (int x = 11; x < 20; x++) {
        set_cell_bit(game, x, 20, CELL_WALL);
    }
    
    /* Add some items for collection */
    for (int i = 0; i < 40; i++) {
        int x = rand() & GRID_WIDTH_MASK;  /* Random x (0-63) */
        int y = rand() & GRID_HEIGHT_MASK; /* Random y (0-31) */
        if (get_cell_bit(game, x, y) == CELL_EMPTY) {
            set_cell_bit(game, x, y, CELL_ITEM);
        }
    }
    
    /* Initialize player in center of grid */
    game->player.pos_x = GRID_WIDTH >> 1;   /* Center X (32) */
    game->player.pos_y = GRID_HEIGHT >> 1;  /* Center Y (16) */
    game->player.target_x = game->player.pos_x;
    game->player.target_y = game->player.pos_y;
    game->player.state_flags = 0;  /* Not moving, no direction */
    game->player.move_frame = 0;
    
    /* Reset input state using direct assignment instead of bitwise ops for initialization */
    game->input.key_states = 0;
    game->input.current_dir = DIR_NONE;
    game->input.buffered_dir = DIR_NONE;
    
    /* Reset timing */
    game->last_tick_time = SDL_GetTicks();
    game->accumulated_time = 0;
    game->frame_count = 0;
}

/* Process key press/release using bit operations */
static void process_key_event(InputState* input, SDL_Scancode key, bool pressed) {
    Direction dir = DIR_NONE;
    
    /* Map keyboard to direction - classic switch-case avoidance technique */
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

/* Efficient gamepad state polling with bit operations */
static void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad) {
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

/* Update game logic with fixed time step and bit operations */
static void update_game_logic_fixed_step(GameState* game) {
    MovementState* movement = &game->player;
    InputState* input = &game->input;
    
    /* Check for restart request */
    if (is_restart_requested(input)) {
        init_game(game);
        set_restart_requested(input, false);
        return;
    }
    
    /* Increment frame counter */
    game->frame_count++;
    
    /* If not moving, check for direction input to start movement */
    if (!is_moving(movement)) {
        /* Try to move in current input direction */
        if (input->current_dir != DIR_NONE) {
            start_movement(game, input->current_dir);
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
            complete_movement(game);
        }
    }
    
    /* Clear the just started moving flag after first frame */
    if (just_started_moving(movement)) {
        set_just_started(movement, false);
    }
}

/*
 * Rendering Functions with Bit Operations
 */

/* Configure renderer for proper scaling */
static void configure_rendering(AppState* app) {
    SDL_SetRenderScale(app->renderer, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(app->renderer, WINDOW_WIDTH, WINDOW_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

/* Bit Flags for app state */
#define APP_FULLSCREEN    0x01
#define APP_TIME_SCALE    0x06    /* Bits 1-2 for time scale */
#define APP_TS_SHIFT      1       /* Shift amount for time scale */

/* Get time scale factor */
static float get_time_scale(const AppState* app) {
    static const float time_scales[] = {1.0f, 0.5f, 0.25f, 2.0f};
    return time_scales[(app->app_flags & APP_TIME_SCALE) >> APP_TS_SHIFT];
}

/* Set time scale */
static void set_time_scale(AppState* app, uint8_t scale_index) {
    app->app_flags = (app->app_flags & ~APP_TIME_SCALE) | ((scale_index & 0x3) << APP_TS_SHIFT);
}

/* Render the current game state with bit operations - FIXED FOR FULL GRID DISPLAY */
static void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    SDL_FRect rect;
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    
    /* Render the grid - Full grid, no camera */
    rect.w = rect.h = PIXEL_SCALE;
    
    /* Render all cells in the grid */
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            /* Calculate screen position */
            rect.x = (float)(x * PIXEL_SCALE);
            rect.y = (float)(y * PIXEL_SCALE);
            
            /* Get cell type with bit operations */
            CellType cell = get_cell_bit(game, x, y);
            
            /* Render based on cell type */
            switch (cell) {
                case CELL_WALL:
                    SDL_SetRenderDrawColor(renderer, 64, 64, 192, SDL_ALPHA_OPAQUE);
                    SDL_RenderFillRect(renderer, &rect);
                    break;
                case CELL_ITEM:
                    /* Draw items as smaller squares */
                    rect.x += PIXEL_SCALE * 0.25f;
                    rect.y += PIXEL_SCALE * 0.25f;
                    rect.w = rect.h = PIXEL_SCALE * 0.5f;
                    SDL_SetRenderDrawColor(renderer, 255, 255, 0, SDL_ALPHA_OPAQUE);
                    SDL_RenderFillRect(renderer, &rect);
                    rect.x -= PIXEL_SCALE * 0.25f;
                    rect.y -= PIXEL_SCALE * 0.25f;
                    rect.w = rect.h = PIXEL_SCALE;
                    break;
                default:
                    break;
            }
        }
    }
    
    /* Draw grid lines for visual reference */
    SDL_SetRenderDrawColor(renderer, 32, 32, 32, SDL_ALPHA_OPAQUE);
    for (int i = 0; i <= GRID_WIDTH; i++) {
        /* Vertical lines */
        SDL_RenderLine(renderer, i * PIXEL_SCALE, 0, i * PIXEL_SCALE, WINDOW_HEIGHT);
    }
    for (int i = 0; i <= GRID_HEIGHT; i++) {
        /* Horizontal lines */
        SDL_RenderLine(renderer, 0, i * PIXEL_SCALE, WINDOW_WIDTH, i * PIXEL_SCALE);
    }
    
    /* Calculate player's visual position */
    float visual_x, visual_y;
    get_visual_position(&game->player, &visual_x, &visual_y);
    
    /* Render the player */
    rect.x = visual_x * PIXEL_SCALE;
    rect.y = visual_y * PIXEL_SCALE;
    rect.w = rect.h = PIXEL_SCALE;
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(renderer, &rect);
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
}

/*
 * Control Functions
 */

/* Initialize gamepad */
static void initialize_gamepad(AppState* app) {
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

/* Toggle fullscreen mode */
static void toggle_fullscreen(AppState* app) {
    app->app_flags ^= APP_FULLSCREEN;  /* Toggle fullscreen bit */
    bool fullscreen = (app->app_flags & APP_FULLSCREEN) != 0;
    
    SDL_SetWindowFullscreen(app->window, fullscreen);
    if (!fullscreen) {
        SDL_SetWindowSize(app->window, WINDOW_WIDTH, WINDOW_HEIGHT);
    }
    
    configure_rendering(app);
}

/* Cycle time scale for debugging */
static void cycle_time_scale(AppState* app) {
    uint8_t current = (app->app_flags & APP_TIME_SCALE) >> APP_TS_SHIFT;
    uint8_t next = (current + 1) & 0x3;  /* Cycle through 0-3 */
    set_time_scale(app, next);
    
    static const char* scale_names[] = {"normal (1x)", "slow (0.5x)", "very slow (0.25x)", "fast (2x)"};
    SDL_Log("Time scale: %s", scale_names[next]);
}

/*
 * SDL App Callbacks
 */

/* Initialize the application */
SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    /* Initialize SDL */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        return SDL_APP_FAILURE;
    }
    
    /* Set VSync hint */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    
    /* Allocate application state */
    AppState* app = SDL_calloc(1, sizeof(AppState));
    if (!app) {
        return SDL_APP_FAILURE;
    }
    
    *appstate = app;
    app->app_flags = 0;  /* Not fullscreen, normal time scale */
    
    /* Create window and renderer */
    Uint32 window_flags = 0;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags = SDL_WINDOW_FULLSCREEN;
    app->app_flags |= APP_FULLSCREEN;
#endif
    
    if (!SDL_CreateWindowAndRenderer("Bit-Twiddled Game Engine", WINDOW_WIDTH, WINDOW_HEIGHT,
                                     window_flags, &app->window, &app->renderer)) {
        return SDL_APP_FAILURE;
    }
    
    /* Configure rendering */
    configure_rendering(app);
    
    /* Initialize gamepad */
    initialize_gamepad(app);
    
    /* Initialize game state */
    init_game(&app->game);
    
    /* Seed random number generator */
    srand((unsigned int)SDL_GetTicks());
    
    return SDL_APP_CONTINUE;
}

/* Main game loop iteration with fixed time step */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Calculate elapsed time */
    Uint64 current_time = SDL_GetTicks();
    int delta_time = (int)(current_time - game->last_tick_time);
    game->last_tick_time = current_time;
    
    /* Apply time scaling */
    delta_time = (int)(delta_time * get_time_scale(app));
    
    /* Check gamepad state every frame */
    if (app->gamepad) {
        process_gamepad_state(&game->input, app->gamepad);
    }
    
    /* Add to accumulator */
    game->accumulated_time += delta_time;
    
    /* Run fixed time step updates */
    while (game->accumulated_time >= LOGIC_TICK_MS) {
        update_game_logic_fixed_step(game);
        game->accumulated_time -= LOGIC_TICK_MS;
    }
    
    /* Render the game */
    render_game(app);
    
    return SDL_APP_CONTINUE;
}

/* Process SDL events with bit operations where possible */
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;
            
        case SDL_EVENT_KEY_DOWN:
            /* Handle keyboard input */
            if (event->key.scancode == SDL_SCANCODE_ESCAPE || event->key.scancode == SDL_SCANCODE_Q) {
                return SDL_APP_SUCCESS;
            } else if (event->key.scancode == SDL_SCANCODE_F) {
                toggle_fullscreen(app);
            } else if (event->key.scancode == SDL_SCANCODE_T) {
                cycle_time_scale(app);
            } else {
                process_key_event(&game->input, event->key.scancode, true);
            }
            break;
            
        case SDL_EVENT_KEY_UP:
            /* Handle key release */
            process_key_event(&game->input, event->key.scancode, false);
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* For individual button press events, maintain compatibility */
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                return SDL_APP_SUCCESS;
            }
            break;
            
        case SDL_EVENT_WINDOW_RESIZED:
            configure_rendering(app);
            break;
            
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            /* Re-initialize gamepad if connection changes */
            if (app->gamepad) {
                SDL_CloseGamepad(app->gamepad);
                app->gamepad = NULL;
            }
            initialize_gamepad(app);
            break;
            
        default:
            break;
    }
    
    return SDL_APP_CONTINUE;
}

/* Clean up on exit */
void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    if (appstate) {
        AppState* app = (AppState*)appstate;
        
        if (app->gamepad) {
            SDL_CloseGamepad(app->gamepad);
        }
        
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_free(app);
    }
    
    SDL_Quit();
}

/*
 * tvOS Specific Main Function
 */
#if defined(__APPLE__) && TARGET_OS_TV
int main(int argc, char* argv[]) {
    /* Setup custom signal handlers for tvOS */
    signal(SIGTERM, SIG_IGN);  /* Ignore SIGTERM signal */
    
    /* Use SDL_RunApp with explicit cleanup handling */
    int result = SDL_RunApp(argc, argv, SDL_main, NULL);
    
    /* Additional cleanup for tvOS */
    SDL_Quit();
    
    return result;
}
#endif
