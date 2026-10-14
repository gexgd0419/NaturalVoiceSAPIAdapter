Pinned reference: rany2/edge-tts tag `7.3.0`.

- https://github.com/rany2/edge-tts/blob/7.3.0/src/edge_tts/drm.py
- https://github.com/rany2/edge-tts/blob/7.3.0/src/edge_tts/constants.py
- https://github.com/rany2/edge-tts/blob/7.3.0/LICENSE

`EdgeGec.h` and `EdgeGecTables.h` port the v2 digest from `drm.py`.
These portions retain edge-tts's LGPLv3 license; see `LICENSE.edge-tts`.
The repository's original MIT license is unchanged for its original code.

`test_gec.py` runs the pinned Python digest without installing edge-tts or
requiring aiohttp. It compares the compiled production algorithm with this
independent reference, including exact FILETIME window boundaries.
`port_gec.py` regenerates only the constant table; run clang-format afterward.
