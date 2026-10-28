#!/bin/sh
case "$1" in
  --version) echo "0.29.2"; exit 0 ;;
  --exists|--modversion|--print-errors) exit 1 ;;
  *) exit 0 ;;
esac
