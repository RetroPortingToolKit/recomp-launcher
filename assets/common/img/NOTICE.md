# Images

recomp-launcher ships no image files. Its look is drawn at runtime from the
skin definitions in assets/skins/ (procedural shapes, gradients and text),
written for this project by its authors and covered by recomp-launcher's MIT
license (licenses/recomp-ui-LICENSE in a release).

The R4 skin's menu sounds (assets/skins/r4/sfx/*.wav) are synthesised by
tools/gen_sfx.py in the recomp-launcher repository, also MIT.

Images a skin can reference but which are never shipped:
- `$disc/...`: art extracted from the player's own disc at runtime into
  disc-assets/ next to the executable.
- `$exe/assets/skins/<skin>/user/...`: files the player adds.
