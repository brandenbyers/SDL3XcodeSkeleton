/*
 * game.h - Main header file for the Bit-Twiddled Game Engine
 *
 * This header contains all declarations for the game engine components.
 */

#ifndef GAME_H
#define GAME_H

#include <SDL3/SDL.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_gamepad.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>  /* For memset */

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_MAC && !TARGET_OS_IOS && !TARGET_OS_TV
/* macOS specific headers for power management */
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
#define BACKGROUND_FPS      10      /* Very low frame rate when in background */

/* Bit Flags for app state */
#define APP_FULLSCREEN      0x01
#define APP_TIME_SCALE      0x06    /* Bits 1-2 for time scale */
#define APP_TS_SHIFT        1       /* Shift amount for time scale */

/* Input state flags */
#define KEY_RIGHT           0x01
#define KEY_UP              0x02
#define KEY_LEFT            0x04
#define KEY_DOWN            0x08
#define HAS_BUFFERED        0x10
#define RESTART_REQ         0x20

/*
 * Type Definitions
 */

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
    bool is_paused;                /* True if game is paused (zero processing) */
    int target_fps;                /* Target FPS based on power state */
    
    /* Performance tracking */
    uint64_t last_fps_time;        /* Last time FPS was calculated */
    int fps_count;                 /* Frame count for FPS calculation */
    int current_fps;               /* Current FPS value */
} AppState;

/*
 * Pre-computed lookup tables for movement
 */
extern const int8_t DIR_OFFSET_X[4];  /* RIGHT, UP, LEFT, DOWN */
extern const int8_t DIR_OFFSET_Y[4];  /* RIGHT, UP, LEFT, DOWN */

/* Power state colors */
extern const SDL_Color CELL_COLORS[CELL_MAX];  /* Colors for different cell types */
extern const SDL_Color PLAYER_COLOR;           /* Player color */
extern const SDL_Color GRID_LINE_COLOR;        /* Grid line color */
extern const SDL_Color PAUSED_OVERLAY_COLOR;   /* Semi-transparent overlay for paused state */

/*
 * Function Declarations
 */

/* Game functions (game.c) */
void init_game(GameState* game);
void update_game_logic_fixed_step(GameState* game);
CellType get_cell(const GameState* game, int x, int y);
void set_cell(GameState* game, int x, int y, CellType type);
bool is_valid_move(const GameState* game, int x, int y, Direction dir);
void get_target_position(int x, int y, Direction dir, int* target_x, int* target_y);
bool are_directions_opposite(Direction dir1, Direction dir2);
bool start_movement(GameState* game, Direction dir);
void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y);
void complete_movement(GameState* game);
Direction get_direction(const MovementState* movement);
void set_direction(MovementState* movement, Direction dir);

/* Input functions (input.c) */
void process_key_event(InputState* input, SDL_Scancode key, bool pressed);
void process_gamepad_state(InputState* input, SDL_Gamepad* gamepad);
bool is_key_pressed(const InputState* input, Direction dir);
void set_key_state(InputState* input, Direction dir, bool pressed);
bool has_buffered_dir(const InputState* input);
void set_has_buffered(InputState* input, bool has_buffered);
bool is_restart_requested(const InputState* input);
void set_restart_requested(InputState* input, bool requested);
void initialize_gamepad(AppState* app);

/* Rendering functions (render.c) */
void render_game(AppState* app);
void create_textures(AppState* app);
void destroy_textures(AppState* app);
void create_background_texture(AppState* app);
void configure_rendering(AppState* app);
void update_fps(AppState* app);

/* Platform-specific functions (platform.c) */
bool is_running_on_battery(void);
void update_power_state(AppState* app);
float get_time_scale(const AppState* app);
void set_time_scale(AppState* app, uint8_t scale_index);
void toggle_fullscreen(AppState* app);
void cycle_time_scale(AppState* app);

#endif /* GAME_H */
