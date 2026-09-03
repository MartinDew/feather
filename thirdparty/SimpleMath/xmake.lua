-- The sources live in tools/SDK/thirdparty/SimpleMath: a plugin vendors them
-- and compiles the same math types the engine does, which is what lets those
-- types cross the C boundary as themselves rather than as opaque handles. This
-- target is the engine's own view of that one copy.
local SDK_SIMPLEMATH = path.join(path.directory(path.directory(os.scriptdir())),
    "tools", "SDK", "thirdparty", "SimpleMath")

target("simplemath")
    -- object, not static: avoids MSVC's per-lib RuntimeLibrary/LNK2038
    -- mismatches, and a linker dropping a TU with no directly-referenced symbol.
    set_kind("object")
    set_warnings("none")
    -- Linked into shared project DLLs too; a non-PIC static archive can't go
    -- into a shared object on ELF.
    if not is_plat("windows") then
        add_cxflags("-fPIC")
    end
    add_files(path.join(SDK_SIMPLEMATH, "SimpleMath.cpp"))
    add_headerfiles(path.join(SDK_SIMPLEMATH, "SimpleMath.h"), path.join(SDK_SIMPLEMATH, "SimpleMath.inl"))
    add_includedirs(SDK_SIMPLEMATH, {public = true})
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", {public = true})
    add_packages("directxmath", {public = true}) -- types appear in public headers
target_end()
