package com.golomb;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class GolombRulerTest {
    
    @Test
    void testValidRulerCreation() {
        GolombRuler ruler = new GolombRuler(6, 4, new int[]{0, 1, 4, 6});
        assertEquals(6, ruler.length());
        assertEquals(4, ruler.marks());
        assertArrayEquals(new int[]{0, 1, 4, 6}, ruler.positions());
    }
    
    @Test
    void testRulerOf() {
        GolombRuler ruler = GolombRuler.of(0, 1, 4, 6);
        assertEquals(6, ruler.length());
        assertEquals(4, ruler.marks());
        assertArrayEquals(new int[]{0, 1, 4, 6}, ruler.positions());
    }
    
    @Test
    void testRulerOfWithUnsortedPositions() {
        GolombRuler ruler = GolombRuler.of(6, 0, 4, 1);
        assertEquals(6, ruler.length());
        assertEquals(4, ruler.marks());
        assertArrayEquals(new int[]{0, 1, 4, 6}, ruler.positions());
    }
    
    @Test
    void testCalculateDistances() {
        GolombRuler ruler = GolombRuler.of(0, 1, 4, 6);
        int[] distances = ruler.calculateDistances();
        assertArrayEquals(new int[]{1, 2, 3, 4, 5, 6}, distances);
    }
    
    @Test
    void testIsValid() {
        GolombRuler validRuler = GolombRuler.of(0, 1, 4, 6);
        assertTrue(validRuler.isValid());
        
        // This would be invalid if we could create it (duplicate distance of 1)
        // But our constructor ensures sorted positions, so we can't easily test invalid rulers
    }
    
    @Test
    void testFindMissingDistances() {
        GolombRuler ruler = GolombRuler.of(0, 1, 4, 6);
        int[] missing = ruler.findMissingDistances();
        assertEquals(0, missing.length); // This is a perfect ruler with no missing distances
    }
    
    @Test
    void testInvalidRulerCreation() {
        // Test first position not zero
        assertThrows(IllegalArgumentException.class, () -> 
            new GolombRuler(6, 4, new int[]{1, 2, 4, 6}));
        
        // Test last position not matching length
        assertThrows(IllegalArgumentException.class, () -> 
            new GolombRuler(7, 4, new int[]{0, 1, 4, 6}));
        
        // Test mark count mismatch
        assertThrows(IllegalArgumentException.class, () -> 
            new GolombRuler(6, 3, new int[]{0, 1, 4, 6}));
        
        // Test unsorted positions
        assertThrows(IllegalArgumentException.class, () -> 
            new GolombRuler(6, 4, new int[]{0, 4, 1, 6}));
    }
    
    @Test
    void testEmptyRuler() {
        GolombRuler ruler = GolombRuler.of();
        assertEquals(0, ruler.length());
        assertEquals(0, ruler.marks());
        assertEquals(0, ruler.positions().length);
    }
    
    @Test
    void testSingleMarkRuler() {
        GolombRuler ruler = GolombRuler.of(0);
        assertEquals(0, ruler.length());
        assertEquals(1, ruler.marks());
        assertArrayEquals(new int[]{0}, ruler.positions());
    }
    
    @Test
    void testToString() {
        GolombRuler ruler = GolombRuler.of(0, 1, 4, 6);
        String str = ruler.toString();
        assertTrue(str.contains("length=6"));
        assertTrue(str.contains("marks=4"));
        assertTrue(str.contains("[0, 1, 4, 6]"));
    }
    
    @Test
    void testToFormattedString() {
        GolombRuler ruler = GolombRuler.of(0, 1, 4, 6);
        String formatted = ruler.toFormattedString();
        assertTrue(formatted.contains("length=6"));
        assertTrue(formatted.contains("marks=4"));
        assertTrue(formatted.contains("positions=0 1 4 6"));
        assertTrue(formatted.contains("distances=1 2 3 4 5 6"));
    }
}
