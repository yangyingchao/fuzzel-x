#!/usr/bin/fish

set got

# Quick-launch: <modifier>+N executes the Nth entry on the current page
# (dmenu mode prints it to stdout, exit code 0)
function test_quick_launch --argument-names input n
    rm -f out.txt
    echo -e "$input" \
        | $FUZZEL_TEST_BIN --dmenu >out.txt &
    sleep .1
    wtype -M Super -k $n -m Super
    sleep .1
    set got (cat out.txt)
end

# Quick-launch must be a no-op when the page has fewer than N entries
function test_quick_launch_noop --argument-names input n
    rm -f out.txt
    echo -e "$input" \
        | $FUZZEL_TEST_BIN --dmenu >out.txt &
    sleep .1
    wtype -M Super -k $n -m Super
    sleep .1
    wtype -k Escape
    sleep .1
    set got (cat out.txt)
end

set nine "a1\nb2\nc3\nd4\ne5\nf6\ng7\nh8\ni9"

test_quick_launch "$nine" 3
@test "quick-launch: Super+3 prints 3rd entry" "$got" = "c3"

test_quick_launch "$nine" 9
@test "quick-launch: Super+9 prints 9th entry" "$got" = "i9"

test_quick_launch_noop "a1\nb2" 5
@test "quick-launch: Super+5 with 2 entries prints nothing" "$got" = ""
