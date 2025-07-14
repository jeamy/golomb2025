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
        m.insert(18, vec![0, 2, 10, 22, 53, 56, 82, 83, 89, 98, 130, 148, 155, 175, 177, 199, 201, 216]);
        m.insert(19, vec![0, 1, 6, 25, 32, 72, 100, 108, 120, 130, 153, 169, 187, 190, 204, 231, 233, 242, 246]);
        m.insert(20, vec![0, 1, 8, 11, 37, 49, 58, 77, 94, 116, 135, 154, 170, 203, 206, 227, 232, 270, 273, 283]);
        m.insert(21, vec![0, 10, 21, 27, 59, 85, 93, 111, 135, 137, 149, 186, 199, 207, 229, 257, 279, 283, 291, 309, 333]);
        m.insert(22, vec![0, 4, 15, 23, 31, 57, 78, 104, 122, 139, 149, 185, 202, 211, 246, 260, 271, 292, 311, 329, 347, 356]);
        m.insert(23, vec![0, 2, 11, 29, 62, 77, 86, 111, 123, 132, 150, 161, 173, 189, 199, 222, 272, 279, 293, 330, 342, 365, 372]);
        m.insert(24, vec![0, 1, 17, 21, 41, 73, 77, 88, 98, 118, 152, 177, 191, 206, 238, 252, 278, 296, 329, 349, 387, 400, 413, 425]);
        m.insert(25, vec![0, 6, 35, 51, 61, 84, 96, 122, 127, 148, 154, 175, 191, 205, 242, 278, 296, 307, 338, 348, 382, 398, 417, 431, 480]);
        m.insert(26, vec![0, 17, 30, 56, 81, 96, 126, 142, 152, 180, 181, 203, 230, 256, 281, 289, 317, 344, 358, 384, 403, 425, 445, 466, 475, 492]);
        m.insert(27, vec![0, 3, 16, 39, 71, 83, 104, 130, 151, 167, 198, 204, 220, 242, 288, 304, 324, 359, 381, 399, 452, 475, 492, 499, 526, 532, 553]);
        m.insert(28, vec![0, 2, 8, 31, 41, 53, 92, 117, 155, 190, 203, 209, 230, 245, 257, 272, 292, 304, 337, 377, 386, 434, 457, 483, 507, 523, 553, 585]);
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
