# Classic Frame-Based Game Architecture

This document explains the classic frame-based game architecture we've implemented, inspired by the efficiency of classic arcade games like Amazing Penguin and Pac-Man. This approach prioritizes simplicity, determinism, and efficient CPU usage while maintaining smooth animations through GPU-based interpolation.

## Core Architecture Principles

1. **Frame-Based Logic Updates**: Game state updates at a fixed 60 FPS
2. **Tile-Based Collision Detection**: Grid-based collision map for efficient collision detection
3. **GPU-Based Visual Interpolation**: Smooth movement between grid positions handled by the GPU
4. **Pivot-Point Movement System**: Entity movement governed by pivot points with bit-encoded directions
5. **Bit Manipulation**: Extensive use of bit operations for maximum efficiency

## Memory Layout

The architecture uses a data-oriented approach with careful attention to memory layout:

### Grid System (64×64)
- **Visual Grid**: Stores cell types (wall, item, pivot, empty)
- **Collision Map**: Stores collision flags using bit masks
- **Pivot Direction Map**: Stores allowed directions at pivot points

All grid structures use a power-of-two size (64×64) which enables efficient bit manipulation for position calculations and wrapping.

## Data Flow Pipeline

The game follows a clear data pipeline:

1. **Input Processing**: Convert input events to directional intent
2. **Game Logic Update**: Update player and entity positions at a fixed frame rate
3. **Collision Detection**: Use collision map to validate moves
4. **Visual Interpolation**: GPU smoothly interpolates between grid positions
5. **Rendering**: Draw game state with visual interpolation

## Collision System

The collision system uses a bit-flag approach for maximum efficiency:

```c
/* Collision map cell contents (bit flags) */
#define CMAP_EMPTY         0x00
#define CMAP_WALL          0x01
#define CMAP_ITEM          0x02
#define CMAP_ENTITY        0x04
#define CMAP_PLAYER        0x08
#define CMAP_PIVOT         0x10
```

This allows for efficient checking of what's in each cell and enables a single cell to contain multiple things (e.g., an entity on a pivot point).

## Pivot Point System

The pivot point system is the heart of the entity movement logic:

```c
/* Pivot direction flags */
#define PIVOT_UP           0x01
#define PIVOT_RIGHT        0x02
#define PIVOT_DOWN         0x04
#define PIVOT_LEFT         0x08
```

Each pivot point defines which directions are valid exits. When an entity reaches a pivot point, it consults its rule set to choose a new direction from the available options.

## Entity Rules

Entity behavior is defined by rule flags that determine how it chooses directions at pivot points:

```c
/* Entity rule flags */
#define RULE_REVERSE       0x01  /* Prefers to reverse direction */
#define RULE_CLOCKWISE     0x02  /* Prefers to turn clockwise */
#define RULE_COUNTER_CW    0x04  /* Prefers to turn counter-clockwise */
```

This creates emergent behavior from simple rules without requiring randomness.

## Visual Interpolation

While game logic operates on a discrete grid, rendering uses smooth interpolation:

1. Game logic updates the target position
2. Each frame, we calculate interpolated positions based on progress
3. The GPU renders entities at these interpolated positions
4. Wrapping is handled by rendering duplicates at wrapped positions

This creates smooth animation while keeping game logic simple and deterministic.

## CPU Efficiency

The architecture is designed for minimal CPU usage:

1. **Fixed Updates**: Game logic updates at exactly 60 FPS
2. **Simple Calculations**: No complex physics or trajectory predictions
3. **Bit Operations**: Fast bit manipulation for position and collision checks
4. **Cache Friendly**: Grid-based data structures with good spatial locality
5. **Batch Rendering**: Group similar drawing operations for GPU efficiency

## Key Benefits

- **Deterministic Behavior**: Game state is always predictable and reproducible
- **Simplified Reasoning**: Clear, linear flow makes code easier to understand
- **Efficient CPU Usage**: Simple calculations with good cache locality
- **Smooth Animations**: GPU handles visual interpolation for smooth movement
- **Extensible Design**: Easy to add new entity types with different rules

## Comparison to Event-Driven Approach

The frame-based approach differs from the previous event-driven approach:

| Aspect | Frame-Based Approach | Event-Driven Approach |
|--------|----------------------|----------------------|
| Update Frequency | Regular 60 FPS | Only on events |
| CPU Usage | Consistent, predictable | Low when idle, spikes during events |
| Logic Complexity | Simple, grid-based | Complex trajectory calculations |
| State Management | Single authoritative state | Multiple states with timing |
| Determinism | Fully deterministic | Timing-dependent |
| Debugging | Easy to trace frame-by-frame | Hard to reproduce timing issues |

## Class Diagram

```
GameState
├── grid[] - Visual grid
├── collision - Collision system
│   ├── map[] - Collision flags
│   └── pivot_dirs[] - Allowed directions
├── player - Player state
│   ├── pos_x, pos_y - Current position
│   ├── target_x, target_y - Target position
│   ├── direction - Current direction
│   └── move_frame - Movement progress
└── entities - Entity system
    ├── pos_x[], pos_y[] - Current positions
    ├── target_x[], target_y[] - Target positions
    ├── direction[] - Current directions
    ├── entity_type[] - Entity behavior types
    └── move_frame[] - Movement progress
```

## Conclusion

This classic frame-based architecture provides an excellent balance of performance, simplicity, and visual quality. By separating game logic (which operates on a grid) from visual presentation (which uses interpolation), we achieve both computational efficiency and smooth animation. The emergent behavior from simple entity rules creates engaging gameplay without complex AI or physics calculations.
