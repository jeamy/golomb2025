#!/usr/bin/env ruby
# Golomb Ruler Finder - Ruby Implementation
# Compatible with C and Go versions

require 'optparse'
require 'time'
require 'set'
require_relative 'solver'
require_relative 'ruler'

# Hilfsfunktion zum Generieren der Ausgabedatei
def generate_output(filename, ruler, elapsed_time, options_str, optimal)
  File.open(filename, 'w') do |file|
    file.puts "length=#{ruler.length}"
    file.puts "marks=#{ruler.marks}"
    file.puts "positions=#{ruler.positions.join(' ')}"
    
    # Calculate all distances
    distances = []
    missing = []
    for i in 0...ruler.positions.length
      for j in (i+1)...ruler.positions.length
        distances << ruler.positions[j] - ruler.positions[i]
      end
    end
    distances.sort!
    
    # Find missing distances
    current_distances = Set.new(distances)
    (1..ruler.length).each do |d|
      missing << d unless current_distances.include?(d)
    end
    
    file.puts "distances=#{distances.join(' ')}"
    file.puts "missing=#{missing.join(' ')}"
    file.puts "seconds=#{elapsed_time}"
    file.puts "time=#{sprintf('%.3f', elapsed_time)} s"
    file.puts "options=#{options_str}"
    file.puts "optimal=#{optimal ? 'yes' : 'no'}" if optimal != nil
    file.puts ""
  end
end

# Parse command-line arguments
options = {
  verbose: false,
  best: false,
  multi_processing: false,
  output_file: nil
}

# Create a new OptionParser
parser = OptionParser.new do |opts|
  opts.banner = "Usage: #{File.basename($PROGRAM_NAME)} <marks> [options]"
  opts.on('-v', '--verbose', 'Enable verbose output') { options[:verbose] = true }
  opts.on('-b', '--best', 'Use best known ruler length as target') { options[:best] = true }
  opts.on('-mp', '--multi-processing', 'Use multiple processes for search') { options[:multi_processing] = true }
  opts.on('-o FILE', '--output=FILE', 'Output file') { |file| options[:output_file] = file }
  opts.on('-h', '--help', 'Show this help message') do
    puts opts
    exit
  end
end

begin
  parser.parse!
  
  # Get the number of marks from the first positional argument
  if ARGV.empty?
    puts "Error: marks argument is required"
    puts parser.help
    exit 1
  end
  
  marks = Integer(ARGV[0])
  
  if marks < 2
    puts "Error: marks must be at least 2"
    exit 1
  end
  
  # Create configuration
  config = {
    marks: marks,
    verbose: options[:verbose],
    best_length: options[:best],
    multi_processing: options[:multi_processing]
  }
  
  # Generate option string for filename
  option_str = ""
  option_str += "mp_" if options[:multi_processing]
  option_str += "b_" if options[:best]
  option_str += "v" if options[:verbose]
  option_str.chomp!('_')
  
  # Create the output filename if not specified
  unless options[:output_file]
    out_dir = File.join(File.dirname(__FILE__), "out")
    Dir.mkdir(out_dir) unless Dir.exist?(out_dir)
    options[:output_file] = File.join(out_dir, "GOL_n#{marks}_#{option_str.empty? ? 'std' : option_str}.txt")
  end

  puts "Golomb Ruler Finder - Ruby Edition"
  puts "Start time: #{Time.now.strftime('%Y-%m-%d %H:%M:%S')}"
  puts "Searching for #{options[:best] ? 'optimal' : 'valid'} ruler with #{marks} marks"
  
  # Create the solver and search for a ruler
  solver = GolombSolver.new(config)
  start_time = Time.now
  result = solver.solve
  elapsed = Time.now - start_time
  
  # Print the result
  if result[:found]
    ruler = result[:ruler]
    puts "Found ruler with #{ruler.marks} marks and length #{ruler.length}"
    puts "Positions: #{ruler.positions.join(' ')}"
    puts "Elapsed time: #{elapsed} seconds"
    puts "States searched: #{solver.states_searched}"
    puts "Optimal: #{result[:optimal] ? 'yes' : 'no'}"
    
    # Generate the output file
    generate_output(options[:output_file], ruler, elapsed, option_str, result[:optimal])
    puts "Results written to #{options[:output_file]}"
  else
    puts "No valid ruler found"
  end
  
rescue OptionParser::InvalidOption => e
  puts e
  puts parser.help
  exit 1
rescue ArgumentError => e
  puts "Error: #{e.message}"
  puts parser.help
  exit 1
end

def generate_output(filename, ruler, elapsed_time, options_str, optimal)
  File.open(filename, 'w') do |file|
    file.puts "length=#{ruler.length}"
    file.puts "marks=#{ruler.marks}"
    file.puts "positions=#{ruler.positions.join(' ')}"
    
    # Calculate all distances
    distances = []
    missing = []
    for i in 0...ruler.positions.length
      for j in (i+1)...ruler.positions.length
        distances << ruler.positions[j] - ruler.positions[i]
      end
    end
    distances.sort!
    
    # Find missing distances
    current_distances = Set.new(distances)
    (1..ruler.length).each do |d|
      missing << d unless current_distances.include?(d)
    end
    
    file.puts "distances=#{distances.join(' ')}"
    file.puts "missing=#{missing.join(' ')}"
    file.puts "seconds=#{elapsed_time}"
    file.puts "time=#{sprintf('%.3f', elapsed_time)} s"
    file.puts "options=#{options_str}"
    file.puts "optimal=#{optimal ? 'yes' : 'no'}" if optimal != nil
    file.puts ""
  end
end
