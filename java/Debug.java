import com.golomb.*;
import java.util.Arrays;

public class Debug {
    public static void main(String[] args) {
        GolombRuler ruler9 = GolombLUT.getOptimalRuler(9);
        System.out.println("Ruler 9: " + ruler9);
        System.out.println("Valid: " + ruler9.isValid());
        int[] distances = ruler9.calculateDistances();
        System.out.println("Distances: " + Arrays.toString(distances));
        
        // Check for duplicates
        for (int i = 1; i < distances.length; i++) {
            if (distances[i] == distances[i-1]) {
                System.out.println("Duplicate distance found: " + distances[i]);
            }
        }
    }
}
