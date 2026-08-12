target("simplemath")
    set_kind("static")
    set_warnings("none")
    -- feather_public_api depends on this publicly, so it is linked into
    -- downstream project DLLs (kind = "shared") as well as into the engine's
    -- own executables. A non-PIC static archive can't go into a shared object
    -- on ELF targets -- ld rejects it outright ("relocation R_X86_64_PC32
    -- against symbol ... can not be used when making a shared object").
    -- if not is_plat("windows") then
    --     add_cxflags("-fPIC")
    -- end
    add_files("SimpleMath.cpp")
    add_headerfiles("SimpleMath.h", "SimpleMath.inl")
    add_includedirs("$(scriptdir)", {public = true})
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", {public = true})
    add_packages("directxmath", {public = true}) -- types appear in public headers
target_end()
