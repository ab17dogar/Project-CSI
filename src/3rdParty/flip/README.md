# FLIP (vendored)

NVIDIA's reference implementation of the FLIP image-difference metric
(Andersson et al., "FLIP: A Difference Evaluator for Alternating Images",
HPG 2020; HDR-FLIP: Andersson et al., 2021).

- Source: https://github.com/NVlabs/flip, file `src/cpp/FLIP.h`
- Commit: d3ab02a4425eb167d8a4021b382a2bafc6e6c838
- License: BSD 3-Clause (see `LICENSE`), unmodified.

Used only by `src/eval/metrics.cpp`; that one translation unit includes it
because the header defines global `Min`/`Max` macros.
