target("simplemath")
    set_kind("static")
    set_warnings("none")
    add_files("SimpleMath.cpp")
    add_headerfiles("SimpleMath.h", "SimpleMath.inl")
    add_includedirs("$(scriptdir)", {public = true})
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", {public = true})
    add_packages("directxmath", {public = true}) -- types appear in public headers
target_end()
