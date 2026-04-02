#!/bin/sh
set -eu

IMAGE=$1
QEMU=$2
IMG_TOOL=$3
FAT_TOOL=$4
MBR=$5
STAGE1=$6
STAGE2=$7
KERNEL=$8
ROOT_TEST=$9
DEMO_TEST=${10}
HELLO_PROGRAM=${11}
ARGS_PROGRAM=${12}

OUT_DIR=build/test-artifacts
DEFAULT_OUT="$OUT_DIR/qemu-default.log"
COMMANDS_IN="$OUT_DIR/qemu-commands.txt"
MISSING_KERNEL_IMG="$OUT_DIR/missing-kernel.img"
MISSING_KERNEL_OUT="$OUT_DIR/qemu-missing-kernel.log"
MUTABLE_IMAGE="$OUT_DIR/funnyos-mutable.img"

mkdir -p "$OUT_DIR"

"$FAT_TOOL" "$IMAGE" "MYDIR/TEST.TXT" | grep -F "Hello FunnyOS From a file, Solus here!!~~" >/dev/null
"$FAT_TOOL" "$IMAGE" "BIGDIR" | grep -F "ITEM69.TXT" >/dev/null
"$FAT_TOOL" "$IMAGE" "BIGFILE.TXT" | grep -F "FunnyOS big file line for FAT16 multi-cluster testing." >/dev/null
"$FAT_TOOL" "$IMAGE" "/" | grep -F "HELLO.BIN" >/dev/null
"$FAT_TOOL" "$IMAGE" "/" | grep -F "ARGS.BIN" >/dev/null

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "qemu not found, host-side FAT16 hard-disk check passed"
    exit 0
fi

cp "$IMAGE" "$MUTABLE_IMAGE"

run_qemu_capture() {
    image_path=$1
    output_path=$2
    input_path=$3
    input_fifo=$OUT_DIR/qemu-input.fifo

    rm -f "$output_path"
    rm -f "$input_fifo"
    mkfifo "$input_fifo"

    if [ "$input_path" = /dev/null ]; then
        stdbuf -o0 -e0 "$QEMU" -drive format=raw,file="$image_path",if=ide -display none -serial stdio -monitor none -no-reboot <"$input_fifo" >"$output_path" 2>&1 &
        qemu_pid=$!
        : >"$input_fifo" &
        writer_pid=$!
        sleep 3
    else
        (
            sleep 3
            while IFS= read -r line; do
                printf '%s\r' "$line"
                sleep 1
            done <"$input_path"
            sleep 1
        ) >"$input_fifo" &
        writer_pid=$!
        stdbuf -o0 -e0 "$QEMU" -drive format=raw,file="$image_path",if=ide -display none -serial stdio -monitor none -no-reboot <"$input_fifo" >"$output_path" 2>&1 &
        qemu_pid=$!
        wait "$writer_pid" >/dev/null 2>&1 || true
        sleep 5
    fi
    kill "$qemu_pid" >/dev/null 2>&1 || true
    wait "$qemu_pid" >/dev/null 2>&1 || true
    wait "$writer_pid" >/dev/null 2>&1 || true
    rm -f "$input_fifo"
}

cat >"$COMMANDS_IN" <<'EOF'
help
pwd
ls /
HELLO
/HELLO.BIN
ARGS one two
/MYDIR/TEST.TXT
mkdir /KEEP
mkdir /KEEP
write /KEEP/FILE.TXT hello world
cat /KEEP/FILE.TXT
append /KEEP/FILE.TXT more
cat /KEEP/FILE.TXT
write /KEEP/EMPTY.TXT
mv /KEEP/FILE.TXT /KEEP/FINAL.TXT
write /KEEP/OTHER.TXT sample
mv /KEEP/FINAL.TXT /KEEP/OTHER.TXT
rm /KEEP
mkdir /WORK
write /WORK/NOTE.TXT temp
rm /WORK
rm /WORK/NOTE.TXT
rm /WORK
cd /MYDIR
pwd
cat /MYDIR/TEST.TXT
ls /BIGDIR
cat /BIGFILE.TXT
/NOPE.BIN
cd /NOPE
cat /NOPE
ZZZ
EOF

run_qemu_capture "$MUTABLE_IMAGE" "$DEFAULT_OUT" "$COMMANDS_IN"
grep -F "FunnyOS shell ready" "$DEFAULT_OUT" >/dev/null
grep -F "Commands: help ls cd pwd cat clear mkdir write append rm mv" "$DEFAULT_OUT" >/dev/null
grep -F "FunnyOS:/> pwd" "$DEFAULT_OUT" >/dev/null
grep -F "Hello from HELLO.BIN" "$DEFAULT_OUT" >/dev/null
grep -F "argc=3" "$DEFAULT_OUT" >/dev/null
grep -F "argv[1]=one" "$DEFAULT_OUT" >/dev/null
grep -F "argv[2]=two" "$DEFAULT_OUT" >/dev/null
grep -F "FunnyOS:/> /MYDIR/TEST.TXT" "$DEFAULT_OUT" >/dev/null
grep -F "already exists" "$DEFAULT_OUT" >/dev/null
grep -F "hello world" "$DEFAULT_OUT" >/dev/null
grep -F "hello worldmore" "$DEFAULT_OUT" >/dev/null
grep -F "directory not empty" "$DEFAULT_OUT" >/dev/null
grep -F "/MYDIR" "$DEFAULT_OUT" >/dev/null
grep -F "Hello FunnyOS From a file, Solus here!!~~" "$DEFAULT_OUT" >/dev/null
grep -F "ITEM69.TXT" "$DEFAULT_OUT" >/dev/null
grep -F "FunnyOS big file line for FAT16 multi-cluster testing." "$DEFAULT_OUT" >/dev/null
grep -F "program not found" "$DEFAULT_OUT" >/dev/null
grep -F "unknown command" "$DEFAULT_OUT" >/dev/null
grep -F "not found" "$DEFAULT_OUT" >/dev/null

"$FAT_TOOL" "$MUTABLE_IMAGE" "KEEP" | grep -F "FINAL.TXT" >/dev/null
"$FAT_TOOL" "$MUTABLE_IMAGE" "KEEP" | grep -F "OTHER.TXT" >/dev/null
"$FAT_TOOL" "$MUTABLE_IMAGE" "KEEP" | grep -F "EMPTY.TXT" >/dev/null
"$FAT_TOOL" "$MUTABLE_IMAGE" "KEEP/FINAL.TXT" | grep -F "hello worldmore" >/dev/null
if "$FAT_TOOL" "$MUTABLE_IMAGE" "KEEP/FILE.TXT" >/dev/null 2>&1; then
    echo "old file name still exists after rename"
    exit 1
fi
if "$FAT_TOOL" "$MUTABLE_IMAGE" "WORK" >/dev/null 2>&1; then
    echo "removed directory still exists"
    exit 1
fi

"$IMG_TOOL" "$MISSING_KERNEL_IMG" "$MBR" "$STAGE1" "$STAGE2" "-" "$ROOT_TEST" "$DEMO_TEST" "$HELLO_PROGRAM" "$ARGS_PROGRAM"
run_qemu_capture "$MISSING_KERNEL_IMG" "$MISSING_KERNEL_OUT" /dev/null
grep -F "Stage2: KERNEL.BIN not found" "$MISSING_KERNEL_OUT" >/dev/null
