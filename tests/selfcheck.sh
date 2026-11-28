#!/bin/bash
# runs the 12 rows of §5.4 on 127.0.0.1
# usage: tests/selfcheck.sh [row ...]    (no rows = all 12)

cd "$(dirname "$0")/.." || exit 1

PORT=5000
C="./client 127.0.0.1 $PORT"
LOG=$(mktemp -d)
pass=0
fail=0

ok()  { echo "PASS $1"; pass=$((pass + 1)); }
bad() { echo "FAIL $1: $2"; fail=$((fail + 1)); }

first() { "$@" 2>/dev/null | head -n 1; }

# wait up to $1 seconds for status of $2 to be $3
wait_status() {
    local end=$((SECONDS + $1))
    while [ $SECONDS -le $end ]; do
        [ "$(first $C status "$2")" = "$3" ] && return 0
        sleep 0.5
    done
    return 1
}

# wait up to $1 seconds for no process matching $2
gone() {
    local end=$((SECONDS + $1))
    while [ $SECONDS -le $end ]; do
        pgrep -xf "$2" >/dev/null || return 0
        sleep 0.2
    done
    return 1
}

fds() { ls /proc/"$1"/fd 2>/dev/null | wc -l; }

no_zombies() { ! ps -o stat= --ppid "$1" | grep -q Z; }

cleanup() {
    kill "$SPID" "$WPID" 2>/dev/null
    wait 2>/dev/null
}
trap cleanup EXIT

start() {
    make >"$LOG/make" 2>&1 || return
    ./server $PORT >"$LOG/server" 2>&1 &
    SPID=$!
    sleep 0.5
    ./worker 127.0.0.1 $PORT 1 >"$LOG/worker" 2>&1 &
    WPID=$!
    sleep 0.5
}

row1() {
    for f in server worker client; do
        [ -x $f ] || { bad 1 "no ./$f, see $LOG/make"; return; }
    done
    grep -q "^server: listening on port $PORT" "$LOG/server" || { bad 1 "server start line"; return; }
    grep -q "^worker: connected to 127.0.0.1:$PORT, K=1" "$LOG/worker" || { bad 1 "worker start line"; return; }
    ok 1
}

row2() {
    local id
    id=$(first $C submit "echo hello" 10)
    sleep 2
    [ "$(first $C status "$id")" = "$id DONE 0" ] || { bad 2 "status of '$id'"; return; }
    [ "$($C fetch "$id" 2>/dev/null)" = "hello" ] || { bad 2 "fetch"; return; }
    ok 2
}

row3() {
    local a b
    a=$(first $C submit "exit 3" 10)
    b=$(first $C submit "ls /nonexistent" 10)
    wait_status 5 "$a" "$a DONE 3" || { bad 3 "exit 3 should be DONE 3"; return; }
    wait_status 5 "$b" "$b DONE 2" || { bad 3 "ls should be DONE 2"; return; }
    $C fetch "$b" 2>/dev/null | grep -q "No such file" || { bad 3 "fetch has no 'No such file'"; return; }
    ok 3
}

row4() {
    local id
    id=$(first $C submit "sleep 100" 2)
    sleep 4
    [ "$(first $C status "$id")" = "$id KILLED TIMEOUT" ] || { bad 4 "not KILLED TIMEOUT"; return; }
    gone 0 "sleep 100" || { bad 4 "sleep 100 still running"; return; }
    ok 4
}

row5() {
    local id
    id=$(first $C submit "sleep 30" 60)
    sleep 1
    [ "$(first $C status "$id")" = "$id RUNNING" ] || { bad 5 "not RUNNING"; return; }
    [ "$(first $C cancel "$id")" = "$id CANCELLED" ] || { bad 5 "cancel answer"; return; }
    [ "$(first $C status "$id")" = "$id CANCELLED" ] || { bad 5 "status after cancel"; return; }
    gone 2 "sleep 30" || { bad 5 "sleep 30 still running"; return; }
    ok 5
}

row6() {
    local ids=() max=0 n end fin
    for _ in 1 2 3; do
        ids+=("$(first $C submit "sleep 3" 10)")
    done
    end=$((SECONDS + 12))
    while [ $SECONDS -le $end ]; do
        n=$(pgrep -xfc "sleep 3")
        [ "$n" -gt "$max" ] && max=$n
        fin=0
        for id in "${ids[@]}"; do
            [ "$(first $C status "$id")" = "$id DONE 0" ] && fin=$((fin + 1))
        done
        [ $fin -eq 3 ] && break
        sleep 0.5
    done
    [ "$max" -le 1 ] || { bad 6 "$max ran at once with K=1"; return; }
    [ "$fin" -eq 3 ] || { bad 6 "not all DONE 0 in 12 s"; return; }
    ok 6
}

row7() {
    local t0=$SECONDS ids pids=()
    for i in $(seq 8); do
        first $C submit "sleep 2" 10 >"$LOG/c$i" &
        pids+=($!)
    done
    wait "${pids[@]}"
    [ $((SECONDS - t0)) -le 2 ] || { bad 7 "ids took more than 2 s"; return; }
    ids=$(cat "$LOG"/c[1-8] | sort -u | grep -c .)
    [ "$ids" -eq 8 ] || { bad 7 "got $ids different ids, want 8"; return; }
    for f in "$LOG"/c[1-8]; do
        local id
        id=$(cat "$f")
        wait_status $((t0 + 25 - SECONDS)) "$id" "$id DONE 0" || { bad 7 "$id not DONE 0 in 25 s"; return; }
    done
    ok 7
}

row8() {
    local id
    exec 3<>/dev/tcp/127.0.0.1/$PORT || { bad 8 "silent connect failed"; return; }
    id=$(timeout 2 $C submit "echo x" 10 2>/dev/null | head -n 1)
    exec 3>&-
    [ -n "$id" ] || { bad 8 "submit blocked by a silent client"; return; }
    ok 8
}

row9() {
    local out rc
    out=$($C status T4-99999 2>/dev/null)
    rc=$?
    [ "${out%%$'\n'*}" = "T4-99999 UNKNOWN" ] || { bad 9 "answer"; return; }
    [ $rc -eq 2 ] || { bad 9 "exit code $rc, want 2"; return; }
    ok 9
}

row10() {
    no_zombies "$WPID" || { bad 10 "zombie under worker"; return; }
    no_zombies "$SPID" || { bad 10 "zombie under server"; return; }
    ok 10
}

row11() {
    local s0 w0 id
    sleep 1
    s0=$(fds "$SPID")
    w0=$(fds "$WPID")
    for _ in $(seq 100); do
        id=$(first $C submit "echo hi" 10)
    done
    wait_status 60 "$id" "$id DONE 0" || { bad 11 "jobs did not finish"; return; }
    sleep 1
    [ "$(fds "$SPID")" -eq "$s0" ] || { bad 11 "server fds $s0 -> $(fds "$SPID")"; return; }
    [ "$(fds "$WPID")" -eq "$w0" ] || { bad 11 "worker fds $w0 -> $(fds "$WPID")"; return; }
    ok 11
}

row12() {
    local id st t0
    id=$(first $C submit "sleep 30" 60)
    sleep 1
    kill -TERM "$WPID"
    sleep 2
    [ "$(first $C status "$id")" = "$id KILLED SIGNAL 9" ] || { bad 12 "not KILLED SIGNAL 9"; return; }
    gone 0 "sleep 30" || { bad 12 "sleep 30 still running"; return; }
    st=$(ps -o stat= -p "$WPID")
    [ -z "$st" ] || [[ $st == Z* ]] || { bad 12 "worker did not exit"; return; }

    kill -TERM "$SPID"
    t0=$SECONDS
    while kill -0 "$SPID" 2>/dev/null && ! [[ $(ps -o stat= -p "$SPID") == Z* ]]; do
        [ $((SECONDS - t0)) -ge 2 ] && { bad 12 "server still up after 2 s"; return; }
        sleep 0.1
    done
    ./server $PORT >"$LOG/server2" 2>&1 &
    SPID=$!
    sleep 0.5
    grep -q "Address already in use" "$LOG/server2" && { bad 12 "Address already in use"; return; }
    kill -0 "$SPID" 2>/dev/null || { bad 12 "new server died"; return; }
    ok 12
}

start
for r in ${*:-1 2 3 4 5 6 7 8 9 10 11 12}; do
    row$r
done

echo "$pass passed, $fail failed (logs in $LOG)"
[ $fail -eq 0 ]
