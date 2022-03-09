#!/usr/bin/tclsh

# Extract the readout configuration parameters from source files comments.
# Script re-assembles them when in multiple lines, and sort them.
# Result can then be inserted in configuration parameters doc and editor.

set lf [glob *.cxx]
set key "configuration parameter:"
set commentHeader "// "
set doc {}
foreach f $lf {
  set fd [open $f "r"]
  set nl 0
  set currentComment ""
  set nc 0
  while {1} {   
    gets $fd line
    if {[eof $fd]} {break}
    incr nl
    set l [string trim $line]
    set ix [string first $key $l]
    set cl ""    
    if {$ix>=0} {
      if {$ix!=3} {
        puts "Warning: $f,${nl} -> occurence of key($key) at wrong position"
      }
      if {[string range $l 0 2]!=$commentHeader} {
        puts "Warning: $f,${nl} -> comment headerformat"
      }
      if {$currentComment!=""} {
        puts "Warning: $f,${nl} -> expected continuation, found new key"
	set currentComment ""
      }
      set cl [string trim [string range $l [expr $ix + [string length $key]] end]]
      incr nc
    }
    if {$currentComment!=""} {
      # try to read continuation
      if {[string range $l 0 2]!=$commentHeader} {
        puts "Warning: $f,${nl} -> expected continuation, found wrong comment headerformat"
	set currentComment ""
      } else {
        set cl [string trim [string range $l 2 end]]
      }
    }
    
    if {$cl!=""} {
      set currentComment "${currentComment}${cl} "
      #puts "$currentComment"
      set ixk 0
      set nbar 0
      while {1} {
        set ixk [string first "|" $currentComment $ixk]
	if {$ixk<0} {break}
	incr nbar
	incr ixk
	if {$ixk<[string length $currentComment]} {
	  set cc [string range $currentComment $ixk $ixk]
	  if {$cc!=" "} {
	    puts "Warning: $f,${nl} -> missing space, found $cc @ $ixk"
	  }
	}
      }
      #puts "$nbar"
      if {$nbar==6} {
	#puts "ok -> "
	lappend doc "$currentComment"
	set currentComment ""
      } elseif {$nbar>6} {
        puts "Warning: $f,${nl} -> Wrong comment format, number of fields exceeded"
	set currentComment ""
      } else {
        # try to continue on next line	
      }
    }
  }
  close $fd
  #if {$nc} {break}
}

set doc [lsort -dictionary $doc]
foreach p $doc {
  puts [string trim $p]
}
