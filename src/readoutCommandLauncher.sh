#!/bin/sh
# wrapper script to execute commands from stdin
# invoked by o2-readout-exe
# if launched without argument, read commands from stdin line by line

# ensure log output not going to stdout
unset O2_INFOLOGGER_MODE

LOG="/opt/o2-InfoLogger/bin/o2-infologger-log -oFacility=readout -oLevel=11 -oErrorCode=3013"
CMD="'$*'"

if [ "$1" == "" ]; then

  $LOG "Starting command launcher"
  while read line
  do
    $LOG "Executing command $line"
    eval $line 2>&1 | $LOG -x -oSeverity=Debug

    if [ 0 -eq ${PIPESTATUS[0]} ]; then
      echo "ok"
      $LOG "Command ok"
    else
      echo "error"
      $LOG -oSeverity=Error "Command error"
    fi
  done
  $LOG "Exiting command launcher"
  exit 0 

else

  $LOG "Executing command $CMD"
  $@ 2>&1 | $LOG -x -oSeverity=Debug

  if [ 0 -eq ${PIPESTATUS[0]} ]; then
    echo "ok"
    $LOG "Command ok"
    exit 0
  else
    echo "error"
    $LOG -oSeverity=Error "Command error"
    exit 1
  fi

fi

