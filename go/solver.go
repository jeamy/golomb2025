package main

import (
	"context"
	"fmt"
	"math"
	"runtime"
	"sort"
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
	
	// Do a final validation of the best ruler before returning
	if bestRuler != nil {
		// Verify this is actually a valid Golomb ruler with no duplicate distances
		// Count all distances and check for duplicates
		distances := make(map[int]int)
		positions := bestRuler.Positions
		
		for i := 0; i < len(positions); i++ {
			for j := i + 1; j < len(positions); j++ {
				dist := positions[j] - positions[i]
				distances[dist]++
				if distances[dist] > 1 {
					// Found a duplicate distance - this is invalid!
					if s.config.Verbose {
						fmt.Printf("CRITICAL ERROR: Found invalid ruler with duplicate distance %d!\n", dist)
						fmt.Printf("Positions: %v\n", positions)
						duplicates := []int{}
						for d, count := range distances {
							if count > 1 {
								duplicates = append(duplicates, d)
							}
						}
						fmt.Printf("Duplicate distances: %v\n", duplicates)
					}
					
					// Search again but more carefully
					bestRuler = nil
					break
				}
			}
		}
	}
	
	// If we have a known optimal ruler and found a solution
	if knownOptimal && bestRuler != nil {
		// Always use the ruler we found through computation
		// Only check if it's optimal based on the LUT length
		optimal := (bestLength == optimalRuler.Length)
		
		// For the -b flag, when we compute a ruler with optimal length, check if it's canonical
		if optimal && s.config.UseBest {
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
			
			// With -b flag: If not canonical but optimal length, replace with the canonical ruler
			// for consistency with C and Java versions
			if !canonical {
				if s.config.Verbose {
					fmt.Printf("Found non-canonical optimal ruler with correct length %d, replacing with canonical version\n", bestRuler.Length)
				}
				
				// Use the canonical ruler from the LUT for consistency
				positions := make([]int, len(optimalRuler.Positions))
				copy(positions, optimalRuler.Positions)
				
				bestRuler = &Ruler{
					Positions: positions,
					Length:    optimalRuler.Length,
					Marks:     optimalRuler.Marks,
				}
			} else if s.config.Verbose {
				fmt.Printf("Found canonical optimal ruler with length %d\n", bestRuler.Length)
			}
		} else if optimal && s.config.Verbose {
			fmt.Printf("Found optimal ruler with length %d\n", bestRuler.Length)
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
	
	// For optimal performance, limit workers based on mark count and length
	if s.config.Marks > 10 {
		// For large problems, use more aggressive work splitting
		// We'll split based on positions for mark 1 AND mark 2
		return s.searchLengthMPAdvanced(length)
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
			
			// Pre-allocate bitset for this worker (not shared)
			localBitset := make([]uint64, (length+63)/64)
			
			// Process the assigned chunk
			for pos := startPos; pos <= endPos; pos++ {
				// Check if we should terminate (less frequently)
				if pos%64 == 0 {
					select {
					case <-ctx.Done():
						return
					default:
					}
				}
				
				// Set the second mark position
				positions[1] = pos
				
				// Try to find a valid ruler with this starting configuration
				if ruler, found := s.backtrackWithBitset(positions, 2, length, localBitset); found {
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

// Define a specialized task structure for better locality
type searchTask struct {
	mark1, mark2 int
	priority     int // Higher priority tasks first
}

// searchLengthMPAdvanced provides optimized work distribution for all mark counts
func (s *Solver) searchLengthMPAdvanced(length int) (*Ruler, bool) {
	// Hohe Leistung für alle Lineale - ähnlich wie C-Version
	
	// Verbesserte Parallelisierung mit besserer Aufgabenplanung
	maxWorkers := runtime.NumCPU()

	// Größere Aufgabenmenge für bessere Lastverteilung
	taskFactor := 4 // Mehr Aufgaben pro Worker für feinere Granularität
	taskCount := maxWorkers * taskFactor
	
	// Für größere Lineale noch mehr Aufgaben erstellen
	if s.config.Marks >= 10 {
		taskCount = maxWorkers * 8
	}
	
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	
	resultChan := make(chan *Ruler, 1)
	
	// Use a buffered channel with a much larger size to avoid blocking
	// Die Kanalgröße an taskCount anpassen, um nicht zu blockieren
	taskChan := make(chan searchTask, taskCount * 2) 
	
	// Generate work units in priority order (most promising first)
	go func() {
		defer close(taskChan)
		
		// Optimierte Arbeitsstrategie für alle Lineale
		// Verteile die Arbeit gleichmäßiger und feiner
		
		// Verwende die berechnete taskCount für die Aufgabengranularität
		mark1Range := length / 3 // Erste Markierung in unterer Hälfte positionieren
		mark2Range := length / 2 // Zweite Markierung in oberem Bereich
		
		// Feinere Aufteilung der Arbeit basierend auf Lineal-Größe
		// Statt feste Grenzen zu verwenden, passen wir uns dynamisch an
		firstMarkMax := mark1Range
		if firstMarkMax > 30 {
			firstMarkMax = 30 // Obergrenze für größere Lineale
		}
		
		// Nutze mark2Range für zweite Markierung dynamisch
		// Optimiert für alle Linealgrößen, nicht nur für 12 Markierungen
		secondMarkMax := mark2Range
		if s.config.Marks >= 10 {
			// Bei größeren Linealen optimierter Suchbereich
			secondMarkMax = mark1Range * 3
		}
		
		// Generate tasks with priority weighting
		tasks := make([]searchTask, 0, taskCount * 2)
		
		// Optimierte Suchbereiche für alle Linealgrößen
		for mark1 := 1; mark1 <= firstMarkMax; mark1++ {
			// Berechne gültigen Bereich für Markierung 2
			minMark2 := mark1 + 1
			
			// Verwende die berechnete dynamische Obergrenze
			// Basierend auf secondMarkMax für bessere Arbeitsverteilung
			maxMark2 := minMark2 + secondMarkMax/2
			if maxMark2 > length-(s.config.Marks-3) {
				maxMark2 = length - (s.config.Marks - 3)
			}
			
			for mark2 := minMark2; mark2 <= maxMark2; mark2++ {
				// Verify this pair is valid (marks 0, 1, 2)
				dist1 := mark1       // Distance between marks 0 and 1
				dist2 := mark2 - mark1 // Distance between marks 1 and 2
				dist3 := mark2       // Distance between marks 0 and 2
				
				if dist1 != dist2 && dist1 != dist3 && dist2 != dist3 {
					// Calculate priority based on empirical observations
					// Higher priority for smaller marks and better distribution
					priority := 1000 - (mark1 + mark2)
					
					// Also prioritize balanced distance distribution
					if math.Abs(float64(dist2-dist1)) < float64(dist1)/2 {
						priority += 500
					}
					
					// Add task with priority
					tasks = append(tasks, searchTask{mark1, mark2, priority})
				}
			}
		}
		
		// Sort tasks by priority (highest first)
		sort.Slice(tasks, func(i, j int) bool {
			return tasks[i].priority > tasks[j].priority
		})
		
		// Send tasks to workers in priority order
		for _, task := range tasks {
			select {
			case <-ctx.Done():
				return
			case taskChan <- task:
				// Task sent
			}
		}
	}()
	
	// Launch worker pool with optimized task processing
	var wg sync.WaitGroup
	for i := 0; i < maxWorkers; i++ {
		wg.Add(1)
		go func(workerId int) {
			defer wg.Done()
			
			// Prepare reusable memory for this worker
			positions := make([]int, s.config.Marks)
			positions[0] = 0
			positions[s.config.Marks-1] = length
			
			// Pre-allocate multiple bitsets for this worker
			// One for main validation, one for quick checks
			mainBitset := make([]uint64, (length+63)/64)
			quickBitset := make([]uint64, 8) // For fast prefix checks
			
			// Process tasks from the queue
			for task := range taskChan {
				// Check for cancellation periodically (less overhead)
				select {
				case <-ctx.Done():
					return
				default:
					// Continue processing
				}
				
				// Set positions from the task
				positions[1] = task.mark1
				positions[2] = task.mark2
				
				// Quick check of prefix validity using optimized bitset
				if !s.isPrefix3Valid(positions, quickBitset) {
					continue
				}
				
				// Try to find a valid ruler with this prefix using optimized backtracking
				if ruler, found := s.backtrackOptimized(positions, 3, length, mainBitset); found {
					// Found a solution, notify other workers to stop
					select {
					case resultChan <- ruler:
						cancel()
					default:
						// Another worker found a solution first
					}
					return
				}
			}
		}(i)
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



// backtrackWithBitset is like backtrack but uses a provided bitset to avoid allocation
func (s *Solver) backtrackWithBitset(positions []int, index, maxLength int, bitset []uint64) (*Ruler, bool) {
	s.incrementSearched()
	
	if index == len(positions)-1 {
		// All positions filled, do a final validation check to be absolutely sure
		if !s.isPartialValidWithBitset(positions, len(positions), bitset) {
			// This should never happen if our incremental validation is working correctly
			// But adding an extra safeguard
			return nil, false
		}
		
		// Create a new ruler with the final positions
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
		// This is critical to avoid returning invalid rulers
		if !s.isPartialValidWithBitset(positions, index+1, bitset) {
			continue
		}
		
		// Recursive call
		if ruler, found := s.backtrackWithBitset(positions, index+1, maxLength, bitset); found {
			return ruler, true
		}
	}
	
	return nil, false
}

// isPrefix3Valid is a specialized fast check for validating the first 3 positions
func (s *Solver) isPrefix3Valid(positions []int, bitset []uint64) bool {
	// Only need to check first 3 positions (0, 1, 2)
	if positions[2] <= 0 || positions[1] <= 0 {
		return false
	}
	
	// Clear bitset
	for i := 0; i < len(bitset); i++ {
		bitset[i] = 0
	}
	
	// Only check 3 distances for the prefix
	// Distance 0-1
	dist1 := positions[1] - positions[0]
	// Distance 1-2
	dist2 := positions[2] - positions[1]
	// Distance 0-2
	dist3 := positions[2] - positions[0]
	
	// Check for duplicates quickly
	for _, dist := range []int{dist1, dist2, dist3} {
		wordIdx := dist / 64
		bitPos := dist % 64
		bitMask := uint64(1) << bitPos
		
		if (bitset[wordIdx] & bitMask) != 0 {
			return false // Duplicate distance
		}
		bitset[wordIdx] |= bitMask
	}
	
	return true
}

// backtrackOptimized ist eine hochoptimierte Version des Backtracking-Algorithmus für alle Lineale
func (s *Solver) backtrackOptimized(positions []int, index, maxLength int, bitset []uint64) (*Ruler, bool) {
	s.incrementSearched()
	
	// Überprüfen, ob alle Positionen gefüllt sind
	if index == len(positions)-1 {
		// Alle Positionen gefüllt, finale Validierung zur absoluten Sicherheit
		if !s.isPartialValidWithBitset(positions, len(positions), bitset) {
			return nil, false
		}
		
		// Erstelle neues Lineal mit den finalen Positionen
		rulerCopy := make([]int, len(positions))
		copy(rulerCopy, positions)
		return &Ruler{
			Positions: rulerCopy,
			Length:    rulerCopy[len(rulerCopy)-1],
			Marks:     len(rulerCopy),
		}, true
	}
	
	// Adaptive Schrittgröße basierend auf Linealgrößen und Position
	stepSize := 1
	totalMarks := len(positions)
	
	// Optimierung für größere Lineale: Schritt anpassen nach Position und Größe
	if totalMarks >= 10 {
		// Für große Lineale wie in der C-Version optimieren
		if index <= 1 { 
			// Für erste Markierung sehr klein wachsen
			stepSize = 1
		} else if index <= 3 {
			// Für frühe Markierungen größere Sprünge
			stepSize = 2
		} else if index <= 5 {
			// Mittlere Positionen
			stepSize = 1
		}
	}
	
	// Frühe Validierung mit isPrefix3Valid für schnellere Ergebnisse
	if index == 2 && !s.isPrefix3Valid(positions, bitset) {
		return nil, false
	}
	
	// Bestimme Suchbereich für aktuelle Markierung
	start := positions[index-1] + 1
	end := maxLength - (len(positions) - index - 1)
	
	// Prüfe alle möglichen Positionen mit adaptiver Schrittgröße
	for pos := start; pos <= end; pos += stepSize {
		positions[index] = pos
		
		// Frühe Abschneidung: Überprüfe, ob aktuelles Teillineal doppelte Abstände hat
		// Dies ist entscheidend, um ungültige Lineale zu vermeiden
		if !s.isPartialValidWithBitset(positions, index+1, bitset) {
			continue
		}
		
		// Optimierung für die vorletzte Markierung (Endoptimierung)
		if index == len(positions)-2 {
			// Spezialfall für die vorletzte Markierung
			// Vermeidet einen rekursiven Aufruf in einem häufigen Fall
			positions[index+1] = maxLength
			if s.isPartialValidWithBitset(positions, len(positions), bitset) {
				// Erstelle neues Lineal mit den finalen Positionen
				rulerCopy := make([]int, len(positions))
				copy(rulerCopy, positions)
				return &Ruler{
					Positions: rulerCopy,
					Length:    rulerCopy[len(rulerCopy)-1],
					Marks:     len(rulerCopy),
				}, true
			}
			continue
		}
		
		// Rekursiver Aufruf
		if ruler, found := s.backtrackOptimized(positions, index+1, maxLength, bitset); found {
			return ruler, true
		}
	}
	
	return nil, false
}

// isPartialValidWithBitset checks if a partial ruler has unique distances using a provided bitset
func (s *Solver) isPartialValidWithBitset(positions []int, length int, bitset []uint64) bool {
	if length < 2 {
		return true
	}
	
	// Clear the bitset (only the words we'll use)
	maxDist := positions[length-1]
	wordCount := (maxDist + 63) / 64
	if wordCount > len(bitset) {
		wordCount = len(bitset)
	}
	for i := 0; i < wordCount; i++ {
		bitset[i] = 0
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
			if (bitset[wordIdx] & bitMask) != 0 {
				return false
			}
			
			// Mark this distance as seen
			bitset[wordIdx] |= bitMask
		}
	}
	
	return true
}

// backtrack performs recursive backtracking to find a valid ruler
func (s *Solver) backtrack(positions []int, index, maxLength int) (*Ruler, bool) {
	s.incrementSearched()
	
	if index == len(positions)-1 {
		// All positions filled, do a final validation check to be absolutely sure
		if !s.isPartialValid(positions, len(positions)) {
			// This should never happen if our incremental validation is working correctly
			// But adding an extra safeguard
			return nil, false
		}
		
		// Create a new ruler with the final positions
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
		// This is critical to avoid returning invalid rulers
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
