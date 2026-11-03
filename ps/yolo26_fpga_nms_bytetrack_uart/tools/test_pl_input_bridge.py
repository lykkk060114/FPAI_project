#!/usr/bin/env python3
"""One-shot FPGA-007 PL input bridge test using a reserved u-dma-buf range."""

import argparse
import mmap
import os
import struct
import sys
import time

CSR_BASE = 0x400C4000
INPUT_ID = 0x584A4901
READY = 1 << 4
BUSY = 1 << 0
DONE = 1 << 1
ERROR = 1 << 2
REJECTED = 1 << 3
ACCEPTED = 1 << 5
FRAME_BYTES = 2_703_360
FRAME_BEATS = 42_240
TEST_OFFSET = 32 * 1024 * 1024
PAGE = 4096


def read_u32(mm, offset):
    return struct.unpack_from("<I", mm, offset)[0]


def write_u32(mm, offset, value):
    struct.pack_into("<I", mm, offset, value)
    read_u32(mm, offset)


def sync_sysfs(root, name, value):
    with open(os.path.join(root, name), "w", encoding="ascii") as stream:
        stream.write(str(value))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true", help="start one PL capture/write task")
    parser.add_argument("--timeout", type=float, default=5.0)
    args = parser.parse_args()

    csr_fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    csr = mmap.mmap(csr_fd, PAGE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                    offset=CSR_BASE)
    try:
        hwid = read_u32(csr, 0)
        status = read_u32(csr, 4)
        print(f"INPUT_ID=0x{hwid:08x} STATUS=0x{status:08x}", flush=True)
        if hwid != INPUT_ID or not (status & READY) or (status & BUSY):
            raise RuntimeError("INPUT CSR is missing or not READY/idle")
        if not args.run:
            print("Read-only probe. Add --run to start the single-frame bridge test.")
            return 0

        root = "/sys/class/u-dma-buf/udmabuf0"
        with open(os.path.join(root, "phys_addr"), encoding="ascii") as stream:
            pool_base = int(stream.read().strip(), 0)
        with open(os.path.join(root, "size"), encoding="ascii") as stream:
            pool_size = int(stream.read().strip(), 0)
        if TEST_OFFSET + FRAME_BYTES > pool_size:
            raise RuntimeError("reserved u-dma-buf range is too small")
        target = pool_base + TEST_OFFSET
        if target & 63:
            raise RuntimeError("test address is not 64-byte aligned")
        print(f"TARGET=0x{target:08x} BYTES={FRAME_BYTES} BEATS={FRAME_BEATS}", flush=True)

        dma_fd = os.open("/dev/udmabuf0", os.O_RDWR | os.O_SYNC)
        dma = mmap.mmap(dma_fd, pool_size, mmap.MAP_SHARED,
                        mmap.PROT_READ | mmap.PROT_WRITE, offset=0)
        test_region = memoryview(dma)[TEST_OFFSET:TEST_OFFSET + FRAME_BYTES]
        task_started = False
        task_finished = False
        try:
            # A marker makes a non-written target detectable. The range is 32 MiB
            # into the dedicated 128 MiB u-dma-buf pool, away from HDMI's base.
            test_region[:] = b"\xa5" * FRAME_BYTES
            sync_sysfs(root, "sync_direction", 1)
            sync_sysfs(root, "sync_offset", TEST_OFFSET)
            sync_sysfs(root, "sync_size", FRAME_BYTES)
            sync_sysfs(root, "sync_for_device", 1)

            for offset, value in ((0x0C, target), (0x10, FRAME_BYTES),
                                  (0x14, int(time.time()) & 0xFFFFFFFF),
                                  (0x18, 0), (0x1C, 100_000_000), (0x20, 64)):
                write_u32(csr, offset, value)
            started_at = time.monotonic()
            write_u32(csr, 0x08, 1)
            task_started = True
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                status = read_u32(csr, 4)
                if status & (ERROR | REJECTED | ACCEPTED | DONE):
                    break
                time.sleep(0.002)
            count = read_u32(csr, 0x24)
            beats = read_u32(csr, 0x28)
            byte_count = read_u32(csr, 0x2C)
            print(f"AFTER_CAPTURE STATUS=0x{status:08x} ACCEPTED={bool(status & ACCEPTED)} "
                  f"COUNT={count} BEATS={beats} BYTES={byte_count} "
                  f"ELAPSED_MS={(time.monotonic() - started_at) * 1000:.2f}", flush=True)

            if not (status & ACCEPTED) or count != FRAME_BEATS or beats != FRAME_BEATS:
                raise RuntimeError("bridge did not accept one complete 42240-beat frame")

            sync_sysfs(root, "sync_direction", 2)
            sync_sysfs(root, "sync_for_cpu", 1)
            raw = test_region.tobytes()
            floats = struct.unpack_from("<64f", raw)
            tail = struct.unpack_from("<64f", raw, FRAME_BYTES - 256)
            sample = floats + tail
            finite = [x for x in sample if x == x and abs(x) != float("inf")]
            valid = [x for x in finite if 0.0 <= x <= 255.0 and abs(x - round(x)) < 1e-5]
            print(f"READBACK finite={len(finite)}/128 byte-valued={len(valid)}/128 "
                  f"head16={floats[:16]} tail16={tail[-16:]}", flush=True)
            if len(valid) != 128:
                raise RuntimeError("DDR readback does not match packed u8-to-FP32 data")

            # No NPU fence is currently proven. Do not send COMMIT; ABORT only
            # releases the FPGA task after collecting its pre-commit write data.
            write_u32(csr, 0x08, 4)
            task_finished = True
            end = time.monotonic() + 2.0
            while time.monotonic() < end:
                status = read_u32(csr, 4)
                if (status & DONE) and not (status & BUSY):
                    break
                time.sleep(0.002)
            print(f"AFTER_ABORT STATUS=0x{status:08x}", flush=True)
        finally:
            if task_started and not task_finished:
                try:
                    status = read_u32(csr, 4)
                    if status & (BUSY | ACCEPTED):
                        write_u32(csr, 0x08, 4)
                        deadline = time.monotonic() + 2.0
                        while time.monotonic() < deadline:
                            status = read_u32(csr, 4)
                            if (status & DONE) and not (status & BUSY):
                                break
                            time.sleep(0.002)
                        print(f"CLEANUP_ABORT STATUS=0x{status:08x}", flush=True)
                except Exception as cleanup_error:
                    print(f"CLEANUP_ABORT_FAILED: {cleanup_error}", file=sys.stderr, flush=True)
            test_region.release()
            dma.close()
            os.close(dma_fd)
    finally:
        csr.close()
        os.close(csr_fd)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
