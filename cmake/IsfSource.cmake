include(FetchContent)
FetchContent_Declare(vvisf
    URL https://codeload.github.com/mrRay/VVISF-GL/tar.gz/7306b1abdd0b5536f0f230c0519453e79184e08b
    URL_HASH SHA256=db4d1fb30ceb9c08b0a798df9d4bb6f2ff421ead90cbcbc812267914337fec43)
FetchContent_GetProperties(vvisf)
if(NOT vvisf_POPULATED)
    FetchContent_Populate(vvisf)
endif()
