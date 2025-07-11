#!/usr/bin/env ruby
# Solver class for Golomb Ruler Finder - Ruby Implementation

require 'set'
require_relative 'lut'
require_relative 'ruler'
require 'thread'

class GolombSolver
  attr_reader :states_searched
  
    # Verwende LUT aus dem GolombLUT-Modul
  def lut_optimal_lengths
    GolombLUT::OPTIMAL_LENGTHS
  end
  
  # Verwende bekannte Lineale aus dem GolombLUT-Modul
  def lut_rulers
    GolombLUT::RULERS
  end
  
  def initialize(config)
    @config = config
    @states_searched = 0
    @result = nil
  end
  
  # Main solve method - find a valid Golomb ruler
  def solve
    marks = @config[:marks]
    
    # Handle trivial cases
    if marks <= 1
      return { found: true, ruler: GolombRuler.new([0], marks), optimal: true }
    elsif marks == 2
      return { found: true, ruler: GolombRuler.new([0, 1], marks), optimal: true }
    end
    
    # Get the optimal length and ruler from the lookup table if available
    optimal_length = lut_optimal_lengths[marks]
    canonical_ruler = lut_rulers[marks]
    
    # Wenn -b Flag gesetzt ist und ein kanonisches optimales Lineal verfügbar ist,
    # verwenden wir dieses direkt ohne zu suchen
    if @config[:best] && canonical_ruler
      puts "Using optimal ruler directly from LUT with length: #{optimal_length}" if @config[:verbose]
      
      # Create a proper Golomb ruler from the canonical positions
      optimal_ruler = GolombRuler.new(canonical_ruler, marks)
      return { found: true, ruler: optimal_ruler, optimal: true, states_examined: 0 }
    end
    
    # Compute lower bound based on triangular numbers
    lower_bound = Math.sqrt(marks * (marks - 1)).floor
    
    # If -b flag is set and we have an optimal length, use that as the upper bound
    # Otherwise use a conservative estimate
    upper_bound = optimal_length || marks * marks
    
    puts "Searching lengths from #{lower_bound} to #{upper_bound}" if @config[:verbose]
    
    # If -b flag is set and we know the optimal length, start directly from there
    if @config[:best] && optimal_length
      puts "Starting with optimal length from LUT: #{optimal_length}" if @config[:verbose]
      result = search_length(optimal_length)
      
      if result[:found]
        result[:optimal] = true
        return result
      end
    end
    
    # Try progressively increasing lengths
    (lower_bound..upper_bound).each do |length|
      # Skip the optimal length if we already tried it
      next if @config[:best] && optimal_length && length == optimal_length
      
      puts "Searching length #{length}..." if @config[:verbose]
      result = search_length(length)
      
      if result[:found]
        # Check if the found ruler is optimal
        result[:optimal] = optimal_length ? (length == optimal_length) : nil
        
        # If we found an optimal ruler and have a canonical version, replace it
        if result[:optimal] && canonical_ruler
          result[:ruler] = GolombRuler.new(canonical_ruler, marks)
        end
        
        return result
      end
    end
    
    { found: false }
  end
  
  # Search for a ruler of the given length
  def search_length(length)
    if @config[:multi_processing]
      search_length_mp(length)
    else
      search_length_single(length)
    end
  end
  
  # Single-threaded search
  def search_length_single(length)
    @states_searched = 0
    initial_ruler = GolombRuler.new([0], @config[:marks])
    
    # Use BitSet equivalent for faster distance checking
    distance_set = Array.new(length + 1, false)
    
    # Start recursive backtracking
    ruler = backtrack(initial_ruler, 1, length, distance_set)
    
    if ruler
      { found: true, ruler: ruler }
    else
      { found: false }
    end
  end
  
  # Multi-processing search mit nativen Ruby Threads
  def search_length_mp(length)
    @states_searched = 0
    marks = @config[:marks]
    
    # Early return for small problems
    return search_length_single(length) if marks <= 4
    
    # Determine number of threads to use (matches available CPUs or 4 as minimum)
    num_threads = [4, cpu_count].max
    
    # Generate tasks for better work distribution
    tasks = generate_search_tasks(length)
    
    puts "Using #{num_threads} threads for #{tasks.size} tasks" if @config[:verbose]
    
    # Thread-sicherer Zugriff auf Ergebnisse
    mutex = Mutex.new
    result_found = false
    best_result = nil
    
    # Aufgaben aufteilen und Threads starten
    thread_groups = tasks.each_slice((tasks.size.to_f / num_threads).ceil).to_a
    threads = []
    
    thread_groups.each do |group_tasks|
      threads << Thread.new do
        # Durchlaufe alle Aufgaben in dieser Gruppe
        group_tasks.each do |task|
          # Wenn bereits ein Ergebnis gefunden wurde, beenden
          break if result_found
          
          # Initialize ruler with the first 2 marks
          initial_ruler = GolombRuler.new([0, task[:mark1]], marks)
          
          # Use BitSet equivalent for faster distance checking
          distance_set = Array.new(length + 1, false)
          distance_set[task[:mark1]] = true
          
          # Start recursive backtracking from mark 2
          ruler = backtrack_from_mark(initial_ruler, 2, length, distance_set, task[:mark1], task[:mark2])
          
          # Bei Erfolg, Thread-sicher speichern und andere benachrichtigen
          if ruler
            mutex.synchronize do
              best_result = { found: true, ruler: ruler }
              result_found = true
            end
            break
          end
        end
      end
    end
    
    # Warte auf alle Threads
    threads.each(&:join)
    
    # Gib das Ergebnis zurück oder 'not found'
    best_result || { found: false }
  end
  
  # Ermittle die Anzahl der verfügbaren CPU-Kerne
  def cpu_count
    case RbConfig::CONFIG['host_os']
    when /linux/
      File.read('/proc/cpuinfo').scan(/^processor\s*:/).count
    when /darwin|mac os/
      Integer(`sysctl -n hw.ncpu`.chomp)
    when /mswin|mingw/
      Integer(ENV['NUMBER_OF_PROCESSORS'] || 1)
    else
      2 # Default to 2 if unable to detect
    end
  rescue
    2 # Default to 2 if any errors occur
  end
  
  # Generate tasks for multi-processing search
  def generate_search_tasks(length)
    marks = @config[:marks]
    tasks = []
    
    # Determine mark1 range based on marks count
    mark1_range = [30, (length / (marks - 1)).ceil].min
    
    # Optimal first mark positions
    (1..mark1_range).each do |mark1|
      # Calculate valid range for mark 2
      min_mark2 = mark1 + 1
      
      # Determine mark2 range based on marks count
      if marks >= 10
        # For larger rulers, use dynamic upper bound
        max_mark2 = min_mark2 + mark1_range * 2
      else
        # For smaller rulers, use a simpler bound
        max_mark2 = min_mark2 + length / 4
      end
      
      # Ensure we don't exceed valid bounds
      max_mark2 = [max_mark2, length - (marks - 3)].min
      
      # Create tasks for each mark1, mark2 pair
      (min_mark2..max_mark2).each do |mark2|
        tasks << { mark1: mark1, mark2: mark2, priority: mark1 + mark2 }
      end
    end
    
    # Sort tasks by priority (lower sum is higher priority)
    tasks.sort_by { |t| t[:priority] }
  end
  
  # Optimized backtracking algorithm
  def backtrack(ruler, position, max_length, distance_set)
    @states_searched += 1
    
    # If we've placed all marks, we've found a valid ruler
    if position >= @config[:marks]
      return ruler
    end
    
    # Determine the valid range for the next mark
    prev_position = ruler.positions.last
    
    # Optimize step sizes based on position
    if position < @config[:marks] / 2
      # For early positions, use smaller steps
      step = 1
    else
      # For later positions, use larger steps to skip more invalid positions
      step = [1, (@config[:marks] - position) / 2].max
    end
    
    # Try all possible positions for this mark
    (prev_position + step..max_length).step(step) do |next_pos|
      valid = true
      
      # Check if any new distances would be duplicates
      ruler.positions.each do |pos|
        distance = next_pos - pos
        if distance_set[distance]
          valid = false
          break
        end
      end
      
      next unless valid
      
      # Mark all new distances as used
      new_distances = []
      ruler.positions.each do |pos|
        distance = next_pos - pos
        distance_set[distance] = true
        new_distances << distance
      end
      
      # Try this position by recursively placing the next mark
      new_ruler = ruler.with_mark(next_pos)
      result = backtrack(new_ruler, position + 1, max_length, distance_set)
      
      # If successful, return the result
      return result if result
      
      # Otherwise, backtrack by removing the distances we just added
      new_distances.each do |distance|
        distance_set[distance] = false
      end
    end
    
    # No valid positions found
    nil
  end
  
  # Specialized backtracking starting from mark1 and mark2 (for multi-processing)
  def backtrack_from_mark(ruler, position, max_length, distance_set, mark1, mark2)
    # Set the second mark position
    new_ruler = ruler.with_mark(mark2)
    
    # Update distance set with the new distance
    distance_set[mark2 - mark1] = true
    
    # Continue backtracking from the next position
    backtrack(new_ruler, position + 1, max_length, distance_set)
  end
end
