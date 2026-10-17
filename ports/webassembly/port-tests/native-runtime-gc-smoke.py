"""Run inside rebuilt native WASM, where gc.collect() must reclaim mid-CLI."""

import gc


def verify_gc_works_during_single_cli():
    gc.collect()
    beginning = gc.mem_alloc()

    for i in range(2048):
        block = bytes([i & 0xFF]) * 4096
        if i % 128 == 127:
            gc.collect()

    del block
    gc.collect()
    ending = gc.mem_alloc()
    additional = ending - beginning
    # Allow conservative roots and interpreter bookkeeping.
    assert additional < 2 * 1024 * 1024, (beginning, ending)
    print("NATIVE_CLI_GC_OK", beginning, ending)


verify_gc_works_during_single_cli()
