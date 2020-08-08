#!/bin/bash
if [[ $1 == "-f" ]]
then
  shift
  tail -fn +0 "$@"
else
  cat "$@"
fi | grep -e '^XDF' -e '^JDF' -e '^ERR' -e '^HDR' 
