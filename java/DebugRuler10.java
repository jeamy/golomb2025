import com.golomb.GolombLUT;
import com.golomb.GolombRuler;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

public class DebugRuler10 {
    public static void main(String[] args) {
        // Get the 10-mark ruler from LUT
        GolombRuler ruler10 = GolombLUT.getOptimalRuler(10);
        
        System.out.println("10-mark ruler positions: " + Arrays.toString(ruler10.positions()));
        System.out.println("Is valid according to GolombRuler.isValid(): " + ruler10.isValid());
        
        // Manual validation to find the issue
        int[] positions = ruler10.positions();
        System.out.println("Ruler length: " + ruler10.length());
        
        // Check if positions are strictly increasing
        boolean strictlyIncreasing = true;
        for (int i = 1; i < positions.length; i++) {
            if (positions[i] <= positions[i-1]) {
                strictlyIncreasing = false;
                System.out.println("Not strictly increasing at index " + i + ": " + 
                                  positions[i-1] + " followed by " + positions[i]);
            }
        }
        System.out.println("Positions are strictly increasing: " + strictlyIncreasing);
        
        // Check for duplicate distances
        Set<Integer> distances = new HashSet<>();
        boolean duplicateDistances = false;
        
        System.out.println("Distances:");
        for (int i = 0; i < positions.length; i++) {
            for (int j = i + 1; j < positions.length; j++) {
                int distance = positions[j] - positions[i];
                System.out.println("Distance between positions[" + i + "]=" + positions[i] + 
                                  " and positions[" + j + "]=" + positions[j] + " is " + distance);
                
                if (!distances.add(distance)) {
                    duplicateDistances = true;
                    System.out.println("DUPLICATE DISTANCE FOUND: " + distance);
                }
            }
        }
        
        System.out.println("Has duplicate distances: " + duplicateDistances);
        
        // Compare with C implementation
        int[] cImplementation = {0, 1, 4, 12, 25, 32, 41, 53, 54, 55};
        System.out.println("C implementation: " + Arrays.toString(cImplementation));
        System.out.println("Matches C implementation: " + Arrays.equals(positions, cImplementation));
        
        // Check if all optimal rulers are valid
        System.out.println("\nChecking all optimal rulers in LUT:");
        var allRulers = GolombLUT.getAllOptimalRulers();
        for (var entry : allRulers.entrySet()) {
            int marks = entry.getKey();
            GolombRuler ruler = entry.getValue();
            boolean isValid = ruler.isValid();
            if (!isValid) {
                System.out.println("Ruler for " + marks + " marks is NOT valid");
            }
        }
    }
}
