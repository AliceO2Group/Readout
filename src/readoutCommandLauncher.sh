#!/bin/sh
# wrapper script to execute commands from stdin
# invoked by o2-readout-exe
# if launched without argument, read commands from stdin line by line

# ensure log output not going to stdout, all goes to stderr

LOG="eval >&2 echo"
CMD="'$*'"

if [ "$1" == "" ]; then

  $LOG "Starting command launcher"
  while read line
  do
    $LOG "Executing command $line"
    >&2 eval $line

    if [ 0 -eq ${PIPESTATUS[0]} ]; then
      echo "ok"
      $LOG "Command ok"
    else
      echo "error"
      $LOG "Command error"
    fi
  done
  $LOG "Exiting command launcher"
  exit 0 

else

  $LOG "Executing command $CMD"
  >&2 $@

  if [ 0 -eq ${PIPESTATUS[0]} ]; then
    echo "ok"
    $LOG "Command ok"
    exit 0
  else
    echo "error"
    $LOG "Command error"
    exit 1
  fi

fi

