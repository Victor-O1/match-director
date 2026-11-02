# Dataset status

Only facts that were checked are marked VERIFIED. Everything else is UNVERIFIED until checked.

## Own footage (primary)
| Source | What we know | Status |
|---|---|---|
| Drive folder `1XI0dgKO0lEZgo3cper8oYHEZyh_ewgHu` | User says: panoramic football match, 2 halves | Not readable from the connected Drive account (2026-11-02). Contents UNVERIFIED |
| Drive folder `19JQ9Uq1doemU8PlGDs5oPlNTpi6L0xnS` | same | same |
| Drive folder `1LAteNdWaa1eJQ3vOVR8BziN2LoIsQlHc` | same | same |
| Drive folder "videos" `1Wy1LgQPm9hW4aOQLw2Z3newzuzlKXcNv` | User says: fixed wide match shots | same |

**To fill in once one clip is downloaded to `data/` (git-ignored):** run `md_probe data/<file>` and record:

| Clip | Resolution | FPS | Codec / pix fmt | Duration | Ball visible at native res? | Annotations | Source / consent / licence |
|---|---|---|---|---|---|---|---|
| unknown — to be measured | | | | | | | |

Questions to answer for every clip: who recorded it, whether the players (especially minors) consented to filming, and whether it may appear in a public demo video.

## Public datasets
| Dataset | Use | Status |
|---|---|---|
| SoccerTrack v2 (arXiv 2508.01802) | Ground-truth player positions + ball events for evaluating the director | Paper read earlier. Repo/HF links UNVERIFIED. No data licence stated, so research/portfolio use only |
| SoccerTrack v1 | Multi-view (fisheye + drone) | UNVERIFIED download |
| SoccerNet | How human directors cut | UNVERIFIED access terms (registration) |

## Rules
- No video or model files in git (`.gitignore`).
- No downloaded YouTube/broadcast matches in anything public.
