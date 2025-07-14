use std::path::PathBuf;
use std::time::Instant;
use clap::Parser;
use chrono;

mod solver;
mod ruler;
mod lut;

use crate::solver::GolombSolver;
use crate::ruler::GolombRuler;

#[derive(Parser, Debug)]
#[command(name = "golomb")]
#[command(about = "Golomb Ruler Finder - Rust Implementation", long_about = None)]
struct Args {
    /// Number of marks
    #[arg(index = 1)]
    marks: usize,

    /// Verbose output
    #[arg(short = 'v', long = "verbose")]
    verbose: bool,

    /// Use multi-processing
    #[arg(long = "mp")]
    multi_processing: bool,

    /// Use best length as starting point
    #[arg(short = 'b', long = "best")]
    best_length: bool,

    /// Output file
    #[arg(short = 'o', long = "output")]
    output_file: Option<PathBuf>,
}

fn main() {
    let args = Args::parse();
    
    println!("Golomb Ruler Finder - Rust Edition");
    println!("Start time: {}", chrono::Local::now().format("%Y-%m-%d %H:%M:%S"));
    println!("Searching for optimal ruler with {} marks", args.marks);
    
    let config = solver::SolverConfig {
        marks: args.marks,
        verbose: args.verbose,
        multi_processing: args.multi_processing,
        best_length: args.best_length,
    };
    
    let start_time = Instant::now();
    let solver = GolombSolver::new(config);
    let result = solver.solve();
    let elapsed = start_time.elapsed();
    
    if let Some(ruler) = result.ruler {
        println!("Found ruler with {} marks and length {}", ruler.marks, ruler.length());
        println!("Positions: {}", ruler.positions().iter().map(|&p| p.to_string()).collect::<Vec<_>>().join(" "));
        println!("Elapsed time: {:.6} seconds", elapsed.as_secs_f64());
        println!("States searched: {}", result.states_examined);
        
        if let Some(is_optimal) = result.optimal {
            println!("Optimal: {}", if is_optimal { "yes" } else { "no" });
        }
        
        // Generate output file
        let option_str = generate_option_string(&args);
        let output_file = args.output_file.unwrap_or_else(|| {
            let mut path = PathBuf::from("./out");
            std::fs::create_dir_all(&path).unwrap_or_default();
            path.push(format!("GOL_n{}_{}.txt", args.marks, option_str));
            path
        });
        
        generate_output(&output_file, &ruler, elapsed.as_secs_f64(), &option_str, result.optimal);
        println!("Results written to {}", output_file.display());
    } else {
        println!("No ruler found");
    }
}

fn generate_option_string(args: &Args) -> String {
    let mut options = Vec::new();
    
    if args.multi_processing {
        options.push("mp");
    }
    
    if args.best_length {
        options.push("b");
    }
    
    if args.verbose {
        options.push("v");
    }
    
    if options.is_empty() {
        "std".to_string()
    } else {
        options.join("_")
    }
}

fn generate_output(
    path: &PathBuf,
    ruler: &GolombRuler,
    elapsed_time: f64,
    options_str: &str,
    optimal: Option<bool>,
) {
    use std::collections::HashSet;
    use std::fs::File;
    use std::io::Write;
    
    let mut file = File::create(path).unwrap();
    
    writeln!(file, "length={}", ruler.length()).unwrap();
    writeln!(file, "marks={}", ruler.marks).unwrap();
    writeln!(file, "positions={}", ruler.positions().iter().map(|&p| p.to_string()).collect::<Vec<_>>().join(" ")).unwrap();
    
    // Calculate distances
    let mut distances = Vec::new();
    let mut distance_set = HashSet::new();
    
    for i in 0..ruler.positions().len() {
        for j in i + 1..ruler.positions().len() {
            let dist = ruler.positions()[j] - ruler.positions()[i];
            distances.push(dist);
            distance_set.insert(dist);
        }
    }
    
    distances.sort();
    
    // Calculate missing distances
    let mut missing = Vec::new();
    for d in 1..=ruler.length() {
        if !distance_set.contains(&d) {
            missing.push(d);
        }
    }
    
    writeln!(file, "distances={}", distances.iter().map(|&d| d.to_string()).collect::<Vec<_>>().join(" ")).unwrap();
    writeln!(file, "missing={}", missing.iter().map(|&d| d.to_string()).collect::<Vec<_>>().join(" ")).unwrap();
    writeln!(file, "seconds={}", elapsed_time).unwrap();
    writeln!(file, "time={:.3} s", elapsed_time).unwrap();
    writeln!(file, "options={}", options_str).unwrap();
    
    if let Some(is_optimal) = optimal {
        writeln!(file, "optimal={}", if is_optimal { "yes" } else { "no" }).unwrap();
    }
    
    writeln!(file, "").unwrap();
}
