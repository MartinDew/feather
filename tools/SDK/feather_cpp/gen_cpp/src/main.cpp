// feather_gen_cpp: turns mrbind_gen_c's descriptor into header-only C++
// wrappers over the generated C API.
//
// The descriptor is the same file mrbind_gen_csharp reads, so the mapping
// problem is the one that generator already solves -- with the simplifications
// C++ allows: real const, real inheritance, real RAII, and a direct #include of
// the C headers instead of a P/Invoke declaration per function.

#include "generator.h"

#include "common/command_line_args_as_utf8.h"
#include "common/command_line_parser.h"
#include "common/filesystem.h"
#include "common/set_error_handlers.h"
#include "generators/c_interop/desc_to_and_from_json.h"

#include <cstdio>
#include <optional>
#include <string>

int main(int argc, char **argv)
{
    mrbind::SetErrorHandlers();

    feather_gen::Options opts;
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
            opts.native_types.insert_or_assign(std::string(args[0]), feather_gen::NativeType{.header = std::string(args[1])});
        },
    });
    args_parser.AddFlag("--native-alias-namespace", {
        .arg_names = {"name"},
        .desc = "Additionally alias every --native-type into this namespace under its own short name, matching how the engine spells them (feather::Vector3, ...).",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {opts.native_alias_namespace = args.front();},
    });
    args_parser.AddFlag("--verbose", {
        .desc = "Print a summary of what was read and emitted.",
        .func = [&](mrbind::CommandLineParser::ArgSpan args) {(void)args; opts.verbose = true;},
    });

    mrbind::CommandLineArgsAsUtf8 utf8_args(argc, argv);
    args_parser.Parse(utf8_args.argc, utf8_args.argv);

    if (opts.input_json.empty())
        throw std::runtime_error("`--input-json` is required.");
    if (opts.output_dir.empty())
        throw std::runtime_error("`--output-dir` is required.");

    const mrbind::CInterop::OutputDesc desc = mrbind::CInterop::LoadOutputDescFromFile(opts.input_json.c_str());

    const std::filesystem::path output_dir = mrbind::MakePath(opts.output_dir);
    mrbind::PrepareOutputDir(output_dir, opts.clean_output_dir ? "" : "--clean-output-dir");

    feather_gen::Generator generator;
    generator.desc = &desc;
    generator.opts = opts;
    generator.Run();

    std::printf("feather_gen_cpp: %zu functions emitted, %zu skipped (types with no C++ spelling yet)\n",
        generator.num_emitted, generator.num_skipped);

    if (opts.verbose)
    {
        for (const auto &[reason, count] : generator.skip_reasons)
            std::printf("  skipped %4zu x %s\n", count, reason.c_str());
    }

    return 0;
}
