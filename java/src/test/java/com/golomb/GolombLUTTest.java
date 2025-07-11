package com.golomb;

import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class GolombLUTTest {
    
    @Test
    void testGetOptimalRuler() {
        GolombRuler ruler4 = GolombLUT.getOptimalRuler(4);
        assertNotNull(ruler4);
        assertEquals(4, ruler4.marks());
        assertEquals(6, ruler4.length());
        assertArrayEquals(new int[]{0, 1, 4, 6}, ruler4.positions());
        
        GolombRuler ruler5 = GolombLUT.getOptimalRuler(5);
        assertNotNull(ruler5);
        assertEquals(5, ruler5.marks());
        assertEquals(11, ruler5.length());
        assertArrayEquals(new int[]{0, 1, 4, 9, 11}, ruler5.positions());
    }
    
    @Test
    void testGetOptimalRulerUnknown() {
        GolombRuler ruler = GolombLUT.getOptimalRuler(100);
        assertNull(ruler);
    }
    
    @Test
    void testGetOptimalRulerByLength() {
        GolombRuler ruler = GolombLUT.getOptimalRulerByLength(6);
        assertNotNull(ruler);
        assertEquals(4, ruler.marks());
        
        GolombRuler unknownRuler = GolombLUT.getOptimalRulerByLength(1000);
        assertNull(unknownRuler);
    }
    
    @Test
    void testIsOptimal() {
        GolombRuler optimal = GolombRuler.of(0, 1, 4, 6);
        assertTrue(GolombLUT.isOptimal(optimal));
        
        GolombRuler suboptimal = GolombRuler.of(0, 1, 4, 7);
        assertFalse(GolombLUT.isOptimal(suboptimal));
        
        GolombRuler unknown = GolombRuler.of(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 100);
        assertFalse(GolombLUT.isOptimal(unknown)); // Unknown ruler
    }
    
    @Test
    void testGetOptimalLength() {
        assertEquals(6, GolombLUT.getOptimalLength(4));
        assertEquals(11, GolombLUT.getOptimalLength(5));
        assertEquals(-1, GolombLUT.getOptimalLength(100));
    }
    
    @Test
    void testGetMaxKnownMarks() {
        int maxMarks = GolombLUT.getMaxKnownMarks();
        assertTrue(maxMarks >= 20); // We should have at least 20 known rulers
        
        // Verify that we actually have a ruler for the max marks
        GolombRuler maxRuler = GolombLUT.getOptimalRuler(maxMarks);
        assertNotNull(maxRuler);
    }
    
    @Test
    void testGetAllOptimalRulers() {
        var allRulers = GolombLUT.getAllOptimalRulers();
        assertFalse(allRulers.isEmpty());
        
        // Verify some known rulers are present
        assertTrue(allRulers.containsKey(4));
        assertTrue(allRulers.containsKey(5));
        
        // Verify all rulers are valid
        for (var entry : allRulers.entrySet()) {
            int marks = entry.getKey();
            GolombRuler ruler = entry.getValue();
            
            assertEquals(marks, ruler.marks());
            assertTrue(ruler.isValid());
            assertEquals(0, ruler.positions()[0]); // First position should be 0
        }
    }
    
    @Test
    void testRulerValidity() {
        // Test that all stored rulers are actually valid Golomb rulers
        for (int marks = 2; marks <= GolombLUT.getMaxKnownMarks(); marks++) {
            GolombRuler ruler = GolombLUT.getOptimalRuler(marks);
            if (ruler != null) {
                assertTrue(ruler.isValid(), "Ruler for " + marks + " marks is not valid");
                assertEquals(marks, ruler.marks());
                assertEquals(0, ruler.positions()[0]);
                assertEquals(ruler.length(), ruler.positions()[ruler.marks() - 1]);
            }
        }
    }
}
