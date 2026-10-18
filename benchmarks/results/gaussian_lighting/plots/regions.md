| scene | region | pixels | method | relMSE | relMSE / MIS |
|---|---|---:|---|---:|---:|
| gaussian_cornell_sphere | direct | 15840 | mis | 3.776e-05 | 1.00 |
| gaussian_cornell_sphere | direct | 15840 | bsdf | 2.982e-05 | 0.79 |
| gaussian_cornell_sphere | direct | 15840 | nee | 4.336e-05 | 1.15 |
| gaussian_cornell_sphere | direct | 15840 | mis_tree | 4.046e-05 | 1.07 |
| gaussian_cornell_sphere | direct | 15840 | nee_tree | 3.000e-05 | 0.79 |
| gaussian_cornell_sphere | surfaces | 544 | mis | 4.674e-03 | 1.00 |
| gaussian_cornell_sphere | surfaces | 544 | bsdf | 7.406e-03 | 1.58 |
| gaussian_cornell_sphere | surfaces | 544 | nee | 7.538e-02 | 16.13 |
| gaussian_cornell_sphere | surfaces | 544 | mis_tree | 5.017e-03 | 1.07 |
| gaussian_cornell_sphere | surfaces | 544 | nee_tree | 4.588e-01 | 98.15 |
| gaussian_lights | direct | 29901 | mis | 2.936e-05 | 1.00 |
| gaussian_lights | direct | 29901 | bsdf | 2.486e-05 | 0.85 |
| gaussian_lights | direct | 29901 | nee | 4.420e-05 | 1.51 |
| gaussian_lights | direct | 29901 | mis_tree | 2.848e-05 | 0.97 |
| gaussian_lights | direct | 29901 | nee_tree | 4.535e-05 | 1.54 |
| gaussian_lights | surfaces | 46899 | mis | 1.689e-03 | 1.00 |
| gaussian_lights | surfaces | 46899 | bsdf | 1.028e-02 | 6.08 |
| gaussian_lights | surfaces | 46899 | nee | 5.195e-03 | 3.08 |
| gaussian_lights | surfaces | 46899 | mis_tree | 9.094e-04 | 0.54 |
| gaussian_lights | surfaces | 46899 | nee_tree | 1.967e-03 | 1.16 |

relMSE corrected for reference noise, mean over seeds; per channel as in the metrics library.
