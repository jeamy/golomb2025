package com.golomb;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import static org.junit.jupiter.api.Assertions.*;

class GolombSolverTest {
    
    @Test
    @Timeout(5)
    void testSolveSmallRuler() {
        GolombSolver solver = new GolombSolver(false, false);
        
        // Get the optimal length for 4 marks first
        GolombRuler optimal = GolombLUT.getOptimalRuler(4);
        assertNotNull(optimal);
        
        // Search with the optimal length
        GolombRuler result = solver.solve(4, optimal.length());
        
        assertNotNull(result);
        assertEquals(4, result.marks());
        assertTrue(result.isValid());
        assertEquals(optimal.length(), result.length());
    }
    
    @Test
    @Timeout(10)
    void testSolveWithParallel() {
        GolombSolver solver = new GolombSolver(false, true);
        GolombRuler result = solver.solve(5, 15);
        
        assertNotNull(result);
        assertEquals(5, result.marks());
        assertTrue(result.isValid());
    }
    
    @Test
    void testSolveImpossible() {
        GolombSolver solver = new GolombSolver(false, false);
        GolombRuler result = solver.solve(4, 5); // Too short for 4 marks
        
        assertNull(result);
    }
    
    @Test
    void testInvalidInput() {
        GolombSolver solver = new GolombSolver(false, false);
        
        assertThrows(IllegalArgumentException.class, () -> solver.solve(1, 10));
        assertThrows(IllegalArgumentException.class, () -> solver.solve(33, 10));
        assertThrows(IllegalArgumentException.class, () -> solver.solve(4, 601));
    }
    
    @Test
    @Timeout(2)
    void testCancel() throws InterruptedException {
        GolombSolver solver = new GolombSolver(false, false);
        
        // Start a search in a separate thread
        Thread searchThread = new Thread(() -> {
            solver.solve(10, 100); // This should take a while
        });
        
        searchThread.start();
        Thread.sleep(100); // Let it start
        solver.cancel();
        searchThread.join(1000); // Wait up to 1 second for cancellation
        
        assertFalse(searchThread.isAlive());
    }
    
    @Test
    @Timeout(3)
    void testKnownOptimalRulers() {
        GolombSolver solver = new GolombSolver(false, false);
        
        // Test some small known optimal rulers
        for (int marks = 2; marks <= 6; marks++) {
            GolombRuler optimal = GolombLUT.getOptimalRuler(marks);
            assertNotNull(optimal, "No optimal ruler known for " + marks + " marks");
            
            GolombRuler found = solver.solve(marks, optimal.length());
            assertNotNull(found, "Could not find ruler for " + marks + " marks");
            assertEquals(optimal.length(), found.length(), 
                "Found ruler length doesn't match optimal for " + marks + " marks");
        }
    }
}
