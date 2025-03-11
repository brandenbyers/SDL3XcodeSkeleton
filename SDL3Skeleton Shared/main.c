/*
 * High-Performance Game Engine
 *
 * Simplified optimization focusing on:
 * 1. Direct access to grid data for cache efficiency
 * 2. Minimal draw calls for efficient rendering
 * 3. Smart sleep management to reduce CPU usage
 * 4. Power-aware operation on Apple platforms
 */

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>  // For memset
#include "main.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
// macOS specific headers for power management
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif
#endif

/*
 * Grid Configuration With Power-of-Two Dimensions
 */
#define GRID_WIDTH          64      /* Must be power of 2 for bit shifts */
#define GRID_HEIGHT         32      /* Must be power of 2 for bit shifts */
#define GRID_WIDTH_SHIFT    6       /* log2(64) = 6, used for shifting */
#define GRID_HEIGHT_MASK    0x1F    /* 2^5 - 1 = 31, masks lower 5 bits */
#define GRID_WIDTH_MASK     0x3F    /* 2^6 - 1 = 63, masks lower 6 bits */
#define GRID_SIZE           (GRID_WIDTH * GRID_HEIGHT)

/* Display configuration */
#define PIXEL_SCALE         12      /* Screen pixels per grid cell */
#define WINDOW_WIDTH        (GRID_WIDTH * PIXEL_SCALE)    /* Show full grid width */
#define WINDOW_HEIGHT       (GRID_HEIGHT * PIXEL_SCALE)   /* Show full grid height */

/* Game timing configuration */
#define LOGIC_TICK_RATE     60      /* Game logic updates per second */
#define FRAMES_PER_TILE     3       /* Frames to move one tile */
#define LOGIC_TICK_MS       (1000 / LOGIC_TICK_RATE)
#define CORNER_BUFFER_FRAMES 2      /* Frames before tile end to accept corner input */

/* Energy management */
#define BATTERY_SAVER_FPS   30      /* Lower frame rate when on battery */
#define BACKGROUND_FPS      10      /* Very low frame rate when in background */

/* Cell types - One byte per cell for cache efficiency */
typedef enum {
    CELL_EMPTY = 0,
    CELL_WALL  = 1,
    CELL_ITEM  = 2,
    CELL_MAX   = 3  /* Not used as a cell value, just for array bounds */
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

/* Input state structure */
typedef struct {
    uint8_t key_states;        /* Bit 0-3: direction keys, 4: has_buffered, 5: restart */
    uint8_t current_dir;       /* Current direction (0-3, 255 for none) */
    uint8_t buffered_dir;      /* Buffered direction (0-3, 255 for none) */
} InputState;

/* Movement state - 8 bytes */
typedef struct {
    uint8_t pos_x;            /* Current X (0-63) */
    uint8_t pos_y;            /* Current Y (0-31) */
    uint8_t target_x;         /* Target X (0-63) */
    uint8_t target_y;         /* Target Y (0-31) */
    uint8_t direction;        /* Current direction (0-3, 255 for none) */
    uint8_t is_moving;        /* Boolean: 1 if moving, 0 if not */
    uint8_t just_started;     /* Boolean: 1 if just started, 0 if not */
    uint8_t move_frame;       /* Current frame (0-11) */
} MovementState;

/* Grid change tracking */
typedef struct {
    bool cells_changed;       /* True if any cells changed */
    uint64_t last_frame_updated; /* Last frame the grid texture was updated */
} GridState;

/* Game State */
typedef struct {
    uint8_t grid[GRID_SIZE];      /* Grid: one byte per cell for better cache efficiency */
    MovementState player;          /* Player movement state */
    InputState input;              /* Input state */
    GridState grid_state;          /* Grid change tracking */
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
    
    /* Simple texture-based rendering */
    SDL_Texture* background_texture;  /* Static walls and grid lines */
    
    GameState game;
    uint8_t app_flags;             /* Bit 0: fullscreen, 1-2: time scale */
    
    /* Power management */
    bool is_on_battery;            /* True if running on battery */
    bool is_in_background;         /* True if app is in background */
    bool is_low_power_mode;        /* True if in low power mode */
    int target_fps;                /* Target FPS based on power state */
    
    /* Performance tracking */
    uint64_t last_fps_time;        /* Last time FPS was calculated */
    int fps_count;                 /* Frame count for FPS calculation */
    int current_fps;               /* Current FPS value */
} AppState;

/* Pre-computed lookup tables for movement */
static const int8_t DIR_OFFSET_X[4] = {1, 0, -1, 0};   /* RIGHT, UP, LEFT, DOWN */
static const int8_t DIR_OFFSET_Y[4] = {0, -1, 0, 1};   /* RIGHT, UP, LEFT, DOWN */

/* Power state colors */
static const SDL_Color CELL_COLORS[CELL_MAX] = {
    { 0,   0,   0,   255 },  /* CELL_EMPTY: black */
    { 64,  64,  192, 255 },  /* CELL_WALL: blue */
    { 255, 255, 0,   255 },  /* CELL_ITEM: yellow */
};
static const SDL_Color PLAYER_COLOR = { 0, 255, 0, 255 };  /* Player: green */
static const SDL_Color GRID_LINE_COLOR = { 32, 32, 32, 255 }; /* Grid lines: dark gray */

/* Function declarations */
static void init_game(GameState* game);
static void update_game_logic_fixed_step(GameState* game);
static void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad);
static void process_key_event(InputState* input, SDL_Scancode key, bool pressed);
static void render_game(AppState* app);
static void create_textures(AppState* app);
static void destroy_textures(AppState* app);
static void update_power_state(AppState* app);
static void create_background_texture(AppState* app);

/*
 * Optimized Grid Functions
 */

/* Get cell with direct array access for better cache performance */
static inline CellType get_cell(const GameState* game, int x, int y) {
    /* Mask coordinates to ensure they wrap properly */
    x &= GRID_WIDTH_MASK;
    y &= GRID_HEIGHT_MASK;
    
    /* Calculate flat index with bit shifts (still efficient for power-of-two sizes) */
    int idx = (y << GRID_WIDTH_SHIFT) | x;
    
    /* Direct array access - much more cache friendly */
    return (CellType)game->grid[idx];
}

/* Set cell with direct array access */
static inline void set_cell(GameState* game, int x, int y, CellType type) {
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
 * Input State Functions
 */

/* Check if a direction key is pressed */
static inline bool is_key_pressed(const InputState* input, Direction dir) {
    return (input->key_states & (1 << dir)) != 0;
}

/* Set key state */
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
 * Movement Functions
 */

/* Get current direction */
static inline Direction get_direction(const MovementState* movement) {
    return (Direction)movement->direction;
}

/* Set current direction */
static inline void set_direction(MovementState* movement, Direction dir) {
    movement->direction = dir;
}

/* Check if a move is valid */
static bool is_valid_move(const GameState* game, int x, int y, Direction dir) {
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
static void get_target_position(int x, int y, Direction dir, int* target_x, int* target_y) {
    *target_x = (x + DIR_OFFSET_X[dir]) & GRID_WIDTH_MASK;
    *target_y = (y + DIR_OFFSET_Y[dir]) & GRID_HEIGHT_MASK;
}

/* Check if directions are opposite */
static bool are_directions_opposite(Direction dir1, Direction dir2) {
    /* If either direction is NONE, they're not opposite */
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) return false;
    
    /* Directions are opposite if they differ by 2 (when 2-bit values) */
    return ((dir1 ^ dir2) == 2);
}

/* Start Movement */
static bool start_movement(GameState* game, Direction dir) {
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
static void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y) {
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
static void complete_movement(GameState* game) {
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

/*
 * Power Management Functions
 */

/* Check if running on battery power */
static bool is_running_on_battery(void) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
    CFTypeRef power_sources = IOPSCopyPowerSourcesInfo();
    if (!power_sources) return false;
    
    CFArrayRef power_source_list = IOPSCopyPowerSourcesList(power_sources);
    if (!power_source_list) {
        CFRelease(power_sources);
        return false;
    }
    
    bool on_battery = false;
    int power_source_count = (int)CFArrayGetCount(power_source_list);
    
    for (int i = 0; i < power_source_count; i++) {
        CFTypeRef power_source = CFArrayGetValueAtIndex(power_source_list, i);
        CFDictionaryRef description = IOPSGetPowerSourceDescription(power_sources, power_source);
        
        if (description) {
            CFStringRef power_source_state = CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
            if (power_source_state && CFEqual(power_source_state, CFSTR(kIOPSBatteryPowerValue))) {
                on_battery = true;
                break;
            }
        }
    }
    
    CFRelease(power_source_list);
    CFRelease(power_sources);
    return on_battery;
#else
    /* For iOS/tvOS, assume always on battery */
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    return true;
#else
    return false;
#endif
#endif
}

/* Check if in low power mode */
static bool is_in_low_power_mode(void) {
#if defined(__APPLE__) && TARGET_OS_IOS
    // For iOS, we could check NSProcessInfo.processInfo.lowPowerModeEnabled
    // But since we're in C, we'll simplify and just check if on battery
    return true;  // Assume low power mode on iOS
#else
    return false;
#endif
}

/* Update power state and adjust settings accordingly */
static void update_power_state(AppState* app) {
    bool prev_battery_state = app->is_on_battery;
    
    app->is_on_battery = is_running_on_battery();
    app->is_low_power_mode = is_in_low_power_mode();
    
    /* Determine target FPS based on power state */
    if (app->is_in_background) {
        app->target_fps = BACKGROUND_FPS;
    } else if (app->is_on_battery || app->is_low_power_mode) {
        app->target_fps = BATTERY_SAVER_FPS;
    } else {
        app->target_fps = LOGIC_TICK_RATE;
    }
    
    /* If power state changed, update textures to match brightness */
    if (prev_battery_state != app->is_on_battery) {
        /* Regenerate textures with appropriate colors */
        create_background_texture(app);
        SDL_Log("Power state changed: %s", app->is_on_battery ? "On Battery" : "On AC Power");
    }
}

/*
 * Texture Creation Functions for Optimized Rendering
 */

/* Create background texture containing walls and grid lines */
static void create_background_texture(AppState* app) {
    GameState* game = &app->game;
    
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
    }
    
    /* Create texture for walls and grid lines (static elements) */
    app->background_texture = SDL_CreateTexture(
                                                app->renderer,
                                                SDL_PIXELFORMAT_RGBA8888,
                                                SDL_TEXTUREACCESS_TARGET,
                                                WINDOW_WIDTH,
                                                WINDOW_HEIGHT
                                                );
    
    /* Set render target to background texture */
    SDL_SetRenderTarget(app->renderer, app->background_texture);
    
    /* Clear with black background */
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    
    /* Render walls */
    SDL_FRect rect = { 0, 0, PIXEL_SCALE, PIXEL_SCALE };
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            CellType cell = get_cell(game, x, y);
            if (cell == CELL_WALL) {
                rect.x = x * PIXEL_SCALE;
                rect.y = y * PIXEL_SCALE;
                
                SDL_SetRenderDrawColor(app->renderer,
                                       CELL_COLORS[CELL_WALL].r,
                                       CELL_COLORS[CELL_WALL].g,
                                       CELL_COLORS[CELL_WALL].b,
                                       CELL_COLORS[CELL_WALL].a);
                SDL_RenderFillRect(app->renderer, &rect);
            }
        }
    }
    
    /* Draw grid lines */
    SDL_SetRenderDrawColor(app->renderer,
                           GRID_LINE_COLOR.r,
                           GRID_LINE_COLOR.g,
                           GRID_LINE_COLOR.b,
                           GRID_LINE_COLOR.a);
    
    /* Vertical lines */
    for (int i = 0; i <= GRID_WIDTH; i++) {
        SDL_RenderLine(app->renderer, i * PIXEL_SCALE, 0, i * PIXEL_SCALE, WINDOW_HEIGHT);
    }
    
    /* Horizontal lines */
    for (int i = 0; i <= GRID_HEIGHT; i++) {
        SDL_RenderLine(app->renderer, 0, i * PIXEL_SCALE, WINDOW_WIDTH, i * PIXEL_SCALE);
    }
    
    /* Reset render target */
    SDL_SetRenderTarget(app->renderer, NULL);
    
    /* Mark grid as updated */
    game->grid_state.cells_changed = false;
    game->grid_state.last_frame_updated = game->frame_count;
}

/* Create all textures */
static void create_textures(AppState* app) {
    /* Create background texture (walls and grid lines) */
    create_background_texture(app);
}

/* Destroy all textures */
static void destroy_textures(AppState* app) {
    if (app->background_texture) {
        SDL_DestroyTexture(app->background_texture);
        app->background_texture = NULL;
    }
}

/*
 * Game Logic Functions
 */

/* Initialize the game state */
static void init_game(GameState* game) {
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

/* Process key press/release */
static void process_key_event(InputState* input, SDL_Scancode key, bool pressed) {
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

/* Efficient gamepad state polling */
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

/* Update game logic with fixed time step */
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
    if (!movement->is_moving) {
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
    if (movement->just_started) {
        movement->just_started = false;
    }
}

/*
 * Rendering Functions
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

/* Calculate and display FPS */
static void update_fps(AppState* app) {
    app->fps_count++;
    
    uint64_t current_time = SDL_GetTicks();
    if (current_time - app->last_fps_time >= 1000) {
        app->current_fps = app->fps_count;
        app->fps_count = 0;
        app->last_fps_time = current_time;
        
        char title[64];
        snprintf(title, sizeof(title), "Bit-Twiddled Game Engine - FPS: %d", app->current_fps);
        SDL_SetWindowTitle(app->window, title);
    }
}

/* Simplified render function */
static void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    /* 1. Render the background (walls and grid lines) */
    if (app->background_texture) {
        SDL_RenderTexture(renderer, app->background_texture, NULL, NULL);
    }
    
    /* 2. Render items */
    SDL_FRect rect = { 0, 0, PIXEL_SCALE, PIXEL_SCALE };
    SDL_SetRenderDrawColor(renderer,
                           CELL_COLORS[CELL_ITEM].r,
                           CELL_COLORS[CELL_ITEM].g,
                           CELL_COLORS[CELL_ITEM].b,
                           CELL_COLORS[CELL_ITEM].a);
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (get_cell(game, x, y) == CELL_ITEM) {
                /* Create smaller rectangle for item */
                rect.x = x * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rect.y = y * PIXEL_SCALE + PIXEL_SCALE * 0.25f;
                rect.w = rect.h = PIXEL_SCALE * 0.5f;
                
                SDL_RenderFillRect(renderer, &rect);
                
                /* Reset rectangle size */
                rect.w = rect.h = PIXEL_SCALE;
            }
        }
    }
    
    /* 3. Render player */
    float visual_x, visual_y;
    get_visual_position(&game->player, &visual_x, &visual_y);
    
    rect.x = visual_x * PIXEL_SCALE;
    rect.y = visual_y * PIXEL_SCALE;
    rect.w = rect.h = PIXEL_SCALE;
    
    SDL_SetRenderDrawColor(renderer,
                           PLAYER_COLOR.r,
                           PLAYER_COLOR.g,
                           PLAYER_COLOR.b,
                           PLAYER_COLOR.a);
    SDL_RenderFillRect(renderer, &rect);
    
    /* Present the rendered frame */
    SDL_RenderPresent(renderer);
    
    /* Update FPS counter */
    update_fps(app);
    
    /* Check if we need to update background texture due to grid changes */
    if (game->grid_state.cells_changed) {
        create_background_texture(app);
    }
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
    /* Set hints for optimal performance */
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");  /* Use Metal on Apple platforms */
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");       /* Enable VSync */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1"); /* Allow screensaver for energy saving */
    
    SDL_SetHint("SDL_POWERSTATE_POLLING_INTERVAL", "5000"); /* Check power state every 5 seconds */
    
    /* Initialize SDL */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        return SDL_APP_FAILURE;
    }
    
    /* Allocate application state */
    AppState* app = SDL_calloc(1, sizeof(AppState));
    if (!app) {
        return SDL_APP_FAILURE;
    }
    
    *appstate = app;
    app->app_flags = 0;  /* Not fullscreen, normal time scale */
    app->target_fps = LOGIC_TICK_RATE;  /* Start with standard frame rate */
    
    /* Create window and renderer with better defaults */
    Uint32 window_flags = 0;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags = SDL_WINDOW_FULLSCREEN;
    app->app_flags |= APP_FULLSCREEN;
#endif
    
    app->window = SDL_CreateWindow("Bit-Twiddled Game Engine", WINDOW_WIDTH, WINDOW_HEIGHT, window_flags);
    if (!app->window) {
        return SDL_APP_FAILURE;
    }
    
    /* In SDL3, we just specify the renderer name (Metal for Apple platforms) */
    app->renderer = SDL_CreateRenderer(app->window, "metal");
    if (!app->renderer) {
        /* Fall back to default renderer if Metal isn't available */
        app->renderer = SDL_CreateRenderer(app->window, NULL);
        if (!app->renderer) {
            return SDL_APP_FAILURE;
        }
    }
    
    /* Check power state */
    update_power_state(app);
    
    /* Configure rendering */
    configure_rendering(app);
    
    /* Initialize game state first */
    init_game(&app->game);
    
    /* Create textures (needs initialized game state) */
    create_textures(app);
    
    /* Initialize gamepad */
    initialize_gamepad(app);
    
    /* Initialize FPS counter */
    app->last_fps_time = SDL_GetTicks();
    app->fps_count = 0;
    app->current_fps = 0;
    
    /* Seed random number generator */
    srand((unsigned int)SDL_GetTicks());
    
    /* Log renderer information */
    const char* renderer_name = SDL_GetRendererName(app->renderer);
    SDL_Log("Using renderer: %s", renderer_name ? renderer_name : "Unknown");
    
    return SDL_APP_CONTINUE;
}

/* Main game loop iteration with fixed time step and sleep optimization */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Record start time for frame timing */
    Uint64 frame_start_time = SDL_GetTicks();
    
    /* Check power state periodically (every 5 seconds) */
    static Uint64 last_power_check = 0;
    if (frame_start_time - last_power_check > 5000) {
        update_power_state(app);
        last_power_check = frame_start_time;
    }
    
    /* Calculate elapsed time */
    Uint64 current_time = frame_start_time;
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
    
    /* Calculate frame time and sleep if ahead of schedule */
    Uint64 frame_end_time = SDL_GetTicks();
    Uint64 frame_duration = frame_end_time - frame_start_time;
    
    /* Target frame time in milliseconds */
    Uint64 target_frame_time = 1000 / app->target_fps;
    
    /* If we completed the frame early, sleep to save energy */
    if (frame_duration < target_frame_time) {
        SDL_Delay((Uint32)(target_frame_time - frame_duration));
    }
    
    return SDL_APP_CONTINUE;
}

/* Process SDL events */
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
            
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            app->is_in_background = false;
            update_power_state(app);
            break;
            
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            app->is_in_background = true;
            update_power_state(app);
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
        
        destroy_textures(app);
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
