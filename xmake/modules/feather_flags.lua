-- Toolchain-conditional compile/link flags, shared between the engine's own
-- targets and downstream project DLLs (tools/SDK/FeatherSDK.lua). Applied
-- from on_config, not on_load: queries the resolved toolchain.

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

    -- Reflection's bare [[get]]/[[set(...)]]/[[method]] attributes just need
    -- compilers to ignore them; consumers need these flags too (FCLASS/FSTRUCT).
    if is_msvc() then
        target:add("cxflags", "/W4", "/wd4100", "/wd5030", {force = true})
        if is_clang_cl() then
            -- clang-cl matches is_msvc() above but emits Clang diagnostics for
            -- the bare attributes, not MSVC's C5030, so /wd5030 alone misses it.
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
            -- GCC rejects -Wno-unknown-attributes as unrecognized; Clang needs it.
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
