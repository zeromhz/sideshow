# VVISF compatibility integration (in progress)

Pinned upstream: https://github.com/mrRay/VVISF-GL/tree/7306b1abdd0b5536f0f230c0519453e79184e08b
Archive SHA-256: `db4d1fb30ceb9c08b0a798df9d4bb6f2ff421ead90cbcbc812267914337fec43`.

VVGL/VVISF use BSD-3-Clause; bundled nlohmann JSON and exprtk use MIT.
Their notices are retained here. The pinned archive supplies all compiled sources;
no submodule or system shader interpreter is used. Apple framework dependencies:
OpenGL, Cocoa, IOSurface, CoreVideo, CoreMedia. Objective-C++ is confined to vendor files.

The in-memory document constructor requires an explicit vertex source; use upstream
`ISFVertPassthru_GL2` when no paired vertex file exists. Unlike std::exception,
`VVISF::ISFErr` must be caught separately at the runtime boundary.

The context adapter retains the current CGL context without taking SDL ownership,
and gives VVGL a private pool. Pool teardown precedes context release. No global
VVGL pool is installed. EGL compilation and resource/failure integration are still
under investigation; this is not yet a selectable player mode.

Patch 0001 replaces legacy Broadcom/GLES2 headers with EGL/GLES3, enables GLES3
code paths, adds missing texture/PBO enums, and removes the Pi-specific silent
RGBA8 substitution in CreateRGBAFloatTex. GLES format support remains a runtime
check; an unsupported framebuffer must fail, never downgrade silently.

Compile-only checks use Khronos OpenGL-Registry revision
6af574a14089ccfee87efe230ebcdd8742859813 and EGL-Registry revision
db3425b8246136faccb5e2782b5694960bd6edf1. Those headers are test scratch,
not shipped dependencies. Pi execution remains unverified.
