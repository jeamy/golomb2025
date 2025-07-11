package com.golomb;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.Arrays;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Main class for the Golomb ruler finder application.
 * Java 24 port of the original C implementation with multi-processing support.
 */
public class GolombMain {
    
    private static final DateTimeFormatter TIME_FORMATTER = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
    
    public static void main(String[] args) {
        if (args.length == 0 || Arrays.asList(args).contains("--help")) {
            printHelp();
            return;
        }
        
        try {
            Config config = parseArgs(args);
            runSearch(config);
        } catch (IllegalArgumentException e) {
            System.err.println("Error: " + e.getMessage());
            System.exit(1);
        } catch (Exception e) {
            System.err.println("Unexpected error: " + e.getMessage());
            e.printStackTrace();
            System.exit(1);
        }
    }
    
    private static void printHelp() {
        System.out.println("Usage: java -jar golomb-ruler.jar <n> [options]");
        System.out.println();
        System.out.println("Finds an optimal Golomb ruler with <n> marks.");
        System.out.println();
        System.out.println("Options:");
        System.out.println("  -v, --verbose      Enable verbose output during search.");
        System.out.println("  -s, --single       Force single-threaded solver.");
        System.out.println("  -mp                Use multi-threaded solver (default).");
        System.out.println("  -b                 Use best-known ruler length as starting point.");
        System.out.println("  -o <file>          Write the found ruler to a file.");
        System.out.println("  -vt <min>          Periodic heartbeat every <min> minutes.");
        System.out.println("  --help             Display this help message and exit.");
        System.out.println();
        System.out.println("Examples:");
        System.out.println("  java -jar golomb-ruler.jar 5");
        System.out.println("  java -jar golomb-ruler.jar 8 -v -mp");
        System.out.println("  java -jar golomb-ruler.jar 10 -b -o ruler10.txt");
    }
    
    private static Config parseArgs(String[] args) {
        Config config = new Config();
        
        try {
            config.marks = Integer.parseInt(args[0]);
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException("Invalid number of marks: " + args[0]);
        }
        
        if (config.marks < 2 || config.marks > 32) {
            throw new IllegalArgumentException("Marks must be between 2 and 32");
        }
        
        for (int i = 1; i < args.length; i++) {
            switch (args[i]) {
                case "-v", "--verbose" -> config.verbose = true;
                case "-s", "--single" -> config.useParallel = false;
                case "-mp" -> config.useParallel = true;
                case "-b" -> config.useHeuristic = true;
                case "-o" -> {
                    if (i + 1 >= args.length) {
                        throw new IllegalArgumentException("Option -o requires a filename");
                    }
                    config.outputFile = args[++i];
                }
                case "-vt" -> {
                    if (i + 1 >= args.length) {
                        throw new IllegalArgumentException("Option -vt requires a number");
                    }
                    try {
                        config.heartbeatMinutes = Integer.parseInt(args[++i]);
                    } catch (NumberFormatException e) {
                        throw new IllegalArgumentException("Invalid heartbeat interval: " + args[i]);
                    }
                }
                default -> throw new IllegalArgumentException("Unknown option: " + args[i]);
            }
        }
        
        return config;
    }
    
    private static void runSearch(Config config) {
        System.out.println("Golomb Ruler Finder - Java 24 Edition");
        System.out.println("Start time: " + LocalDateTime.now().format(TIME_FORMATTER));
        System.out.println("Searching for optimal ruler with " + config.marks + " marks");
        
        if (config.useHeuristic) {
            int optimalLength = GolombLUT.getOptimalLength(config.marks);
            if (optimalLength > 0) {
                System.out.println("Using heuristic: known optimal length is " + optimalLength);
                config.maxLength = optimalLength;
            }
        }
        
        // Set up heartbeat if requested
        ScheduledExecutorService heartbeatService = null;
        AtomicLong startTime = new AtomicLong(System.currentTimeMillis());
        
        if (config.heartbeatMinutes > 0) {
            heartbeatService = Executors.newScheduledThreadPool(1);
            heartbeatService.scheduleAtFixedRate(() -> {
                long elapsed = System.currentTimeMillis() - startTime.get();
                System.out.printf("[VT] %s elapsed%n", formatElapsed(elapsed));
            }, config.heartbeatMinutes, config.heartbeatMinutes, TimeUnit.MINUTES);
        }
        
        try {
            GolombSolver solver = new GolombSolver(config.verbose, config.useParallel);
            
            long searchStart = System.currentTimeMillis();
            GolombRuler result = solver.solve(config.marks, config.maxLength);
            long searchEnd = System.currentTimeMillis();
            
            if (result == null) {
                System.err.println("Could not find a Golomb ruler with " + config.marks + 
                                 " marks within length limit " + config.maxLength);
                System.exit(1);
            }
            
            // Display results
            System.out.println("End time:   " + LocalDateTime.now().format(TIME_FORMATTER));
            System.out.println("Found ruler: " + Arrays.toString(result.positions()));
            System.out.println("Elapsed time: " + formatElapsed(searchEnd - searchStart));
            
            // Calculate and display distances
            int[] distances = result.calculateDistances();
            int[] missing = result.findMissingDistances();
            
            System.out.println("Distances (" + distances.length + "): " + Arrays.toString(distances));
            System.out.println("Missing (" + missing.length + "): " + Arrays.toString(missing));
            
            // Check optimality
            boolean optimal = GolombLUT.isOptimal(result);
            System.out.println("Status: " + (optimal ? "Optimal ✅" : "Not optimal ❌"));
            
            // Write to file
            writeResultToFile(config, result, searchEnd - searchStart, optimal);
            
            System.exit(optimal ? 0 : 1);
            
        } finally {
            if (heartbeatService != null) {
                heartbeatService.shutdown();
            }
        }
    }
    
    private static void writeResultToFile(Config config, GolombRuler result, long elapsedMs, boolean optimal) {
        String filename = config.outputFile;
        if (filename == null) {
            // Create output directory if it doesn't exist
            try {
                Files.createDirectories(Paths.get("out"));
            } catch (IOException e) {
                System.err.println("Warning: Could not create output directory: " + e.getMessage());
                return;
            }
            
            StringBuilder suffix = new StringBuilder();
            if (!config.useParallel) suffix.append("_s");
            if (config.useParallel) suffix.append("_mp");
            if (config.useHeuristic) suffix.append("_b");
            if (config.verbose) suffix.append("_v");
            
            filename = "out/GOL_n" + config.marks + suffix + ".txt";
        }
        
        try {
            StringBuilder content = new StringBuilder();
            content.append(result.toFormattedString());
            content.append("seconds=").append(String.format("%.6f", elapsedMs / 1000.0)).append("\n");
            content.append("time=").append(formatElapsed(elapsedMs)).append("\n");
            
            StringBuilder options = new StringBuilder();
            if (!config.useParallel) options.append("-s ");
            if (config.useParallel) options.append("-mp ");
            if (config.useHeuristic) options.append("-b ");
            if (config.verbose) options.append("-v ");
            
            content.append("options=").append(options.toString().trim()).append("\n");
            content.append("optimal=").append(optimal ? "yes" : "no").append("\n");
            
            Files.writeString(Paths.get(filename), content.toString());
            System.out.println("Results written to: " + filename);
            
        } catch (IOException e) {
            System.err.println("Error writing to file " + filename + ": " + e.getMessage());
        }
    }
    
    private static String formatElapsed(long millis) {
        long seconds = millis / 1000;
        long minutes = seconds / 60;
        long hours = minutes / 60;
        
        seconds %= 60;
        minutes %= 60;
        
        if (hours > 0) {
            return String.format("%d:%02d:%02d.%03d", hours, minutes, seconds, millis % 1000);
        } else if (minutes > 0) {
            return String.format("%02d:%02d.%03d", minutes, seconds, millis % 1000);
        } else {
            return String.format("%.3f s", millis / 1000.0);
        }
    }
    
    private static class Config {
        int marks;
        boolean verbose = false;
        boolean useParallel = true;
        boolean useHeuristic = false;
        String outputFile = null;
        int heartbeatMinutes = 0;
        int maxLength = 600; // Default maximum length
    }
}
