#!/usr/bin/env ruby
# Ruler class for Golomb Ruler Finder - Ruby Implementation

require 'set'

class GolombRuler
  attr_reader :positions, :marks, :length
  
  def initialize(positions = nil, marks = nil)
    if positions
      @positions = positions.dup
      @marks = positions.length
      @length = positions.last
    else
      # Create a ruler with just the first mark at position 0
      @positions = [0]
      @marks = marks || 1
      @length = 0
    end
  end
  
  # Create a new ruler by adding a mark
  def with_mark(position)
    new_positions = @positions.dup
    new_positions << position
    new_positions.sort!
    GolombRuler.new(new_positions)
  end
  
  # Check if this is a valid Golomb ruler (all distances are unique)
  def valid?
    return false if @positions.length < 2
    
    distances = Set.new
    for i in 0...@positions.length
      for j in (i+1)...@positions.length
        distance = @positions[j] - @positions[i]
        return false if distances.include?(distance)
        distances.add(distance)
      end
    end
    
    true
  end
  
  # Check if this ruler is optimal (matches known optimal length)
  def optimal?(known_length)
    @length == known_length
  end
  
  # Check if adding a mark at the given position would maintain validity
  def valid_with_mark?(position)
    return false if position <= @positions.last
    
    # Check if any new distance would be a duplicate
    distances = Set.new
    
    # Calculate existing distances
    for i in 0...@positions.length
      for j in (i+1)...@positions.length
        distances.add(@positions[j] - @positions[i])
      end
    end
    
    # Check distances with new mark
    for i in 0...@positions.length
      distance = position - @positions[i]
      return false if distances.include?(distance)
      distances.add(distance)
    end
    
    true
  end
  
  # Create a copy of this ruler
  def clone
    GolombRuler.new(@positions)
  end
  
  # String representation
  def to_s
    "GolombRuler[#{@marks} marks, length #{@length}, positions: #{@positions.join(',')}]"
  end
end
