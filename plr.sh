#!/bin/bash

export LD_PRELOAD="./lib/libplrPreload.so"
exec "${@}"
