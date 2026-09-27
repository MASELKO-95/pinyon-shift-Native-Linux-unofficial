# Sky pixel lineage check

The later [marked moving-race capture](RAYMAN_SKY_WRITER_2026-09-27.md)
identifies the sky writer. The inconclusive result below is retained as the
history of the earlier captures.

The paired live race frames after the two ground-material fixes still show a
flat native sky where the compatibility frame has clouds and lighting. The
next work must identify a writer of those pixels before adding a sky shader
or reusing an earlier color resolve.

In the existing guest RenderDoc capture
`.local/native-renderer/snr04/renderdoc-gatea-full-b_frame5001.rdc`, an
upper-frame pixel at `(650,50)` reaches the swapchain through draw 22104
(texture `ResourceId::1630`), compute 22079 (reads texture 1615), copy 22071
(buffer 1600 to texture 1615), and compute 22069 (reads shared buffer 317 and
writes buffer 1600). The final texture's pixel history therefore cannot
identify which earlier scene draw painted the pixel. It ends at a guest
memory buffer, not a retained scene-color texture.

The older race-start capture at source frame 4709 identifies a 4×MSAA scene
color target (`ResourceId::2487`), but pixel history at three upper-frame
locations includes repeated clears and writes because that resource is reused
across passes. Those events cannot be assigned to the paired 5040–5044 race
view without a frame/pass anchor. This experiment has not established that
the missing sky is an unsupported draw, a material approximation, or a
target-version error.

Next: capture one paired race frame with a RenderDoc marker on the main
scene's first clear and terminal color resolve. Export that scene target
immediately before resolve, verify its image against the same-run guest
reference, then query pixel history at a visually confirmed sky location.
Join the resulting writer event to the ordered draw/support manifest. Admit
only that writer or its required input, with whole-frame fallback, and retain
the change only if several moving final frames gain visible sky detail.

The two available captures did not establish that link. Under the backlog's
bounded-experiment rule, defer another sky implementation trial until a drive
shows that this gap blocks playability. The next pass should assess driving,
navigation and car readability on the current textured-ground build.
