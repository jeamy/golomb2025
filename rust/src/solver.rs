//! Module implementing the Golomb ruler search algorithm.
//!
//! The search engine is a direct port of the C solver's endpoint-aware DFS
//! (`src/solver_traditional_opt.c`, the `-to` solver) and the technique used
//! by the CUDA variant (`nvidia/golomb_bits.h`): both ruler endpoints (`0`
//! and `L`) are fixed before any inner mark is placed, so every candidate
//! mark's distance to the fixed right endpoint is checked immediately
//! instead of only once all marks are placed. Used distances are tracked in
//! a bitset (one bit per distance value) that is updated incrementally on
//! descent and rolled back on backtrack, replacing the previous approach of
//! cloning the whole distance set at every node.

use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
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

    /// Number of DFS nodes examined during search
    pub states_examined: usize,
}

// ---------------------------------------------------------------------------
// Bitset helpers: one bit per distance value, exactly as in
// src/solver_traditional_opt.c and nvidia/golomb_bits.h.
// ---------------------------------------------------------------------------

fn new_bitset(max_val: usize) -> Vec<u64> {
    vec![0u64; max_val / 64 + 1]
}

#[inline(always)]
fn set_bit(bs: &mut [u64], idx: usize) {
    bs[idx >> 6] |= 1u64 << (idx & 63);
}

#[inline(always)]
fn clr_bit(bs: &mut [u64], idx: usize) {
    bs[idx >> 6] &= !(1u64 << (idx & 63));
}

#[inline(always)]
fn test_bit(bs: &[u64], idx: usize) -> bool {
    bs[idx >> 6] & (1u64 << (idx & 63)) != 0
}

// ---------------------------------------------------------------------------
// dfs_endpoint places inner marks pos[depth..n-2] given that pos[0] = 0 and
// pos[n-1] = L are already fixed and every distance implied by
// pos[0..depth-1] (plus the endpoint distances they imply) is already set
// in `bs`.
//
// `buf` is a reusable (n*n)-sized scratch buffer; buf[depth*n..depth*n+depth]
// holds the new left-side distances discovered for `next` at this depth, so
// each recursion level gets its own slice without allocating on every call.
// ---------------------------------------------------------------------------
fn dfs_endpoint(
    depth: usize,
    n: usize,
    l: usize,
    pos: &mut [usize],
    bs: &mut [u64],
    buf: &mut [usize],
    states: &mut usize,
) -> bool {
    *states += 1;

    if depth == n - 1 {
        return true;
    }

    let last = pos[depth - 1];

    // After placing `next`, (n-2-depth) more inner marks plus the endpoint L
    // still need at least 1 apart each: next + (n-1-depth) <= L.
    let mut max_next = l - (n - 1 - depth);

    // Symmetry break on the first inner mark: at most L/2 (eliminates the
    // mirror image of every ruler).
    if depth == 1 {
        let limit = (l / 2).max(last + 1);
        if max_next > limit {
            max_next = limit;
        }
    }

    for next in (last + 1)..=max_next {
        let gap = next - last;
        if test_bit(bs, gap) {
            continue;
        }

        // Endpoint-aware pruning: check the distance to the fixed right
        // endpoint immediately, instead of only once all marks are placed.
        let d_end = l - next;
        if test_bit(bs, d_end) {
            continue;
        }

        let mut ok = true;
        for i in 0..depth {
            let d = next - pos[i];
            if test_bit(bs, d) {
                ok = false;
                break;
            }
            buf[depth * n + i] = d;
        }
        if !ok {
            continue;
        }

        // Intra-step collision: a left-side distance introduced in this same
        // step might equal d_end; the bitset alone can't catch that.
        let mut clash = false;
        for i in 0..depth {
            if buf[depth * n + i] == d_end {
                clash = true;
                break;
            }
        }
        if clash {
            continue;
        }

        pos[depth] = next;
        for i in 0..depth {
            set_bit(bs, buf[depth * n + i]);
        }
        set_bit(bs, d_end);

        if dfs_endpoint(depth + 1, n, l, pos, bs, buf, states) {
            return true;
        }

        for i in 0..depth {
            clr_bit(bs, buf[depth * n + i]);
        }
        clr_bit(bs, d_end);
    }

    false
}

/// Single-threaded endpoint-aware search for a ruler of exactly `n` marks
/// and length `l` (mirrors `solve_golomb_traditional_opt`).
fn solve_endpoint_dfs(n: usize, l: usize) -> (Option<Vec<usize>>, usize) {
    if n < 2 {
        return (None, 0);
    }
    if n == 2 {
        return (Some(vec![0, l]), 0);
    }

    let mut pos = vec![0usize; n];
    pos[n - 1] = l;

    let mut bs = new_bitset(l);
    set_bit(&mut bs, l);

    let mut buf = vec![0usize; n * n];
    let mut states = 0usize;

    if dfs_endpoint(1, n, l, &mut pos, &mut bs, &mut buf, &mut states) {
        (Some(pos), states)
    } else {
        (None, states)
    }
}

/// Parallel endpoint-aware search: enumerates all valid `(pos[1], pos[2])`
/// prefixes and completes each with `dfs_endpoint` via rayon, mirroring
/// `solve_golomb_traditional_opt_mt` in the C code.
fn solve_endpoint_dfs_mp(n: usize, l: usize) -> (Option<Vec<usize>>, usize) {
    if n <= 3 {
        return solve_endpoint_dfs(n, l);
    }

    // Same bounds as the C version: t <= t_max, s <= min(L/2, t_max - 1).
    let t_max = l - (n - 3);
    let second_max = (l / 2).min(t_max.saturating_sub(1));
    if second_max < 1 {
        return (None, 0);
    }

    let mut candidates = Vec::new();
    for s in 1..=second_max {
        for t in (s + 1)..=t_max {
            if t - s == s {
                // the three initial distances must already differ
                continue;
            }
            candidates.push((s, t));
        }
    }
    if candidates.is_empty() {
        return (None, 0);
    }

    let found = AtomicBool::new(false);
    let states = AtomicUsize::new(0);

    let winner = candidates.par_iter().find_map_any(|&(s, t)| {
        if found.load(Ordering::Relaxed) {
            return None;
        }

        let mut pos = vec![0usize; n];
        pos[1] = s;
        pos[2] = t;
        pos[n - 1] = l;

        let mut bs = new_bitset(l);
        let initial = [l, s, t, t - s, l - s, l - t];
        let mut valid = true;
        for &d in &initial {
            if test_bit(&bs, d) {
                valid = false;
                break;
            }
            set_bit(&mut bs, d);
        }
        if !valid {
            return None;
        }

        let mut buf = vec![0usize; n * n];
        let mut local_states = 0usize;
        let hit = dfs_endpoint(3, n, l, &mut pos, &mut bs, &mut buf, &mut local_states);
        // One atomic add per candidate rather than one per DFS node avoids
        // cache-line contention across threads on the hot path.
        states.fetch_add(local_states, Ordering::Relaxed);

        if hit {
            found.store(true, Ordering::Relaxed);
            Some(pos)
        } else {
            None
        }
    });

    (winner, states.load(Ordering::Relaxed))
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
            return SolverResult { ruler: Some(ruler), optimal: Some(true), states_examined: 0 };
        } else if marks == 2 {
            let ruler = GolombRuler::new(vec![0, 1], marks);
            return SolverResult { ruler: Some(ruler), optimal: Some(true), states_examined: 0 };
        }

        let optimal_length = lut::get_optimal_length(marks);
        let canonical_ruler = lut::get_optimal_ruler(marks);

        if self.config.verbose && self.config.best_length && optimal_length.is_some() {
            println!("Using known optimal length from LUT: {}", optimal_length.unwrap_or(0));
        }

        let lower_bound = (((marks * (marks - 1)) as f64).sqrt() as usize).max(marks - 1);
        let upper_bound = optimal_length.unwrap_or(marks * marks);

        if self.config.verbose {
            println!("Searching lengths from {} to {}", lower_bound, upper_bound);
        }

        // If -b is set and we know the optimal length, try only that length.
        if self.config.best_length && optimal_length.is_some() {
            let length = optimal_length.unwrap();

            if self.config.verbose {
                println!("Starting with optimal length from LUT: {}", length);
            }

            let (found, states) = self.search_length(length);

            if let Some(mut positions) = found {
                // For -b at the optimal length, use the canonical LUT ruler
                // for consistency with the C/Go/Java output (same length,
                // same distance multiset, possibly different positions).
                if let Some(canonical_pos) = canonical_ruler.clone() {
                    positions = canonical_pos;
                }
                return SolverResult {
                    ruler: Some(GolombRuler::new(positions, marks)),
                    optimal: Some(true),
                    states_examined: states,
                };
            }
        }

        // Try progressively increasing lengths.
        for length in lower_bound..=upper_bound {
            if self.config.best_length && optimal_length == Some(length) {
                continue; // already tried above
            }

            if self.config.verbose {
                println!("Searching length {}...", length);
            }

            let (found, states) = self.search_length(length);

            if let Some(mut positions) = found {
                let is_optimal = optimal_length.map(|ol| length == ol);
                if is_optimal == Some(true) {
                    if let Some(canonical_pos) = canonical_ruler.clone() {
                        positions = canonical_pos;
                    }
                }
                return SolverResult {
                    ruler: Some(GolombRuler::new(positions, marks)),
                    optimal: is_optimal,
                    states_examined: states,
                };
            }
        }

        SolverResult { ruler: None, optimal: None, states_examined: 0 }
    }

    /// Searches for a ruler of exactly `length`, single- or multi-threaded.
    fn search_length(&self, length: usize) -> (Option<Vec<usize>>, usize) {
        if self.config.multi_processing {
            solve_endpoint_dfs_mp(self.config.marks, length)
        } else {
            solve_endpoint_dfs(self.config.marks, length)
        }
    }
}
