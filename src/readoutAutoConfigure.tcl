#!/usr/bin/tclsh

# utility to create an optimized readout configuration file
# based on devices and resources available on system
# generates the configuration code for detected equipments/memory banks

# arguments:
# -o configFile : save configuration to given file name (instead of stdout) - file is appended

# defaults
set configFile ""


# command line args
set x 0
while {[set opt [lindex $argv $x]] != ""} {
 switch -exact -- $opt {
   -o {
        set configFile [lindex $argv [expr $x + 1]]
        incr x
   }
 }
 incr x
}


# get list of ROCs
set ldev {}
if {[catch {set rocOutput [exec roc-list-cards]} err]} {
  puts "roc-list-cards failed: $err"
  exit 1
}
set rocOutputLines [split $rocOutput "\n"]
if {[catch {
  set rocHeader "#   Type   PCI Addr   Serial   Endpoint ID   NUMA  Vendor ID   Device ID   FW Version                Card ID"
  if {[string trim [lindex $rocOutputLines 1]]!=$rocHeader} {
    # throw only in tcl 8.7... but if not it generates an error anyway, which is what we want
    throw
  }
  for {set i 3} {$i<[expr [llength $rocOutputLines] -1]} {incr i} {
    set l [lindex $rocOutputLines $i]
    set type [string trim [string range $l 6 12]]
    set pci [string trim [string range $l 13 23]]
    set serial [string trim [string range $l 24 32]]
    set endpoint [string trim [string range $l 33 46]]
    set numa [string trim [string range $l 47 52]]

    lappend ldev "$type" "$pci" "$endpoint" "$numa" "$serial"
  }
} err]} {
  puts "Failed to parse roc-list-cards output: $err"
}
if {[llength $ldev]==0} {
  puts "No ROC device found, exiting"
  exit 1
}

# get memory configuration

if {[catch {set hugeOutput [exec hugeadm --pool-list]} err]} {
  puts "hugeadm failed: $err"
  exit 1
}
set hugeOutputLines [split $hugeOutput "\n"]
if {[catch {
  set hugeHeader "Size  Minimum  Current  Maximum  Default"
  if {[string trim [lindex $hugeOutputLines 0]]!=$hugeHeader} {
    # throw only in tcl 8.7... but if not it generates an error anyway, which is what we want
    throw
  }
  for {set i 1} {$i<[llength $hugeOutputLines]} {incr i} {
    set l [lindex $hugeOutputLines $i]
    set size [string trim [string range $l 0 11]]
    set min [string trim [string range $l 12 20]]
    set current [string trim [string range $l 21 29]]
    set max [string trim [string range $l 30 38]]
    lappend lmem "$size" "$current"
  }
} err]} {
  puts "Failed to parse hugeadm output: $err"
}


# get NUMA config

if {[catch {set numaOutput [exec lscpu | grep NUMA]} err]} {
  puts "lscpu | grep NUMA failed: $err"
  exit 1
}
if {[scan $numaOutput "NUMA node(s):          %d" numaNodes]!=1} {
  puts "Failed to parse lscpu output: $err"
}


# get memory configuration

if {[catch {set memOutput [exec free -b]} err]} {
  puts "free failed: $err"
  exit 1
}
set memOutputLines [split $memOutput "\n"]
if {[catch {
  set memHeader "total        used        free      shared  buff/cache   available"
  if {[string trim [lindex $memOutputLines 0]]!=$memHeader} {
    # throw only in tcl 8.7... but if not it generates an error anyway, which is what we want
    throw
  }
  set l [lindex $memOutputLines 1]
  set memTotal [string trim [string range $l 7 18]]
  set memUsed [string trim [string range $l 19 30]]
  set memFree [string trim [string range $l 31 42]]
} err]} {
  puts "Failed to parse free output: $err"
}


# flag to count errors
set err 0


# log information found

puts "Memory: ${memFree}/${memTotal} bytes available/total"

puts "NUMA nodes: $numaNodes"

if {$numaNodes==0} {
  puts "Should not be zero"
  incr err
}

puts "Memory available (hugepages):"
set nHuge1G 0
foreach {pageSize pagesAvailable} $lmem {
 puts "  $pagesAvailable * $pageSize bytes"
 if {$pageSize==1073741824} {
   set nHuge1G $pagesAvailable
 }
}
if {$nHuge1G==0} {
  puts "1G pages should not be zero"
  incr err
}

puts "Devices found:"
set nROC 0
for {set i 0} {$i<$numaNodes} {incr i} {
    set nRocNuma($i) 0
}
foreach {type pci endpoint numa serial} $ldev {
  if {("$type"=="CRU")||("$type"=="CRORC")} {
     puts "  $type @ $pci endpoint $endpoint numa $numa serial $serial"
     incr nROC
     if {"$numa">=$numaNodes} {
       puts "Inconsistent NUMA id!"
       incr err
     }
     incr nRocNuma($numa)
  }
}
puts "$nROC ROCs"

set maxPerNuma 0
for {set i 0} {$i<$numaNodes} {incr i} {
  if ($nRocNuma($i)>$maxPerNuma) {
    set maxPerNuma $nRocNuma($i)
  }
}
puts "Maximum $maxPerNuma ROC(s) per NUMA node"
if ($maxPerNuma==0) {
  puts "Should not be zero"
  incr err  
}

if {$err>0} {
  puts "Can not generate config"
  exit 1  
}
puts "Available [expr ($nHuge1G/$numaNodes)] page(s) per NUMA node"
set pagesPerRoc [expr ($nHuge1G/$numaNodes) / $maxPerNuma]

if {($pagesPerRoc<1)} {
  puts "Not enough 1G HugePages allocated, need at least 1 per ROC"
  exit 1  
}

puts "Using $pagesPerRoc x 1G page per ROC"
set readoutPageSize [expr 1024*1024]
set readoutNPages [expr int($pagesPerRoc * (1024.0*1024.0*1024.0) / $readoutPageSize)]
if {0} {
  # enforce a minimum number of pages
  # this could be needed if aggregator slicing with many links / many pages per STF
  if {$readoutNPages<1} {
    set readoutNPages 2048
    set readoutPageSize [expr int($pagesPerRoc * (1024.0*1024.0*1024.0) / $readoutNPages)]
    set readoutPageSize [expr $readoutPageSize - ($readoutPageSize % (8*1024))]
  }
}
puts "Using for readout equipment $readoutNPages * $readoutPageSize bytes"

# Generate readout configuration

# general parameters
lappend config "
\[readout\]
# disable slicing into timeframes
# needed if we don't have enough pages to buffer at least 1 STF per link
disableAggregatorSlicing=1
"

# stats
lappend config "
\[consumer-stats\]
consumerType=stats
enabled=1
monitoringEnabled=0
monitoringUpdatePeriod=1
consoleUpdate=1

# recording to file
\[consumer-rec\]
consumerType=fileRecorder
enabled=0
fileName=/tmp/data.raw
"

# membanks - one per numa node
if {0} {
  for {set i 0} {$i<$numaNodes} {incr i} {
    set sz [expr $nRocNuma($i) * $pagesPerRoc]
    if {$sz==0} {continue}
    lappend config "
\[bank-${i}\]
type=MemoryMappedFile
size=${sz}G
numaNode=${i}
  "
  }
}

# ROCs
set rocN 0
foreach {type pci endpoint numa serial} $ldev {
  if {("$type"=="CRU")||("$type"=="CRORC")} {
    incr rocN

if {1} {
  # one bank per equipment
  lappend config "
\[bank-${rocN}\]
type=MemoryMappedFile
size=${pagesPerRoc}G
numaNode=${numa}
"
}
    lappend config "
\[equipment-roc-${rocN}\]
enabled=1
equipmentType=rorc
cardId=${pci}
#channel=0
dataSource=Internal
memoryBankName=bank-${rocN}
memoryPoolNumberOfPages=${readoutNPages}
memoryPoolPageSize=${readoutPageSize}
"
  }
}

# convert to string
set config [join $config]


if {$configFile==""} {
  puts "readout.exe configuration template:"
  puts $config
  
} else {
  puts "Creating configuration file $configFile"
  set fd [open $configFile "w"]
  puts $fd "$config"
  close $fd
}
