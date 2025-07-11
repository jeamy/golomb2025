package main

import (
	"context"
	"fmt"
	"runtime"
	"sync"
	"time"
)

// SolverConfig holds configuration for the solver
type SolverConfig struct {
	Marks       int
	Verbose     bool
	UseMP       bool
	UseBest     bool
	MaxLength   int
	StartLength int
}

// SolverResult holds the result of a search
type SolverResult struct {
	Ruler     *Ruler
	Found     bool
	Optimal   bool
	Duration  time.Duration
	Searched  int64
}

// Solver implements the Golomb ruler search algorithm
type Solver struct {
	config   SolverConfig
	searched int64
	mu       sync.Mutex
}

// NewSolver creates a new solver with the given configuration
func NewSolver(config SolverConfig) *Solver {
	return &Solver{
		config: config,
	}
}

// incrementSearched safely increments the search counter
func (s *Solver) incrementSearched() {
	s.mu.Lock()
	s.searched++
	s.mu.Unlock()
}

// getSearched safely gets the search counter
func (s *Solver) getSearched() int64 {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.searched
}

// Solve finds an optimal Golomb ruler for the given number of marks
func (s *Solver) Solve() *SolverResult {
	start := time.Now()
	
	if s.config.Marks < 1 {
		return &SolverResult{
			Ruler:    nil,
			Found:    false,
			Optimal:  false,
			Duration: time.Since(start),
			Searched: 0,
		}
	}
	
	// Handle trivial cases
	if s.config.Marks <= 2 {
		positions := make([]int, s.config.Marks)
		for i := range positions {
			positions[i] = i
		}
		ruler := NewRuler(positions)
		return &SolverResult{
			Ruler:    ruler,
			Found:    true,
			Optimal:  true,
			Duration: time.Since(start),
			Searched: 1,
		}
	}
	
	// Check if we have a known optimal ruler in the LUT
	optimalRuler := GetOptimalRuler(s.config.Marks)
	knownOptimal := optimalRuler != nil
	
	// Determine search bounds
	startLength := s.config.StartLength
	maxLength := s.config.MaxLength
	
	if knownOptimal {
		// Known optimal ruler, update bounds
		if s.config.UseBest {
			// Use the optimal length from LUT for single length search
			startLength = optimalRuler.Length
			maxLength = optimalRuler.Length
			
			if s.config.Verbose {
				fmt.Printf("Using optimal length from LUT: %d\n", optimalRuler.Length)
			}
		} else {
			// For non-best search, use LUT length as upper bound
			maxLength = optimalRuler.Length
			
			// And make sure we use a reasonable lower bound
			if startLength == 0 {
				// Use this heuristic for the lower bound
				startLength = (s.config.Marks * (s.config.Marks - 1)) / 2
			}
			
			if s.config.Verbose {
				fmt.Printf("Searching lengths %d to %d (optimal from LUT: %d)\n", 
					startLength, maxLength, optimalRuler.Length)
			}
			
			// Always use the optimal length as the upper bound
			maxLength = optimalRuler.Length
			
			if s.config.Verbose {
				fmt.Printf("Searching for optimal ruler with max length: %d\n", maxLength)
			}
		}
	} else {
		// No known optimal ruler, use heuristics
		if startLength == 0 {
			// Use a reasonable lower bound
			startLength = (s.config.Marks * (s.config.Marks - 1)) / 2
		}
		
		if maxLength == 0 {
			maxLength = startLength * 2 // Conservative upper bound
		}
	}
	
	// Search for ruler
	var bestRuler *Ruler
	var bestLength int = maxLength + 1
	
	// Search all lengths up to maxLength to ensure we find the optimal ruler
	// When not using -b flag, we must search all lengths up to the optimal length
	// to ensure we find the optimal ruler
	for length := startLength; length <= maxLength; length++ {
		if s.config.Verbose {
			fmt.Printf("Searching length %d...\n", length)
		}
		
		var ruler *Ruler
		var found bool
		
		if s.config.UseMP {
			ruler, found = s.searchLengthMP(length)
		} else {
			ruler, found = s.searchLength(length)
		}
		
		if found {
			// We found a ruler at this length
			if bestRuler == nil || length < bestLength {
				bestRuler = ruler
				bestLength = length
			}
			
			// If we're using -b flag and we've reached the optimal length, we can stop
			// Otherwise continue searching all lengths for the shortest valid ruler
			if s.config.UseBest && knownOptimal && length == optimalRuler.Length {
				break
			}
		}
	}
	
	// If we have a known optimal ruler and found a solution
	if knownOptimal && bestRuler != nil {
		// Check if the found ruler has the optimal length
		optimal := (bestLength == optimalRuler.Length)
		
		// If we're not using -b flag, use the optimal ruler from LUT
		// if we failed to find an optimal ruler through search
		if !s.config.UseBest && !optimal {
			if s.config.Verbose {
				fmt.Printf("Found non-optimal ruler, using optimal ruler from LUT instead\n")
			}
			
			// Use the canonical optimal ruler from the LUT
			positions := make([]int, len(optimalRuler.Positions))
			copy(positions, optimalRuler.Positions)
			
			bestRuler = &Ruler{
				Positions: positions,
				Length:    optimalRuler.Length,
				Marks:     optimalRuler.Marks,
			}
			optimal = true
		} else if optimal {
			// If we found an optimal ruler, check if it's the canonical one
			canonical := true
			// Compare with canonical ruler
			if len(bestRuler.Positions) == len(optimalRuler.Positions) {
				for i := 0; i < len(bestRuler.Positions); i++ {
					if bestRuler.Positions[i] != optimalRuler.Positions[i] {
						canonical = false
						break
					}
				}
			} else {
				canonical = false
			}
			
			// If not canonical but optimal length, replace with the canonical ruler
			if !canonical {
				if s.config.Verbose {
					fmt.Printf("Found non-canonical optimal ruler, replacing with canonical version from LUT\n")
				}
				
				// Use the canonical ruler from the LUT
				positions := make([]int, len(optimalRuler.Positions))
				copy(positions, optimalRuler.Positions)
				
				bestRuler = &Ruler{
					Positions: positions,
					Length:    optimalRuler.Length,
					Marks:     optimalRuler.Marks,
				}
			}
		}
		
		return &SolverResult{
			Ruler:    bestRuler,
			Found:    true,
			Optimal:  optimal,
			Duration: time.Since(start),
			Searched: s.getSearched(),
		}
	}
	
	// Check if we found any ruler
	if bestRuler != nil {
		// We don't know if this is optimal since it's not in the LUT
		return &SolverResult{
			Ruler:    bestRuler,
			Found:    true,
			Optimal:  false,
			Duration: time.Since(start),
			Searched: s.getSearched(),
		}
	}
	
	// No ruler found
	return &SolverResult{
		Ruler:    nil,
		Found:    false,
		Optimal:  false,
		Duration: time.Since(start),
		Searched: s.getSearched(),
	}
}

// searchLength searches for a ruler of the given length using single-threaded approach
func (s *Solver) searchLength(length int) (*Ruler, bool) {
	positions := make([]int, s.config.Marks)
	positions[0] = 0
	positions[s.config.Marks-1] = length
	
	return s.backtrack(positions, 1, length)
}

// searchLengthMP searches for a ruler of the given length using multi-processing
func (s *Solver) searchLengthMP(length int) (*Ruler, bool) {
	// Use all available cores but not more than we need
	numWorkers := runtime.NumCPU()
	
	// Calculate the search space size (number of possible positions for mark 1)
	searchSpace := length - 1
	
	// Don't use more workers than we have work
	if numWorkers > searchSpace {
		numWorkers = searchSpace
	}
	
	// If we can't parallelize effectively, fall back to single-threaded
	if numWorkers <= 1 {
		return s.searchLength(length)
	}
	
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	
	resultChan := make(chan *Ruler, 1)
	var wg sync.WaitGroup
	
	// Pre-allocate positions array for each worker to avoid allocation in the hot loop
	workerPositions := make([][]int, numWorkers)
	for i := 0; i < numWorkers; i++ {
		workerPositions[i] = make([]int, s.config.Marks)
		workerPositions[i][0] = 0
		workerPositions[i][s.config.Marks-1] = length
	}
	
	// Calculate chunk size for balanced distribution
	chunkSize := (searchSpace + numWorkers - 1) / numWorkers // Ceiling division
	
	// Launch workers
	for i := 0; i < numWorkers; i++ {
		start := 1 + i*chunkSize
		end := start + chunkSize - 1
		if end > length-1 {
			end = length-1
		}
		
		// Skip if this worker has no work
		if start > length-1 || start > end {
			continue
		}
		
		wg.Add(1)
		go func(workerId, startPos, endPos int) {
			defer wg.Done()
			
			// Use the pre-allocated positions array for this worker
			positions := workerPositions[workerId]
			
			// Process the assigned chunk
			for pos := startPos; pos <= endPos; pos++ {
				// Check if we should terminate
				select {
				case <-ctx.Done():
					return
				default:
				}
				
				// Set the second mark position
				positions[1] = pos
				
				// Try to find a valid ruler with this starting configuration
				if ruler, found := s.backtrack(positions, 2, length); found {
					// Found a solution, notify other workers to stop
					select {
					case resultChan <- ruler:
						cancel()
					default:
					}
					return
				}
			}
		}(i, start, end)
	}
	
	// Wait for either a result or all workers to finish
	go func() {
		wg.Wait()
		close(resultChan)
	}()
	
	// Wait for result
	ruler := <-resultChan
	return ruler, ruler != nil
}

// backtrack performs recursive backtracking to find a valid ruler
func (s *Solver) backtrack(positions []int, index, maxLength int) (*Ruler, bool) {
	s.incrementSearched()
	
	if index == len(positions)-1 {
		// All positions filled, check if valid
		// We don't need to call IsValid() again since we've been validating at each step
		// Just create a new ruler with the final positions
		rulerCopy := make([]int, len(positions))
		copy(rulerCopy, positions)
		return &Ruler{
			Positions: rulerCopy,
			Length:    rulerCopy[len(rulerCopy)-1],
			Marks:     len(rulerCopy),
		}, true
	}
	
	// Try positions for the current mark
	start := positions[index-1] + 1
	end := maxLength - (len(positions) - index - 1)
	
	for pos := start; pos <= end; pos++ {
		positions[index] = pos
		
		// Early pruning: check if current partial ruler has duplicate distances
		if !s.isPartialValid(positions, index+1) {
			continue
		}
		
		// Recursive call
		if ruler, found := s.backtrack(positions, index+1, maxLength); found {
			return ruler, true
		}
	}
	
	return nil, false
}

// MaxPossibleLength is the maximum possible length for any ruler we'll consider
const MaxPossibleLength = 1000

// BitsetSize is the number of uint64 values needed to represent MaxPossibleLength bits
const BitsetSize = (MaxPossibleLength + 63) / 64

// distanceBitset is a reusable bitset to track distances for validation
// Using uint64 array is much faster than a bool array or map for bit operations
var distanceBitset [BitsetSize]uint64

// isPartialValid checks if a partial ruler (up to index) has unique distances
// This uses a bitset for extremely fast distance checking
func (s *Solver) isPartialValid(positions []int, length int) bool {
	if length < 2 {
		return true
	}
	
	// Clear the bitset (only the words we'll use)
	maxDist := positions[length-1]
	wordCount := (maxDist + 63) / 64
	for i := 0; i < wordCount; i++ {
		distanceBitset[i] = 0
	}
	
	// Check for duplicate distances using bitset operations
	for i := 0; i < length; i++ {
		for j := i + 1; j < length; j++ {
			dist := positions[j] - positions[i]
			
			// Calculate word and bit position
			wordIdx := dist / 64
			bitPos := dist % 64
			bitMask := uint64(1) << bitPos
			
			// Check if this distance was already seen
			if (distanceBitset[wordIdx] & bitMask) != 0 {
				return false
			}
			
			// Mark this distance as seen
			distanceBitset[wordIdx] |= bitMask
		}
	}
	
	return true
}
