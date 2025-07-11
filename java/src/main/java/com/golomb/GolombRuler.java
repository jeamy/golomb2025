package com.golomb;

import java.util.Arrays;
import java.util.Objects;

/**
 * Representation of a Golomb ruler - a set of marks where every pair defines a unique distance.
 */
public record GolombRuler(int length, int marks, int[] positions) {
    
    public GolombRuler {
        Objects.requireNonNull(positions, "Positions cannot be null");
        if (marks != positions.length) {
            throw new IllegalArgumentException("Mark count must match positions array length");
        }
        if (marks > 0 && positions[0] != 0) {
            throw new IllegalArgumentException("First position must be 0");
        }
        if (marks > 1 && positions[marks - 1] != length) {
            throw new IllegalArgumentException("Last position must equal ruler length");
        }
        
        // Ensure positions are sorted
        for (int i = 1; i < positions.length; i++) {
            if (positions[i] <= positions[i - 1]) {
                throw new IllegalArgumentException("Positions must be strictly increasing");
            }
        }
    }
    
    /**
     * Creates a new ruler with the given positions.
     * The length is automatically set to the last position.
     */
    public static GolombRuler of(int... positions) {
        if (positions.length == 0) {
            return new GolombRuler(0, 0, new int[0]);
        }
        
        int[] sortedPositions = Arrays.copyOf(positions, positions.length);
        Arrays.sort(sortedPositions);
        
        return new GolombRuler(
            sortedPositions[sortedPositions.length - 1],
            sortedPositions.length,
            sortedPositions
        );
    }
    
    /**
     * Calculates all pairwise distances between marks.
     */
    public int[] calculateDistances() {
        if (marks < 2) {
            return new int[0];
        }
        
        int[] distances = new int[marks * (marks - 1) / 2];
        int index = 0;
        
        for (int i = 0; i < marks; i++) {
            for (int j = i + 1; j < marks; j++) {
                distances[index++] = positions[j] - positions[i];
            }
        }
        
        Arrays.sort(distances);
        return distances;
    }
    
    /**
     * Checks if this ruler is valid (all distances are unique).
     */
    public boolean isValid() {
        int[] distances = calculateDistances();
        for (int i = 1; i < distances.length; i++) {
            if (distances[i] == distances[i - 1]) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * Finds missing distances from 1 to the ruler length.
     */
    public int[] findMissingDistances() {
        boolean[] present = new boolean[length + 1];
        int[] distances = calculateDistances();
        
        for (int distance : distances) {
            if (distance <= length) {
                present[distance] = true;
            }
        }
        
        return java.util.stream.IntStream.rangeClosed(1, length)
            .filter(d -> !present[d])
            .toArray();
    }
    
    @Override
    public String toString() {
        return "GolombRuler{length=%d, marks=%d, positions=%s}"
            .formatted(length, marks, Arrays.toString(positions));
    }
    
    /**
     * Returns a formatted string representation suitable for output files.
     */
    public String toFormattedString() {
        StringBuilder sb = new StringBuilder();
        sb.append("length=").append(length).append("\n");
        sb.append("marks=").append(marks).append("\n");
        sb.append("positions=");
        for (int i = 0; i < positions.length; i++) {
            if (i > 0) sb.append(" ");
            sb.append(positions[i]);
        }
        sb.append("\n");
        
        int[] distances = calculateDistances();
        sb.append("distances=");
        for (int i = 0; i < distances.length; i++) {
            if (i > 0) sb.append(" ");
            sb.append(distances[i]);
        }
        sb.append("\n");
        
        int[] missing = findMissingDistances();
        sb.append("missing=");
        for (int i = 0; i < missing.length; i++) {
            if (i > 0) sb.append(" ");
            sb.append(missing[i]);
        }
        sb.append("\n");
        
        return sb.toString();
    }
}
