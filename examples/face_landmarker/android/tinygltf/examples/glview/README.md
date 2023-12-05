Simple OpenGL viewer for glTF geometry.

## Requirements

* premake5 : Requires recent `premake5`(alpha12 or later) for macosx and linux. `premake5` for windows is included in `$tinygltf/tools/window` directory.
* GLEW
  * Ubuntu 16.04: sudo apt install libglew-dev
* glfw3
  * Ubuntu 16.04: sudo apt install libglfw3-dev

### MacOSX

    > brew install glew
    > brew install glfw
    > mkdir build
    > cmake .. -G"Xcode"


## TODO

* [ ] PBR Material
* [ ] PBR Texture
* [ ] Animation
* [ ] Morph targets
