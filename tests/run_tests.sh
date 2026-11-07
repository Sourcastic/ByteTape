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
        examples/cat.bt|tests/input.bt) run_one "$f" "Hello, tapes!
second line
" ;;
        *) run_one "$f" "" ;;
    esac
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
