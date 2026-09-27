package main

import (
	"runtime"
	"sync"
	"sync/atomic"
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
	Ruler    *Ruler
	Found    bool
	Optimal  bool
	Duration time.Duration
	Searched int64
}

// Solver implements the Golomb ruler search algorithm.
//
// The search engine is the endpoint-aware bit-parallel DFS ported from the
// C solver (src/solver_traditional_opt.c, the `-to` solver) and the CUDA
// variant (nvidia/golomb_bits.h): both endpoints of the ruler (0 and L) are
// fixed before any inner mark is placed, so the distance to the fixed right
// endpoint can be checked and pruned immediately instead of only at the
// deepest recursion level. Used distances are tracked in a bitset (one bit
// per distance) that is updated incrementally on descent and rolled back on
// backtrack, replacing the previous O(depth^2) from-scratch validation.
//
// The `-mp` parallel path mirrors solve_golomb_traditional_opt_mt in the C
// code: it enumerates all valid endpoint-aware prefixes (pos[1], pos[2]) up
// front and hands them out to a worker pool, each worker running the same
// sequential DFS from depth 3.
type Solver struct {
	config SolverConfig
}

// NewSolver creates a new solver with the given configuration
func NewSolver(config SolverConfig) *Solver {
	return &Solver{
		config: config,
	}
}

// ---------------------------------------------------------------------------
// Bitset helpers: one bit per distance value, exactly as in
// src/solver_traditional_opt.c and nvidia/golomb_bits.h.
// ---------------------------------------------------------------------------

func newBitset(maxVal int) []uint64 {
	return make([]uint64, maxVal/64+1)
}

func setBit(bs []uint64, idx int) { bs[idx>>6] |= 1 << uint(idx&63) }
func clrBit(bs []uint64, idx int) { bs[idx>>6] &^= 1 << uint(idx&63) }
func testBit(bs []uint64, idx int) bool {
	return bs[idx>>6]&(1<<uint(idx&63)) != 0
}

func clearBitset(bs []uint64) {
	for i := range bs {
		bs[i] = 0
	}
}

// ---------------------------------------------------------------------------
// dfsEndpoint places inner marks pos[depth..n-2] given that pos[0] = 0 and
// pos[n-1] = L are already fixed and every distance implied by pos[0..depth-1]
// (plus the endpoint distances they imply) is already set in bs.
//
// buf is a reusable (n*n)-sized scratch buffer; buf[depth*n : depth*n+depth]
// holds the new left-side distances discovered for `next` at this depth, so
// each recursion level gets its own slice without allocating on every call.
// ---------------------------------------------------------------------------
func dfsEndpoint(depth, n, L int, pos []int, bs []uint64, buf []int) bool {
	if depth == n-1 {
		return true
	}

	last := pos[depth-1]

	// After placing `next`, (n-2-depth) more inner marks plus the endpoint L
	// still need at least 1 apart each: next + (n-1-depth) <= L.
	maxNext := L - (n - 1 - depth)

	// Symmetry break on the first inner mark: at most L/2 (eliminates the
	// mirror image of every ruler).
	if depth == 1 {
		limit := L / 2
		if limit < last+1 {
			limit = last + 1
		}
		if maxNext > limit {
			maxNext = limit
		}
	}

	newDists := buf[depth*n : depth*n+depth]

	for next := last + 1; next <= maxNext; next++ {
		gap := next - last
		if testBit(bs, gap) {
			continue
		}

		// Endpoint-aware pruning: check the distance to the fixed right
		// endpoint immediately, instead of only once all marks are placed.
		dEnd := L - next
		if testBit(bs, dEnd) {
			continue
		}

		ok := true
		for i := 0; i < depth; i++ {
			d := next - pos[i]
			if testBit(bs, d) {
				ok = false
				break
			}
			newDists[i] = d
		}
		if !ok {
			continue
		}

		// Intra-step collision: a left-side distance introduced in this same
		// step might equal dEnd; the bitset alone can't catch that.
		clash := false
		for i := 0; i < depth; i++ {
			if newDists[i] == dEnd {
				clash = true
				break
			}
		}
		if clash {
			continue
		}

		pos[depth] = next
		for i := 0; i < depth; i++ {
			setBit(bs, newDists[i])
		}
		setBit(bs, dEnd)

		if dfsEndpoint(depth+1, n, L, pos, bs, buf) {
			return true
		}

		for i := 0; i < depth; i++ {
			clrBit(bs, newDists[i])
		}
		clrBit(bs, dEnd)
	}

	return false
}

// solveEndpointDFS is the single-threaded endpoint-aware search for a ruler
// of exactly n marks and length L (mirrors solve_golomb_traditional_opt).
func solveEndpointDFS(n, L int) (*Ruler, bool) {
	if n < 2 {
		return nil, false
	}
	if n == 2 {
		return &Ruler{Positions: []int{0, L}, Length: L, Marks: 2}, true
	}

	pos := make([]int, n)
	pos[0] = 0
	pos[n-1] = L

	bs := newBitset(L)
	setBit(bs, L)

	buf := make([]int, n*n)

	if !dfsEndpoint(1, n, L, pos, bs, buf) {
		return nil, false
	}

	rulerCopy := make([]int, n)
	copy(rulerCopy, pos)
	return &Ruler{Positions: rulerCopy, Length: L, Marks: n}, true
}

// prefixCandidate is a valid, distinct (pos[1], pos[2]) prefix.
type prefixCandidate struct{ s, t int }

// solveEndpointDFSMP is the parallel endpoint-aware search: it enumerates all
// valid (pos[1], pos[2]) prefixes and completes each with dfsEndpoint on a
// worker pool, mirroring solve_golomb_traditional_opt_mt.
func solveEndpointDFSMP(n, L int) (*Ruler, bool) {
	if n <= 3 {
		return solveEndpointDFS(n, L)
	}

	// Same bounds as the C version: t <= T, s <= min(L/2, T-1).
	T := L - (n - 3)
	secondMax := L / 2
	if secondMax > T-1 {
		secondMax = T - 1
	}
	if secondMax < 1 {
		return nil, false
	}

	var candidates []prefixCandidate
	for s := 1; s <= secondMax; s++ {
		for t := s + 1; t <= T; t++ {
			if t-s == s { // the three initial distances must already differ
				continue
			}
			candidates = append(candidates, prefixCandidate{s, t})
		}
	}
	if len(candidates) == 0 {
		return nil, false
	}

	numWorkers := runtime.NumCPU()
	if numWorkers > len(candidates) {
		numWorkers = len(candidates)
	}
	if numWorkers < 1 {
		numWorkers = 1
	}

	var found int32
	var winner *Ruler
	var winnerOnce sync.Once

	// Sized to len(candidates) so the producer below can never block on a
	// send after every worker has already returned (found == 1): it always
	// either finds room in the buffer or observes found != 0 first.
	candChan := make(chan prefixCandidate, len(candidates))
	go func() {
		defer close(candChan)
		for _, c := range candidates {
			if atomic.LoadInt32(&found) != 0 {
				return
			}
			candChan <- c
		}
	}()

	var wg sync.WaitGroup
	wg.Add(numWorkers)
	for w := 0; w < numWorkers; w++ {
		go func() {
			defer wg.Done()

			pos := make([]int, n)
			pos[0] = 0
			pos[n-1] = L
			bs := newBitset(L)
			buf := make([]int, n*n)

			for c := range candChan {
				if atomic.LoadInt32(&found) != 0 {
					return
				}

				clearBitset(bs)
				pos[1] = c.s
				pos[2] = c.t

				initial := [6]int{L, c.s, c.t, c.t - c.s, L - c.s, L - c.t}
				valid := true
				for _, d := range initial {
					if d <= 0 || testBit(bs, d) {
						valid = false
						break
					}
					setBit(bs, d)
				}
				if !valid {
					continue
				}

				if dfsEndpoint(3, n, L, pos, bs, buf) {
					if atomic.CompareAndSwapInt32(&found, 0, 1) {
						winnerOnce.Do(func() {
							rulerCopy := make([]int, n)
							copy(rulerCopy, pos)
							winner = &Ruler{Positions: rulerCopy, Length: L, Marks: n}
						})
					}
					return
				}
			}
		}()
	}
	wg.Wait()

	return winner, winner != nil
}

// Solve finds an optimal Golomb ruler for the given number of marks
func (s *Solver) Solve() *SolverResult {
	start := time.Now()

	if s.config.Marks < 1 {
		return &SolverResult{Duration: time.Since(start)}
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

	startLength := s.config.StartLength
	maxLength := s.config.MaxLength

	if knownOptimal {
		if s.config.UseBest {
			// -b: only ever try the LUT length.
			startLength = optimalRuler.Length
			maxLength = optimalRuler.Length
		} else {
			// Otherwise, search every length up to the LUT length so the
			// shortest valid ruler is still found.
			if startLength == 0 {
				startLength = (s.config.Marks * (s.config.Marks - 1)) / 2
			}
			maxLength = optimalRuler.Length
		}
	} else {
		if startLength == 0 {
			startLength = (s.config.Marks * (s.config.Marks - 1)) / 2
		}
		if maxLength == 0 {
			maxLength = startLength * 2
		}
	}

	var bestRuler *Ruler
	var bestLength = maxLength + 1

	for length := startLength; length <= maxLength; length++ {
		var ruler *Ruler
		var found bool

		if s.config.UseMP {
			ruler, found = solveEndpointDFSMP(s.config.Marks, length)
		} else {
			ruler, found = solveEndpointDFS(s.config.Marks, length)
		}

		if found {
			if bestRuler == nil || length < bestLength {
				bestRuler = ruler
				bestLength = length
			}
			if s.config.UseBest && knownOptimal && length == optimalRuler.Length {
				break
			}
		}
	}

	if bestRuler != nil && !bestRuler.IsValid() {
		// The endpoint-aware DFS is exact and should never produce an
		// invalid ruler; treat this as "not found" rather than reporting
		// a bad result.
		bestRuler = nil
	}

	if knownOptimal && bestRuler != nil {
		optimal := bestLength == optimalRuler.Length

		// For -b at the optimal length, use the canonical LUT ruler for
		// consistency with the C/Java/Rust output (same length, same
		// distance multiset, possibly different mark positions).
		if optimal && s.config.UseBest {
			positions := make([]int, len(optimalRuler.Positions))
			copy(positions, optimalRuler.Positions)
			bestRuler = &Ruler{
				Positions: positions,
				Length:    optimalRuler.Length,
				Marks:     optimalRuler.Marks,
			}
		}

		return &SolverResult{
			Ruler:    bestRuler,
			Found:    true,
			Optimal:  optimal,
			Duration: time.Since(start),
		}
	}

	if bestRuler != nil {
		return &SolverResult{
			Ruler:    bestRuler,
			Found:    true,
			Optimal:  false,
			Duration: time.Since(start),
		}
	}

	return &SolverResult{Duration: time.Since(start)}
}
