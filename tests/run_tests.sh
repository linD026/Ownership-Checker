#/usr/bin/env bash

if [[ -n "$1" ]]; then
    DIR="$1"
else
    DIR="$(pwd)/.."
fi

BIN="$DIR/osc"
log="stderr_tests.log"

declare -a test_files=(
    "test_function_declaration.c"
    "test_function_definition.c"
    "test_comment.c"
    "test_structure.c"
    "test_write.c"
    "test_loop.c"
    "test_if.c"
    # "test_macro.c"
)

# file
function do_test {
    local file="$1"

	# Execute and pipe the stderr to log file
    $BIN $DIR/tests/$file >> /dev/null 2> $log
    # Check " ERROR:" string and the count
	local error_count=$(cat $log | egrep -c "WARN ON:")
	local bug_count=$(cat $log | egrep -c "BUG ON:")

	if [ $error_count -gt 0 ] || [ $bug_count -gt 0 ]; then
        printf "[TEST] %-30s ... failed %2d warning(s), %2d error(s)\n" \
            $file $error_count $bug_count
        cat $log
        return 1
    fi

    printf "[TEST] %-30s ... passed\n" $file
}

make -C $DIR clean quiet=1 --no-print-directory
if [ $? -ne 0 ]; then
    exit 1
fi
make -C $DIR verbose=1 quiet=1 --no-print-directory
if [ $? -ne 0 ]; then
    exit 1
fi

for i in "${test_files[@]}"; do
    do_test $i
done

rm -f $log
