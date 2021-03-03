#!/usr/bin/tclsh

# utility to create an optimized readout configuration file
# based on devices and resources available on system
# generates the configuration code for detected equipments/memory banks

# arguments:
# -o configFile : save configuration to given file name (instead of stdout) - file is appended
# -m mode       : optimization mode, one of standalone (readout-only) or full (with extra components)
# -s silent     : no log output, just the config
# -k key=value  : set a variable

# dependencies
package require json

# defaults
set configFile ""
set mode "standalone"

# configuration
# buffer size in MB, for each equipment
set bufferPerEquipment 8000
# superpage size
set readoutPageSize 1048576
# stf spare copy buffer ratio, as a fraction of the main buffer
set bufferStfCopyFraction 0.4
# maximum buffer size, in fraction of total system memory
set bufferMaxSystemUse 0.5
# source to use for ROCs
# e.g. Fee, Internal, Ddg
set dataSource "Fee"

# url for monitoring. if set, monitoring is enabled
# eg: influxdb-unix:///tmp/telegraf.sock
set monitoringURI ""

# output log info
set logEnabled 1

# version of this script
set version "2.1"

# defaults
set generateQC 0
set generateREC 0
set recBytesMax "0"
set recPagesMax "1000"
set disableSending 0
set rdhCheckEnabled 0

# channels to configure per card, if CRORC
# string in the form "serial1:channel1,channel2, serial2:channel1,..."
set channelMask ""

# log function
proc doLog {txt} {
  global logEnabled
  if {$logEnabled} {
    puts "$txt"
  }
}

# command line args
set x 0
while {[set opt [lindex $argv $x]] != ""} {
 switch -exact -- $opt {
   -o {
        set configFile [lindex $argv [expr $x + 1]]
        incr x
   }
   -m {
        set mode [lindex $argv [expr $x + 1]]
        incr x
   }
   -s {
        set logEnabled 0
   }
   -k {
        set o [lindex $argv [expr $x + 1]]
	set lo [split $o "="]
	if {[llength $lo]==2} {
	  set [lindex $lo 0] [lindex $lo 1]
	} else {
	  doLog "Bad option ${opt} ${o}"
	  exit 1
	}
   }
   
 }
 incr x
}

# get hostname
set hostname ""
if {[catch {set hostname [exec hostname -s]} err]} {
  doLog "hostname failed: $err"
  exit 1
}

# get list of ROCs
set ldev {}
if {[catch {set rocOutput [exec roc-list-cards --json]} err]} {
  doLog "roc-list-cards failed: $err"
  exit 1
}

# return 1 if provided serial/channel found in channelMask (or if channelMask empty)
# return 0 if no match for serial/channel
proc matchMask {serial channel} {
  global channelMask
  if {"$channelMask"==""} {
    return 1
  }
  foreach e [split $channelMask " "] {
    set ls [split $e ":"]
    if {[llength $ls]!=2} {continue}
    set s [lindex $ls 0]
    if {"$s"!="$serial"} {continue}
    foreach c [split [lindex $ls 1] ","] {
      if {"$c"=="$channel"} {
        return 1
      }
    }
  }
  return 0
}

if {[catch {
  set dd [::json::json2dict [exec roc-list-cards --json]]
  dict for {id params} $dd {
      set type [dict get $params "type"]
      set pci [dict get $params "pciAddress"]
      set serial [dict get $params "serial"]
      set endpoint [dict get $params "endpoint"]    
      set numa [dict get $params "numa"]
      if {$type=="CRORC"} {
        foreach cix {0 1 2 3 4 5} {
          if {[matchMask "$serial" "$cix"]} {
            lappend ldev "$type" "$pci" "$endpoint" "$numa" "$serial" "$cix"
          }
        }
      } else {
          lappend ldev "$type" "$pci" "$endpoint" "$numa" "$serial" "0"      
      }
  }
} err]} {
  doLog "Failed to parse roc-list-cards output: $err"
}
if {[llength $ldev]==0} {
  doLog "No ROC device found, exiting"
  exit 1
}

# get memory configuration

if {[catch {set hugeOutput [exec hugeadm --pool-list]} err]} {
  doLog "hugeadm failed: $err"
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
  doLog "Failed to parse hugeadm output: $err"
}


# get NUMA config

if {[catch {set numaOutput [exec lscpu | grep NUMA]} err]} {
  doLog "lscpu | grep NUMA failed: $err"
  exit 1
}
if {[scan $numaOutput "NUMA node(s):          %d" numaNodes]!=1} {
  doLog "Failed to parse lscpu output: $err"
}


# get memory configuration

if {[catch {set memOutput [exec free -b]} err]} {
  doLog "free failed: $err"
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
  doLog "Failed to parse free output: $err"
}


# flag to count errors
set err 0


# log information found

doLog "Memory: ${memFree}/${memTotal} bytes available/total"

doLog "NUMA nodes: $numaNodes"

if {$numaNodes==0} {
  doLog "Should not be zero"
  incr err
}

doLog "Memory available (hugepages):"
set nHuge1G 0
foreach {pageSize pagesAvailable} $lmem {
 doLog "  $pagesAvailable * $pageSize bytes"
 if {$pageSize==1073741824} {
   set nHuge1G $pagesAvailable
 }
}
if {$nHuge1G==0} {
  doLog "1G pages should not be zero"
  incr err
}

doLog "Devices found:"
set nROC 0
for {set i 0} {$i<$numaNodes} {incr i} {
    set nRocNuma($i) 0
}
foreach {type pci endpoint numa serial channel} $ldev {
  if {("$type"=="CRU")||("$type"=="CRORC")} {
     doLog "  $type @ $pci endpoint $endpoint numa $numa serial $serial channel $channel"
     incr nROC
     if {"$numa">=$numaNodes} {
       doLog "Inconsistent NUMA id!"
       incr err
     }
     incr nRocNuma($numa)
  }
}
doLog "$nROC ROCs"

set maxPerNuma 0
for {set i 0} {$i<$numaNodes} {incr i} {
  if ($nRocNuma($i)>$maxPerNuma) {
    set maxPerNuma $nRocNuma($i)
  }
}
doLog "Maximum $maxPerNuma ROC(s) per NUMA node"
if ($maxPerNuma==0) {
  doLog "Should not be zero"
  incr err  
}

if {$err>0} {
  doLog "Can not generate config"
  exit 1  
}

# Generate readout configuration
doLog "Generating config optimized for readout $mode operation"
set config {}

lappend config "# readout.exe configuration file
# auto-generated v${version}
# on ${hostname}
# [clock format [clock seconds]]

"


if {$mode == "standalone"} {

doLog "Available [expr ($nHuge1G/$numaNodes)] page(s) per NUMA node"
set pagesPerRoc [expr ($nHuge1G/$numaNodes) / $maxPerNuma]

if {($pagesPerRoc<1)} {
  doLog "Not enough 1G HugePages allocated, need at least 1 per ROC"
  exit 1  
}

doLog "Using $pagesPerRoc x 1G page per ROC"
set readoutNPages [expr int($pagesPerRoc * (1024.0*1024.0*1024.0) / $readoutPageSize) - 1]
if {0} {
  # enforce a minimum number of pages
  # this could be needed if aggregator slicing with many links / many pages per STF
  if {$readoutNPages<1} {
    set readoutNPages 2048
    set readoutPageSize [expr int($pagesPerRoc * (1024.0*1024.0*1024.0) / $readoutNPages)]
    set readoutPageSize [expr $readoutPageSize - ($readoutPageSize % (8*1024))]
  }
}
doLog "Using for readout equipment $readoutNPages * $readoutPageSize bytes"


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
foreach {type pci endpoint numa serial channel} $ldev {
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
"
  if {$type=="CRORC"} {
    lappend config "channelNumber=${channel}
"
  }
  lappend config "dataSource=Internal
memoryBankName=bank-${rocN}
memoryPoolNumberOfPages=${readoutNPages}
memoryPoolPageSize=${readoutPageSize}
"

  }
}

} elseif {$mode == "full"} {
 
 set nPagesAlign 4
 set nPagesPerEquipment [expr int($bufferPerEquipment * 1024 * 1024 * 1.0 / $readoutPageSize)]
 set bufferTotal [expr ($nROC * $readoutPageSize * ($nPagesPerEquipment + $nPagesAlign)) * (1 + $bufferStfCopyFraction)]
 set maxAllowed [expr $bufferMaxSystemUse * $memTotal]
 
 doLog "Readout page size = $readoutPageSize"
 
 if ($bufferTotal>$maxAllowed) {
   doLog "Wished: $bufferTotal ($nPagesPerEquipment pages per equipment, $nROC devices)"
   doLog "Exceeding [expr $bufferMaxSystemUse * 100]% memory resources, limiting buffer to $maxAllowed bytes"
   set nPagesPerEquipment [expr int(($maxAllowed * 1.0 / (1 + $bufferStfCopyFraction)) / ($nROC * $readoutPageSize)) - $nPagesAlign]
   set bufferTotal $maxAllowed   
 }

 set nPagesPerEquipment [expr int($nPagesPerEquipment)]
 set bufMB [expr int($bufferTotal / (1024*1024)) + 1]
 set nPageStfb [expr int($nROC * $nPagesPerEquipment * $bufferStfCopyFraction)]
 set pageSizeKb "[expr int($readoutPageSize / 1024)]k"
 doLog "Using: $bufferTotal ($nPagesPerEquipment pages per equipment, $nROC devices)"
    
  # general config params
  lappend config " 
\[readout\]
flushEquipmentTimeout=2
aggregatorStfTimeout=0.5
aggregatorSliceTimeout=1
 
\[consumer-stats\]
enabled=1
consumerType=stats
monitoringUpdatePeriod=1
consoleUpdate=0
"

if {${monitoringURI}==""} {
  lappend config "monitoringEnabled=0"
} else {
  lappend config "monitoringEnabled=1
monitoringURI=${monitoringURI}"
}


lappend config "

\[consumer-fmq-stfb\]
enabled=1
consumerType=FairMQChannel
# fmq-name should be 'readout'
# to allow OCC to overwrite params for StfBuilder connection
fmq-name=readout
fmq-address=ipc://@readout-fmq-stfb
fmq-type=push
fmq-transport=shmem
sessionName=default
unmanagedMemorySize=${bufMB}M
memoryPoolNumberOfPages=${nPageStfb}
memoryPoolPageSize=${pageSizeKb}
disableSending=${disableSending}

\[rx-fmq-stfb\]
decodingMode=stfHbf
dumpRDH=0
dumpTF=1
channelAddress=ipc://@readout-fmq-stfb
channelType=pull
transportType=shmem
"

if {$generateQC} {
lappend config "
\[consumer-fmq-qc\]
enabled=1
consumerType=FairMQChannel
fmq-name=readout-qc
fmq-address=ipc://@readout-fmq-qc
fmq-type=pub
fmq-transport=zeromq
sessionName=default
enableRawFormat=1

\[rx-fmq-qc\]
decodingMode=none
dumpRDH=0
dumpTF=0
channelAddress=ipc://@readout-fmq-qc
channelType=sub
transportType=zeromq
"
}

if {$generateREC} {
lappend config "
\[consumer-rec\]
enabled=1
consumerType=fileRecorder
fileName=/tmp/data_eq%i.raw
bytesMax=${recBytesMax}
pagesMax=${recPagesMax}
"
}


set rocN 0
foreach {type pci endpoint numa serial channel} $ldev {
  incr rocN
  set rocName "$type $pci"
  if {$type=="CRU"} {
    set rocName "${type} ${serial}:${endpoint}"
  } elseif {$type=="CRORC"} {
  set rocName "${type} ${serial}:${channel}"
  }
  lappend config "
# ${rocName}
\[equipment-roc-${rocN}\]
enabled=1
equipmentType=rorc
cardId=${pci}
"
  if {$type=="CRORC"} {
    lappend config "channelNumber=${channel}
"  
  }
  lappend config "dataSource=${dataSource}
memoryPoolNumberOfPages=${nPagesPerEquipment}
memoryPoolPageSize=${pageSizeKb}
rdhUseFirstInPageEnabled=1
rdhCheckEnabled=${rdhCheckEnabled}
rdhDumpEnabled=0
firmwareCheckEnabled=0
"
}

}




# convert to string
set config [join $config ""]


if {$configFile==""} {
  doLog "readout.exe configuration template:"
  puts $config
  
} else {
  doLog "Creating configuration file $configFile"
  set fd [open $configFile "w"]
  puts $fd "$config"
  close $fd
}
