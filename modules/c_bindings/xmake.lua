option("enable_c_bindings")
    set_default(true)
    set_description("Generate C bindings for Feather's public API via MRBind")
option_end()

if not has_config("enable_c_bindings") then
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local SOURCE_OUTPUT_DIR = path.join(os.scriptdir(), ".gen")
local HEADER_OUTPUT_DIR = path.join(FEATHER_ROOT, ".gen", "bindings", "c")

-- Implementation (.cpp) compiles into `feather` itself: mrbind_gen_c's
-- generated implementation calls straight into feather:: classes, whose
-- method bodies only exist inside target("feather") (see xmake/engine.lua --
-- there's no separate linkable "feather core" library), so this must be a
-- real feather_module_target() module, not a standalone artifact.
feather_module_target("c_bindings", os.scriptdir(), {}, {})

-- feather_module_target()'s add_deps("c_bindings") links it lazily, same as
-- any other module -- but nothing inside feather ever CALLS a generated
-- feather_RID_Get_id()-style function (their only purpose is to be called
-- from outside), so the linker's normal "only pull in archive members that
-- resolve an existing undefined reference" behavior drops the entire
-- library from the final binary (confirmed empirically: symbols present in
-- libc_bindings.a, absent from `feather`). Forcing whole-archive linking for
-- just this one dependency keeps them in. xmake's gcc/clang toolchain module
-- is the only one implementing `whole` (-Wl,--whole-archive); on MSVC this
-- is a silent no-op today (would need /WHOLEARCHIVE:c_bindings.lib), same
-- unfinished-Windows-story as the engine's own .def-file export setup
-- elsewhere in xmake/engine.lua.
target("feather")
    add_linkgroups("c_bindings", {whole = true})
target_end()

target("c_bindings")
    -- Only this target's own build step needs the generator binaries, not
    -- feather's -- deliberately not passed via feather_module_target's
    -- exe_packages.
    add_packages("mrbind")
    -- Windows-only: mrbind was built against libllvm's clang there (see
    -- thirdparty/xmake.lua), not any system clang -- needed directly so
    -- resolve_clang_resource_dir() can find it via target:pkg("libllvm").
    if is_plat("windows") then
        add_packages("libllvm")
    end
    -- Generated .cpp files #include their sibling generated header by quoted
    -- path; it lives in HEADER_OUTPUT_DIR, not colocated with the .cpp, so it
    -- must be on the include path (public: consumers of c_bindings need it
    -- too).
    add_includedirs(HEADER_OUTPUT_DIR, {public = true})
    -- mrbind_gen_c's internal helper header (__mrbind_c_details.h) is
    -- written into --output-source-dir, not --output-header-dir (confirmed
    -- empirically) -- private, implementation detail only.
    add_includedirs(SOURCE_OUTPUT_DIR, {public = false})

    -- Must be on_config, not on_load: resolve_compile_flags() queries the
    -- resolved cxx toolchain (compiler.load), which isn't available until
    -- the target's toolchain is set up -- confirmed empirically ("we cannot
    -- get tool(cxx) before target is loaded... call it in on_config()"),
    -- same constraint feather_flags.lua documents for apply_compile_flags.
    on_config(function (target)
        -- c_bindings is a dependency of target("feather") (feather_module_target's
        -- add_deps), so it configures/builds BEFORE feather's own before_build
        -- (run_codegen in xmake/engine.lua) has generated core's *.gen.h --
        -- confirmed on a genuinely fresh checkout (CI, and locally after
        -- deleting all *.gen.h): core/main/simulation.h's own
        -- #include "simulation.gen.h" fails since nothing has generated it
        -- yet. Masked on a dev box with leftover .gen.h files from any
        -- earlier build, which is why this wasn't caught locally at first.
        -- Same problem vex_renderer already solved for reflection codegen
        -- itself (see feather_codegen.lua's run_module_codegen comment) --
        -- mirror that exact fix here.
        import("feather_codegen")
        feather_codegen.run_module_codegen(os.scriptdir(), {feather_root = FEATHER_ROOT})

        import("feather_bindings")
        local result = feather_bindings.run_c_pipeline(target, {
            feather_root = FEATHER_ROOT,
            dirs = {path.join(FEATHER_ROOT, "core")},
            header_output_dir = HEADER_OUTPUT_DIR,
            source_output_dir = SOURCE_OUTPUT_DIR,
            -- StaticString (core/framework/static_string.hpp) is real Feather
            -- infrastructure used pervasively (ClassInfo, ClassDB, Reflected,
            -- ...), just namespaced "nassimp" rather than "feather" -- needs
            -- its own --allow or mrbind refuses to bind anything that
            -- mentions it.
            extra_parser_flags = {
                "--allow", "nassimp",
                -- std::initializer_list can't cross a C ABI; drop the
                -- overloads that take one rather than the whole class. Bare
                -- "std::initializer_list" (no template args) matched
                -- nothing empirically -- every concrete instantiation
                -- (VariantArray's, CowVector<T>'s, ...) needs to match, so
                -- use a regex rather than enumerating each T by hand.
                "--skip-mentions-of", "/std::initializer_list<.*>/",
                "--skip-mentions-of", "/std::span<.*>/",
                -- Same container-of-a-skipped-type issue as span/
                -- initializer_list above: skip-mentions-of on the element
                -- type alone doesn't cascade into std::array<T, N> using it
                -- (e.g. Projection::get_frustum_corners() -> std::array<Vector3, 8>).
                "--skip-mentions-of", "/std::array<.*>/",
                "--skip-mentions-of", "/std::tuple<.*>/",
                -- Generic STL container binding out of scope for this pass,
                -- same reasoning as span/array/tuple/initializer_list above.
                "--skip-mentions-of", "/std::unordered_map<.*>/",
                -- EngineSettings is a private-constructor singleton (only
                -- reachable via its own static _instance) whose private
                -- `_settings` member is unordered_map<string_view,
                -- unique_ptr<ISettingStorage>> -- implicitly non-copyable.
                -- mrbind still attempts a copy-constructor wrapper for the
                -- whole class (skip-mentions-of on the map field alone
                -- doesn't stop that), which fails to compile. Not meant to
                -- be constructed via bindings at all; excluded wholesale.
                "--ignore", "feather::EngineSettings",
                "--skip-mentions-of", "feather::EngineSettings",
                -- DirectX::SimpleMath's Vector2/3/4, Matrix, Quaternion,
                -- Color are trivial PODs (float members only) and would be
                -- better exposed as real C structs via mrbind_gen_c's
                -- --expose-as-struct; skipping them for now just to get the
                -- pipeline itself working end to end.
                "--skip-mentions-of", "/DirectX::SimpleMath::.*/",
                -- Same reasoning for flecs (ECS library) types, encountered
                -- once core/world's headers are included.
                "--skip-mentions-of", "/flecs::.*/",
                -- taywee_args (command-line parser), same reasoning.
                "--skip-mentions-of", "/args::.*/",
                -- Variant is a type-erasure wrapper over many alternative
                -- types (including SimpleMath's, which mrbind can't
                -- process) -- its per-alternative as<T>()/is<T>() surface
                -- needs traits for every alternative even when the
                -- individual functions are skip-mentions-of'd, so the whole
                -- class needs --ignore, not just its problem members.
                "--ignore", "feather::Variant",
                "--skip-mentions-of", "feather::Variant",
                -- VariantArray is built directly on Variant (including its
                -- nested ::iterator) -- same reasoning cascades to it.
                "--ignore", "feather::VariantArray",
                "--skip-mentions-of", "feather::VariantArray",
                -- --ignore on the outer class doesn't cascade to nested
                -- types -- needs its own entry.
                "--ignore", "feather::VariantArray::iterator",
                "--skip-mentions-of", "feather::VariantArray::iterator",
                "--ignore", "feather::VariantArray::const_iterator",
                "--skip-mentions-of", "feather::VariantArray::const_iterator",
                -- ClassInfo carries a Variant-returning factory function
                -- field -- same cascade.
                "--ignore", "feather::ClassInfo",
                "--skip-mentions-of", "feather::ClassInfo",
                -- Delegate<T> exhaustively instantiated by mrbind's bindings
                -- surfaces a genuine pre-existing template bug (unrelated to
                -- bindings generation): class_db.h's
                -- `using subclass_delegate_t = Delegate<const std::string_view>`
                -- fails to compile delegate.h's forwarding call when actually
                -- instantiated down this path, something normal engine code
                -- never happens to trigger. Worth a real fix separately;
                -- excluded here rather than papering over it.
                "--ignore", "feather::Delegate",
                "--skip-mentions-of", "feather::Delegate",
                -- Same story as Delegate above: StaticIndexedArray<T>::begin()
                -- const's `std::max(_elements.size() - 1, 0ULL)` compares
                -- size_t against unsigned long long (distinct types on
                -- Linux, std::max requires identical types) -- broken for
                -- ANY T, but normal engine code apparently never calls
                -- begin()/end() on one, so it's never instantiated there.
                -- mrbind's exhaustive bindings do call it. Pre-existing bug,
                -- unrelated to bindings generation; excluded, not fixed.
                "--ignore", "feather::StaticIndexedArray",
                "--skip-mentions-of", "feather::StaticIndexedArray",
                -- Declared in math_defs.h but never actually defined
                -- anywhere in the codebase -- a dead declaration nothing in
                -- the real engine calls, so it never became a link error
                -- until whole-archive linking forced this generated wrapper
                -- (which calls everything declared) into the final binary.
                -- Pre-existing gap, not a bindings-generation bug.
                "--ignore", "feather::raise_to_next_multiple_of",
                "--skip-mentions-of", "feather::raise_to_next_multiple_of",
                -- RID::invalid()'s out-of-line `constexpr` definition
                -- (core/resources/rid.cpp) is only ever called from
                -- RID::is_valid() in that same file; in release-mode builds
                -- GCC constant-folds that call away entirely and elides the
                -- out-of-line definition, so nothing emits a real symbol for
                -- it -- fine for normal engine code (never called
                -- cross-TU), but the generated C binding needs a real
                -- runtime-callable function, which release mode doesn't
                -- provide. Debug/releasedbg build fine; only release hits
                -- this. Excluded rather than restructuring the engine's
                -- constexpr API for a codegen-only requirement.
                "--ignore", "feather::RID::invalid",
                "--skip-mentions-of", "feather::RID::invalid",
                -- (CowVector<T>::at()'s std::out_of_range throw,
                -- StaticIndexedArray::remove()'s throw, ResolverSetting::set_key()'s
                -- throw (engine_settings.h; a different class from EngineSettings
                -- above, unrelated to its exclusion reason), and LaunchSettings's
                -- throw were all fixed at the source instead of excluded here:
                -- converted to fassert(), matching this engine's own established
                -- assertion convention. See core/framework/cow_vector.h,
                -- core/framework/static_indexed_array.h, core/main/engine_settings.h,
                -- core/main/launch_settings.cpp.)
            },
        })
        for _, src in ipairs(result.sources) do
            target:add("files", src, {always_added = true})
        end
    end)
target_end()
