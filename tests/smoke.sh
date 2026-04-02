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

OUT_DIR=build/test-artifacts
DEFAULT_OUT="$OUT_DIR/qemu-default.log"
COMMANDS_IN="$OUT_DIR/qemu-commands.txt"
MISSING_KERNEL_IMG="$OUT_DIR/missing-kernel.img"
MISSING_KERNEL_OUT="$OUT_DIR/qemu-missing-kernel.log"

mkdir -p "$OUT_DIR"

"$FAT_TOOL" "$IMAGE" "MYDIR/TEST.TXT" | grep -F "Hello FunnyOS From a file, Solus here!!~~" >/dev/null
"$FAT_TOOL" "$IMAGE" "BIGDIR" | grep -F "ITEM69.TXT" >/dev/null
"$FAT_TOOL" "$IMAGE" "BIGFILE.TXT" | grep -F "FunnyOS big file line for FAT16 multi-cluster testing." >/dev/null

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "qemu not found, host-side FAT16 hard-disk check passed"
    exit 0
fi

run_qemu_capture() {
    image_path=$1
    output_path=$2
    input_path=$3
    input_fifo=$OUT_DIR/qemu-input.fifo

    rm -f "$output_path"
    rm -f "$input_fifo"
    mkfifo "$input_fifo"

    if [ "$input_path" = /dev/null ]; then
        "$QEMU" -drive format=raw,file="$image_path",if=ide -display none -serial stdio -monitor none -no-reboot <"$input_fifo" >"$output_path" 2>&1 &
        qemu_pid=$!
        : >"$input_fifo" &
        writer_pid=$!
        sleep 3
    else
        (
            sleep 1
            while IFS= read -r line; do
                printf '%s\r' "$line"
                sleep 1
            done <"$input_path"
            sleep 1
        ) >"$input_fifo" &
        writer_pid=$!
        "$QEMU" -drive format=raw,file="$image_path",if=ide -display none -serial stdio -monitor none -no-reboot <"$input_fifo" >"$output_path" 2>&1 &
        qemu_pid=$!
        sleep 15
    fi
    kill "$qemu_pid" >/dev/null 2>&1 || true
    kill "$writer_pid" >/dev/null 2>&1 || true
    wait "$qemu_pid" >/dev/null 2>&1 || true
    wait "$writer_pid" >/dev/null 2>&1 || true
    rm -f "$input_fifo"
}

cat >"$COMMANDS_IN" <<'EOF'
help
pwd
ls /
cd /MYDIR
pwd
cat /MYDIR/TEST.TXT
ls /BIGDIR
cat /BIGFILE.TXT
cd /NOPE
cat /NOPE
EOF

run_qemu_capture "$IMAGE" "$DEFAULT_OUT" "$COMMANDS_IN"
grep -F "FunnyOS shell ready" "$DEFAULT_OUT" >/dev/null
grep -F "Commands: help ls cd pwd cat clear" "$DEFAULT_OUT" >/dev/null
grep -F "FunnyOS:/> pwd" "$DEFAULT_OUT" >/dev/null
grep -F "/MYDIR" "$DEFAULT_OUT" >/dev/null
grep -F "Hello FunnyOS From a file, Solus here!!~~" "$DEFAULT_OUT" >/dev/null
grep -F "ITEM69.TXT" "$DEFAULT_OUT" >/dev/null
grep -F "FunnyOS big file line for FAT16 multi-cluster testing." "$DEFAULT_OUT" >/dev/null
grep -F "not found" "$DEFAULT_OUT" >/dev/null

"$IMG_TOOL" "$MISSING_KERNEL_IMG" "$MBR" "$STAGE1" "$STAGE2" "-" "$ROOT_TEST" "$DEMO_TEST"
run_qemu_capture "$MISSING_KERNEL_IMG" "$MISSING_KERNEL_OUT" /dev/null
grep -F "Stage2: KERNEL.BIN not found" "$MISSING_KERNEL_OUT" >/dev/null
