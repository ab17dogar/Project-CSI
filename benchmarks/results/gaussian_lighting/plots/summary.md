| scene | method | spp | time (s) | Mrays/s | relMSE | relMSE / MIS | HDR-FLIP |
|---|---|---:|---:|---:|---:|---:|---:|
| gaussian_lights | MIS | 1238 | 30.0 | 7.3 | 1.043e-03 | 1.00 | 0.0188 |
| gaussian_lights | BSDF sampling | 1818 | 30.0 | 7.8 | 6.286e-03 | 6.03 | 0.0362 |
| gaussian_lights | Light sampling (NEE) | 1262 | 30.1 | 7.4 | 3.190e-03 | 3.06 | 0.0216 |
| gaussian_lights | MIS, light tree | 1121 | 30.1 | 6.6 | 5.664e-04 | 0.54 | 0.0155 |
| gaussian_lights | Light sampling, light tree | 1125 | 30.0 | 6.6 | 1.219e-03 | 1.17 | 0.0169 |
| gaussian_cornell_sphere | MIS | 270 | 30.1 | 0.2 | 1.917e-04 | 1.00 | 0.0099 |
| gaussian_cornell_sphere | BSDF sampling | 274 | 30.1 | 0.2 | 2.747e-04 | 1.43 | 0.0109 |
| gaussian_cornell_sphere | Light sampling (NEE) | 274 | 30.0 | 0.2 | 2.545e-03 | 13.27 | 0.0266 |
| gaussian_cornell_sphere | MIS, light tree | 252 | 30.0 | 0.1 | 2.057e-04 | 1.07 | 0.0102 |
| gaussian_cornell_sphere | Light sampling, light tree | 254 | 30.0 | 0.1 | 1.526e-02 | 79.61 | 0.0263 |

relMSE is corrected for reference noise; relMSE / MIS < 1 means lower error than MIS at equal render time. Values are means over seeds.
