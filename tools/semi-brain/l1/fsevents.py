#!/usr/bin/env python3
"""
fsevents.py
Minimal macOS FSEvents binding over ctypes (no pyobjc/watchdog on this machine's python).

    watch(paths, callback, latency=0.5)   blocks; callback(list_of_changed_paths) on the run loop
"""

import ctypes
import ctypes.util

_cf = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreFoundation"))
_cs = ctypes.cdll.LoadLibrary(ctypes.util.find_library("CoreServices"))

kCFStringEncodingUTF8 = 0x08000100
kFSEventStreamEventIdSinceNow = 0xFFFFFFFFFFFFFFFF
kFSEventStreamCreateFlagNoDefer = 0x02
kFSEventStreamCreateFlagFileEvents = 0x10

_cf.CFStringCreateWithCString.restype = ctypes.c_void_p
_cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
_cf.CFArrayCreate.restype = ctypes.c_void_p
_cf.CFArrayCreate.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.c_long, ctypes.c_void_p]
_cf.CFRunLoopGetCurrent.restype = ctypes.c_void_p
_cf.CFRunLoopRun.restype = None
_cf.CFRunLoopRunInMode.restype = ctypes.c_int32
_cf.CFRunLoopRunInMode.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_bool]
kCFRunLoopDefaultMode = ctypes.c_void_p.in_dll(_cf, "kCFRunLoopDefaultMode")

CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                            ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_uint32),
                            ctypes.POINTER(ctypes.c_uint64))

_cs.FSEventStreamCreate.restype = ctypes.c_void_p
_cs.FSEventStreamCreate.argtypes = [ctypes.c_void_p, CALLBACK, ctypes.c_void_p, ctypes.c_void_p,
                                    ctypes.c_uint64, ctypes.c_double, ctypes.c_uint32]
_cs.FSEventStreamScheduleWithRunLoop.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
_cs.FSEventStreamStart.restype = ctypes.c_bool
_cs.FSEventStreamStart.argtypes = [ctypes.c_void_p]


def _cfstr(s):
    return _cf.CFStringCreateWithCString(None, s.encode("utf-8"), kCFStringEncodingUTF8)


def watch(paths, callback, latency=0.5, tick=None, tick_every=0.5):
    """Run forever. callback(paths) gets batches of changed file paths (FSEvents coalesces
    within `latency` seconds). tick(), if given, runs every `tick_every` s between events."""

    def _cb(stream, info, n, event_paths, flags, ids):
        callback([event_paths[i].decode("utf-8", "replace") for i in range(n)])

    cb = CALLBACK(_cb)  # keep a reference for the stream's lifetime
    arr = (ctypes.c_void_p * len(paths))(*[_cfstr(p) for p in paths])
    cfpaths = _cf.CFArrayCreate(None, arr, len(paths), None)
    stream = _cs.FSEventStreamCreate(None, cb, None, cfpaths, kFSEventStreamEventIdSinceNow, latency,
                                     kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer)
    _cs.FSEventStreamScheduleWithRunLoop(stream, _cf.CFRunLoopGetCurrent(), kCFRunLoopDefaultMode)
    if not _cs.FSEventStreamStart(stream):
        raise RuntimeError("FSEventStreamStart failed")
    while True:
        _cf.CFRunLoopRunInMode(kCFRunLoopDefaultMode, tick_every, False)
        if tick:
            tick()
