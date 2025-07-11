#!/usr/bin/env ruby
# Look-Up Table (LUT) for Golomb Ruler Finder - Ruby Implementation

# This module contains the lookup tables for known optimal rulers
module GolombLUT
  # Optimal ruler lengths
  OPTIMAL_LENGTHS = {
    1 => 0,
    2 => 1,
    3 => 3,
    4 => 6,
    5 => 11,
    6 => 17,
    7 => 25,
    8 => 34,
    9 => 44,
    10 => 55,
    11 => 72,
    12 => 85,
    13 => 106,
    14 => 127,
    15 => 151,
    16 => 177,
    17 => 199,
    18 => 216,
    19 => 246,
    20 => 283,
    21 => 333,
    22 => 356,
    23 => 372,
    24 => 425,
    25 => 480,
    26 => 492,
    27 => 553,
    28 => 585
  }
  
  # Known optimal rulers (selected subset of key rulers)
  RULERS = {
    5 => [0, 1, 4, 9, 11],
    6 => [0, 1, 4, 10, 12, 17],
    7 => [0, 1, 4, 10, 18, 23, 25],
    8 => [0, 1, 4, 9, 15, 22, 32, 34],
    9 => [0, 1, 5, 12, 25, 27, 35, 41, 44],
    10 => [0, 1, 6, 10, 23, 26, 34, 41, 53, 55],
    11 => [0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72],
    12 => [0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85]
  }
end
