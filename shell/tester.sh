#!/bin/bash

# Remove the results. file if it exists
if [ -f results.txt ]; then
    rm results.txt
fi

# Loop over the test numbers
for i in $(seq -f "%02g" 1 16)
do
    # Replace 'xx' with the test number and run the command, appending the output to results.
    echo "Running test$i:" >> results.txt
    diff <(make test$i) <(make rtest$i) >> results.txt
    echo "" >> results.txt
done

echo "Tests completed. Results are in results.txt."