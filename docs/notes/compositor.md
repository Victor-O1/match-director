# GPU compositor: how it works, why, what can go wrong

Code: `src/gfx/*.cpp`. This is the answer to "how do you render text and graphics on a GPU, fast?"

## How it works (OpenGL ES 3.0, headless via EGL surfaceless)
1. **Upload only what you need.** The Y, U and V planes go into three R8 textures that are allocated once at the full 3840×1906. Each frame, `glTexSubImage2D` updates only the crop window plus a 4 px margin. `GL_UNPACK_ROW_LENGTH`/`SKIP_PIXELS`/`SKIP_ROWS` let GL read the sub-rectangle straight out of the decoder's padded buffer, with no CPU copy. **Measured: 4.5 ms vs 7.9 ms compositor p50.**
2. **Video pass:** a full-screen quad maps the crop's texture coordinates; the fragment shader converts BT.601 limited-range YUV to RGB. When the crop is minified more than 1.2×, it takes 4 bilinear taps across the output pixel's footprint (cheap anti-aliasing for grass and lines).
3. **Graphics pass:** FreeType renders printable ASCII at 3 sizes into **one** 8-bit atlas at start-up. `OverlayBatch` turns the whole layout (scoreboard, clock, lower third, REPLAY bug) into one vertex array, so it's **one buffer upload and one draw call** per frame. Text and solid boxes share a shader (a per-vertex `mode` switches between them), so there's no state change between them. The lower third slides in with an ease-out curve.
4. **RGB→NV12 on the GPU:** two packing passes render into RGBA8 targets where each texel holds 4 luma bytes, or 2 interleaved UV pairs (each a 2×2 average). The readback is already in the encoder's layout at 1.5 bytes/pixel instead of 4, with no CPU colour conversion.
5. **Readback:** either directly into the encoder frame (`GL_PACK_ROW_LENGTH` matches its stride), or via two pixel-buffer objects (map the previous frame while the GPU works on the current one). **Measured: no gain from PBOs on Mesa D3D12 under WSL**, so both paths are kept and documented.

## Precision detail
All shaders use `highp`. On many GPUs `mediump` is fp16, whose 10-bit mantissa can't address individual texels in a 3840-wide texture, so the picture would wobble.

## What can go wrong
- **Driver lifetime:** `eglTerminate` unloaded the D3D12 driver while it still had a thread-local destructor registered, which segfaulted at thread exit (see LESSONS.md). Fixed by not terminating the display.
- On a discrete NVIDIA card, the decoder's frames could stay on the GPU (NVDEC → CUDA/GL interop) and skip the upload entirely. Here the integrated GPU shares RAM, so the upload is a memcpy into driver memory.
- Text is ASCII only; HarfBuzz shaping would be needed for other scripts.
