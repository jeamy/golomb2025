package com.golomb;

import java.util.BitSet;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.RecursiveTask;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Solver for finding optimal Golomb rulers using branch-and-bound search.
 */
public class GolombSolver {
    
    private static final int MAX_MARKS = 32;
    private static final int MAX_LENGTH = 600;
    
    private final boolean verbose;
    private final boolean useParallel;
    private volatile AtomicBoolean cancelled = new AtomicBoolean(false);
    
    public GolombSolver(boolean verbose, boolean useParallel) {
        this.verbose = verbose;
        this.useParallel = useParallel;
    }
    
    /**
     * Attempts to find a Golomb ruler with the given number of marks and maximum length.
     */
    public GolombRuler solve(int marks, int maxLength) {
        if (marks < 2 || marks > MAX_MARKS) {
            throw new IllegalArgumentException("Marks must be between 2 and " + MAX_MARKS);
        }
        if (maxLength > MAX_LENGTH) {
            throw new IllegalArgumentException("Max length cannot exceed " + MAX_LENGTH);
        }
        
        cancelled.set(false);
        
        if (useParallel && marks > 3) {
            return solveParallel(marks, maxLength);
        } else {
            return solveSingle(marks, maxLength);
        }
    }
    
    private GolombRuler solveSingle(int marks, int maxLength) {
        int[] positions = new int[marks];
        BitSet distanceSet = new BitSet(maxLength + 1);
        
        positions[0] = 0;
        
        if (dfs(1, marks, maxLength, positions, distanceSet)) {
            return new GolombRuler(positions[marks - 1], marks, positions.clone());
        }
        
        return null;
    }
    
    private GolombRuler solveParallel(int marks, int maxLength) {
        AtomicReference<GolombRuler> result = new AtomicReference<>();
        
        try (ForkJoinPool pool = new ForkJoinPool()) {
            int half = maxLength / 2; // Symmetry breaking
            
            pool.submit(() -> {
                java.util.stream.IntStream.rangeClosed(1, half)
                    .parallel()
                    .forEach(second -> {
                        if (cancelled.get() || result.get() != null) return;
                        
                        for (int third = second + 1; third <= maxLength - (marks - 2); third++) {
                            if (cancelled.get() || result.get() != null) return;
                            
                            int[] positions = new int[marks];
                            BitSet distanceSet = new BitSet(maxLength + 1);
                            
                            positions[0] = 0;
                            positions[1] = second;
                            positions[2] = third;
                            
                            // Check initial distances
                            int d12 = second;
                            int d13 = third;
                            int d23 = third - second;
                            
                            if (d12 == d13 || d12 == d23 || d13 == d23) continue;
                            
                            distanceSet.set(d12);
                            distanceSet.set(d13);
                            distanceSet.set(d23);
                            
                            if (dfs(3, marks, maxLength, positions, distanceSet)) {
                                GolombRuler ruler = new GolombRuler(
                                    positions[marks - 1], marks, positions.clone()
                                );
                                result.compareAndSet(null, ruler);
                                cancelled.set(true);
                                return;
                            }
                        }
                    });
            }).join();
        }
        
        return result.get();
    }
    
    /**
     * Recursive depth-first search with branch-and-bound pruning.
     */
    private boolean dfs(int depth, int marks, int maxLength, int[] positions, BitSet distanceSet) {
        if (cancelled.get()) return false;
        
        if (depth == marks) {
            return positions[marks - 1] == maxLength;
        }
        
        int lastPos = positions[depth - 1];
        
        // Pruning: check if we can fit remaining marks
        if (lastPos + (marks - depth) > maxLength) {
            return false;
        }
        
        int maxNext = maxLength - (marks - depth - 1);
        
        // Symmetry breaking for second position
        if (depth == 1) {
            int limit = maxLength / 2;
            if (limit < lastPos + 1) {
                limit = lastPos + 1;
            }
            if (maxNext > limit) {
                maxNext = limit;
            }
        }
        
        // Try each possible next position
        for (int next = lastPos + 1; next <= maxNext; next++) {
            if (cancelled.get()) return false;
            
            // Check if all new distances are unique
            boolean valid = true;
            int[] newDistances = new int[depth];
            
            for (int i = 0; i < depth; i++) {
                int distance = next - positions[i];
                newDistances[i] = distance;
                if (distanceSet.get(distance)) {
                    valid = false;
                    break;
                }
            }
            
            if (!valid) continue;
            
            // Set the new distances
            for (int distance : newDistances) {
                distanceSet.set(distance);
            }
            
            positions[depth] = next;
            
            if (verbose && depth <= 2) {
                System.out.printf("Trying depth %d, position %d%n", depth, next);
            }
            
            if (dfs(depth + 1, marks, maxLength, positions, distanceSet)) {
                return true;
            }
            
            // Backtrack: clear the distances
            for (int distance : newDistances) {
                distanceSet.clear(distance);
            }
        }
        
        return false;
    }
    
    /**
     * Cancels the current search operation.
     */
    public void cancel() {
        cancelled.set(true);
    }
    
    /**
     * Parallel search task for Fork-Join framework.
     */
    private static class SearchTask extends RecursiveTask<GolombRuler> {
        private final int marks;
        private final int maxLength;
        private final int startSecond;
        private final int endSecond;
        private final boolean verbose;
        private final AtomicBoolean cancelled;
        
        public SearchTask(int marks, int maxLength, int startSecond, int endSecond, 
                         boolean verbose, AtomicBoolean cancelled) {
            this.marks = marks;
            this.maxLength = maxLength;
            this.startSecond = startSecond;
            this.endSecond = endSecond;
            this.verbose = verbose;
            this.cancelled = cancelled;
        }
        
        @Override
        protected GolombRuler compute() {
            if (cancelled.get()) return null;
            
            if (endSecond - startSecond <= 10) {
                // Base case: search directly
                return searchRange(startSecond, endSecond);
            } else {
                // Divide and conquer
                int mid = (startSecond + endSecond) / 2;
                SearchTask left = new SearchTask(marks, maxLength, startSecond, mid, verbose, cancelled);
                SearchTask right = new SearchTask(marks, maxLength, mid + 1, endSecond, verbose, cancelled);
                
                left.fork();
                GolombRuler rightResult = right.compute();
                if (rightResult != null) {
                    cancelled.set(true);
                    return rightResult;
                }
                
                GolombRuler leftResult = left.join();
                return leftResult;
            }
        }
        
        private GolombRuler searchRange(int startSecond, int endSecond) {
            GolombSolver solver = new GolombSolver(false, false);
            solver.cancelled = this.cancelled;
            
            for (int second = startSecond; second <= endSecond; second++) {
                if (cancelled.get()) return null;
                
                for (int third = second + 1; third <= maxLength - (marks - 2); third++) {
                    if (cancelled.get()) return null;
                    
                    int[] positions = new int[marks];
                    BitSet distanceSet = new BitSet(maxLength + 1);
                    
                    positions[0] = 0;
                    positions[1] = second;
                    positions[2] = third;
                    
                    // Check initial distances
                    int d12 = second;
                    int d13 = third;
                    int d23 = third - second;
                    
                    if (d12 == d13 || d12 == d23 || d13 == d23) continue;
                    
                    distanceSet.set(d12);
                    distanceSet.set(d13);
                    distanceSet.set(d23);
                    
                    if (solver.dfs(3, marks, maxLength, positions, distanceSet)) {
                        return new GolombRuler(positions[marks - 1], marks, positions.clone());
                    }
                }
            }
            
            return null;
        }
    }
}
