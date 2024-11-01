//
//  SDL3SkeletonTests.swift
//  SDL3SkeletonTests
//
//  Created by Branden Byers on 11/1/24.
//

import Testing

struct SDL3SkeletonTests {

    @Test func example() async throws {
        let result = addFromC(1, 1)
        #expect(result == 2)
    }

}
