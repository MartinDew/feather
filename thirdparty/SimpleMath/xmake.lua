target("simplemath")
    set_kind("static")
    set_warnings("none")
    -- Linked into downstream project DLLs (shared) as well as the engine's
    -- own executables; a non-PIC static archive can't go into a shared
    -- object on ELF (ld: "relocation ... can not be used when making a
    -- shared object").
    if not is_plat("windows") then
        add_cxflags("-fPIC")
    end
    add_files("SimpleMath.cpp")
    add_headerfiles("SimpleMath.h", "SimpleMath.inl")
    add_includedirs("$(scriptdir)", {public = true})
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", {public = true})
    add_packages("directxmath", {public = true}) -- types appear in public headers
target_end()
