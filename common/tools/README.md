# Shared tooling

`recomp/` is the ARM-to-C++ recompiler: the image reader, the instruction decoder, the
control-flow walk, the C++ writer, and the function table that ties them together. A title's own
`tools/` drives it — `funcs.py` seeds the function table, `emit.py` runs the generator — and adds
`recomps/common/tools` to `sys.path` to import it.
