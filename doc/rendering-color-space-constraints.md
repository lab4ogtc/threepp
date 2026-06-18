# Rendering Color Space Constraints

This document records renderer color-space constraints that are intentional and
should not be treated as pixel-parity bugs.

## ImGui Overlay Blending

- The ImGui overlay is appended directly to the final present target. It is not
  rendered to a separate overlay texture or OS layer before compositing.
- The OpenGL backend currently follows the legacy GL path: the scene is already
  written as sRGB/display-encoded bytes, and ImGui source colors are blended in
  that sRGB/gamma-encoded framebuffer space.
- The Metal backend prioritizes color-correct output: for sRGB/Gamma output it
  uses an sRGB drawable, converts ImGui-authored sRGB vertex colors to linear,
  and lets the hardware perform linear-space blending plus final sRGB encoding.
- Therefore ImGui overlay opacity and dark backgrounds are not expected to be
  visually identical between GL and Metal. A darker or more solid-looking Metal
  ImGui background can be the result of linear blending rather than a layering
  or compositing bug.
- Do not force Metal to match GL's sRGB-space overlay blending unless the task is
  explicitly about legacy GL visual parity. Prefer moving other hardware-backed
  renderers toward the Metal color-correct model when practical.

## ImGui Metal Implementation Note

- The current Metal ImGui path keeps the upstream ImGui Metal backend unchanged.
  It performs a scoped, CPU-side conversion of ImGui vertex colors from sRGB to
  linear only when the current Metal framebuffer is an sRGB pixel format.
- This is intentionally low-intrusion: it avoids maintaining a forked ImGui
  backend shader while preserving correct color input for hardware sRGB blending.
- Optimization point: replace the scoped CPU-side draw-data rewrite with a Metal
  ImGui shader or pipeline variant that converts vertex colors from sRGB to
  linear on the GPU before fragment output. That would keep the same color
  semantics, avoid per-frame CPU traversal/mutation of ImDrawData, and reduce
  coupling to ImGui's vertex buffer layout.
