package com.golomb;

import java.util.HashMap;
import java.util.Map;

/**
 * Look-up table for known optimal Golomb rulers.
 * Contains the best known rulers for various mark counts.
 */
public class GolombLUT {
    
    private static final Map<Integer, GolombRuler> OPTIMAL_RULERS = new HashMap<>();
    
    static {
        // Initialize with known optimal rulers from the C implementation
        // Format: marks -> ruler positions
        OPTIMAL_RULERS.put(2, GolombRuler.of(0, 1));
        OPTIMAL_RULERS.put(3, GolombRuler.of(0, 1, 3));
        OPTIMAL_RULERS.put(4, GolombRuler.of(0, 1, 4, 6));
        OPTIMAL_RULERS.put(5, GolombRuler.of(0, 1, 4, 9, 11));
        OPTIMAL_RULERS.put(6, GolombRuler.of(0, 1, 4, 10, 12, 17));
        OPTIMAL_RULERS.put(7, GolombRuler.of(0, 1, 4, 10, 18, 23, 25));
        OPTIMAL_RULERS.put(8, GolombRuler.of(0, 1, 4, 9, 15, 22, 32, 34));
        OPTIMAL_RULERS.put(9, GolombRuler.of(0, 1, 5, 12, 25, 27, 35, 41, 44));
        OPTIMAL_RULERS.put(10, GolombRuler.of(0, 1, 6, 10, 23, 26, 34, 41, 53, 55));
        OPTIMAL_RULERS.put(11, GolombRuler.of(0, 1, 4, 13, 28, 33, 47, 54, 64, 70, 72));
        OPTIMAL_RULERS.put(12, GolombRuler.of(0, 2, 6, 24, 29, 40, 43, 55, 68, 75, 76, 85));
        OPTIMAL_RULERS.put(13, GolombRuler.of(0, 2, 5, 25, 37, 43, 59, 70, 85, 89, 98, 99, 106));
        OPTIMAL_RULERS.put(14, GolombRuler.of(0, 4, 6, 20, 35, 52, 59, 77, 78, 86, 89, 99, 122, 127));
        OPTIMAL_RULERS.put(15, GolombRuler.of(0, 4, 20, 30, 57, 59, 62, 76, 100, 111, 123, 136, 144, 145, 151));
        OPTIMAL_RULERS.put(16, GolombRuler.of(0, 1, 4, 11, 26, 32, 56, 68, 76, 115, 117, 134, 150, 163, 168, 177));
        OPTIMAL_RULERS.put(17, GolombRuler.of(0, 5, 7, 17, 52, 56, 67, 80, 81, 100, 122, 138, 159, 165, 168, 191, 199));
        OPTIMAL_RULERS.put(18, GolombRuler.of(0, 2, 10, 22, 53, 56, 82, 83, 89, 98, 130, 148, 153, 167, 188, 192, 205, 216));
        OPTIMAL_RULERS.put(19, GolombRuler.of(0, 1, 6, 25, 32, 72, 100, 108, 120, 130, 153, 169, 187, 190, 204, 231, 233, 242, 246));
        OPTIMAL_RULERS.put(20, GolombRuler.of(0, 1, 8, 11, 68, 77, 94, 116, 121, 156, 158, 179, 194, 208, 212, 228, 240, 253, 259, 283));
        OPTIMAL_RULERS.put(21, GolombRuler.of(0, 2, 24, 56, 77, 82, 83, 95, 129, 144, 179, 186, 195, 255, 265, 285, 293, 296, 310, 329, 333));
        OPTIMAL_RULERS.put(22, GolombRuler.of(0, 1, 9, 14, 43, 70, 106, 122, 124, 128, 159, 179, 204, 223, 253, 263, 270, 291, 330, 341, 353, 356));
        OPTIMAL_RULERS.put(23, GolombRuler.of(0, 3, 7, 17, 61, 66, 91, 99, 114, 159, 171, 199, 200, 226, 235, 246, 277, 316, 329, 348, 350, 366, 372));
        OPTIMAL_RULERS.put(24, GolombRuler.of(0, 9, 33, 37, 38, 97, 122, 129, 140, 142, 152, 191, 205, 208, 252, 278, 286, 326, 332, 353, 368, 384, 403, 425));
        OPTIMAL_RULERS.put(25, GolombRuler.of(0, 12, 29, 39, 72, 91, 146, 157, 160, 161, 166, 191, 207, 214, 258, 290, 316, 354, 372, 394, 396, 431, 459, 467, 480));
        OPTIMAL_RULERS.put(26, GolombRuler.of(0, 1, 33, 83, 104, 110, 124, 163, 185, 200, 203, 249, 251, 258, 314, 318, 343, 356, 386, 430, 440, 456, 464, 475, 487, 492));
        OPTIMAL_RULERS.put(27, GolombRuler.of(0, 3, 15, 41, 66, 95, 97, 106, 142, 152, 220, 221, 225, 242, 295, 330, 338, 354, 382, 388, 402, 415, 486, 504, 523, 546, 553));
        OPTIMAL_RULERS.put(28, GolombRuler.of(0, 3, 15, 41, 66, 95, 97, 106, 142, 152, 220, 221, 225, 242, 295, 330, 338, 354, 382, 388, 402, 415, 486, 504, 523, 546, 553, 585));
    }
    
    /**
     * Returns the optimal ruler for the given number of marks, or null if unknown.
     */
    public static GolombRuler getOptimalRuler(int marks) {
        return OPTIMAL_RULERS.get(marks);
    }
    
    /**
     * Returns the optimal ruler for the given length, or null if unknown.
     */
    public static GolombRuler getOptimalRulerByLength(int length) {
        return OPTIMAL_RULERS.values().stream()
            .filter(ruler -> ruler.length() == length)
            .findFirst()
            .orElse(null);
    }
    
    /**
     * Checks if a ruler is optimal by comparing with the known optimal ruler.
     */
    public static boolean isOptimal(GolombRuler ruler) {
        GolombRuler optimal = getOptimalRuler(ruler.marks());
        return optimal != null && ruler.length() == optimal.length();
    }
    
    /**
     * Returns the optimal length for the given number of marks, or -1 if unknown.
     */
    public static int getOptimalLength(int marks) {
        GolombRuler optimal = getOptimalRuler(marks);
        return optimal != null ? optimal.length() : -1;
    }
    
    /**
     * Returns all known optimal rulers.
     */
    public static Map<Integer, GolombRuler> getAllOptimalRulers() {
        return new HashMap<>(OPTIMAL_RULERS);
    }
    
    /**
     * Returns the maximum number of marks for which we have optimal rulers.
     */
    public static int getMaxKnownMarks() {
        return OPTIMAL_RULERS.keySet().stream().mapToInt(Integer::intValue).max().orElse(0);
    }
}
