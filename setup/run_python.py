"""Launch an ordinary script with its directory importable in embedded CPython."""
import os
import sys

if __name__ == "__main__":
    script = os.path.abspath(sys.argv[1])
    sys.argv = sys.argv[1:]
    sys.argv[0] = script
    sys.path.insert(0, os.path.dirname(script))
    # Avoid runpy.run_path's package/zip discovery on every ordinary .py run.
    # A fresh __main__ module preserves pickling and multiprocessing spawn.
    from importlib.machinery import SourceFileLoader
    main = type(sys)("__main__")
    main.__file__ = script
    main.__package__ = None
    main.__spec__ = None
    main.__cached__ = None
    main.__loader__ = SourceFileLoader("__main__", script)
    sys.modules["__main__"] = main
    with open(script, "rb") as source:
        code = compile(source.read(), script, "exec", dont_inherit=True)
    exec(code, main.__dict__)
