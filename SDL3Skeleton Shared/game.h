/*
 * game.h - Core game structures and constants
 *
 * This header contains the main game state structures and configuration values.
 */

#ifndef GAME_H
#define GAME_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations of structures from other headers */
struct InputState;
struct MovementState;
struct GridState;
struct ViewportState;
struct EntitySystem;
struct CollisionSystem;

/*
 * Grid and Viewport Configuration With Power-of-Two Dimensions
 */
#define GRID_WIDTH          64      /* Must be power of 2 for bit shifts */
#define GRID_HEIGHT         64      /* Must be power of 2 for bit shifts */
#define GRID_WIDTH_SHIFT    6       /* log2(64) = 6, used for shifting */
#define GRID_HEIGHT_SHIFT   6       /* log2(64) = 6, used for shifting */
#define GRID_HEIGHT_MASK    0x3F    /* 2^6 - 1 = 63, masks lower 6 bits */
#define GRID_WIDTH_MASK     0x3F    /* 2^6 - 1 = 63, masks lower 6 bits */
#define GRID_SIZE           (GRID_WIDTH * GRID_HEIGHT)

/* Viewport configuration for 16:9 aspect ratio */
#define VIEWPORT_WIDTH      64      /* Same as grid width */
#define VIEWPORT_HEIGHT     36      /* 16:9 aspect ratio */
#define VIEWPORT_OFFSET_Y   14      /* (64-36)/2 = 14, centers the viewport vertically */

/* Display configuration */
#define PIXEL_SCALE         12      /* Screen pixels per grid cell */
#define WINDOW_WIDTH        (VIEWPORT_WIDTH * PIXEL_SCALE)   /* Show full viewport width */
#define WINDOW_HEIGHT       (VIEWPORT_HEIGHT * PIXEL_SCALE)  /* Show full viewport height */

/* Game timing configuration */
#define LOGIC_TICK_RATE     60      /* Game logic updates per second */
#define FRAMES_PER_TILE     3       /* Frames to move one tile */
#define LOGIC_TICK_MS       (1000 / LOGIC_TICK_RATE)
#define CORNER_BUFFER_FRAMES 2      /* Frames before tile end to accept corner input */

/* Energy management */
#define BACKGROUND_FPS      10      /* Very low frame rate when in background */
#define MAX_DIRTY_REGIONS   16      /* Maximum number of dirty regions to track */

/* Bit Flags for app state */
#define APP_FULLSCREEN      0x01
#define APP_TIME_SCALE      0x06    /* Bits 1-2 for time scale */
#define APP_TS_SHIFT        1       /* Shift amount for time scale */

/* Power modes for adaptive performance */
#define POWER_MODE_PERFORMANCE  0   /* Full speed, optimal responsiveness */
#define POWER_MODE_BALANCED     1   /* Good balance of performance and efficiency */
#define POWER_MODE_EFFICIENT    2   /* Maximum energy efficiency */

/* Cell types - One byte per cell for cache efficiency */
typedef enum {
    CELL_EMPTY = 0,
    CELL_WALL  = 1,
    CELL_ITEM  = 2,
    CELL_PIVOT = 3,    /* Pivot point that affects entity movement */
    CELL_MAX   = 4     /* Not used as a cell value, just for array bounds */
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

/* Game State */
typedef struct {
    uint8_t grid[GRID_SIZE];       /* Visual grid: one byte per cell */
    struct MovementState* player;   /* Player movement state */
    struct InputState* input;       /* Input state */
    struct GridState* grid_state;   /* Grid change tracking */
    struct ViewportState* viewport; /* Viewport position in grid */
    struct EntitySystem* entities;  /* Entity system */
    struct CollisionSystem* collision; /* Collision system */
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
    bool was_auto_paused;          /* True if game was auto-paused by system */
    int target_fps;                /* Target FPS based on power state */
    
    /* Rendering timing */
    uint64_t last_render_time;     /* Last time we rendered a frame */
    
    /* Performance tracking */
    uint64_t last_fps_time;        /* Last time FPS was calculated */
    int fps_count;                 /* Frame count for FPS calculation */
    int current_fps;               /* Current FPS value */
    
    /* Efficiency tracking */
    bool needs_render;             /* Only render when true */
    bool game_state_changed;       /* Track if game state changed */
    uint64_t last_activity_time;   /* Time of last user activity */
    uint8_t power_mode;            /* Current power mode (0=Performance, 1=Balanced, 2=Efficient) */
    
    /* Dirty region tracking */
    SDL_FRect dirty_regions[MAX_DIRTY_REGIONS];   /* List of regions needing redraw */
    int dirty_region_count;        /* Number of dirty regions */
} AppState;

/* Pre-computed lookup tables for movement */
extern const int8_t DIR_OFFSET_X[4];  /* RIGHT, UP, LEFT, DOWN */
extern const int8_t DIR_OFFSET_Y[4];  /* RIGHT, UP, LEFT, DOWN */

/* Core game functions */
void init_game(GameState* game);
void update_game_logic_fixed_step(GameState* game);
CellType get_cell(const GameState* game, int x, int y);
void set_cell(GameState* game, int x, int y, CellType type);

/* Platform-specific functions (platform.c) */
bool is_running_on_battery(void);
void update_power_state(AppState* app);
float get_time_scale(const AppState* app);
void set_time_scale(AppState* app, uint8_t scale_index);
void toggle_fullscreen(AppState* app);
void cycle_time_scale(AppState* app);
void reset_activity_timer(AppState* app);

#endif /* GAME_H */
