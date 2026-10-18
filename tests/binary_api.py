"""Check ABI exports on the target architecture before Kodi loads the module."""
import ctypes
import pathlib
import sys

library = ctypes.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
for name in ("ADDON_GetTypeVersion", "ADDON_GetTypeMinVersion"):
    function = getattr(library, name)
    function.argtypes = [ctypes.c_int]
    function.restype = ctypes.c_char_p
    assert function(109) == b"4.0.0", f"{name}: expected visualization API 4.0.0"
print("PASS: exported visualization API 4.0.0")
