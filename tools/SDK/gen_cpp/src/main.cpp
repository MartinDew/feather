// feather_gen_cpp: turns mrbind_gen_c's descriptor into header-only C++
// wrappers over the generated C API.
//
// The descriptor is the same file mrbind_gen_csharp reads, so the mapping
// problem is the one that generator already solves -- with the simplifications
// C++ allows: real const, real inheritance, real RAII, and a direct #include of
// the C headers instead of a P/Invoke declaration per function.

#include "common/command_line_args_as_utf8.h"
#include "common/command_line_parser.h"
#include "common/filesystem.h"
#include "common/set_error_handlers.h"
#include "generators/c_interop/c_output_desc.h"
#include "generators/c_interop/desc_to_and_from_json.h"

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace
{
    // A C++ type the consumer already defines identically (the vendored
    // SimpleMath types), aliased instead of wrapped. See --native-type.
    struct NativeType
    {
        std::string header;
    };

    struct Options
    {
        std::optional<std::string> input_json;
        std::optional<std::string> output_dir;
        bool clean_output_dir = false;
        std::string c_dir_prefix = "feather_c";
        std::string cpp_dir_prefix = "feather_cpp";
        std::string helper_macro_prefix = "FEATHER_C_";
        std::map<std::string, NativeType> native_types;
        bool verbose = false;
    };
}

int main(int argc, char **argv)
{
    mrbind::SetErrorHandlers();

    Options opts;
    mrbind::CommandLineParser args_parser;

    args_parser.AddFlag("--input-json", {
        .arg_names = {"filename.json"},
        .desc = "Path to the input json as produced by `mrbind_gen_c --output-desc-json`.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.input_json = args.front();},
    });
    args_parser.AddFlag("--output-dir", {
        .arg_names = {"dir"},
        .desc = "Output directory path. Created if missing. Otherwise it must be empty, or you must specify `--clean-output-dir`.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.output_dir = args.front();},
    });
    args_parser.AddFlag("--clean-output-dir", {
        .desc = "Destroys the contents of `--output-dir` before generating.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {(void)args; opts.clean_output_dir = true;},
    });
    args_parser.AddFlag("--c-dir-prefix", {
        .arg_names = {"dir"},
        .desc = "The directory prefix the generated C headers are included through, matching `mrbind_gen_c --map-path`. Defaults to `feather_c`.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.c_dir_prefix = args.front();},
    });
    args_parser.AddFlag("--cpp-dir-prefix", {
        .arg_names = {"dir"},
        .desc = "The directory prefix for the generated C++ headers. Defaults to `feather_cpp`.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.cpp_dir_prefix = args.front();},
    });
    args_parser.AddFlag("--helper-macro-prefix", {
        .arg_names = {"prefix"},
        .desc = "The macro name prefix passed to `mrbind_gen_c --helper-macro-name-prefix`. The descriptor does not record it. Defaults to `FEATHER_C_`.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.helper_macro_prefix = args.front();},
    });
    args_parser.AddFlag("--native-type", {
        .allow_repeat = true,
        .arg_names = {"cpp_type", "header"},
        .desc = "The consumer already has a bit-identical definition of this C++ type in this header. The generator aliases it and asserts the recorded layout, instead of emitting a wrapper class.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args)
        {
            opts.native_types.insert_or_assign(std::string(args[0]), NativeType{.header = std::string(args[1])});
        },
    });
    args_parser.AddFlag("--verbose", {
        .desc = "Print a summary of what was read and emitted.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {(void)args; opts.verbose = true;},
    });

    mrbind::CommandLineArgsAsUtf8 utf8_args(argc, argv);
    args_parser.Parse(utf8_args.argc, utf8_args.argv);

    if (!opts.input_json)
        throw std::runtime_error("`--input-json` is required.");
    if (!opts.output_dir)
        throw std::runtime_error("`--output-dir` is required.");

    const mrbind::CInterop::OutputDesc desc = mrbind::CInterop::LoadOutputDescFromFile(opts.input_json->c_str());

    const std::filesystem::path output_dir = mrbind::MakePath(*opts.output_dir);
    mrbind::PrepareOutputDir(output_dir, opts.clean_output_dir ? "" : "--clean-output-dir");

    // Skeleton: the descriptor loads and is walkable. Emission lands next.
    std::size_t num_classes = 0, num_enums = 0, num_exposed_structs = 0, num_methods = 0;
    for (const auto &[cpp_name, type_desc] : desc.cpp_types.Map())
    {
        (void)cpp_name;
        if (const auto *cl = std::get_if<mrbind::CInterop::TypeKinds::Class>(&type_desc.var))
        {
            num_classes++;
            num_methods += cl->methods.size();
            if (cl->kind == mrbind::CInterop::ClassKind::exposed_struct)
                num_exposed_structs++;
        }
        else if (std::holds_alternative<mrbind::CInterop::TypeKinds::Enum>(type_desc.var))
        {
            num_enums++;
        }
    }

    std::printf("feather_gen_cpp: %zu types (%zu classes, %zu of them exposed structs, %zu enums), %zu methods, %zu free functions, %zu native types, helpers_prefix=%s, exceptions=%s\n",
        desc.cpp_types.Map().size(), num_classes, num_exposed_structs, num_enums, num_methods,
        desc.functions.size(), opts.native_types.size(), desc.helpers_prefix.c_str(),
        desc.exception_handling_enabled ? "on" : "off");

    if (opts.verbose)
    {
        for (const auto &[cpp_name, native] : opts.native_types)
        {
            const auto *type_desc = desc.cpp_types.Map().count(cpp_name)
                ? &desc.cpp_types.Map().at(cpp_name) : nullptr;
            const char *kind = "MISSING FROM DESCRIPTOR";
            if (type_desc)
            {
                if (const auto *cl = std::get_if<mrbind::CInterop::TypeKinds::Class>(&type_desc->var))
                    kind = cl->kind == mrbind::CInterop::ClassKind::exposed_struct ? "exposed_struct" : "opaque class";
                else
                    kind = "not a class";
            }
            std::printf("  native %-45s %-24s <%s>\n", cpp_name.c_str(), kind, native.header.c_str());
        }
    }

    return 0;
}
