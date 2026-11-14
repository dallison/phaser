# cmake -DOUTPUT=<file> -DINPUTS=<file>;<file>... -P concatenate.cmake
#
# Writes the INPUTS, in order, to OUTPUT.

file(WRITE "${OUTPUT}.tmp" "")
foreach(input IN LISTS INPUTS)
    file(READ "${input}" contents)
    file(APPEND "${OUTPUT}.tmp" "${contents}")
endforeach()
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
