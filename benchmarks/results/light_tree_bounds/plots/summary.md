| scene | method | spp | time (s) | Mrays/s | relMSE | relMSE / MIS | HDR-FLIP |
|---|---|---:|---:|---:|---:|---:|---:|
| gaussian_lights | MIS | 1238 | 30.1 | 7.3 | 1.041e-03 | 1.00 | 0.0188 |
| gaussian_lights | Light sampling (NEE) | 1228 | 30.0 | 7.2 | 3.246e-03 | 3.12 | 0.0218 |
| gaussian_lights | MIS, tree (distance) | 1083 | 30.1 | 6.4 | 5.874e-04 | 0.56 | 0.0157 |
| gaussian_lights | MIS, tree (emitter) | 1040 | 30.0 | 6.1 | 6.178e-04 | 0.59 | 0.0160 |
| gaussian_lights | MIS, tree (all bounds) | 982 | 30.0 | 5.9 | 5.704e-04 | 0.55 | 0.0157 |
| gaussian_lights | Light sampling, tree (distance) | 1116 | 30.2 | 6.5 | 1.239e-03 | 1.19 | 0.0169 |
| gaussian_lights | Light sampling, tree (emitter) | 1046 | 30.1 | 6.1 | 1.370e-03 | 1.32 | 0.0174 |
| gaussian_lights | Light sampling, tree (all bounds) | 1012 | 30.1 | 6.0 | 1.371e-03 | 1.32 | 0.0168 |
| gaussian_spots | MIS | 666 | 30.1 | 8.7 | 3.472e-03 | 1.00 | 0.0358 |
| gaussian_spots | Light sampling (NEE) | 670 | 30.1 | 8.8 | 1.621e-02 | 4.67 | 0.0477 |
| gaussian_spots | MIS, tree (distance) | 610 | 30.0 | 7.9 | 3.138e-03 | 0.90 | 0.0343 |
| gaussian_spots | MIS, tree (emitter) | 546 | 30.0 | 7.1 | 3.042e-03 | 0.88 | 0.0336 |
| gaussian_spots | MIS, tree (all bounds) | 510 | 30.2 | 6.7 | 2.818e-03 | 0.81 | 0.0325 |
| gaussian_spots | Light sampling, tree (distance) | 621 | 30.1 | 8.1 | 2.063e-02 | 5.94 | 0.0421 |
| gaussian_spots | Light sampling, tree (emitter) | 548 | 30.1 | 7.1 | 1.589e-02 | 4.58 | 0.0428 |
| gaussian_spots | Light sampling, tree (all bounds) | 511 | 30.2 | 6.7 | 1.582e-02 | 4.56 | 0.0410 |
| gaussian_cornell_sphere | MIS | 260 | 30.2 | 0.1 | 2.033e-04 | 1.00 | 0.0101 |
| gaussian_cornell_sphere | Light sampling (NEE) | 266 | 30.1 | 0.2 | 2.716e-03 | 13.36 | 0.0268 |
| gaussian_cornell_sphere | MIS, tree (distance) | 259 | 30.3 | 0.1 | 2.027e-04 | 1.00 | 0.0101 |
| gaussian_cornell_sphere | MIS, tree (emitter) | 250 | 30.3 | 0.1 | 2.090e-04 | 1.03 | 0.0102 |
| gaussian_cornell_sphere | MIS, tree (all bounds) | 254 | 30.4 | 0.1 | 1.974e-04 | 0.97 | 0.0100 |
| gaussian_cornell_sphere | Light sampling, tree (distance) | 258 | 30.1 | 0.1 | 1.500e-02 | 73.78 | 0.0263 |
| gaussian_cornell_sphere | Light sampling, tree (emitter) | 253 | 30.2 | 0.1 | 1.570e-02 | 77.20 | 0.0262 |
| gaussian_cornell_sphere | Light sampling, tree (all bounds) | 254 | 30.3 | 0.1 | 2.172e-03 | 10.68 | 0.0255 |

relMSE is corrected for reference noise; relMSE / MIS < 1 means lower error than MIS at equal render time. Values are means over seeds.
