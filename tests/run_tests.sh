#!/bin/sh
# Differential test: for every program, the Phase 1 interpreter and the Phase 2
# compiled executable must produce identical stdout, stderr and exit code.
cd "$(dirname "$0")/.." || exit 2
mkdir -p tests/out
pass=0; fail=0

run_one() {   # $1 = program   $2 = stdin text (may be empty)
    prog=$1; input=$2
    name=$(basename "$prog" .bt)
    exe=tests/out/$name

    if ! ./bytetapec "$prog" -o "$exe" >tests/out/$name.build 2>&1; then
        echo "FAIL  $prog   (did not compile)"; cat tests/out/$name.build; fail=$((fail+1)); return
    fi
    printf '%s' "$input" | ./bytetape "$prog"    >tests/out/$name.i.out 2>tests/out/$name.i.err; ri=$?
    printf '%s' "$input" | "$exe"                >tests/out/$name.c.out 2>tests/out/$name.c.err; rc=$?

    if [ $ri -eq $rc ] && cmp -s tests/out/$name.i.out tests/out/$name.c.out \
                       && cmp -s tests/out/$name.i.err tests/out/$name.c.err; then
        echo "ok    $prog   (exit $rc)"; pass=$((pass+1))
    else
        echo "FAIL  $prog   interpreter exit=$ri  compiled exit=$rc"
        diff tests/out/$name.i.out tests/out/$name.c.out
        diff tests/out/$name.i.err tests/out/$name.c.err
        fail=$((fail+1))
    fi
}

for f in examples/*.bt tests/*.bt; do
    case $f in
        examples/forever.bt) continue ;;                # never halts
        tests/fail_*.bt) continue ;;                    # must NOT compile: checked below
        tests/strict_fail_*.bt) continue ;;             # valid programs, but must fail --strict: checked below
        tests/alpha_input.bt) run_one "$f" "abz" ;;     # 'z' is outside the alphabet
        examples/cat.bt|tests/input.bt) run_one "$f" "Hello, tapes!
second line
" ;;
        *) run_one "$f" "" ;;
    esac
done

# programs that must be rejected at compile time (exit code 3) by BOTH the interpreter and the compiler
for f in tests/fail_*.bt; do
    ./bytetape -q "$f" >/dev/null 2>&1; ri=$?
    ./bytetapec "$f" -o tests/out/failed >/dev/null 2>&1; rc=$?
    if [ $ri -eq 3 ] && [ $rc -eq 3 ] && [ ! -e tests/out/failed.ll ]; then
        echo "ok    $f   (compile error, exit 3)"; pass=$((pass+1))
    else
        echo "FAIL  $f   interpreter exit=$ri  compiler exit=$rc  (both should be 3)"; fail=$((fail+1))
    fi
done

# --strict: programs that are valid ByteTape but not formal Turing machines. Both tools must
# reject them with exit code 3 and leave no .ll behind; WITHOUT --strict they must not be a compile error.
for f in tests/strict_fail_*.bt; do
    rm -f tests/out/failed.ll
    ./bytetape --strict -q "$f" >/dev/null 2>&1; ri=$?
    ./bytetapec --strict "$f" -o tests/out/failed >/dev/null 2>&1; rc=$?
    ./bytetape -q "$f" >/dev/null 2>&1; rn=$?
    if [ $ri -eq 3 ] && [ $rc -eq 3 ] && [ $rn -ne 3 ] && [ ! -e tests/out/failed.ll ]; then
        echo "ok    $f   (--strict: exit 3)"; pass=$((pass+1))
    else
        echo "FAIL  $f   strict interpreter=$ri  strict compiler=$rc  without --strict=$rn"; fail=$((fail+1))
    fi
done

# --strict: formal Turing machines must be accepted, and --strict must not change what they do.
for f in examples/anbn.bt examples/increment.bt examples/palindrome.bt tests/strict_ok_*.bt; do
    name=$(basename "$f" .bt)
    ./bytetape -q "$f"          >tests/out/$name.plain.out 2>&1; rp=$?
    ./bytetape --strict -q "$f" >tests/out/$name.strict.out 2>&1; rs=$?
    ./bytetapec --strict -S "$f" -o tests/out/strict_$name >/dev/null 2>&1; rc=$?
    if [ $rs -ne 3 ] && [ $rs -eq $rp ] && [ $rc -eq 0 ] && cmp -s tests/out/$name.plain.out tests/out/$name.strict.out; then
        echo "ok    $f   (passes --strict)"; pass=$((pass+1))
    else
        echo "FAIL  $f   plain exit=$rp  strict exit=$rs  strict compile=$rc"; cat tests/out/$name.strict.out; fail=$((fail+1))
    fi
done

# compile errors must be reported by the compiler too (exit code 3, no output file)
cat > tests/out/bad.bt <<'BAD'
%% tapes
T: {1}
%% states
state S { move Nope R  transition Missing }
BAD
./bytetapec tests/out/bad.bt -o tests/out/bad >/dev/null 2>&1; rc=$?
if [ $rc -eq 3 ] && [ ! -e tests/out/bad.ll ]; then
    echo "ok    compile error -> exit 3, no .ll left behind"; pass=$((pass+1))
else
    echo "FAIL  compile error handling (exit $rc)"; fail=$((fail+1))
fi

echo "---- $pass passed, $fail failed"
[ $fail -eq 0 ]
