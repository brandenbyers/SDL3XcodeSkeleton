import Testing

@Suite("Collision System Tests")
struct CollisionTests {
    // Test are_directions_opposite function
    @Test("Opposite directions should be detected correctly")
    func testOppositeDirections() throws {
        // Test all possible direction pairs
        #expect(are_directions_opposite(DIR_RIGHT, DIR_LEFT))
        #expect(are_directions_opposite(DIR_LEFT, DIR_RIGHT))
        #expect(are_directions_opposite(DIR_UP, DIR_DOWN))
        #expect(are_directions_opposite(DIR_DOWN, DIR_UP))
        
        // Test non-opposite directions
        #expect(!are_directions_opposite(DIR_RIGHT, DIR_UP))
        #expect(!are_directions_opposite(DIR_LEFT, DIR_DOWN))
        #expect(!are_directions_opposite(DIR_UP, DIR_LEFT))
        #expect(!are_directions_opposite(DIR_DOWN, DIR_RIGHT))
        
        // Test with DIR_NONE
        #expect(!are_directions_opposite(DIR_RIGHT, DIR_NONE))
        #expect(!are_directions_opposite(DIR_NONE, DIR_LEFT))
        #expect(!are_directions_opposite(DIR_NONE, DIR_NONE))
    }
    
    @Test("New direction should be calculated correctly based on pivot directions and entity rules")
    func testGetNewDirection() throws {
        // Test continuing in same direction if possible
        let continueStraight = UInt8(PIVOT_RIGHT | PIVOT_LEFT | PIVOT_UP | PIVOT_DOWN)
        #expect(get_new_direction(continueStraight, 0, DIR_RIGHT) == DIR_RIGHT)
        #expect(get_new_direction(continueStraight, 0, DIR_LEFT) == DIR_LEFT)
        #expect(get_new_direction(continueStraight, 0, DIR_UP) == DIR_UP)
        #expect(get_new_direction(continueStraight, 0, DIR_DOWN) == DIR_DOWN)
        
        // Test RULE_REVERSE - should prefer opposite direction
        let noForward = UInt8(PIVOT_LEFT | PIVOT_UP | PIVOT_DOWN) // No right
        #expect(get_new_direction(noForward, UInt8(RULE_REVERSE), DIR_RIGHT) == DIR_LEFT)
        
        // Test RULE_CLOCKWISE - should prefer clockwise turn
        let noForwardOrReverse = UInt8(PIVOT_UP | PIVOT_DOWN) // No left or right
        #expect(get_new_direction(noForwardOrReverse, UInt8(RULE_CLOCKWISE), DIR_RIGHT) == DIR_DOWN)
        
        // Test RULE_COUNTER_CW - should prefer counter-clockwise turn
        #expect(get_new_direction(noForwardOrReverse, UInt8(RULE_COUNTER_CW), DIR_RIGHT) == DIR_UP)
        
        // Test fallback when no preferred direction is available
        let onlyUp = UInt8(PIVOT_UP)
        #expect(get_new_direction(onlyUp, UInt8(RULE_CLOCKWISE), DIR_RIGHT) == DIR_UP)
    }
}
