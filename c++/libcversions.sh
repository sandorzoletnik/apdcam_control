#!/bin/bash
##############################################################################
##
##  List the ABI versions of all detected libc and libstdc++'s
##
##  Author:   Kevin Ernst <ernstki -at- mail.uc.edu>
##  Date:     5 February 2018
##  License:  MIT
##  Source:   https://gist.github.com/ernstki/593076d2d164ca51d4a6c0a15731846f
## 
##  See also:
##  ---------
##    1. https://gcc.gnu.org/onlinedocs/libstdc++/manual/abi.html
##
##############################################################################

# Print copies of character "$2" for length of string $1
print_x_for_length_of() {
    for (( i=0; i<${#2}; i++ )); do echo -n "$1"; done
}

print_n_of() {
    for (( i=0; i<$1; i++ )); do echo -n "$2"; done
}

indent_and_ul() {
    print_n_of 2 ' '
    echo -ne "$1\n"
    print_n_of 2 ' '
    print_x_for_length_of '-' "$1"
    echo
}


echo "Library paths found in /etc/ld.so.conf (system linker config)"
echo "============================================================="
echo

for lib in $( ldconfig -p | awk -F'=>' '$2 ~ /libc\.|libstdc/{print $2}' ); do
    indent_and_ul "$lib supports the following ABI versions:"
    objdump -x "$lib" | grep -P '\sGLIBC(..)?_\d+\.\d+(\.\d+)?$' \
                      | cut -d' ' -f4 | sed 's/\(.*\)/  \1/' | column -c78
    echo
done

echo
echo "Library paths found in LD_LIBRARY_PATH (dynamic linker runtime path)"
echo "===================================================================="
echo

for libdir in $( echo $LD_LIBRARY_PATH | tr : '\n' ); do
    if [[ ! -d $libdir ]]; then
        echo "  WARNING: skipping '$libdir'; not found or not a directory" >&2
        continue
    fi

    pushd "$libdir" >/dev/null

    # see: http://mywiki.wooledge.org/BashPitfalls#for_i_in_.24.28ls_.2A.mp3.29
    while IFS= read -r -d '' lib; do
        indent_and_ul "$libdir/$lib supports the following ABI versions:"
        objdump -x "$lib" | grep -P '\sGLIBC(..)?_\d+\.\d+(\.\d+)?$' \
                          | cut -d' ' -f4 \
                          | sed 's/\(.*\)/  \1/' | column -c78
        echo
    done < <( find * -maxdepth 0 -type f \
                     -regex ".*lib\(std\)?c\(\+\+\)?\.so\.[.0-9]+$" -print0 )
    popd >/dev/null
done
