# Bit-Twiddled Game Engine

A minimalist, performance-oriented 2D game engine built with C and SDL3, inspired by classic 8-bit game console architecture. This engine employs bit-twiddling optimization techniques, GPU offloading principles, and power-aware rendering to create an exceptionally efficient platform for 2D grid-based games.

## Core Design Philosophy

The Bit-Twiddled Game Engine follows three core principles:

1. **Data-Oriented Design**: Optimize for data locality and cache efficiency rather than OOP abstractions
2. **Bit-Level Optimization**: Use power-of-two dimensions and bitwise operations for performance
3. **GPU as PPU**: Treat the GPU like a classic console's Picture Processing Unit (PPU)

## Technical Architecture

### Memory Model

The engine uses a 64×64 grid for game data, accessed using highly optimized bit operations:

```c
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
```

### Rendering Pipeline

The engine uses a multi-layered rendering approach that minimizes CPU usage:

1. **Background Layer**: Static elements (walls, grid lines) rendered to a texture once
2. **Dynamic Entities**: Items and player drawn with batched rectangles
3. **Dirty Region Tracking**: Only redraw areas that have changed

### Power Management

The engine includes a sophisticated power management system that adapts based on:

- Battery state (plugged in vs. battery powered)
- Application focus (foreground vs. background)
- User activity (idle time detection)
- Available GPU capabilities

Three power modes are supported:
- **Performance**: Full 60 FPS with all optimizations
- **Balanced**: 30 FPS with moderate power saving
- **Efficient**: Minimal rendering with maximum power saving

## 16:9 Viewport System

The engine uses a viewport system that:

- Maintains a 64×64 grid in memory (power-of-two for efficient bit operations)
- Renders a 64×36 viewport (16:9 aspect ratio) to the screen
- Seamlessly wraps content at viewport edges

### Grid vs. Viewport

The engine distinguishes between grid coordinates (memory) and viewport coordinates (screen):

```
Grid (Memory)       Viewport (Screen)
    64×64               64×36
┌────────────┐      ┌────────────┐
│            │      │            │
│            │      │            │
│            │      └────────────┘
│            │
│            │
└────────────┘
```

### Coordinate Conversion

The engine provides functions to convert between coordinate systems:

```c
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
```

## Using the Engine

### Initial Setup

1. Clone the repository
2. Build with CMake or Xcode (macOS)
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

### Game Creation Basics

1. **Initializing the Game**:
   ```c
   GameState game;
   init_game(&game);
   ```

2. **Modifying the Grid**:
   ```c
   // Place a wall
   set_cell(&game, x, y, CELL_WALL);
   
   // Place an item
   set_cell(&game, x, y, CELL_ITEM);
   ```

3. **Moving the Player**:
   The player automatically moves based on input handling from keyboard and gamepad.

### Input Handling

The engine processes input using a state-based approach:

```c
/* From input.c */
void process_key_event(InputState* input, SDL_Scancode key, bool pressed) {
    Direction dir = DIR_NONE;
    
    /* Map keyboard to direction */
    dir = (key == SDL_SCANCODE_RIGHT) ? DIR_RIGHT :
          (key == SDL_SCANCODE_UP)    ? DIR_UP :
          (key == SDL_SCANCODE_LEFT)  ? DIR_LEFT :
          (key == SDL_SCANCODE_DOWN)  ? DIR_DOWN : DIR_NONE;
    
    /* Update direction key state if valid direction */
    if (dir != DIR_NONE) {
        set_key_state(input, dir, pressed);
        
        /* If key was pressed, update current direction */
        if (pressed) {
            input->current_dir = dir;
        }
        // ...
    }
}
```

### Using the Viewport System

When creating game objects, convert between viewport and grid coordinates:

```c
// Creating a wall in viewport coordinates
int viewport_x = 10, viewport_y = 15;
int grid_x, grid_y;
viewport_to_grid(&game, viewport_x, viewport_y, &grid_x, &grid_y);
set_cell(&game, grid_x, grid_y, CELL_WALL);

// Checking if a grid object is visible
bool visible = is_in_viewport(&game, grid_x, grid_y);
```

## Advanced Features

### Power-Aware Rendering

The engine includes features to minimize power usage:

```c
/* From platform.c */
void update_power_state(AppState* app) {
    /* Check if we're running on battery */
    app->is_on_battery = is_running_on_battery();
    
    /* Adjust based on activity state */
    if (app->is_in_background) {
        app->power_mode = POWER_MODE_EFFICIENT;
        app->target_fps = 0; /* Only render on demand */
    }
    else {
        /* Check idle time */
        uint64_t idle_time = SDL_GetTicks() - app->last_activity_time;
        
        if (idle_time > 10000) {
            /* After 10 seconds of inactivity, go to efficient mode */
            app->power_mode = POWER_MODE_EFFICIENT;
            app->target_fps = 0;
        }
        // ...
    }
}
```

### Adaptive Sleep Strategy

The engine uses a multi-level sleep strategy to minimize CPU usage:

```c
/* From main.c - SDL_AppIterate */
/* LEVEL 4: Adaptive CPU surrender */
uint32_t sleep_duration;

if (app->power_mode == POWER_MODE_PERFORMANCE && game->player.is_moving) {
    /* When actively moving, use very short sleeps for responsiveness */
    sleep_duration = 1; /* Minimal sleep to allow 60fps */
}
else if (app->power_mode == POWER_MODE_BALANCED || !update_needed) {
    /* When in balanced mode or no updates needed, sleep longer */
    sleep_duration = 16; /* ~60fps, but sleep between frames */
}
else {
    /* In efficient mode, surrender CPU for longest time */
    sleep_duration = 100; /* Very long sleep */
}

/* Actually sleep */
SDL_Delay(sleep_duration);
```

## System Requirements

- SDL3 library
- C11 compatible compiler
- macOS, Windows, or Linux

## Extending the Engine

### Adding New Cell Types

1. Update the `CellType` enum in `main.h`
2. Add a color for the new cell type in `render.c`
3. Implement interaction logic in `game.c`

### Customizing the Viewport

You can modify the viewport position by changing the `offset_x` and `offset_y` values in the `ViewportState` structure.

## Performance Optimization Tips

1. Always use power-of-two dimensions for grids and maps
2. Minimize dynamic memory allocation during gameplay
3. Batch similar rendering operations together
4. Use the dirty region system for partial updates
5. Process input events efficiently to avoid input lag

## License

This project is distributed under the MIT License. See LICENSE file for more information.
