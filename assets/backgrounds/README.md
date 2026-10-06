# Environment maps

## `space00_2k.hdr`

Equirectangular HDR environment, 2048x1024, Radiance RGBE. Used as the demo's
skybox and, from IBL sub-slice 4c, as the source for the engine's image-based
lighting.

**Source:** <https://www.spacespheremaps.com/> — supplied by the project owner,
who identified the site as its origin. The original file's header records
`SOFTWARE=CINEMA_4D`, `GAMMA=1`, `EXPOSURE=1`.

**Licence: Creative Commons Attribution 4.0 International (CC BY 4.0).**
Checked against the site's licensing terms on 2026-10-05
(<https://www.spacespheremaps.com/about/#licensing>), which state the images may
be copied and redistributed in any medium or format for any purpose, including
commercially, and remixed, transformed and built upon on the same terms.
Attribution is stated to be appreciated but not required; it is given here
anyway. The terms also state the images may not be used for AI image scraping,
though they may be modified using AI tools — redistributing a downsampled copy
in this repository falls under the Share and Adapt permissions above, not under
scraping.

Credit: space sphere map from spacespheremaps.com, CC BY 4.0, downsampled for
this repository.

A caveat on the record: the licence above was read from the site on the date
given, and the site does not ship a per-file licence alongside the download, so
this records the site-wide terms rather than a licence document specific to
`space00.hdr`. The URL is included so the terms can be re-checked.

**How this file was produced.** The original is 8192x4096 and 67.4 MB, which
stores as roughly 43 MB in git and costs about 536 MB of transient memory to
decode at full resolution. It is kept locally and git-ignored
(`assets/backgrounds/*.hdr`, with `*_2k.hdr` negated); this committed copy is a
box-filtered downsample to 2048x1024, which is 8 MB on disk and stores at about that in git (flat RGBE, so it does not compress further).

2048x1024 is not an arbitrary choice: 90 degrees of a 2048-wide equirect is
exactly 512 pixels, a 1:1 match for the 512-pixel cubemap faces the importer
produces. A larger source would be discarded by the conversion.

**What downsampling cost.** Box-averaging spreads isolated bright pixels across
their neighbours, so the peak radiance fell from 23.4 to 3.5. Total energy is
preserved (mean radiance 0.0017 at both sizes), which is what the diffuse IBL
integral depends on; what is lost is the intensity of individual star
highlights in specular reflections.

**What this environment is, and what it implies.** It is space: mean radiance
0.0017 (about 0.2% grey), with only 187 of 100 million samples above 1.0 in the
original. It makes an excellent skybox. It will produce almost no ambient light
once IBL lands, so a metal surface lit by it stays near-black -- which is
physically correct, a chrome ball in deep space being black with pinpoint star
reflections, but means this environment does not demonstrate that IBL is
working. A bright outdoor or studio environment is the right choice for
verifying 4c.
