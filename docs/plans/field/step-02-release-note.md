# Field Step 2 Release Note

## Release Bullet
* `rand()`, `noise()`, and `sh()` expression functions are now pure seeded integer hash functions — saved patches using them will look different and produce different random sequences.

## What a User Will Notice
* Saved patches containing `=rand(...)`, `=noise(...)`, or `=sh(...)` expressions will render different random sequences upon load, with eliminated autocorrelation and full reproducibility across platforms.
* Added optional 4th `seed` argument: `rand(min, max, speed, seed)`, `noise(min, max, speed, seed)`, and `sh(min, max, speed, seed)` (defaulting to seed 0 if omitted).
* Stepped random generator `sh` and continuous random generator `rand` now maintain a bit-exact 300-second cycle period.
