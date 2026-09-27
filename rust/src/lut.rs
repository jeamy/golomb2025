//! Look-Up Table (LUT) for known optimal Golomb rulers

use std::collections::HashMap;
use lazy_static::lazy_static;

lazy_static! {
    /// Look-up table for optimal ruler lengths
    static ref OPTIMAL_LENGTHS: HashMap<usize, usize> = {
        let mut m = HashMap::new();
        m.insert(1, 0);
        m.insert(2, 1);
        m.insert(3, 3);
        m.insert(4, 6);
        m.insert(5, 11);
        m.insert(6, 17);
        m.insert(7, 25);
        m.insert(8, 34);
        m.insert(9, 44);
        m.insert(10, 55);
        m.insert(11, 72);
        m.insert(12, 85);
        m.insert(13, 106);
        m.insert(14, 127);
        m.insert(15, 151);
        m.insert(16, 177);
        m.insert(17, 199);
        m.insert(18, 216);
        m.insert(19, 246);
        m.insert(20, 283);
        m.insert(21, 333);
        m.insert(22, 356);
        m.insert(23, 372);
        m.insert(24, 425);
        m.insert(25, 480);
        m.insert(26, 492);
        m.insert(27, 553);
        m.insert(28, 585);
        m
    };

    /// Look-up table for known optimal rulers
    static ref RULERS: HashMap<usize, Vec<usize>> = {
        let mut m = HashMap::new();
        m.insert(1, vec![0]);
        m.insert(2, vec![0, 1]);
        m.insert(3, vec![0, 1, 3]);
        m.insert(4, vec![0, 1, 4, 6]);
        m.insert(5, vec![0, 1, 4, 9, 11]);
        m.insert(6, vec![0, 1, 4, 10, 12, 17]);
        m.insert(7, vec![0, 1, 4, 10, 18, 23, 25]);
        m.insert(8, vec![0, 1, 4, 9, 15, 22, 32, 34]);
        m.insert(9, vec![0, 1, 5, 12, 25, 27, 35, 41, 44]);
        m.insert(10, vec![0, 1, 6, 10, 23, 26, 34, 41, 53, 55]);
        m.insert(11, vec![0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72]);
        m.insert(12, vec![0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85]);
        m.insert(13, vec![0, 2, 5, 25, 37, 43, 59, 70, 85, 89, 98, 99, 106]);
        m.insert(14, vec![0, 4, 6, 20, 35, 52, 59, 77, 78, 86, 89, 99, 122, 127]);
        m.insert(15, vec![0, 4, 20, 30, 57, 59, 62, 76, 100, 111, 123, 136, 144, 145, 151]);
        m.insert(16, vec![0, 1, 4, 11, 26, 32, 56, 68, 76, 115, 117, 134, 150, 163, 168, 177]);
        m.insert(17, vec![0, 5, 7, 17, 52, 56, 67, 80, 81, 100, 122, 138, 159, 165, 168, 191, 199]);
        m.insert(18, vec![0, 2, 10, 22, 53, 56, 82, 83, 89, 98, 130, 148, 153, 167, 188, 192, 205, 216]);
        m.insert(19, vec![0, 1, 6, 25, 32, 72, 100, 108, 120, 130, 153, 169, 187, 190, 204, 231, 233, 242, 246]);
        m.insert(20, vec![0, 1, 8, 11, 68, 77, 94, 116, 121, 156, 158, 179, 194, 208, 212, 228, 240, 253, 259, 283]);
        m.insert(21, vec![0, 2, 24, 56, 77, 82, 83, 95, 129, 144, 179, 186, 195, 255, 265, 285, 293, 296, 310, 329, 333]);
        m.insert(22, vec![0, 1, 9, 14, 43, 70, 106, 122, 124, 128, 159, 179, 204, 223, 253, 263, 270, 291, 330, 341, 353, 356]);
        m.insert(23, vec![0, 3, 7, 17, 61, 66, 91, 99, 114, 159, 171, 199, 200, 226, 235, 246, 277, 316, 329, 348, 350, 366, 372]);
        m.insert(24, vec![0, 9, 33, 37, 38, 97, 122, 129, 140, 142, 152, 191, 205, 208, 252, 278, 286, 326, 332, 353, 368, 384, 403, 425]);
        m.insert(25, vec![0, 12, 29, 39, 72, 91, 146, 157, 160, 161, 166, 191, 207, 214, 258, 290, 316, 354, 372, 394, 396, 431, 459, 467, 480]);
        m.insert(26, vec![0, 1, 33, 83, 104, 110, 124, 163, 185, 200, 203, 249, 251, 258, 314, 318, 343, 356, 386, 430, 440, 456, 464, 475, 487, 492]);
        m.insert(27, vec![0, 3, 15, 41, 66, 95, 97, 106, 142, 152, 220, 221, 225, 242, 295, 330, 338, 354, 382, 388, 402, 415, 486, 504, 523, 546, 553]);
        m.insert(28, vec![0, 3, 15, 41, 66, 95, 97, 106, 142, 152, 220, 221, 225, 242, 295, 330, 338, 354, 382, 388, 402, 415, 486, 504, 523, 546, 553, 585]);
        m
    };
}

/// Returns the optimal length for a ruler with the given number of marks, if known
pub fn get_optimal_length(marks: usize) -> Option<usize> {
    OPTIMAL_LENGTHS.get(&marks).copied()
}

/// Returns the known optimal ruler for the given number of marks, if available
pub fn get_optimal_ruler(marks: usize) -> Option<Vec<usize>> {
    RULERS.get(&marks).cloned()
}

/// Returns true if the ruler with the given number of marks is known to be optimal at the given length
#[allow(dead_code)]
pub fn is_optimal_length(marks: usize, length: usize) -> bool {
    OPTIMAL_LENGTHS.get(&marks).map_or(false, |&l| l == length)
}
