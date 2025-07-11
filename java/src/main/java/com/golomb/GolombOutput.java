package com.golomb;

import java.util.Arrays;
import java.util.Map;

/**
 * Utility class to output Golomb ruler information similar to the C implementation.
 */
public class GolombOutput {
    
    public static void main(String[] args) {
        System.out.println("Optimal Golomb Rulers (from Java implementation):");
        System.out.println("=================================================");
        
        Map<Integer, GolombRuler> allRulers = GolombLUT.getAllOptimalRulers();
        
        for (int marks = 1; marks <= GolombLUT.getMaxKnownMarks(); marks++) {
            GolombRuler ruler = GolombLUT.getOptimalRuler(marks);
            if (ruler != null) {
                System.out.printf("n=%-2d length=%-3d marks=%s%n", 
                    marks, 
                    ruler.length(), 
                    Arrays.toString(ruler.positions()));
                
                // Print distances for verification
                int[] distances = ruler.calculateDistances();
                System.out.printf("Distances (%d): %s%n", 
                    distances.length, 
                    Arrays.toString(distances));
                System.out.println();
            }
        }
    }
}
