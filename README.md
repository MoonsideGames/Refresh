[![Build Status](https://gitea.drone.moonside.games/api/badges/MoonsideGames/Refresh/status.svg)](https://gitea.drone.moonside.games/MoonsideGames/Refresh)

This is Refresh, an XNA-inspired 3D graphics library with modern capabilities.

License
-------
Refresh is licensed under the zlib license. See LICENSE for details.

About Refresh
-------------
Refresh is directly inspired by FNA3D and intended to be a replacement for XNA's Graphics namespace.
XNA 4.0 is a powerful API, but its shader system is outdated and certain restrictions are awkward to handle in a modern context.
In the way that XNA was "one step above" DX9, Refresh intends to be "one step above" Vulkan. It should map nicely to modern graphics APIs.
Refresh will initially have a Vulkan runtime implementation. Support for other APIs like DX12 may come later.
For shaders, we consume SPIR-V bytecode.

Dependencies
------------
Refresh depends on SDL2 for portability.
Refresh never explicitly uses the C runtime.

Building Refresh
----------------
For *nix platforms, use CMake:

    $ mkdir build/
    $ cd build/
    $ cmake ../
    $ make

For Windows, use the Refresh.sln in the "visualc" folder.

Want to contribute?
-------------------
Issues can be reported and patches contributed via Github:

https://github.com/thatcosmonaut/Refresh
