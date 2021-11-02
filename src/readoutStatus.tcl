#!/usr/bin/tclsh

# launch with:
# o2-readout-monitor file:/local/readout-emu.cfg readout-monitor | /home/sylvain/aliBuild/Readout/src/readoutBrowser.tcl
# config:   
# [readout-monitor]
# outputFormat=1

package require Tk

set flps {
CPV alio2-cr1-flp 162 162 ""
EMC alio2-cr1-flp 146 147 ""
FDD alio2-cr1-flp 201 201 ""
FT0 alio2-cr1-flp 200 200 ""
FV0 alio2-cr1-flp 180 180 ""
HMP alio2-cr1-flp 160 161 ""
ITS alio2-cr1-flp 187 198 ""
MCH alio2-cr1-flp 148 158 ""
MFT alio2-cr1-flp 182 186 ""
MID alio2-cr1-flp 159 159 ""
PHS alio2-cr1-flp 164 165 ""
TOF alio2-cr1-flp 178 179 ""
TPC alio2-cr1-flp 001 144 ""
TRD alio2-cr1-flp 166 177 ""
ZDC alio2-cr1-flp 181 181 ""
CTP alio2-cr1-flp 163 163 ""
TST alio2-cr1-mvs 01 02 ""
}

set detectors {}
set nix 0
foreach {det host imin imax comment} $flps {
  if {[lsearch $det $detectors]<0} {
    lappend detectors ${det}
	set v_nflp($det) 0
  }
  set nz [string length $imin]
  for {set i $imin} {$i <= $imax} {incr i} {
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
wm geometry . 1920x330+0+0

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

label .fDetail.descr -textvariable description -height 10
# -bg "#BBBBBB"
pack .fDetail.descr -fill both


set color_unselect "#F0F0F0"
set activeC -1
set bgcolor_highlight "yellow"



proc showDetails {f} {
  global description
  set description "FLP $f"
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
   
   set s $lastState($n)

   set i 0
   foreach {ss c} {Undefined "#666666" Standby "#007700" Ready "#00bb00" Running "#00ff00" Error red} {
     if {[string tolower $s]==[string tolower $ss]} {
       set x1 [expr $v_x($n)]
       set x2 [expr $x1 + $cw]
       set y1 [expr $y0_states + $i * $rowh]
       set y2 [expr $y1 + $rowh]
       if {$v_ix($n)==$activeC} {
         set color "$bgcolor_highlight"
       } else {
         set color $v_color($n)
       }
      .fMain.can create rect $x1 $y0_states $x2 [expr $y0_states + 5 * $rowh] -fill "$color" -width 0  
      .fMain.can create rect $x1 $y1 $x2 $y2 -fill "$c" -width 0  
      break
     }
     incr i
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

  .fMain.can create rect 0 0 $hw $fy -fill "#DDDDDD" -width 0
  font create font_head -family verdana -size [expr $cw - 2] -weight bold
  font create font_rhead -family monospace -size -$rowh

  bind .fMain.can <Motion> {
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
  bind .fMain.can <Leave> {
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

}



# parse output of readoutMonitor
proc updateNode {metrics} {
  foreach {t n s nstf bytesReadout bytesRecorded bytesFMQ pagesPendingFMQ nRFMQ avgtFMQ tfidFMQ} $metrics {
    global v_x v_ix
    global cw rowh y0_states
    global v_color bgcolor_highlight activeC
    global nodes
    global lastState lastTime
    
    if {[lsearch $nodes $n]<0} {
      continue
    }

    set lastState($n) $s
    set lastTime($n) $t
    drawState $n
 
    if {$v_ix($n)==$activeC} {
     global description
     set description "FLP $n\nLast update: [clock format $t]\nState: $s\nbytesReadout = $bytesReadout"
    }
  }
}


# routine to process stdin
proc processInput {} {
  while {1} {
    if {[gets stdin msg]==-1} {break}
    set lm [split $msg "\t"]
    if {[llength $lm]!=11} {break}
    updateNode $lm
  }
  fileevent stdin readable processInput
}

# setup stdin for input
fconfigure stdin -blocking false   
fconfigure stdin -buffersize 1000000
fileevent stdin readable processInput


# set nodes without fresh data to undefined state 
proc periodicCleanup {} {
  global lastState
  global lastTime
  global nodes

  set timeout 5
  
  set now [clock seconds]
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
  
  after [expr $timeout*500] {periodicCleanup}
}
periodicCleanup
