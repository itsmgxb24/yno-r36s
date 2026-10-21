#!/bin/bash
# Yume Nikki Online - native client (no browser): appears in the EmulationStation ports menu.
# The work is done by yno-native/run.sh; without an argument it shows the list of YNOproject games.
exec bash "$(cd "$(dirname "$0")" && pwd)/yno-native/run.sh"
