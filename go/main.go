package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// Config holds the command-line configuration
type Config struct {
	Marks      int
	Verbose    bool
	UseMP      bool
	UseBest    bool
	OutputFile string
	ShowHelp   bool
}

// parseArgs parses command-line arguments
func parseArgs() (*Config, error) {
	config := &Config{}
	
	// Define a custom flag set to handle flags after positional arguments
	flagSet := flag.NewFlagSet("golomb", flag.ExitOnError)
	
	// Define flags
	flagSet.BoolVar(&config.Verbose, "v", false, "Enable verbose output")
	flagSet.BoolVar(&config.UseMP, "mp", false, "Use multi-processing solver")
	flagSet.BoolVar(&config.UseBest, "b", false, "Use best-known ruler length as starting point")
	flagSet.StringVar(&config.OutputFile, "o", "", "Write result to specific output file")
	flagSet.BoolVar(&config.ShowHelp, "help", false, "Show help message")
	
	// Check for help flag first
	for _, arg := range os.Args[1:] {
		if arg == "-help" || arg == "--help" {
			config.ShowHelp = true
			return config, nil
		}
	}
	
	// Get positional arguments
	args := os.Args[1:]
	if len(args) < 1 {
		return nil, fmt.Errorf("missing required argument: marks")
	}
	
	// First argument should be marks
	marks, err := strconv.Atoi(args[0])
	if err != nil {
		return nil, fmt.Errorf("invalid marks argument: %s", args[0])
	}
	if marks < 1 {
		return nil, fmt.Errorf("marks must be positive, got %d", marks)
	}
	config.Marks = marks
	
	// Parse remaining arguments as flags
	if len(args) > 1 {
		err := flagSet.Parse(args[1:])
		if err != nil {
			return nil, err
		}
	}
	
	// ShowHelp is already checked above
	return config, nil
}

// showHelp displays the help message
func showHelp() {
	fmt.Println("Golomb Ruler Finder (Go Implementation)")
	fmt.Println("======================================")
	fmt.Println()
	fmt.Println("Usage: golomb <marks> [options]")
	fmt.Println()
	fmt.Println("Arguments:")
	fmt.Println("  marks      Number of marks (required)")
	fmt.Println()
	fmt.Println("Options:")
	fmt.Println("  -v         Enable verbose output")
	fmt.Println("  -mp        Use multi-processing solver")
	fmt.Println("  -b         Use best-known ruler length as starting point")
	fmt.Println("  -o <file>  Write output to file")
	fmt.Println("  -help      Show this help message")
}

// formatDuration formats a duration in a human-readable way
func formatDuration(d time.Duration) string {
	if d < time.Second {
		return fmt.Sprintf("%.3f ms", float64(d.Nanoseconds())/1e6)
	}
	return fmt.Sprintf("%.3f s", d.Seconds())
}

// writeOutput writes the result to an output file
func writeOutput(config *Config, result *SolverResult) error {
	if result.Ruler == nil {
		return fmt.Errorf("no ruler found")
	}
	
	// Generate output filename if not specified
	outputFile := config.OutputFile
	if outputFile == "" {
		// Generate filename with options in the correct order
		filenameOptions := ""
		
		// Order matters: first mp, then b, then v to match C implementation
		if config.UseMP {
			filenameOptions += "mp_"
		}
		if config.UseBest {
			filenameOptions += "b_"
		}
		if config.Verbose {
			filenameOptions += "v"
		}
		
		// Remove trailing underscore if present
		filenameOptions = strings.TrimSuffix(filenameOptions, "_")
		
		// If no options were used, use "std" (standard)
		if filenameOptions == "" {
			filenameOptions = "std"
		}
		
		// Create out directory if it doesn't exist
		outDir := "out"
		if err := os.MkdirAll(outDir, 0755); err != nil {
			return fmt.Errorf("failed to create output directory: %v", err)
		}
		
		outputFile = filepath.Join(outDir, fmt.Sprintf("GOL_n%d_%s.txt", config.Marks, filenameOptions))
	}
	
	file, err := os.Create(outputFile)
	if err != nil {
		return fmt.Errorf("failed to create output file: %v", err)
	}
	defer file.Close()
	
	ruler := result.Ruler
	distances := ruler.CalculateDistances()
	missing := ruler.GetMissingDistances()
	
	// Write output in the same format as C version
	fmt.Fprintf(file, "length=%d\n", ruler.Length)
	fmt.Fprintf(file, "marks=%d\n", ruler.Marks)
	
	// Format positions
	posStr := make([]string, len(ruler.Positions))
	for i, pos := range ruler.Positions {
		posStr[i] = strconv.Itoa(pos)
	}
	fmt.Fprintf(file, "positions=%s\n", strings.Join(posStr, " "))
	
	// Format distances
	distStr := make([]string, len(distances))
	for i, dist := range distances {
		distStr[i] = strconv.Itoa(dist)
	}
	fmt.Fprintf(file, "distances=%s\n", strings.Join(distStr, " "))
	
	// Format missing distances
	missStr := make([]string, len(missing))
	for i, miss := range missing {
		missStr[i] = strconv.Itoa(miss)
	}
	fmt.Fprintf(file, "missing=%s\n", strings.Join(missStr, " "))
	
	fmt.Fprintf(file, "seconds=%.6f\n", result.Duration.Seconds())
	fmt.Fprintf(file, "time=%s\n", formatDuration(result.Duration))
	
	// Format options
	options := []string{}
	if config.UseMP {
		options = append(options, "mp")
	}
	if config.UseBest {
		options = append(options, "b")
	}
	if config.Verbose {
		options = append(options, "v")
	}
	
	// Format options for output file content
	optionsStr := ""
	if config.UseMP {
		optionsStr += "mp "
	}
	if config.UseBest {
		optionsStr += "b "
	}
	if config.Verbose {
		optionsStr += "v"
	}
	
	// Remove trailing space if present
	optionsStr = strings.TrimSpace(optionsStr)
	
	// If no options were used, use "std" (standard)
	if optionsStr == "" {
		optionsStr = "std"
	}
	fmt.Fprintf(file, "options=%s\n", optionsStr)
	
	// Add optimal status if we know the optimal length
	if optimalLength := GetOptimalLength(config.Marks); optimalLength > 0 {
		if result.Optimal {
			fmt.Fprintf(file, "optimal=yes\n")
		} else {
			fmt.Fprintf(file, "optimal=no\n")
		}
	}
	
	fmt.Printf("Results written to: %s\n", outputFile)
	return nil
}

func main() {
	config, err := parseArgs()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		fmt.Fprintf(os.Stderr, "Use -help for usage information\n")
		os.Exit(1)
	}
	
	// Flags have been parsed successfully
	
	if config.ShowHelp {
		showHelp()
		return
	}
	
	fmt.Println("Golomb Ruler Finder - Go Edition")
	
	startTime := time.Now()
	fmt.Printf("Start time: %s\n", startTime.Format("2006-01-02 15:04:05"))
	fmt.Printf("Searching for optimal ruler with %d marks\n", config.Marks)
	
	// Create solver configuration
	solverConfig := SolverConfig{
		Marks:   config.Marks,
		Verbose: config.Verbose,
		UseMP:   config.UseMP,
		UseBest: config.UseBest,
	}
	
	// Create and run solver
	solver := NewSolver(solverConfig)
	result := solver.Solve()
	
	endTime := time.Now()
	fmt.Printf("End time:   %s\n", endTime.Format("2006-01-02 15:04:05"))
	
	if result.Found {
		fmt.Printf("Found ruler: %s\n", result.Ruler.String())
		fmt.Printf("Elapsed time: %s\n", formatDuration(result.Duration))
		
		// Print distances and missing distances
		distances := result.Ruler.CalculateDistances()
		missing := result.Ruler.GetMissingDistances()
		
		fmt.Printf("Distances (%d): %v\n", len(distances), distances)
		if len(missing) > 0 {
			fmt.Printf("Missing (%d): %v\n", len(missing), missing)
		}
		
		// Print optimal status
		if result.Optimal {
			fmt.Println("Status: Optimal ✅")
		} else if optimalLength := GetOptimalLength(config.Marks); optimalLength > 0 {
			fmt.Println("Status: Not optimal ❌")
		}
		
		// Write output file
		if err := writeOutput(config, result); err != nil {
			fmt.Fprintf(os.Stderr, "Error writing output: %v\n", err)
			os.Exit(1)
		}
	} else {
		fmt.Printf("No ruler found for %d marks\n", config.Marks)
		fmt.Printf("Elapsed time: %s\n", formatDuration(result.Duration))
		os.Exit(1)
	}
}
