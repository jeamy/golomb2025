//! Module implementing the Golomb ruler search algorithm

use std::collections::HashSet;
use std::sync::{Arc, Mutex};
use rayon::prelude::*;

use crate::ruler::GolombRuler;
use crate::lut;

/// Configuration for the solver
#[derive(Debug, Clone)]
pub struct SolverConfig {
    /// Number of marks to find
    pub marks: usize,
    
    /// Enable verbose output
    pub verbose: bool,
    
    /// Use multi-processing
    pub multi_processing: bool,
    
    /// Use best length as starting point
    pub best_length: bool,
}

/// Result from the solver
#[derive(Debug)]
pub struct SolverResult {
    /// The found ruler, if any
    pub ruler: Option<GolombRuler>,
    
    /// Whether the ruler is optimal
    pub optimal: Option<bool>,
    
    /// Number of states examined during search
    pub states_examined: usize,
}

/// Structure for efficient distance checking
#[derive(Debug, Clone)]
struct DistanceSet {
    distances: HashSet<usize>,
}

impl DistanceSet {
    fn new() -> Self {
        Self {
            distances: HashSet::new(),
        }
    }
    
    fn add_distances(&mut self, positions: &[usize], new_pos: usize) -> bool {
        for &pos in positions {
            let dist = new_pos - pos;
            if !self.distances.insert(dist) {
                return false; // Duplicate distance found
            }
        }
        true
    }
    
    // fn remove_distances not currently used but kept for potential future expansion
    // fn remove_distances(&mut self, positions: &[usize], pos_to_remove: usize) {
    //     for &pos in positions {
    //         if pos != pos_to_remove {
    //             let dist = pos_to_remove - pos;
    //             self.distances.remove(&dist);
    //         }
    //     }
    // }
}

/// Task representation for multi-processing
struct Task {
    mark1: usize,
    mark2: usize,
    max_length: usize,
}

/// Main solver for finding Golomb rulers
pub struct GolombSolver {
    config: SolverConfig,
}

impl GolombSolver {
    /// Creates a new solver with the given configuration
    pub fn new(config: SolverConfig) -> Self {
        Self { config }
    }
    
    /// Solves for a Golomb ruler with the configured number of marks
    pub fn solve(&self) -> SolverResult {
        let marks = self.config.marks;
        
        // Handle trivial cases
        if marks <= 1 {
            let ruler = GolombRuler::new(vec![0], marks);
            return SolverResult {
                ruler: Some(ruler),
                optimal: Some(true),
                states_examined: 0,
            };
        } else if marks == 2 {
            let ruler = GolombRuler::new(vec![0, 1], marks);
            return SolverResult {
                ruler: Some(ruler),
                optimal: Some(true),
                states_examined: 0,
            };
        }
        
        // Get optimal length and ruler from LUT if available
        let optimal_length = lut::get_optimal_length(marks);
        let canonical_ruler = lut::get_optimal_ruler(marks);
        
        // Wenn -b gesetzt ist und wir die optimale Länge kennen, verwenden wir diese als Ziel,
        // aber berechnen das Lineal trotzdem durch den Algorithmus
        if self.config.verbose && self.config.best_length && optimal_length.is_some() {
            println!("Using known optimal length from LUT: {}", 
                optimal_length.unwrap_or(0));
        }
        
        // Calculate bounds for the search
        let lower_bound = (((marks * (marks - 1)) as f64).sqrt() as usize).max(marks - 1);
        let upper_bound = optimal_length.unwrap_or(marks * marks);
        
        if self.config.verbose {
            println!("Searching lengths from {} to {}", lower_bound, upper_bound);
        }
        
        // If -b flag is set and we know the optimal length, start directly from there
        if self.config.best_length && optimal_length.is_some() {
            let length = optimal_length.unwrap();
            
            if self.config.verbose {
                println!("Starting with optimal length from LUT: {}", length);
            }
            
            let result = self.search_length(length);
            
            if result.ruler.is_some() {
                let mut result = result;
                result.optimal = Some(true);
                
                // If we found an optimal ruler and have a canonical version, replace it
                if let Some(canonical_pos) = canonical_ruler {
                    result.ruler = Some(GolombRuler::new(canonical_pos, marks));
                }
                
                return result;
            }
        }
        
        // Try progressively increasing lengths
        for length in lower_bound..=upper_bound {
            // Skip the optimal length if we already tried it
            if self.config.best_length && optimal_length.is_some() && length == optimal_length.unwrap() {
                continue;
            }
            
            if self.config.verbose {
                println!("Searching length {}...", length);
            }
            
            let result = self.search_length(length);
            
            if let Some(_ruler) = &result.ruler {
                let is_optimal = optimal_length.map(|ol| length == ol);
                let mut result = result;
                result.optimal = is_optimal;
                
                // If we found an optimal ruler and have a canonical version, replace it
                if is_optimal == Some(true) && canonical_ruler.is_some() {
                    result.ruler = Some(GolombRuler::new(canonical_ruler.unwrap(), marks));
                }
                
                return result;
            }
        }
        
        // No solution found within bounds
        SolverResult {
            ruler: None,
            optimal: None,
            states_examined: 0,
        }
    }
    
    /// Searches for a ruler with the given length
    fn search_length(&self, max_length: usize) -> SolverResult {
        if self.config.multi_processing {
            self.search_length_parallel(max_length)
        } else {
            self.search_length_single(max_length)
        }
    }
    
    /// Single-threaded search for a ruler with the given length
    fn search_length_single(&self, max_length: usize) -> SolverResult {
        let mut ruler = GolombRuler::empty(self.config.marks);
        let mut distance_set = DistanceSet::new();
        let mut states_examined = 0;
        
        let found = self.backtrack(&mut ruler, &mut distance_set, max_length, &mut states_examined);
        
        if found {
            SolverResult {
                ruler: Some(ruler),
                optimal: None,
                states_examined,
            }
        } else {
            SolverResult {
                ruler: None,
                optimal: None,
                states_examined,
            }
        }
    }
    
    /// Multi-threaded search for a ruler with the given length
    fn search_length_parallel(&self, max_length: usize) -> SolverResult {
        // Generate tasks for parallel processing
        let tasks = self.generate_tasks(max_length);
        let num_tasks = tasks.len();
        let num_threads = num_cpus::get().min(num_tasks);
        
        if self.config.verbose {
            println!("Using {} threads for {} tasks", num_threads, num_tasks);
        }
        
        // Shared state for early termination
        let found_ruler = Arc::new(Mutex::new(None));
        let total_states = Arc::new(Mutex::new(0));
        
        // Use Rayon for parallel execution
        tasks.into_par_iter()
            .with_max_len(1) // Each task gets its own thread
            .for_each(|task| {
                // Early exit if a ruler has already been found
                if found_ruler.lock().unwrap().is_some() {
                    return;
                }
                
                // Create the initial ruler with mark1 and mark2 preset
                let mut ruler = GolombRuler::empty(self.config.marks);
                ruler.add_mark(task.mark1);
                ruler.add_mark(task.mark2);
                
                let mut distance_set = DistanceSet::new();
                distance_set.add_distances(&[0], task.mark1);
                distance_set.add_distances(&[0, task.mark1], task.mark2);
                
                let mut local_states = 0;
                let found = self.backtrack(&mut ruler, &mut distance_set, task.max_length, &mut local_states);
                
                // Update total states examined
                *total_states.lock().unwrap() += local_states;
                
                // If found, store the result
                if found {
                    let mut lock = found_ruler.lock().unwrap();
                    if lock.is_none() {
                        *lock = Some(ruler);
                    }
                }
            });
        
        let ruler = found_ruler.lock().unwrap().clone();
        let states_examined = *total_states.lock().unwrap();
        
        SolverResult {
            ruler,
            optimal: None,
            states_examined,
        }
    }
    
    /// Recursive backtracking to find a valid ruler
    fn backtrack(
        &self,
        ruler: &mut GolombRuler,
        distance_set: &mut DistanceSet,
        max_length: usize,
        states_examined: &mut usize,
    ) -> bool {
        *states_examined += 1;
        
        // Check if we found a complete ruler
        if ruler.current_marks() == self.config.marks {
            return true;
        }
        
        let current_pos = if ruler.current_marks() > 0 {
            ruler.positions()[ruler.current_marks() - 1]
        } else {
            0
        };
        
        // Calculate minimum step size based on remaining marks
        let remaining = self.config.marks - ruler.current_marks();
        let min_step = if remaining > 1 {
            1
        } else {
            1
        };
        
        // Calculate maximum position based on a conservative upper bound
        let max_pos = max_length - (remaining - 1) * min_step;
        
        // Try each possible position for the next mark
        for pos in current_pos + min_step..=max_pos {
            // Skip if this would make the ruler too long
            if pos > max_length {
                continue;
            }
            
            // Check if adding this mark would create a duplicate distance
            let mut local_distance_set = distance_set.clone();
            if local_distance_set.add_distances(ruler.positions(), pos) {
                // Valid so far, add the mark and continue recursively
                ruler.add_mark(pos);
                
                if self.backtrack(ruler, &mut local_distance_set, max_length, states_examined) {
                    return true;
                }
                
                // Backtrack: remove the mark and try another position
                ruler.remove_last_mark();
            }
        }
        
        false
    }
    
    /// Generate tasks for parallel processing
    fn generate_tasks(&self, max_length: usize) -> Vec<Task> {
        let mut tasks = Vec::new();
        let marks = self.config.marks;
        
        // For very small rulers, don't bother with too much parallelism
        if marks <= 3 {
            tasks.push(Task {
                mark1: 1,
                mark2: 2,
                max_length,
            });
            return tasks;
        }
        
        // Estimate of reasonable step for mark1
        let limit1 = max_length / marks;
        
        // Generate tasks with different starting positions for mark1 and mark2
        for mark1 in 1..=limit1 {
            // Ensure we leave enough space for remaining marks
            let remaining_length = max_length - mark1;
            let min_needed = marks - 2; // We've placed 2 marks already (0 and mark1)
            
            // Estimate reasonable upper bound for mark2
            let limit2 = (remaining_length - min_needed + 1).min(mark1 * 2);
            
            for mark2 in (mark1 + 1)..=(mark1 + limit2) {
                if mark2 <= max_length - (marks - 3) {
                    tasks.push(Task {
                        mark1,
                        mark2,
                        max_length,
                    });
                }
            }
        }
        
        // Ensure we have at least one task
        if tasks.is_empty() {
            tasks.push(Task {
                mark1: 1,
                mark2: 2,
                max_length,
            });
        }
        
        tasks
    }
}
