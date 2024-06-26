#!/usr/bin/tclsh

# launch with:
# o2-readout-monitor file:/local/readout-emu.cfg readout-monitor | /home/sylvain/aliBuild/Readout/src/readoutBrowser.tcl
# config:   
# [readout-monitor]
# outputFormat=1

set configFile "/etc/o2.d/readout-defaults.cfg"
set configSection "readout-monitor"

# defaults
set cfgValue_broadcastPort ""
set cfgValue_broadcastHost ""
set cfgValue_verbose 0

if {[llength $argv] >=2} {
  set configFile [lindex $argv 0]
  set configSection [lindex $argv 1]
}

set configError 1
if {[file exists $configFile]} {
  package require inifile
  set iFile [::ini::open $configFile]
  foreach s [::ini::sections ${iFile}] {
    if {$s == $configSection} {
      foreach {k v} [::ini::get ${iFile} $s] {
	# discard comments
	if {[string range [string trim $k] 0 0]!="#"} {
	  set "cfgValue_${k}" $v
	}
	set configError 0
      }    
      break
    }
  }

  ::ini::close $iFile
}

if {$configError} {
  puts "Error reading $configFile : $configSection"
  return -1
}


package require Tk

set flps {
CPV alio2-cr1-flp 162 162 {} ""
EMC alio2-cr1-flp 146 147 {} ""
FDD alio2-cr1-flp 201 201 {} ""
FT0 alio2-cr1-flp 200 200 {} ""
FV0 alio2-cr1-flp 180 180 {} ""
HMP alio2-cr1-flp 160 161 {} ""
ITS alio2-cr1-flp 187 198 {203} ""
MCH alio2-cr1-flp 148 158 {} ""
MFT alio2-cr1-flp 182 186 {} ""
MID alio2-cr1-flp 159 159 {} ""
PHS alio2-cr1-flp 164 165 {} ""
TOF alio2-cr1-flp 178 179 {} ""
TPC alio2-cr1-flp 001 144 {} ""
TRD alio2-cr1-flp 166 177 {} ""
ZDC alio2-cr1-flp 181 181 {} ""
CTP alio2-cr1-flp 163 163 {} ""
TST alio2-cr1-flp 204 204 {} ""
}

set detectors {}
set nix 0
foreach {det host imin imax iplus comment} $flps {
  if {[lsearch $det $detectors]<0} {
    lappend detectors ${det}
	set v_nflp($det) 0
  }
  set nz [string length $imin]
  set listi {}
  for {set i $imin} {$i <= $imax} {incr i} {
    lappend listi $i
  }
  foreach i $iplus {
    lappend listi $i
  }
  foreach i $listi {
    set id [format "%0${nz}d" $i]
    set h [join [list ${host} ${id}] ""]
    set n [format "%0${nz}d" $i]
	lappend nodes $h
	lappend v_flp($det) $h
	set v_ix($h) $nix
	set v_id($h) $id
	incr nix
	incr v_nflp($det)
	set lastState($h) "Undefined"
	set lastTime($h) 0
	set lastBytes($h) {}
	set lastDraw($h) 0
  }
  
}


# detectors: don't sort, keep same order
# set detectors [lsort $detectors]

# return  "vertical" string
proc verticalize {n} {
	set nn ""	
    for {set j 0} {$j < [string length "$n"]} {incr j} {
	  if {$j > 0} {
	    set nn "${nn}\n"
	  }
	  set nn "${nn}[string range $n $j $j]"
    }	
	return "$nn"
}

wm title . "Readout status"
#wm geometry . 1920x1080+0+0
wm geometry . 1900x330-5+50

if {0} {
  wm state . zoomed
  wm withdraw .
  wm deiconify .
}
update
set szx [winfo width .]
set szy [winfo height .]
puts "Window is $szx x $szy"

frame .fMain -borderwidth 1 -relief solid -height 200
frame .fDetail -height 80
pack .fMain -side top -fill both -expand 1 -pady 10 -padx 10
pack .fDetail -side bottom -fill x

font create font_flp -family monospace -size [expr -int($szx / $nix)]
font create font_det -family verdana -size [expr -int($szx / $nix)] -weight bold

label .fDetail.descr -textvariable description -height 10 -justify left
# -bg "#BBBBBB"
pack .fDetail.descr -fill both


set color_unselect "#F0F0F0"
set activeC -1
set bgcolor_highlight "yellow"



proc showDetails {f} {
  global activeLock
  set ll ""
  if {$activeLock} {
    set ll "(locked)"
  }

  global description
  set description "FLP $f $ll"
#  .fMain.flp${f} configure -bg "red"
  drawFlp $f 1
}
proc resetDetails {f} {
  global description
  set description ""
#  global v_color
#  .fMain.flp${f} configure -bg "$v_color($f)"
  drawFlp $f 0
}


proc getIxFromX {x} {
  global hw cw nix
  set ix [expr ($x - $hw) / $cw]
  if {$ix<0} {return -1}
  if {$ix>=$nix} {return -1}
  return $ix
}

proc drawState {n} {
   global v_x v_ix
   global cw
   global rowh y0_states
   global v_color bgcolor_highlight activeC
   global nodes
   global lastState
   global lastBytes
   global y0_bytes y1_bytes x_tscale
   global lastDraw
   global font_flp
   
   set now [expr [clock milliseconds]/1000.0]
   set lastDraw($n) $now
   
   set s $lastState($n)
   if {$v_ix($n)==$activeC} {
     set color "$bgcolor_highlight"
   } else {
     set color $v_color($n)
   }
   set x1 [expr $v_x($n)]
   set x2 [expr $x1 + $cw]

   set i 0
   foreach {ss act c} {Undefined {} "#666666" Standby {"> conf"} "#007700" Ready {"> start" "> reset"} "#00bb00" Running {"> stop"} "#00ff00" Error {} red} {
     set isMatch 0    
     set cc "" 
     if {[string tolower $s]==[string tolower $ss]} {
       set isMatch 1
     } else {
       if {[lsearch $act $s]>=0} {
         set isMatch 1
	 set cc ">"
       }
     }
     if {$isMatch} {
       set y1 [expr $y0_states + $i * $rowh]
       set y2 [expr $y1 + $rowh]
      .fMain.can create rect $x1 $y0_states $x2 [expr $y0_states + 5 * $rowh] -fill "$color" -width 0  
      .fMain.can create rect $x1 $y1 $x2 $y2 -fill "$c" -width 0  
      if {$cc != ""} {
        set cl "orange"
	#.fMain.can create line $x1 $y1 [expr ($x1 +$x2)/2] $y2 -fill "$cl"
	#.fMain.can create line [expr ($x1 +$x2)/2] $y2 $x2 $y1 -fill "$cl"
        .fMain.can create rect [expr $x1 + 2] [expr $y1 +2] [expr $x2 - 2]  [expr $y2 - 2] -fill "$cl" -width 0
        #.fMain.can create text $x1 $y1 -text "$cc" -anchor nw -font font_flp -fill yellow
      }
      break
     }
     incr i
   }

  
  set i 0
  set newlist {}
  set vals {}
  set lastt -1
  set lastb -1
  foreach {t b} $lastBytes($n) {
    set x [expr round($cw - ($now - $t) * $x_tscale - 1)]
    if {$x<0} {
      continue
    }
    if {$lastt>0} {
      if {([expr $t - $lastt] > 0)} {
        if {($b >= $lastb)} {
          # rate in bytes/sec
          set r [expr ($b - $lastb) / ($t - $lastt)]
	} else {
	  set r 0
	}
	# scale to log. Maxval = 10GB/s
	if {$r<=1} {
	  set y 0
	} else {
	  # log(10GB) = 23
	  set y [expr round(log($r)*$y1_bytes/23)]
	}
	if {$y>$y1_bytes} {
	  set y $y1_bytes	  
	}
        lappend vals $x $y
      }
    }
    lappend newlist $t $b
    set lastt $t
    set lastb $b
  }
  set lastBytes($n) $newlist
  #puts "\n$vals"
  
  # group by x
  for {set i 0} {$i <= $cw} {incr i} {
    set px($i) 0
    set py($i) 0
  }
  foreach {x y} $vals {
    incr px($x)
    incr py($x) $y
  }
  
  set y1 $y0_bytes
  set y2 [expr $y1 + $y1_bytes]
 
  #.fMain.can create rect $x1 $y1 $x2 $y2 -fill "$c" -width 0 -fill "$color"
  for {set i 1} {$i < $cw} {incr i} {
    set x [expr $x1 + $i]
    if {$px($i)>0} {
      set y [expr $y2 - ($py($i) / $px($i))]
      .fMain.can create line $x $y2 $x $y -fill "blue"
      .fMain.can create line $x $y1 $x $y -fill "$color"
    } else {
      .fMain.can create line $x $y1 $x $y2 -fill "$color"
    }
  }

  
  if {0} {  
  foreach {x y} $vals {
    incr x $x1
    set y [expr $y2-$y]
    .fMain.can create line $x $y2 $x $y
  }
  }
  
}

proc drawFlp {f highlight} {
  global cw rowh v_color v_x fy v_id hh bgcolor_highlight
  set x $v_x($f)
  if {$highlight} {
    set c "$bgcolor_highlight"
  } else {
    set c $v_color($f)
  }
  .fMain.can create rect $x [expr $hh] [expr $x + $cw] $fy -fill "$c" -width 0
  .fMain.can create text [expr $x + 0.5 * $cw] [expr $hh * 1.5] -text "[verticalize $v_id($f)]" -anchor center -justify center -font font_flp
  drawState $f
}


# setup view
if {1} {
  update
  set fx [winfo width .fMain]
  set fy [winfo height .fMain]
  set hw 100
  set cw [expr int(floor(($fx - $hw) * 1.0 / $nix)) + 1]
  set tw [expr $cw * $nix]
  set hw [expr $fx - $tw - 20]

  # canvas drawing
  canvas .fMain.can -width [expr $hw+$tw] -height $fy
  pack .fMain.can -fill both -expand 1
  update
  set fx [winfo width .fMain.can]
  set fy [winfo height .fMain.can]
  set hw [expr $fx - $tw]
  set hh 50
  
  set rowh 10

  # y top position of 1st line states
  set y0_states [expr $hh*2]

  set y0_bytes [expr $y0_states + 5 * $rowh]
  # timescale 1 pix = X sec
  set x_tscale [expr 1.0 / 1]
  # space reserved for bytes plot
  set y1_bytes 20
    

  .fMain.can create rect 0 0 $hw $fy -fill "#DDDDDD" -width 0
  font create font_head -family verdana -size [expr $cw - 2] -weight bold
  font create font_rhead -family monospace -size -$rowh


  set activeLock 0
  bind .fMain.can <Motion> {
    if {$activeLock} {
        return
    }
    set ix [getIxFromX %x]
    if {$activeC!=$ix} {
      set previous $activeC
      set activeC $ix
      if {$previous!=-1} {
        resetDetails [lindex $nodes $previous]
      }
      if {$activeC!=-1} {
        showDetails [lindex $nodes $activeC]
      }
    }
  }
  bind .fMain.can <Button-1> {
    if {$activeLock} {
      set activeLock 0
    } else {
      set activeLock 1
      if {$activeC!=-1} {
        showDetails [lindex $nodes $activeC]
      }
    }
    
  }
  bind .fMain.can <Leave> {
      if {$activeLock} {
        return
      }
      set previous $activeC
      set activeC -1
      if {$previous!=-1} {
        resetDetails [lindex $nodes $previous]
      }
  }
  
  set i 0
  set ni 0
  set bgCommands {}
  set fgCommands {}
  foreach d $detectors { 
    if {[expr $i %2]} {
      set color "#DBDBDB"
    } else {
      set color "#E0E0E0"
    }     
        
    set x [expr $hw + $ni * $cw] 
    set w [expr $cw * $v_nflp($d)]

    .fMain.can create rect [expr $x] 1 [expr $x + $w] $fy -fill "$color" -width 0
    .fMain.can create text [expr $x + $w/2] [expr $hh / 2] -text [verticalize $d] -anchor center -justify center -font font_head
        
    set j 0
    foreach f $v_flp($d) {
      set v_color($f) $color
      set v_x($f) [expr int($x + $j * $cw)]
      drawFlp $f 0
      incr j
    }
    
    incr i
    incr ni $v_nflp($d)
  }
  
  set rowix 0
  foreach s {Undef Standby Ready Running Error} {
    set yy [expr $y0_states + $rowix * $rowh]
    .fMain.can create rect 0 $yy $hw [expr $yy + $rowh] -width 0
    .fMain.can create text [expr $hw-5] [expr $yy + $rowh/2] -text "$s" -anchor e -justify right -font font_rhead
    #.fMain.can create text [expr $hw-50] [expr $yy + $rowh/2] -text "$s" -anchor w -justify left -font font_rhead
    incr rowix
  }
  
  .fMain.can create text [expr $hw-5] [expr $y0_bytes + $y1_bytes/2] -text "Rate" -anchor e -justify right -font font_rhead

}


proc NumberOfBytesToString {value suffix} {
  set prefixes { " " "k" "M" "G" "T" "P" }
  set maxPrefixIndex [llength $prefixes]
  set prefixIndex 0
  set l 0
  set scaledValue $value
  catch {
    if {$value > 0} {
      set prefixIndex [expr int(floor(log($value) / log(1024)))]
      if {$prefixIndex > $maxPrefixIndex} {
	set prefixIndex $maxPrefixIndex
      }
      if {$prefixIndex < 0} {
	set prefixIndex 0
      }
      set scaledValue [expr $value / pow(1024, $prefixIndex)]
      set l [expr int(floor(log10(abs($scaledValue))))]
      if {$l < 0} {
	set l 3
      } elseif {$l <= 3} {
	set l [expr 3 - $l]
      } else {
	set l 0
      }
    }
  }
  return [format "%.*f %s%s" $l $scaledValue [lindex $prefixes $prefixIndex] $suffix];
}

# parse output of readoutMonitor
proc updateNode {metrics} {
  foreach {t nn s nstf bytesReadout bytesRecorded bytesFMQ pagesPendingFMQ nRFMQ avgtFMQ tfidFMQ bufferUsage} $metrics {
    global v_x v_ix
    global cw rowh y0_states
    global v_color bgcolor_highlight activeC
    global nodes
    global lastState lastTime lastBytes
    
    set n [lindex [split $nn "."] 0]
    
    if {[lsearch $nodes $n]<0} {
      continue
    }

    set lastState($n) $s
    set lastTime($n) $t
    lappend lastBytes($n) $t $bytesReadout
    drawState $n
 
    if {$v_ix($n)==$activeC} {
     global activeLock
     if {$activeLock} {
       set ll "(locked)"
     } else {
       set ll ""
     }

     global description
     set st [clock format [expr int($t)] -format "%d-%m-%Y %H:%M:%S"]
     set sb [NumberOfBytesToString $bytesReadout "bytes"]
     set sbuf {}
     set i 0
     foreach v [split $bufferUsage ","] {
       if {$v!=""} {lappend sbuf "\[$i\]:$v%"}
       incr i
     }
     set sbuf [join $sbuf " "]
     set savgtFMQ [format "%.2f" $avgtFMQ]
     set description "FLP $n $ll\nLast update: ${st}\nState: $s\nbytesReadout = $sb\nFMQ stats:\t pending=$pagesPendingFMQ\t release rate=${nRFMQ} Hz\t latency=:${savgtFMQ}s\nBuffer stats: $sbuf"
    }
  }
}


# routine to process input data
proc processInput {} {
  global server_fd
  while {1} {
    if {[eof $server_fd]} {
      puts "Connection closed"
      close $server_fd
      set server_fd -1
      global cfgValue_broadcastPort server_reconnect_timeout
      if {$cfgValue_broadcastPort=="stdin"} {
        exit
      } else {
        after $server_reconnect_timeout {server_connect}
        return
      }
    }

    if {[gets $server_fd msg]==-1} {break}
    global cfgValue_verbose
    if {$cfgValue_verbose} {
      puts "Msg: $msg"
    }
    set lm [split $msg "\t"]
    if {[llength $lm]!=12} {
      puts "Warning: input data has wrong number of columns"
      break
    }
    updateNode $lm
  }
  fileevent $server_fd readable processInput
}

set server_fd -1
set reconnect 0
set server_reconnect_timeout 5000
proc server_connect {} {
  global reconnect server_fd
  if {$server_fd>=0} {
    return
  }
  global cfgValue_broadcastHost cfgValue_broadcastPort server_reconnect_timeout
  if {($cfgValue_broadcastHost=="")||($cfgValue_broadcastPort=="stdin")} {
    set cfgValue_broadcastHost ""
    set cfgValue_broadcastPort "stdin"
    set server_fd stdin
    puts "Reading data from stdin"
  } else {
    if {[catch {set server_fd [socket $cfgValue_broadcastHost $cfgValue_broadcastPort]} err]} {
      if {!$reconnect} {
        set reconnect 1
	puts "Failed to connect ${cfgValue_broadcastHost}:${cfgValue_broadcastPort}: $err"
      }
      after $server_reconnect_timeout {server_connect}
    } else {
      puts "Connected to ${cfgValue_broadcastHost}:${cfgValue_broadcastPort}"
      set reconnect 0
    }
  }
  if {$server_fd != -1} {
    # setup stdin for input
    fconfigure $server_fd -blocking false   
    fconfigure $server_fd -buffersize 1000000
    fileevent $server_fd readable processInput
  }
}

server_connect

# set nodes without fresh data to undefined state 
proc periodicCleanup {} {
  global lastState
  global lastTime
  global nodes

  set timeout 5
  
  set now [expr [clock milliseconds]/1000.0]
  foreach n $nodes {
    set dt [expr $now - $lastTime($n)]
    if {$lastTime($n)>$timeout} {
      if {$dt>1} {
	 set lastState($n) "Undefined"
	 set lastTime($n) 0
	 drawState $n
      }
    }
  }
  
  after [expr $timeout*1000/2] {periodicCleanup}
}
periodicCleanup

proc periodicDraw {} {
  global lastDraw
  global nodes
  
  set now [expr [clock milliseconds]/1000.0]
  set timeout 2
  foreach n $nodes {
    set dt [expr $now - $lastDraw($n)]
    if {$dt>=$timeout} {
      drawState $n
    }
  }
  after [expr int($timeout*1000)] {periodicDraw}
}
periodicDraw
