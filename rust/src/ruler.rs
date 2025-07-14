//! Module for representing and manipulating Golomb rulers

/// Represents a Golomb ruler with a specified number of marks and positions
#[derive(Debug, Clone)]
pub struct GolombRuler {
    /// Number of marks on the ruler
    pub marks: usize,
    
    /// Positions of the marks on the ruler
    positions: Vec<usize>,
}

impl GolombRuler {
    /// Creates a new Golomb ruler with the specified positions and number of marks
    pub fn new(positions: Vec<usize>, marks: usize) -> Self {
        debug_assert_eq!(positions.len(), marks, "Number of positions must match the number of marks");
        Self { positions, marks }
    }
    
    /// Creates a new empty Golomb ruler with a specified number of marks
    /// The first mark is always at position 0
    pub fn empty(marks: usize) -> Self {
        let mut positions = Vec::with_capacity(marks);
        positions.push(0); // First mark is always at 0
        Self { positions, marks }
    }
    
    /// Returns a reference to the positions of the ruler marks
    pub fn positions(&self) -> &[usize] {
        &self.positions
    }
    
    /// Returns the length of the ruler, which is the position of the last mark
    pub fn length(&self) -> usize {
        if self.positions.is_empty() {
            0
        } else {
            *self.positions.last().unwrap()
        }
    }
    
    /// Returns the position of a specific mark
    #[allow(dead_code)]
    pub fn position(&self, mark: usize) -> Option<usize> {
        self.positions.get(mark).copied()
    }
    
    /// Adds a new mark at the specified position
    pub fn add_mark(&mut self, position: usize) {
        debug_assert!(self.positions.len() < self.marks, "Cannot add more marks than specified");
        debug_assert!(
            self.positions.is_empty() || position > *self.positions.last().unwrap(),
            "Marks must be added in ascending order"
        );
        self.positions.push(position);
    }
    
    /// Removes the last mark
    pub fn remove_last_mark(&mut self) {
        if !self.positions.is_empty() {
            self.positions.pop();
        }
    }
    
    /// Returns the current number of marks on the ruler
    pub fn current_marks(&self) -> usize {
        self.positions.len()
    }
    
    /// Checks if the ruler is valid (has no duplicate distances)
    #[allow(dead_code)]
    pub fn is_valid(&self) -> bool {
        if self.positions.len() <= 1 {
            return true;
        }
        
        let mut distances = std::collections::HashSet::new();
        
        for i in 0..self.positions.len() {
            for j in i+1..self.positions.len() {
                let dist = self.positions[j] - self.positions[i];
                if !distances.insert(dist) {
                    return false; // Duplicate distance found
                }
            }
        }
        
        true
    }
    
    /// Checks if a new position would create a duplicate distance
    #[allow(dead_code)]
    pub fn is_valid_with(&self, new_position: usize) -> bool {
        if self.positions.len() <= 1 {
            return true;
        }
        
        let mut distances = std::collections::HashSet::new();
        
        // Check existing distances
        for i in 0..self.positions.len() {
            for j in i+1..self.positions.len() {
                let dist = self.positions[j] - self.positions[i];
                if !distances.insert(dist) {
                    return false;
                }
            }
        }
        
        // Check distances with new position
        for i in 0..self.positions.len() {
            let dist = new_position - self.positions[i];
            if !distances.insert(dist) {
                return false;
            }
        }
        
        true
    }
}
