# Car rear overlay: guest stencil versus native output

The same-run RenderDoc capture in
`.local/ray-minimap-readback-20260927/capture-inapp_capture.rdc` shows that
the dark rear-car overlay is a stencil discrepancy, not a mask-texture or
blend discrepancy. Guest draw 18315 and native draw 41245 use the same
64×64 mask (SHA-256 `93ba4fe7c6e9a91d685745d712d7629e9c477750a154579675504f51f60d6404`).
Both shaders output black with alpha near 0.619 at sampled car pixels, and
both use source-alpha blending. At five rear-car points, the guest rejects
the overlay with stencil Equal, reference 128, compare mask 255. The native
scene target is D32_FLOAT without stencil, so it applies the overlay and
darkens the car.

A bounded live approximation omits this one-texture car overlay when its
captured depth control enables stencil. In a normal-exit installed-save race
route, the native-versus-guest rear-car RGB MAE over x=500–780, y=400–575
fell from 31.56/31.25/31.25/32.17 to 25.95/25.71/25.72/26.54 across
outputs 5200–5203. The whole-frame MAE also fell on three of four frames.
Visual inspection confirms the rear paint is legible; taillights and body
lighting still differ. The ordered frame and UI captures passed their
existing verifiers. The native/compatibility/native hot-toggle route also
exited normally with its captured checkpoints.

This is an approximation, not stencil ownership. The eventual ordered replay
needs a stencil-capable native target plus captured stencil reference, masks,
operations and producer order. Before adding that, check whether the missing
detail affects driving in a longer route; do not let exact stencil parity
delay the first usable race.
