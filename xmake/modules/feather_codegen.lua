-- Reflection-codegen helpers shared between the top-level xmake.lua
-- (run_codegen, attached to feather.editor/standalone) and module xmake.lua
-- files (e.g. modules/vex_renderer/xmake.lua's own before_build hook).
--
-- This has to be a proper import()-able module rather than a plain global
-- function in xmake.lua: on_load/before_build/etc scripts run inside a sandbox
-- with its own _ENV, and plain Lua globals defined at xmake.lua description
-- scope are NOT visible inside it -- only xmake's own APIs and import()ed
-- modules are. import("feather_codegen") is the supported way to share logic
-- across script-scope closures in different xmake.lua files.

-- Resolves a clang executable for the JSON-AST parse. Prefers a clang already on
-- PATH (fast, no download), then the target's C++ compiler when it is itself
-- clang-based, then the xrepo 'llvm' package (added to the target as a binary/
-- tool-only dep) -- but only when fetch_llvm_for_codegen is enabled, since that
-- package's first download is the whole toolchain.
--
-- find_tool is passed in rather than import()'d here: import() only resolves
-- relative to the importING script's own sandbox, and this function isn't
-- itself a script entry point -- callers already have find_tool from their own
-- `import("lib.detect.find_tool")`.
function resolve_clang(target, find_tool)
    local t = find_tool("clang")
    if t then return t.program end
    local cxx = target:tool("cxx")
    if cxx and cxx:find("clang", 1, true) then return cxx end
    if has_config("fetch_llvm_for_codegen") then
        local pkg = target:pkg("llvm")
        if pkg and pkg:installdir() then
            local exe = is_host("windows") and "clang.exe" or "clang"
            local cand = path.join(pkg:installdir(), "bin", exe)
            if os.isfile(cand) then return cand end
        end
    end
    return nil
end

-- Collects the include dirs clang needs to parse engine headers during codegen:
-- the engine roots plus every resolved package's include dirs (flecs, sdl3,
-- directxmath, ...). Missing dirs only cause parse errors for headers that need
-- them, so this is best-effort and defensive.
function collect_codegen_includes(target)
    local proj = os.projectdir()
    local incs = {
        proj,
        path.join(proj, "core"),
        path.join(proj, "thirdparty", "SimpleMath"),
    }
    for _, pkg in pairs(target:pkgs() or {}) do
        for _, dir in ipairs(table.wrap(pkg:get("includedirs"))) do table.insert(incs, dir) end
        for _, dir in ipairs(table.wrap(pkg:get("sysincludedirs"))) do table.insert(incs, dir) end
        local installdir = pkg:installdir()
        if installdir then table.insert(incs, path.join(installdir, "include")) end
    end
    return incs
end

local function common_argv(target, clang, extra)
    local proj = os.projectdir()
    local argv = {
        path.join(proj, "tools", "generate_reflection.py"),
        "--core-path", path.join(proj, "core"),
        "--project-root", proj,
        "--clang", clang,
    }
    for _, e in ipairs(extra or {}) do table.insert(argv, e) end
    for _, inc in ipairs(collect_codegen_includes(target)) do
        table.insert(argv, "-I")
        table.insert(argv, inc)
    end
    if is_plat("windows") then
        -- Must be a single "--clang-arg=-DFOO" token, not two separate argv
        -- entries: Python's argparse sees a value starting with "-" as looking
        -- like another option and refuses to consume it as --clang-arg's value
        -- ("expected one argument"), even though it's a valid clang flag.
        for _, def in ipairs({"-DNOMINMAX", "-DWIN32_LEAN_AND_MEAN"}) do
            table.insert(argv, "--clang-arg=" .. def)
        end
    end
    return argv
end

-- Runs generate_reflection.py for core/ plus, when given, one or more module
-- dirs (vex_renderer today) -- the "refresh everything" entry point, attached
-- to feather.editor/standalone's before_build. module_dirs may be nil/empty.
function run_core_codegen(target, find_tool, module_dirs)
    local clang = resolve_clang(target, find_tool)
    if not clang then
        raise("[codegen] no clang found. Put clang on PATH, or configure with "
              .. "`xmake f --fetch_llvm_for_codegen=y` to fetch the xrepo 'llvm' package "
              .. "(large download) so reflection headers can be parsed.")
    end

    local extra = {}
    for _, dir in ipairs(module_dirs or {}) do
        table.insert(extra, "--module-path")
        table.insert(extra, dir)
    end
    local argv = common_argv(target, clang, extra)

    cprint("${cyan}[codegen]${reset} generate_reflection.py (clang=%s)", clang)
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end

-- Runs generate_reflection.py for core/ AND a single module directory.
-- Intended for a *module's own* before_build (see modules/vex_renderer/xmake.lua),
-- not the executable's: a module target like vex_renderer_standalone is a
-- dependency of feather.standalone, and xmake builds dependencies -- including
-- compiling their files -- before the depending target's own before_build runs.
-- So by the time run_core_codegen (attached to feather.standalone) would
-- generate core/*/*.gen.h and register_vex_renderer_types.gen.cpp, xmake may
-- already be trying to compile the module's files -- which now transitively
-- #include core headers that themselves need their own generated .gen.h (e.g.
-- vex_renderer.h -> rendering/render_scene.h -> resources/material.h). This
-- MUST include core (no --skip-core): a module-only pass leaves core's .gen.h
-- files missing entirely, which is a harder failure than the redundant-parse
-- cost of doing both here -- write_if_changed makes that redundant work with
-- run_core_codegen's own later pass harmless (it just finds 0 files changed).
function run_module_codegen(target, find_tool, module_dir)
    local clang = resolve_clang(target, find_tool)
    if not clang then
        raise("[codegen] no clang found for module '%s'. Put clang on PATH, or configure with "
              .. "`xmake f --fetch_llvm_for_codegen=y` to fetch the xrepo 'llvm' package "
              .. "(large download) so reflection headers can be parsed.", path.filename(module_dir))
    end

    local argv = common_argv(target, clang, {"--module-path", module_dir})

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end
