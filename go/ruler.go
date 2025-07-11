package main

import (
	"fmt"
	"sort"
)

// Ruler represents a Golomb ruler with its positions and metadata
type Ruler struct {
	Positions []int
	Length    int
	Marks     int
}

// NewRuler creates a new ruler with the given positions
func NewRuler(positions []int) *Ruler {
	if len(positions) == 0 {
		return &Ruler{Positions: []int{}, Length: 0, Marks: 0}
	}
	
	// Sort positions to ensure they're in ascending order
	sorted := make([]int, len(positions))
	copy(sorted, positions)
	sort.Ints(sorted)
	
	return &Ruler{
		Positions: sorted,
		Length:    sorted[len(sorted)-1],
		Marks:     len(sorted),
	}
}

// IsValid checks if the ruler is a valid Golomb ruler (all distances unique)
func (r *Ruler) IsValid() bool {
	if r.Marks < 2 {
		return true
	}
	
	distances := r.CalculateDistances()
	
	// Check for duplicate distances
	for i := 1; i < len(distances); i++ {
		if distances[i] == distances[i-1] {
			return false
		}
	}
	
	return true
}

// CalculateDistances returns all pairwise distances in sorted order
func (r *Ruler) CalculateDistances() []int {
	if r.Marks < 2 {
		return []int{}
	}
	
	distances := make([]int, 0, r.Marks*(r.Marks-1)/2)
	
	for i := 0; i < r.Marks; i++ {
		for j := i + 1; j < r.Marks; j++ {
			distances = append(distances, r.Positions[j]-r.Positions[i])
		}
	}
	
	sort.Ints(distances)
	return distances
}

// GetMissingDistances returns distances from 1 to length that are NOT measurable
func (r *Ruler) GetMissingDistances() []int {
	if r.Length == 0 {
		return []int{}
	}
	
	distances := r.CalculateDistances()
	distanceSet := make(map[int]bool)
	for _, d := range distances {
		distanceSet[d] = true
	}
	
	missing := make([]int, 0)
	for i := 1; i <= r.Length; i++ {
		if !distanceSet[i] {
			missing = append(missing, i)
		}
	}
	
	return missing
}

// String returns a string representation of the ruler
func (r *Ruler) String() string {
	return fmt.Sprintf("%v", r.Positions)
}

// Copy creates a deep copy of the ruler
func (r *Ruler) Copy() *Ruler {
	positions := make([]int, len(r.Positions))
	copy(positions, r.Positions)
	return &Ruler{
		Positions: positions,
		Length:    r.Length,
		Marks:     r.Marks,
	}
}
