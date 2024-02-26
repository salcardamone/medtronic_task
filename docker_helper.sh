#!/bin/bash

echo "Select what action to take:"
echo "1. Run tests"
echo "2. Run application"

read -p "Enter your selection: " choice

if [ "$choice" = "1" ];
then
    ctest -V --test-dir ./build/test
elif [ "$choice" = "2" ];
then
    read -p "Select the number of sensors to create: " num_sensors
    ./build/src/medtronic_task $num_sensors
else
    echo "Invalid selection. Exiting."
fi
