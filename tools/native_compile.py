"""Host target flags for the pinned Zig compiler; never used for MCU/WASM."""
import platform
import sys


def native_target_flags():
    # Zig 0.14.1's bundled Darwin stubs support this deployment target.
    # Automatically detecting newer macOS releases leaves libc unresolved.
    if sys.platform == 'darwin':
        arch = {'arm64': 'aarch64', 'x86_64': 'x86_64'}[platform.machine()]
        return ['-target', f'{arch}-macos.13.0']
    return []
