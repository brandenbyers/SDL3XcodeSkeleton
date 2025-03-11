/*
 * SDL3 Game Skeleton with Deterministic Movement
 *
 * This implementation follows the philosophy of classic arcade games like Pac-Man
 * and modern grid-based games that utilize fixed time steps for deterministic
 * movement. This approach guarantees that visual representation and logical game
 * state remain perfectly synchronized, producing fluid and predictable motion.
 *
 * FIXED TIME STEP METHODOLOGY
 * ---------------------------
 * Unlike modern variable time-step approaches that separate animation from logic,
 * classic arcade games used a fixed time step where:
 *
 * 1. The game world updates at a constant, known frequency
 * 2. Movement takes a precise, countable number of frames to complete
 * 3. Animation positions are directly computed from the current frame number
 * 4. Logic and rendering are perfectly synchronized by design
 *
 * This approach yields significant benefits:
 * - Deterministic, reproducible behavior
 * - Perfect visual fluidity with no "hitching" between tiles
 * - Simplified collision detection that remains visually coherent
 * - More predictable gameplay for both developers and players
 *
 * The implementation below follows these principles while leveraging modern
 * language features and organization for clearer code structure.
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
 * Game Configuration
 *
 * DESIGN PHILOSOPHY FOR TIMING CONSTANTS
 * --------------------------------------
 * The timing values below create a deterministic relationship between logic ticks
 * and animation. By defining a specific number of ticks for each tile transition,
 * we ensure perfect synchronization between what the game "thinks" is happening
 * and what the player sees.
 *
 * This approach mirrors how classic arcade games like Pac-Man implemented movement.
 * For example, in the original Pac-Man:
 * - The game ran at 60 frames per second
 * - Character movement took exactly 8 frames to move between tiles
 * - Position was computed as: start_pos + (end_pos - start_pos) * (current_frame / 8)
 *
 * We use the same principle here, with configurable values for flexibility.
 */
#define LOGIC_TICK_RATE        60   /* Game logic updates per second */
#define FRAMES_PER_TILE        12   /* Number of logic ticks to move one tile */
#define CORNER_BUFFER_FRAMES   3    /* Frames before tile end to accept corner input */

/* Derived timing constants */
#define LOGIC_TICK_MS         (1000 / LOGIC_TICK_RATE) /* Milliseconds per logic tick */
#define SECONDS_PER_TILE      ((float)FRAMES_PER_TILE / LOGIC_TICK_RATE) /* Time to move one tile */

/* Tile and grid configuration */
#define BLOCK_SIZE             48   /* Size of each block (cell) in pixels */
#define GRID_WIDTH             24   /* Width of the game grid in blocks */
#define GRID_HEIGHT            14   /* Height of the game grid in blocks */
#define GRID_SIZE              (GRID_WIDTH * GRID_HEIGHT)
#define WINDOW_WIDTH           (BLOCK_SIZE * GRID_WIDTH)
#define WINDOW_HEIGHT          (BLOCK_SIZE * GRID_HEIGHT)

/* Cell types stored in the grid */
typedef enum {
    CELL_EMPTY = 0,
    CELL_WALL = 1,
    CELL_ITEM = 2
} CellType;

/* Movement direction
 *
 * DIRECTION ENCODING METHODOLOGY
 * -----------------------------
 * We encode directions as integers from -1 to 3, where:
 * - DIR_NONE (-1) represents no movement
 * - The four cardinal directions (0-3) are arranged in a way that opposite
 *   directions differ by exactly 2
 *
 * This encoding enables efficient operations like:
 * - Checking if directions are opposite: abs(dir1 - dir2) == 2
 * - Reverse direction: (dir + 2) % 4
 *
 * This approach was common in classic arcade games for compact storage and
 * efficient direction-based logic processing.
 */
typedef enum {
    DIR_NONE = -1, /* Not moving */
    DIR_RIGHT = 0,
    DIR_UP = 1,
    DIR_LEFT = 2,
    DIR_DOWN = 3,
    DIR_COUNT = 4
} Direction;

/*
 * Input State
 *
 * DECOUPLED INPUT PROCESSING
 * --------------------------
 * The input state captures raw player intent without directly modifying game state.
 * This separation of input detection from game state modification follows good
 * software design principles and allows for:
 * - Input buffering for responsive controls
 * - Direction prioritization when multiple keys are pressed
 * - Clean handling of key release events
 *
 * This structure purely tracks player intention, which the game logic then
 * interprets and applies based on the current game state.
 */
typedef struct {
    bool direction_keys[DIR_COUNT];  /* Currently held direction keys */
    Direction current_dir;           /* Currently active direction */
    Direction buffered_dir;          /* Direction queued for next intersection */
    bool has_buffered_dir;           /* Whether we have a buffered direction */
    bool restart_requested;          /* Whether player requested game restart */
} InputState;

/*
 * Movement State
 *
 * DETERMINISTIC MOVEMENT TRACKING
 * ------------------------------
 * This structure manages the logical grid-based movement of an entity.
 * It tracks both current position and target position, along with a frame
 * counter that precisely measures progress between tiles.
 *
 * Unlike variable time step approaches that use percentages or time-based
 * progress, this approach counts discrete frames. This creates perfectly
 * deterministic movement where:
 * - Each tile transition takes exactly FRAMES_PER_TILE frames
 * - Position can be calculated precisely for any frame number
 * - Collision detection happens at predictable, exact moments
 *
 * This mirrors how classic arcade games implemented movement, where complex
 * physics calculations were replaced by simple, frame-based state machines.
 */
typedef struct {
    int x;                           /* Current grid X position */
    int y;                           /* Current grid Y position */
    Direction dir;                   /* Current movement direction */
    bool is_moving;                  /* Whether entity is currently moving */
    int target_x;                    /* Target grid X position */
    int target_y;                    /* Target grid Y position */
    int move_frame;                  /* Current frame in movement animation (0 to FRAMES_PER_TILE-1) */
    bool just_started_moving;        /* Flag to track new movement initiation */
} MovementState;

/*
 * Game State
 *
 * CONTIGUOUS MEMORY LAYOUT
 * -----------------------
 * The game state is organized to maximize cache efficiency and minimize
 * memory fragmentation. By using contiguous arrays for grid storage and
 * keeping related data together, we improve cache locality and memory
 * access patterns.
 *
 * Classic arcade games were extremely memory-conscious due to hardware
 * limitations, often using bit-packing and careful data layout to maximize
 * efficiency. While modern systems have fewer constraints, these principles
 * still yield better performance.
 */
typedef struct {
    /* Game grid: flat array of cells for cache-friendly access */
    CellType grid[GRID_SIZE];
    
    /* Player state */
    MovementState player_movement;
    
    /* Input state */
    InputState input;
    
    /* Timing */
    Uint64 last_tick_time;           /* Time of last logic tick */
    int accumulated_time;            /* Time accumulated since last tick (ms) */
    Uint64 frame_count;              /* Total number of logic frames executed */
} GameState;

/*
 * Application State
 *
 * This structure contains the high-level application state, including SDL resources
 * and the game state.
 */
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Gamepad* gamepad;
    SDL_JoystickID gamepad_id;
    GameState game;
    bool fullscreen;
    float time_scale;                /* Time scaling factor for debugging (1.0 = normal) */
} AppState;

/*
 * Function Declarations
 */
static void init_game(GameState* game);
static void process_key_press(InputState* input, SDL_Scancode key, bool pressed);
static void process_gamepad_button(InputState* input, SDL_GamepadButton button, bool pressed);
static void update_game_logic_fixed_step(GameState* game);
static void render_game(AppState* app);
static void toggle_fullscreen(AppState* app);
static void initialize_gamepad(AppState* app);

/*
 * Grid Utility Functions
 */

/* Convert 2D grid coordinates to 1D array index
 *
 * ROW-MAJOR MEMORY LAYOUT
 * ----------------------
 * We use a row-major layout where grid[y*width + x] gives the cell at (x,y).
 * This creates better cache locality when accessing cells in row order, as
 * adjacent cells in a row are adjacent in memory.
 *
 * Classic games often used this layout for efficient memory access patterns,
 * and modern CPUs still benefit from this approach due to cache line loading.
 */
static inline int grid_index(int x, int y) {
    return y * GRID_WIDTH + x;
}

/* Get cell type at grid position with bounds checking
 *
 * TOROIDAL GRID WRAPPING
 * ---------------------
 * This function implements a toroidal (donut-shaped) world where moving off
 * one edge brings you back from the opposite edge. This is accomplished using
 * modulo arithmetic to wrap coordinates.
 *
 * This approach was used in many classic arcade games like Pac-Man, creating
 * a world that feels larger than it actually is by seamlessly wrapping edges.
 */
static CellType get_cell(const GameState* game, int x, int y) {
    /* Handle wrapping around grid edges using modulo arithmetic */
    x = (x + GRID_WIDTH) % GRID_WIDTH;
    y = (y + GRID_HEIGHT) % GRID_HEIGHT;
    
    return game->grid[grid_index(x, y)];
}

/* Set cell type at grid position with bounds checking */
static void set_cell(GameState* game, int x, int y, CellType type) {
    /* Handle wrapping around grid edges */
    x = (x + GRID_WIDTH) % GRID_WIDTH;
    y = (y + GRID_HEIGHT) % GRID_HEIGHT;
    
    game->grid[grid_index(x, y)] = type;
}

/* Check if a movement in given direction is valid
 *
 * COLLISION DETECTION PRINCIPLES
 * ----------------------------
 * This function performs simple grid-based collision detection by checking
 * if the target cell is empty or contains an item. This approach is
 * deterministic and easily understood, avoiding complex collision shapes
 * or physics calculations.
 *
 * Classic arcade games used this approach because:
 * 1. It's computationally efficient (important on limited hardware)
 * 2. It creates clear, predictable gameplay rules
 * 3. It maps cleanly to the grid-based visual representation
 */
static bool is_valid_move(const GameState* game, int x, int y, Direction dir) {
    if (dir == DIR_NONE) {
        return false;
    }
    
    /* Calculate target position */
    int target_x = x;
    int target_y = y;
    
    switch (dir) {
        case DIR_RIGHT: target_x = (x + 1) % GRID_WIDTH; break;
        case DIR_UP:    target_y = (y - 1 + GRID_HEIGHT) % GRID_HEIGHT; break;
        case DIR_LEFT:  target_x = (x - 1 + GRID_WIDTH) % GRID_WIDTH; break;
        case DIR_DOWN:  target_y = (y + 1) % GRID_HEIGHT; break;
        default: return false; /* Invalid direction */
    }
    
    /* Check if target cell is empty or contains an item */
    CellType target_cell = get_cell(game, target_x, target_y);
    return target_cell != CELL_WALL;
}

/* Get target position for movement in given direction */
static void get_target_position(int x, int y, Direction dir, int* target_x, int* target_y) {
    *target_x = x;
    *target_y = y;
    
    switch (dir) {
        case DIR_RIGHT: *target_x = (x + 1) % GRID_WIDTH; break;
        case DIR_UP:    *target_y = (y - 1 + GRID_HEIGHT) % GRID_HEIGHT; break;
        case DIR_LEFT:  *target_x = (x - 1 + GRID_WIDTH) % GRID_WIDTH; break;
        case DIR_DOWN:  *target_y = (y + 1) % GRID_HEIGHT; break;
        default: break; /* No movement */
    }
}

/* Check if two directions are opposite
 *
 * EFFICIENT DIRECTION COMPARISON
 * ----------------------------
 * This function exploits the numerical encoding of directions to efficiently
 * determine if two directions are opposite. Since opposite directions differ
 * by exactly 2 in our encoding, a simple arithmetic check is sufficient.
 *
 * This is more efficient than a switch statement or multiple comparisons,
 * and was a common optimization in classic games with limited CPU resources.
 */
static bool are_directions_opposite(Direction dir1, Direction dir2) {
    if (dir1 == DIR_NONE || dir2 == DIR_NONE) {
        return false;
    }
    return abs(dir1 - dir2) == 2;
}

/*
 * Animation Utility Functions
 */

/* Calculate visual position based on movement state
 *
 * DETERMINISTIC VISUAL INTERPOLATION
 * --------------------------------
 * This function computes the exact visual position of an entity based on:
 * 1. The current grid position
 * 2. The target grid position
 * 3. The current frame in the movement animation
 *
 * Unlike variable time step approaches that can create inconsistent animation,
 * this frame-based approach ensures that movement always looks identical and
 * predictable, with perfect synchronization between logic and visuals.
 *
 * The linear interpolation formula calculates position as:
 *    visual_pos = start_pos + (target_pos - start_pos) * (current_frame / total_frames)
 *
 * This matches how classic arcade games calculated visual positions, ensuring
 * smooth, reproducible movement without relying on delta time.
 */
static void get_visual_position(const MovementState* movement, float* visual_x, float* visual_y) {
    if (!movement->is_moving || movement->move_frame >= FRAMES_PER_TILE) {
        /* Not moving or movement complete - use exact grid position */
        *visual_x = (float)movement->x;
        *visual_y = (float)movement->y;
        return;
    }
    
    /* Calculate interpolation factor (0.0 to 1.0) */
    float t = (float)movement->move_frame / FRAMES_PER_TILE;
    
    /* Start position */
    float start_x = (float)movement->x;
    float start_y = (float)movement->y;
    
    /* Target position */
    float target_x = (float)movement->target_x;
    float target_y = (float)movement->target_y;
    
    /* Handle screen wrapping for smooth animation */
    if (abs(movement->target_x - movement->x) > 1) {
        /* Wrapping horizontally */
        if (movement->target_x < movement->x) {
            /* Moving right to left across the edge */
            target_x += GRID_WIDTH;
        } else {
            /* Moving left to right across the edge */
            start_x += GRID_WIDTH;
        }
    }
    
    if (abs(movement->target_y - movement->y) > 1) {
        /* Wrapping vertically */
        if (movement->target_y < movement->y) {
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

/*
 * Movement Functions
 */

/* Start movement in a given direction if possible
 *
 * MOVEMENT INITIATION PRINCIPLES
 * ----------------------------
 * This function initiates movement in the desired direction if possible.
 * It sets up both the logical destination and resets the frame counter
 * that will track progress toward that destination.
 *
 * The movement system uses a frame counter rather than a time-based
 * approach, ensuring that movement always takes exactly FRAMES_PER_TILE
 * frames to complete, creating predictable, consistent motion.
 */
static bool start_movement(GameState* game, Direction dir) {
    MovementState* movement = &game->player_movement;
    
    /* Validate the direction */
    if (dir == DIR_NONE) {
        return false;
    }
    
    /* Check if the move is valid */
    if (!is_valid_move(game, movement->x, movement->y, dir)) {
        return false;
    }
    
    /* Calculate target position */
    int target_x, target_y;
    get_target_position(movement->x, movement->y, dir, &target_x, &target_y);
    
    /* Update movement state */
    movement->dir = dir;
    movement->is_moving = true;
    movement->target_x = target_x;
    movement->target_y = target_y;
    movement->move_frame = 0;
    movement->just_started_moving = true;
    
    return true;
}

/* Handle movement completion and start next movement if needed
 *
 * CONTINUOUS MOVEMENT CHAIN
 * -----------------------
 * This function handles the completion of a movement and determines what
 * should happen next. It implements a priority system for the next direction:
 *
 * 1. Use buffered direction if valid (for responsive cornering)
 * 2. Continue in the same direction if that key is still held
 * 3. Check any other held direction keys
 * 4. Stop moving if no valid direction is found
 *
 * This approach creates fluid, continuous movement while still being
 * responsive to player input, allowing for skillful navigation.
 */
static void complete_movement(GameState* game) {
    MovementState* movement = &game->player_movement;
    InputState* input = &game->input;
    
    /* Update position to target */
    movement->x = movement->target_x;
    movement->y = movement->target_y;
    movement->move_frame = 0;
    
    /* Check if we should continue moving */
    Direction next_dir = DIR_NONE;
    
    /* First priority: use buffered direction if it's valid */
    if (input->has_buffered_dir &&
        is_valid_move(game, movement->x, movement->y, input->buffered_dir)) {
        next_dir = input->buffered_dir;
        input->has_buffered_dir = false;
    }
    /* Second priority: continue in same direction if key still held */
    else if (input->direction_keys[movement->dir] &&
             is_valid_move(game, movement->x, movement->y, movement->dir)) {
        next_dir = movement->dir;
    }
    /* Third priority: check for any held direction key */
    else {
        for (int i = 0; i < DIR_COUNT; i++) {
            if (input->direction_keys[i] && is_valid_move(game, movement->x, movement->y, i)) {
                next_dir = i;
                break;
            }
        }
    }
    
    /* Start next movement or stop */
    if (next_dir != DIR_NONE) {
        start_movement(game, next_dir);
    } else {
        movement->is_moving = false;
        movement->dir = DIR_NONE;
    }
}

/*
 * Game Logic Functions
 */

/* Initialize the game state
 *
 * GAME STATE INITIALIZATION
 * -----------------------
 * This function sets up the initial game state, including:
 * - Clearing the grid and placing walls/items
 * - Setting up the player's starting position
 * - Initializing timing and input state
 *
 * The initialization creates a clean, well-defined starting state,
 * essential for deterministic gameplay where the same inputs will
 * always produce the same results.
 */
static void init_game(GameState* game) {
    /* Clear the grid */
    for (int i = 0; i < GRID_SIZE; i++) {
        game->grid[i] = CELL_EMPTY;
    }
    
    /* Add some walls for collision testing */
    for (int x = 5; x < 10; x++) {
        set_cell(game, x, 5, CELL_WALL);
        set_cell(game, x + 10, 8, CELL_WALL);
    }
    
    /* Add corner testing area */
    for (int y = 10; y < 13; y++) {
        set_cell(game, 5, y, CELL_WALL);
        set_cell(game, 9, y, CELL_WALL);
    }
    set_cell(game, 6, 10, CELL_WALL);
    set_cell(game, 7, 10, CELL_WALL);
    set_cell(game, 8, 10, CELL_WALL);
    
    /* Add some items */
    for (int i = 0; i < 20; i++) {
        int x = rand() % GRID_WIDTH;
        int y = rand() % GRID_HEIGHT;
        if (get_cell(game, x, y) == CELL_EMPTY) {
            set_cell(game, x, y, CELL_ITEM);
        }
    }
    
    /* Place the player in the center */
    game->player_movement.x = GRID_WIDTH / 2;
    game->player_movement.y = GRID_HEIGHT / 2;
    game->player_movement.dir = DIR_NONE;
    game->player_movement.is_moving = false;
    game->player_movement.target_x = game->player_movement.x;
    game->player_movement.target_y = game->player_movement.y;
    game->player_movement.move_frame = 0;
    game->player_movement.just_started_moving = false;
    
    /* Reset input state */
    for (int i = 0; i < DIR_COUNT; i++) {
        game->input.direction_keys[i] = false;
    }
    game->input.current_dir = DIR_NONE;
    game->input.buffered_dir = DIR_NONE;
    game->input.has_buffered_dir = false;
    game->input.restart_requested = false;
    
    /* Initialize timing */
    game->last_tick_time = SDL_GetTicks();
    game->accumulated_time = 0;
    game->frame_count = 0;
}

/* Process key press/release and update input state
 *
 * INPUT PROCESSING METHODOLOGY
 * --------------------------
 * This function processes raw keyboard input and transforms it into
 * meaningful game input state. It follows these principles:
 *
 * 1. Keep track of which direction keys are currently held down
 * 2. Update the current active direction based on the most recent key press
 * 3. Handle key releases by finding the next most recently pressed key
 *
 * This approach creates responsive controls while handling scenarios
 * where the player holds multiple keys simultaneously.
 */
static void process_key_press(InputState* input, SDL_Scancode key, bool pressed) {
    Direction dir = DIR_NONE;
    
    switch (key) {
        case SDL_SCANCODE_RIGHT:
            dir = DIR_RIGHT;
            break;
        case SDL_SCANCODE_UP:
            dir = DIR_UP;
            break;
        case SDL_SCANCODE_LEFT:
            dir = DIR_LEFT;
            break;
        case SDL_SCANCODE_DOWN:
            dir = DIR_DOWN;
            break;
        case SDL_SCANCODE_R:
            if (pressed) {
                input->restart_requested = true;
            }
            return;
        default:
            return;  /* Not a direction key */
    }
    
    /* Update direction key state */
    input->direction_keys[dir] = pressed;
    
    /* If key was pressed (not released), update current direction */
    if (pressed) {
        input->current_dir = dir;
    }
    /* If key was released and it was the current direction, find a new current direction */
    else if (dir == input->current_dir) {
        input->current_dir = DIR_NONE;
        for (int i = 0; i < DIR_COUNT; i++) {
            if (input->direction_keys[i]) {
                input->current_dir = i;
                break;
            }
        }
    }
}

/* Process gamepad button press/release */
static void process_gamepad_button(InputState* input, SDL_GamepadButton button, bool pressed) {
    Direction dir = DIR_NONE;
    
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            dir = DIR_RIGHT;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            dir = DIR_UP;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            dir = DIR_LEFT;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            dir = DIR_DOWN;
            break;
        case SDL_GAMEPAD_BUTTON_START:
            if (pressed) {
                input->restart_requested = true;
            }
            return;
        default:
            return;  /* Not a direction button */
    }
    
    /* Update direction state same as keyboard */
    process_key_press(input, dir, pressed);
}

/* Update game logic with fixed time step
 *
 * FIXED TIME STEP GAME LOOP
 * ------------------------
 * This function implements a deterministic, fixed time step update cycle
 * that ensures consistent gameplay regardless of hardware speed.
 *
 * Key features of this approach:
 * 1. Game logic runs at exactly LOGIC_TICK_RATE frames per second
 * 2. Each movement always takes FRAMES_PER_TILE frames to complete
 * 3. Game state is advanced in discrete, countable steps
 *
 * This technique has several advantages:
 * - Perfect reproducibility (same inputs always produce same results)
 * - No temporal aliasing or collision detection issues
 * - Simplified physics and animation with frame-based calculations
 * - Predictable CPU usage profile
 *
 * The implementation uses time accumulation to handle cases where the actual
 * frame rate doesn't perfectly match the desired logic tick rate, ensuring
 * the game runs at the same speed on all hardware.
 */
static void update_game_logic_fixed_step(GameState* game) {
    MovementState* movement = &game->player_movement;
    InputState* input = &game->input;
    
    /* Check for restart request */
    if (input->restart_requested) {
        init_game(game);
        input->restart_requested = false;
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
            if (input->current_dir != DIR_NONE &&
                input->current_dir != movement->dir &&
                !are_directions_opposite(input->current_dir, movement->dir)) {
                
                /* Buffer this direction for the next intersection */
                input->buffered_dir = input->current_dir;
                input->has_buffered_dir = true;
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
    if (movement->just_started_moving) {
        movement->just_started_moving = false;
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

/* Render the current game state
 *
 * VISUAL RENDERING METHODOLOGY
 * --------------------------
 * This function renders the current game state to the screen. It follows
 * a straightforward approach:
 *
 * 1. Render the grid (walls and items)
 * 2. Calculate the player's exact visual position based on movement state
 * 3. Render the player at the calculated position
 *
 * The rendering is frame-perfect, meaning the visual presentation exactly
 * matches the logical game state, with no desynchronization or artifacts.
 * This creates a fluid, consistent experience for the player.
 */
static void render_game(AppState* app) {
    GameState* game = &app->game;
    SDL_Renderer* renderer = app->renderer;
    SDL_FRect rect;
    
    /* Clear the screen */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    
    /* Render the grid */
    rect.w = rect.h = BLOCK_SIZE;
    
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            CellType cell = get_cell(game, x, y);
            
            if (cell == CELL_WALL) {
                /* Draw walls */
                rect.x = (float)(x * BLOCK_SIZE);
                rect.y = (float)(y * BLOCK_SIZE);
                SDL_SetRenderDrawColor(renderer, 64, 64, 192, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(renderer, &rect);
            } else if (cell == CELL_ITEM) {
                /* Draw items */
                rect.x = (float)(x * BLOCK_SIZE + BLOCK_SIZE/4);
                rect.y = (float)(y * BLOCK_SIZE + BLOCK_SIZE/4);
                rect.w = rect.h = BLOCK_SIZE/2;
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(renderer, &rect);
                rect.w = rect.h = BLOCK_SIZE; /* Reset for next iteration */
            }
        }
    }
    
    /* Calculate player's visual position */
    float visual_x, visual_y;
    get_visual_position(&game->player_movement, &visual_x, &visual_y);
    
    /* Render the player */
    rect.x = visual_x * BLOCK_SIZE;
    rect.y = visual_y * BLOCK_SIZE;
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
    app->fullscreen = !app->fullscreen;
    
    if (app->fullscreen) {
        SDL_SetWindowFullscreen(app->window, true);
    } else {
        SDL_SetWindowFullscreen(app->window, false);
        SDL_SetWindowSize(app->window, WINDOW_WIDTH, WINDOW_HEIGHT);
    }
    
    configure_rendering(app);
}

/* Toggle time scale for debugging */
static void toggle_time_scale(AppState* app) {
    if (app->time_scale == 1.0f) {
        app->time_scale = 0.5f;
        SDL_Log("Time scale: 0.5x (slow motion)");
    } else if (app->time_scale == 0.5f) {
        app->time_scale = 0.25f;
        SDL_Log("Time scale: 0.25x (very slow)");
    } else if (app->time_scale == 0.25f) {
        app->time_scale = 2.0f;
        SDL_Log("Time scale: 2.0x (fast)");
    } else {
        app->time_scale = 1.0f;
        SDL_Log("Time scale: 1.0x (normal)");
    }
}

/*
 * SDL App Callbacks
 */

/* Initialize the application
 *
 * APPLICATION INITIALIZATION
 * ------------------------
 * This function initializes SDL and sets up the application state.
 * It creates the window and renderer, configures initial states,
 * and prepares the game for execution.
 *
 * The initialization process follows a clean, error-checking approach
 * to ensure the application starts in a well-defined state.
 */
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
    app->fullscreen = false;
    app->time_scale = 1.0f;
    
    /* Create window and renderer */
    Uint32 window_flags = 0;
    
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_TV)
    window_flags = SDL_WINDOW_FULLSCREEN;
    app->fullscreen = true;
#endif
    
    if (!SDL_CreateWindowAndRenderer("SDL3 Grid Movement", WINDOW_WIDTH, WINDOW_HEIGHT,
                                     window_flags, &app->window, &app->renderer)) {
        return SDL_APP_FAILURE;
    }
    
    /* Configure rendering */
    configure_rendering(app);
    
    /* Initialize gamepad */
    initialize_gamepad(app);
    
    /* Initialize game state */
    init_game(&app->game);
    
    return SDL_APP_CONTINUE;
}

/* Main game loop iteration
 *
 * FIXED TIME STEP WITH ACCUMULATOR
 * ------------------------------
 * This function implements a fixed time step game loop using an accumulator
 * pattern. It ensures that game logic runs at a consistent rate regardless
 * of the actual frame rate.
 *
 * The approach:
 * 1. Track elapsed time since last frame
 * 2. Add it to an accumulator
 * 3. Run logic updates in fixed LOGIC_TICK_MS steps
 * 4. Leave any remainder in the accumulator for the next frame
 *
 * This creates a deterministic update cycle while still allowing smooth
 * rendering at whatever frame rate the system can achieve.
 */
SDL_AppResult SDL_AppIterate(void* appstate) {
    AppState* app = (AppState*)appstate;
    GameState* game = &app->game;
    
    /* Calculate elapsed time */
    Uint64 current_time = SDL_GetTicks();
    int delta_time = (int)(current_time - game->last_tick_time);
    game->last_tick_time = current_time;
    
    /* Apply time scaling (for debugging) */
    delta_time = (int)(delta_time * app->time_scale);
    
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

/* Process SDL events
 *
 * EVENT PROCESSING METHODOLOGY
 * --------------------------
 * This function processes SDL events and translates them into application
 * actions. It handles:
 * - Keyboard input
 * - Gamepad input
 * - Window events
 * - Application control (quit, fullscreen toggle, etc.)
 *
 * The event processing is kept separate from game logic, following the
 * principle of separation of concerns. Events update input state, which
 * the game logic then acts upon.
 */
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
                toggle_time_scale(app);
            } else {
                process_key_press(&game->input, event->key.scancode, true);
            }
            break;
            
        case SDL_EVENT_KEY_UP:
            /* Handle key release */
            process_key_press(&game->input, event->key.scancode, false);
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            /* Handle gamepad input */
            if (event->gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                return SDL_APP_SUCCESS;
            } else {
                process_gamepad_button(&game->input, event->gbutton.button, true);
            }
            break;
            
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            /* Handle gamepad button release */
            process_gamepad_button(&game->input, event->gbutton.button, false);
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
