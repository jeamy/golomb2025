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
	
	// Determine search bounds
	startLength := s.config.StartLength
	maxLength := s.config.MaxLength
	
	if s.config.UseBest {
		if optimalLength := GetOptimalLength(s.config.Marks); optimalLength > 0 {
			startLength = optimalLength
			maxLength = optimalLength
			if s.config.Verbose {
				fmt.Printf("Using heuristic: known optimal length is %d\n", optimalLength)
			}
		}
	}
	
	if startLength == 0 {
		// Use a reasonable lower bound
		startLength = (s.config.Marks * (s.config.Marks - 1)) / 2
	}
	
	if maxLength == 0 {
		maxLength = startLength * 2 // Conservative upper bound
	}
	
	// Search for ruler
	var result *SolverResult
	
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
			optimal := false
			if optimalLength := GetOptimalLength(s.config.Marks); optimalLength > 0 {
				optimal = (length == optimalLength)
			}
			
			result = &SolverResult{
				Ruler:    ruler,
				Found:    true,
				Optimal:  optimal,
				Duration: time.Since(start),
				Searched: s.getSearched(),
			}
			break
		}
	}
	
	if result == nil {
		result = &SolverResult{
			Ruler:    nil,
			Found:    false,
			Optimal:  false,
			Duration: time.Since(start),
			Searched: s.getSearched(),
		}
	}
	
	return result
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
	numWorkers := runtime.NumCPU()
	if numWorkers > s.config.Marks-2 {
		numWorkers = s.config.Marks - 2
	}
	
	if numWorkers <= 1 {
		return s.searchLength(length)
	}
	
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	
	resultChan := make(chan *Ruler, 1)
	var wg sync.WaitGroup
	
	// Split the search space by the second mark position
	step := length / numWorkers
	if step < 1 {
		step = 1
	}
	
	for i := 0; i < numWorkers; i++ {
		start := i*step + 1
		end := (i+1)*step
		if i == numWorkers-1 {
			end = length - 1
		}
		
		if start >= end {
			continue
		}
		
		wg.Add(1)
		go func(startPos, endPos int) {
			defer wg.Done()
			
			for pos := startPos; pos <= endPos; pos++ {
				select {
				case <-ctx.Done():
					return
				default:
				}
				
				positions := make([]int, s.config.Marks)
				positions[0] = 0
				positions[1] = pos
				positions[s.config.Marks-1] = length
				
				if ruler, found := s.backtrack(positions, 2, length); found {
					select {
					case resultChan <- ruler:
						cancel() // Signal other goroutines to stop
					default:
					}
					return
				}
			}
		}(start, end)
	}
	
	// Wait for either a result or all workers to finish
	go func() {
		wg.Wait()
		close(resultChan)
	}()
	
	ruler := <-resultChan
	return ruler, ruler != nil
}

// backtrack performs recursive backtracking to find a valid ruler
func (s *Solver) backtrack(positions []int, index, maxLength int) (*Ruler, bool) {
	s.incrementSearched()
	
	if index == len(positions)-1 {
		// All positions filled, check if valid
		ruler := NewRuler(positions)
		if ruler.IsValid() {
			return ruler, true
		}
		return nil, false
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

// isPartialValid checks if a partial ruler (up to index) has unique distances
func (s *Solver) isPartialValid(positions []int, length int) bool {
	if length < 2 {
		return true
	}
	
	distances := make(map[int]bool)
	
	for i := 0; i < length; i++ {
		for j := i + 1; j < length; j++ {
			dist := positions[j] - positions[i]
			if distances[dist] {
				return false
			}
			distances[dist] = true
		}
	}
	
	return true
}
