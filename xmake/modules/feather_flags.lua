-- Toolchain-conditional compile/link flags, shared between the engine's own
-- targets (root xmake.lua's on_config) and downstream "project DLL" consumers
-- (tools/SDK/FeatherSDK.lua's on_config).
--
-- Same reason feather_codegen.lua is a module rather than a plain function in
-- xmake.lua: on_config/on_load/before_build scripts run in a sandbox with its
-- own _ENV that doesn't see description-scope Lua globals, so import() is the
-- only way to share this across xmake.lua files -- and cross-repo, the
-- consumer's xmake.lua can't see the engine's locals at all.
--
-- Must be applied from on_config, not on_load: the checks below query the
-- resolved toolchain, which isn't known at load time.

function apply(target)
    local want_lto     = has_config("production") or has_config("use_lto")
    local want_static  = has_config("production") or has_config("static_cpp")

    -- is_toolchain() is a description-scope global and nil inside on_config,
    -- so query the resolved tool instead.
    local function is_msvc()     return target:has_tool("cxx", "cl", "clang_cl") end
    local function is_clang()    return target:has_tool("cxx", "clang", "clangxx", "clang-cl") end
    local function is_clang_cl() return target:has_tool("cxx", "clang_cl") end

    if want_lto and not is_msvc() then
        target:add("cxflags", "-flto=thin", {force = true})
        target:add("ldflags", "-flto=thin", {force = true})
    end

    if want_static and is_plat("linux") and not is_clang() then
        target:add("ldflags", "-static-libgcc", "-static-libstdc++", {force = true})
    end

    if has_config("enable_sanitizers") and is_mode("debug") and not is_msvc() then
        target:add("cxflags", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", {force = true})
        target:add("ldflags", "-fsanitize=address,undefined", {force = true})
    end

    -- Reflection uses bare [[get]]/[[set(...)]]/[[ignore]]/[[method]] attributes
    -- that the generator reads textually; compilers only need to ignore them.
    -- This is why a consumer needs these flags too, not just the engine: those
    -- attributes appear in the project's own FCLASS/FSTRUCT headers.
    if is_msvc() then
        target:add("cxflags", "/W4", "/wd4100", "/wd5030", {force = true})
        if is_clang_cl() then
            -- clang-cl matches is_msvc() above (it accepts /-style flags), but it
            -- emits its own Clang diagnostics for our bare attributes rather than
            -- MSVC's C5030 -- e.g. "unknown attribute 'method' ignored
            -- [-Wunknown-attributes]" -- so /wd5030 alone doesn't silence it.
            -- clang-cl also accepts Clang's -W/-Wno- flags directly (no /clang:
            -- prefix needed), so pass the same ones the plain-Clang branch below
            -- uses.
            target:add("cxflags", "-Wno-attributes", "-Wno-unknown-attributes", {force = true})
        end
        if is_mode("debug") then
            target:add("cxflags", "/Od", "/Zi", {force = true})
        elseif is_mode("releasedbg") then
            target:add("cxflags", "/O2", "/Zi", {force = true})
        elseif is_mode("release") then
            target:add("cxflags", "/O2", {force = true})
        end
    else
        target:add("cxflags",
            "-Wall", "-Wextra", "-pedantic", "-Wno-unused-parameter",
            "-Wno-attributes",
            {force = true})
        if is_clang() then
            -- GCC only recognizes -Wno-attributes for this; -Wno-unknown-attributes
            -- is Clang's spelling and GCC rejects it as an unrecognized option.
            target:add("cxflags", "-Wno-unknown-attributes", {force = true})
        end
        if is_mode("debug") then
            target:add("cxflags", "-g", "-O0", {force = true})
        elseif is_mode("releasedbg") then
            target:add("cxflags", "-g", "-O2", {force = true})
        elseif is_mode("release") then
            target:add("cxflags", "-O3", {force = true})
        end
    end
end
