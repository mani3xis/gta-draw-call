GTA: One draw call
===================

## GTA: One dra... what?
The main goal of this project is to render the cities from GTA games in only 1 draw call.
Such renderer must be interactive and render the whole city **each** frame.


## But why?
![Why? Why? Why? Why?](http://www.reactiongifs.com/r/lnwh.gif)

Initially this project served as testbed for learning AZDO OpenGL techniques.
Since some people are still struggling to render ~100 meshes at 30 FPS, and
because I haven't seen a project like this, I decided to challenge myself...
You can also use this renderer to find some easter eggs - I did :)


## How on earth?
Simply put: clever data reordering and OpenGL 4.3 Core with bindless textures!
The biggest problem was to figure out how to access all the textures at once.
Since I'm still using a previous-gen GPU that does not support *bindless textures*
nor `gl_DrawID` I tried evaluating various texture preprocessing tricks until I
stumbled upon the maximum number of textures inside `GL_TEXTURE_2D_ARRAY`... so
I ended up using `GL_ARB_bindless_texture` and `GL_ARB_shader_draw_parameters`
extensions and testing it at my friend's workstation.

The overall algorithm is pretty simple:
  1. Group all textures with the same format together (offline)
  2. Upload all texture groups to GPU using `glTexImage3D()` and `glCompressedTexImage3D()`
  3. `glGetTextureHandleARB()` and make them resident with `glMakeTextureHandleResidentARB()`
  4. Fill buffers with baked data (mesh vertices and indices, instance matrices, indirect buffer)
  5. Load, compile and link vertex and fragment shaders
  6. Do the magic with a single call to `glMultiDrawElementsIndirect()`


## You convinced me. How to use it?
### Requirements
If your checklist looks like this below, you are ready to build the project from source code!
 - [x] A **legal** copy of *GTA Vice City* (this application requires the original asset files)
 - [x] GPU that supports at least OpenGL 3.3 Core Profile
 - [x] [SDL2](https://libsdl.org/download-2.0.php) library downloaded
 - [x] [GLEW 1.13](http://glew.sourceforge.net) library downloaded
 - [x] [premake5](https://premake.github.io/download.html#v5) installed for different configuration

You can also download the binaries, but you still have to fulfill the first 2 requirements above.

### Building from source and running
1. Make sure that `SDL2_ROOT` and `GLEW_ROOT` environment variables are set (at least on Windows)
2. `premake5 gmake` or `premake5 vs2013` (depending on your OS / VS version) if you want to build
   with different configuration (I've included VS2015 solution with minimal feature set for you)
3. Go to `build` directory and build the generated project (using `make` or `Visual Studio`)
4. Copy the compiled `vicebaker` application to the game installation directory, create an empty
   `_extracted` directory there and run `vicebaker` in order to preprocess the assets (no harm will
   be done to original files). The baking should take less than a minute and requires about 300 MB
   free space on your HDD. After baking, you can safely remove this "_extracted" directory.
5. Copy the generated `*.blob` files back to the directory with `vicerender` and launch it!

Action            | Reaction
------------------|------------------------
Left Mouse Button | Look around
W/S/A/D           | Standard FPP movement
Q/E               | Fly up/down
Left Shift        | Increase movement speed


## The end?
Of course not! There are still some things that might be worth considering:

- [x] Import and render Vice City
- [x] Achieve 1 draw call
- [x] Provide fallback for unsupported GPUs (more than 1 draw-call)
- [x] Optional asset compression (LZHAM)
- [ ] Some textures seem to be wrong (those hash collisions...)
- [ ] Fix issues with some triangle-stripped meshes (mostly in Mainland)
- [ ] Sort transparent objects back-to-front
- [ ] Loosen some constrains to implement view-frustum culling
- [ ] Add Liberty City from GTA III
- [ ] Improve the visual quality somehow (via Über-shader?)
- [ ] Reversed floating-point depth buffer (no more Z-fighting)
- [ ] Support for hour-constrained models and night time
- [ ] Improve Code quality & readability
- [x] Experimental PlayStation 3 port

Unfortunately due to the 1 draw-call constraint there will be no run-time
shadows or any post-processing effects :/ Yeah, I know - what a pitty... ;)

![Short low-res preview](https://i.imgur.com/rTUmwtE.gif)
