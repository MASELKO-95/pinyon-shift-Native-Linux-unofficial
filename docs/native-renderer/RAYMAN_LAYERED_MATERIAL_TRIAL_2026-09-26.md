# Layered-scene material trial

The selected source-5000 ordered frame had about 270 draws of
`3BC346726C1C2535/9584B309533EF6C9`, the largest flat-colored procedural
family. Its installed pixel program uses one packed float4, one texture
fetch and a single descriptor triplet. A live trial captured the final
texture version, pinned fetch 0, and used that original pixel program for
269 of the family's draws. The RelWithDebInfo installed-save route exited
normally; source frame 5000 promoted with 1,600 supported and 1,518
unsupported ordered draws.

The saved native frame still had the same conspicuous flat ground and
untextured background structure. This family is not the cause of those
visible gaps. The trial was removed, including its extra capture and
snapshot work, and the preview was rebuilt from the restored source.

The next useful step is to map the missing pixels to target-producing draws
and copies in the ordered stream. Adding more original pixel programs to
already visible geometry is unlikely to restore the absent intermediate
background and lighting work.
