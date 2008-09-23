#!/usr/bin/ruby

require 'gtk2'

module Exmap

  class Pool
    def initialize
      @procs = []
    end

    def load_procs
      @pids = _list_pids
      @pids.each {
        |pid| p = Exmap::Process.new(pid)
        @procs.push(p) if p.load
      }
      @pid_to_proc = {}
      @procs.each { |proc| @pid_to_proc[proc.pid] = proc }
    end
    
    def _list_pids
      pids = []
      Dir.foreach("/proc") { |f| pids.push(f) if (f =~ /^[0-9]/) }
      return pids
    end
  end

  class Process
    attr_reader :pid

    def initialize(pid)
      @pid = pid
      @vmas = []
    end

    def load
      _read_mapfile
    end

    def _read_mapfile
      mapfile = "/proc/#{pid}/maps"
      File.open(mapfile) { |f|
        f.each { |line|
          vma = Vma.new(line.chomp)
          @vmas.push(vma) if vma
        }
      }
    end
    private :_read_mapfile
    
  end


  class Vma
    attr_reader :file
    @@ANON_NAME = '[anon]'
    @@HEAP_NAME = '[heap]'
    @@VDSO_NAME = '[vdso]'

    def initialize(line)
      parse_line(line)
      _calculate
    end

    def parse_line(line)
      unless (line =~ /^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+([0-9a-f]+)/)
        warn "Failed to parse vma line #{line}\n"
        return false
      end
      @hex_start = $1
      @hex_end = $2
      @perms = $3
      @hex_offset = $4

      if (line.length >= 49)
        @file = line[49,line.length]
      else
        @file = @@ANON_NAME 
      end

      return true
    end

    def _calculate
      die "can't calculate without parsed data" unless @hex_start
    end
    private :_calculate
  end
end
  
  
e = Exmap::Pool.new
e.load_procs
