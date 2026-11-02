# Dataset status

Only facts that were checked are marked VERIFIED. Everything else is UNVERIFIED until checked.

## Own footage (primary)
| Source | What we know | Status |
|---|---|---|
| Drive folder `1XI0dgKO0lEZgo3cper8oYHEZyh_ewgHu` | User says: panoramic football match, 2 halves | Not readable from the connected Drive account (2026-11-02). Contents UNVERIFIED |
| Drive folder `19JQ9Uq1doemU8PlGDs5oPlNTpi6L0xnS` | same | same |
| Drive folder `1LAteNdWaa1eJQ3vOVR8BziN2LoIsQlHc` | same | same |
| Drive folder "videos" `1Wy1LgQPm9hW4aOQLw2Z3newzuzlKXcNv` | User says: fixed wide match shots | same |

Clip properties (from `md_probe`, ffprobe, and looking at frames):

| Clip | Resolution | FPS | Codec / pix fmt | Duration | Ball visible at native res? | Annotations | Source / consent / licence |
|---|---|---|---|---|---|---|---|
| `117092_panorama_1st_half.mp4` (local `assets/`, git-ignored, 6.4 GB) | **3840×1906** (VERIFIED, md_probe) | **25** | H.264 Constrained Baseline, yuv420p, BT.601 (bt470bg) limited range; keyframe every 2 s, no B-frames; AAC 44.1 kHz stereo | **2695 s** (44:55, first half) | Players ~20–60 px tall at native res; ball only a few px, often not visible at night | None | Source / consent / licence: **unknown**. The user asked (2026-11-02) for 3 wide stills to be published in the README; no video published |

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
