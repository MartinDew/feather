#pragma once

#include "generators/c_interop/c_output_desc.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace feather_gen
{
    namespace CI = mrbind::CInterop;

    // A C++ type the consumer already defines identically, aliased instead of
    // wrapped. The engine's math types, whose SimpleMath source a plugin
    // vendors verbatim.
    struct NativeType
    {
        std::string header;
    };

    struct Options
    {
        std::string input_json;
        std::string output_dir;
        bool clean_output_dir = false;
        std::string c_dir_prefix = "feather_c";
        std::string cpp_dir_prefix = "feather_cpp";
        std::string helper_macro_prefix = "FEATHER_C_";
        // Native types are additionally aliased into this namespace, matching
        // the engine's own math_defs.h spellings (feather::Vector3, ...).
        std::string native_alias_namespace;
        std::map<std::string, NativeType> native_types;
        bool verbose = false;
    };

    // A C++ type spelling split into the parts that decide how it crosses the
    // C boundary. The spellings come from cppdecl's printer, so they are
    // normalized: qualifiers lead, one space before `&`/`*`.
    struct ParsedType
    {
        enum class Form { value, lref, rref, ptr };

        std::string base;
        bool is_const = false;
        Form form = Form::value;

        [[nodiscard]] bool IsIndirect() const {return form != Form::value;}
    };

    [[nodiscard]] ParsedType ParseType(std::string_view spelling);

    enum class Cat { unknown, void_, arithmetic, enum_, class_ };

    // What the generator knows about one entry of `OutputDesc::cpp_types`.
    struct TypeInfo
    {
        Cat cat = Cat::unknown;
        const CI::TypeKinds::Class *cls = nullptr;
        const CI::TypeKinds::Enum *en = nullptr;
        const CI::TypeTraits *traits = nullptr;

        std::string cpp_name;
        std::string c_name;
        // Fully qualified name of the C++ wrapper, or of the native type it is
        // aliased to.
        std::string wrapper;

        bool native = false;
        bool exposed = false;
        // Emitted as a wrapper class (false for natives, exposed structs and
        // anything skipped).
        bool wrapped = false;
    };

    // How one parameter crosses into the C call.
    struct ParamBinding
    {
        bool ok = false;
        // The wrapper's parameter declaration, without a trailing comma.
        std::string decl;
        // Statements to run before the call, if any.
        std::string pre;
        // The argument expression(s) for the C call; may be several, comma-separated.
        std::string arg;
        // Statements to run after the call, if any.
        std::string post;
    };

    // How a return value comes back out of the C call.
    struct ReturnBinding
    {
        bool ok = false;
        // The wrapper's return type, `void` included.
        std::string type;
        // The returned expression, with `$` standing for the C call.
        std::string expr = "$";
        bool is_void = false;
    };

    struct Generator
    {
        const CI::OutputDesc *desc = nullptr;
        Options opts;

        std::map<std::string, TypeInfo, std::less<>> types;
        // Wrapper classes in base-before-derived order.
        std::vector<const TypeInfo *> class_order;
        // Headers pulled in for native types.
        std::set<std::string> native_headers;
        // Relative names of the generated C headers to include.
        std::set<std::string> c_headers;

        // Out-of-line method definitions, keyed by class, emitted once every
        // class is complete so a by-value return of another wrapper works.
        std::map<std::string, std::string> pending_bodies;

        // Signatures already emitted into the class being written, so an
        // inherited member copied into a derived class cannot collide with the
        // one the class declares itself. Cleared per class.
        mutable std::set<std::string> emitted_signatures;

        // Types that made a function unbindable, counted for --verbose so the
        // gaps in coverage are visible rather than silent.
        mutable std::map<std::string, std::size_t> skip_reasons;

        std::size_t num_emitted = 0;
        std::size_t num_skipped = 0;

        void Run();

      private:
        void CollectTypes();
        void OrderClasses();

        [[nodiscard]] const TypeInfo *Find(std::string_view cpp_name) const;

        [[nodiscard]] std::string CoreHeader() const;
        [[nodiscard]] std::string MathHeader() const;
        [[nodiscard]] std::string MainHeader();

        void EmitEnum(std::string &out, const TypeInfo &info) const;
        void EmitClassDecl(std::string &out, const TypeInfo &info);
        void EmitFreeFunctions(std::string &out);

        // Emits one function-like entity. `owner` is null for free functions.
        // Returns false and emits nothing if a type could not be bound.
        bool EmitFuncLike(
            std::string &decls, std::string &bodies,
            const CI::BasicFuncLike &func, const std::string &wrapper_name,
            bool is_static, bool is_ctor, const TypeInfo *owner,
            const std::string &comment) const;

        [[nodiscard]] ParamBinding BindParam(const CI::FuncParam &param, std::size_t index) const;
        [[nodiscard]] ReturnBinding BindReturn(const CI::FuncReturn &ret) const;
    };
}
