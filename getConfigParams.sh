#!/bin/sh

# get from source code de config parameters, sorted by section
# example use:
# ./getConfigParams.sh | awk -F "|" '{ print  $2"."$3"\t"$6}'

grep "configuration parameter:"  src/* | sed 's/^[^|]*//g' | sort -d -b -f -t  '|' -k2

