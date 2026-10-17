| scene | method | spp | time (s) | Mrays/s | relMSE | relMSE / MIS | HDR-FLIP |
|---|---|---:|---:|---:|---:|---:|---:|
| cornell_box | MIS | 1682 | 30.4 | 17.0 | 1.310e-04 | 1.00 | 0.0289 |
| cornell_box | BSDF sampling | 2370 | 30.0 | 15.2 | 9.331e-03 | 71.23 | 0.1444 |
| cornell_box | Light sampling (NEE) | 1692 | 30.2 | 17.2 | 1.302e-04 | 0.99 | 0.0289 |
| cornell_glossy | MIS | 1464 | 30.1 | 16.3 | 2.108e-03 | 1.00 | 0.0653 |
| cornell_glossy | BSDF sampling | 2077 | 30.0 | 14.6 | 1.230e-02 | 5.83 | 0.1414 |
| cornell_glossy | Light sampling (NEE) | 1454 | 30.3 | 16.1 | 7.920e-03 | 3.76 | 0.0851 |
| veach_mis | MIS | 1278 | 30.0 | 16.2 | 1.352e-04 | 1.00 | 0.0150 |
| veach_mis | BSDF sampling | 2348 | 30.1 | 18.3 | 5.957e-02 | 440.53 | 0.3219 |
| veach_mis | Light sampling (NEE) | 1364 | 30.1 | 17.2 | 5.307e-01 | 3924.17 | 0.0307 |
| glass_caustic | MIS | 2020 | 30.0 | 18.8 | 2.409e-02 | 1.00 | 0.0641 |
| glass_caustic | BSDF sampling | 3174 | 30.2 | 19.5 | 3.144e-01 | 13.05 | 0.2928 |
| glass_caustic | Light sampling (NEE) | 2035 | 30.2 | 18.9 | 2.381e-02 | 0.99 | 0.0640 |
| env_objects | MIS | 2191 | 30.0 | 13.6 | 3.055e-02 | 1.00 | 0.0175 |
| env_objects | BSDF sampling | 4018 | 30.0 | 18.7 | 2.737e-01 | 8.96 | 0.2063 |
| env_objects | Light sampling (NEE) | 2316 | 30.1 | 14.3 | 3.176e-02 | 1.04 | 0.0209 |
| sun_sky_bunny | MIS | 1451 | 30.2 | 10.1 | 4.549e-05 | 1.00 | 0.0042 |
| sun_sky_bunny | BSDF sampling | 1638 | 30.0 | 10.8 | 4.490e-05 | 0.99 | 0.0042 |
| sun_sky_bunny | Light sampling (NEE) | 1457 | 30.0 | 10.2 | 1.848e-03 | 40.62 | 0.0156 |

relMSE is corrected for reference noise; relMSE / MIS < 1 means lower error than MIS at equal render time. Values are means over seeds.
