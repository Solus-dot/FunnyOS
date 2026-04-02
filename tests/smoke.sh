#!/bin/sh
set -eu

IMAGE=$1
QEMU=$2
IMG_TOOL=$3
FAT_TOOL=$4
STAGE1=$5
STAGE2=$6
KERNEL=$7
ROOT_TEST=$8
DEMO_TEST=$9

OUT_DIR=build/test-artifacts
DEFAULT_OUT="$OUT_DIR/qemu-default.log"
MISSING_KERNEL_IMG="$OUT_DIR/missing-kernel.img"
MISSING_KERNEL_OUT="$OUT_DIR/qemu-missing-kernel.log"
MISSING_DEMO_IMG="$OUT_DIR/missing-demo.img"
MISSING_DEMO_OUT="$OUT_DIR/qemu-missing-demo.log"

mkdir -p "$OUT_DIR"

"$FAT_TOOL" "$IMAGE" "MYDIR/TEST.TXT" | grep -F "Hello FunnyOS From a file, Solus here!!~~" >/dev/null

if ! command -v "$QEMU" >/dev/null 2>&1; then
    echo "qemu not found, host-side FAT12 check passed"
    exit 0
fi

run_qemu_capture() {
    image_path=$1
    output_path=$2

    rm -f "$output_path"
    "$QEMU" -drive format=raw,file="$image_path",if=floppy -display none -serial stdio -monitor none -no-reboot >"$output_path" 2>&1 &
    qemu_pid=$!
    sleep 2
    kill "$qemu_pid" >/dev/null 2>&1 || true
    wait "$qemu_pid" >/dev/null 2>&1 || true
}

run_qemu_capture "$IMAGE" "$DEFAULT_OUT"
grep -F "FunnyOS kernel up" "$DEFAULT_OUT" >/dev/null
grep -F "Root directory:" "$DEFAULT_OUT" >/dev/null
grep -F "Demo file:" "$DEFAULT_OUT" >/dev/null
grep -F "Hello FunnyOS From a file, Solus here!!~~" "$DEFAULT_OUT" >/dev/null

"$IMG_TOOL" "$MISSING_KERNEL_IMG" "$STAGE1" "$STAGE2" "-" "$ROOT_TEST" "$DEMO_TEST"
run_qemu_capture "$MISSING_KERNEL_IMG" "$MISSING_KERNEL_OUT"
grep -F "Stage2: KERNEL.BIN not found" "$MISSING_KERNEL_OUT" >/dev/null

"$IMG_TOOL" "$MISSING_DEMO_IMG" "$STAGE1" "$STAGE2" "$KERNEL" "$ROOT_TEST" "-"
run_qemu_capture "$MISSING_DEMO_IMG" "$MISSING_DEMO_OUT"
grep -F "Stage2: TEST.TXT not found" "$MISSING_DEMO_OUT" >/dev/null
