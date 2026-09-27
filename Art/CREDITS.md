# Third-party art — sources and licences

Everything here must be free for commercial use with no purchase, per the project's
zero-production-cost rule. Asset IDs are recorded so any surface can be traced back
to its source and re-downloaded.

## Textures — Poly Haven (CC0 / public domain)

https://polyhaven.com — CC0: no attribution required, commercial use allowed.
Attribution is kept here anyway, as a courtesy and so provenance stays auditable.

| Asset ID | Used for |
|---|---|
| `decrepit_wallpaper` | wall paper over the plaster |
| `old_wooden_floor_02` | floorboards |

Maps downloaded per set: `Diffuse`, `nor_dx` (DirectX normal), `arm` (AO/Roughness/Metallic packed).
2K JPG. Re-download with `Tools/fetch_textures.sh`, re-import with `Tools/build_surface_material.py`.

## Stair hall — Poly Haven (CC0 / public domain)

Fetched by `Tools/fetch_assets.py`, imported by `Tools/build_art.py` (`-ArtStage=surfaces`,
`-ArtStage=models -ArtOnly=<ids>`).

| Asset ID | Kind | Used for |
|---|---|---|
| `checkered_pavement_tiles` | texture | the entrance hall floor |
| `dark_paneled_wood` | texture | the wainscot and the panelling under the stairs |
| `marble_01` | texture | the pedestal, the front-door threshold |
| `Chandelier_03` | model | the dead chandelier over the stairwell |
| `marble_bust_01` | model | the bust at the foot of the stairs |
| `gothic_statue` | model | the statue in the alcove |
| `vintage_grandfather_clock_01` | model | the long-case clock, face down |
| `vintage_suitcase` | model | the cases by the front door |
| `antique_ceramic_vase_01` | model | vases on the console and the landing |
| `brass_vase_01` | model | vases on the gallery and the landing |
| `brass_candleholders` | model | the candelabrum on the console |
| `side_table_tall_01` | model | side tables on the gallery and the landing |
| `ClassicConsole_01` | model | the console table in the hall |
| `fancy_picture_frame_02` | model | the family portrait on the landing |

## Generated in-repo

| File | Made by | Used for |
|---|---|---|
| `Art/Source/Generated/stained_glass_*.png` | `Tools/make_stained_glass.py` | the stair window's glass and lead |
| `Art/Source/Generated/glass_crack_*.png` | `Tools/make_glass_crack.py` | the starred pane in the bedroom window |

The stained-glass inscription is set in Cinzel (SIL Open Font License, `Content/Fonts/OFL.txt`).
