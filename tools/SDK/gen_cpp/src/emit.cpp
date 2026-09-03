#include "generator.h"

#include "common/filesystem.h"

#include <fstream>
#include <stdexcept>

namespace feather_gen
{
    namespace
    {
        // Operators worth spelling as real C++ operators. Subscript, call and
        // the increment pair are left out: their C forms need more care than
        // the rest, and nothing in the engine's API depends on them.
        [[nodiscard]] bool IsSupportedOperator(std::string_view token)
        {
            static const std::set<std::string_view> ok = {
                "==", "!=", "<", "<=", ">", ">=",
                "+", "-", "*", "/", "%",
                "+=", "-=", "*=", "/=", "%=",
            };
            return ok.contains(token);
        }

        [[nodiscard]] std::string DocComment(const CI::Comment &comment, const CI::Comment &lifetimes)
        {
            std::string ret;
            for (const CI::Comment *c : {&comment, &lifetimes})
            {
                std::size_t start = 0;
                while (start < c->c_style.size())
                {
                    std::size_t end = c->c_style.find('\n', start);
                    if (end == std::string::npos)
                        end = c->c_style.size();
                    if (end > start)
                        ret += "    " + c->c_style.substr(start, end - start) + "\n";
                    start = end + 1;
                }
            }
            return ret;
        }

        [[nodiscard]] std::string NamespaceOf(std::string_view qualified)
        {
            const std::size_t pos = qualified.rfind("::");
            return pos == std::string_view::npos ? std::string() : std::string(qualified.substr(0, pos));
        }

        [[nodiscard]] std::string NameOf(std::string_view qualified)
        {
            const std::size_t pos = qualified.rfind("::");
            return std::string(pos == std::string_view::npos ? qualified : qualified.substr(pos + 2));
        }

        void OpenNamespace(std::string &out, const std::string &ns)
        {
            if (!ns.empty())
                out += "namespace " + ns + "\n{\n";
        }

        void CloseNamespace(std::string &out, const std::string &ns)
        {
            if (!ns.empty())
                out += "}\n";
        }
    }

    void Generator::EmitEnum(std::string &out, const TypeInfo &info) const
    {
        const std::string ns = NamespaceOf(info.wrapper);
        const std::string name = NameOf(info.wrapper);

        OpenNamespace(out, ns);
        out += "enum class " + name + " : " + info.en->underlying_type + "\n{\n";
        for (const CI::EnumElem &elem : info.en->elems)
            out += "    " + elem.name + " = " + std::to_string(elem.unsigned_value) + ",\n";
        out += "};\n";
        CloseNamespace(out, ns);
        out += "\n";
    }

    void Generator::EmitClassDecl(std::string &out, const TypeInfo &info)
    {
        const std::string ns = NamespaceOf(info.wrapper);
        const std::string name = NameOf(info.wrapper);
        const std::string c = info.c_name;
        const std::string field = "_p_" + c;

        // At most one wrapped base is supported: with two, each C pointer would
        // have to be tracked against the subobject it belongs to.
        const TypeInfo *base = nullptr;
        for (const auto &base_name : info.cls->inheritance_info.bases_direct_combined.Vec())
        {
            if (info.cls->inheritance_info.bases_direct_combined.Map().at(base_name))
                continue; // Virtual bases are not modelled.
            if (const TypeInfo *candidate = Find(base_name); candidate && candidate->wrapped)
            {
                base = candidate;
                break;
            }
        }
        const bool is_root = base == nullptr;

        OpenNamespace(out, ns);
        out += "class " + name;
        if (base)
            out += " : public " + base->wrapper;
        out += "\n{\n  public:\n";
        out += "    using CType = ::" + c + ";\n\n";

        out += "  protected:\n";
        out += "    CType *" + field + " = nullptr;\n";
        if (is_root)
        {
            // The owning flag lives once, in the root. Every level's destructor
            // clears it, so the most-derived one destroys and the bases do not.
            out += "    bool _owned = false;\n";
        }
        out += "\n  public:\n";

        // A default-constructed wrapper is an empty view, not an object:
        // allocating here would hide the C++ default constructor, which is
        // exposed as create() alongside the other constructors.
        out += "    " + name + "() noexcept = default;\n";

        out += "    " + name + "(::feather::detail::AdoptTag, CType *_p, bool _own) noexcept\n";
        if (base)
        {
            out += "        : " + base->wrapper + "(::feather::detail::AdoptTag{}, _p ? "
                + "::" + c + "_MutableUpcastTo_" + base->c_name + "(_p) : nullptr, _own),\n";
            out += "          " + field + "(_p) {}\n";
        }
        else
        {
            out += "        : " + field + "(_p) { _owned = _own; }\n";
        }

        out += "    static " + name + " _adopt(CType *_p) noexcept { return " + name + "(::feather::detail::AdoptTag{}, _p, true); }\n";
        out += "    static " + name + " _view(CType *_p) noexcept { return " + name + "(::feather::detail::AdoptTag{}, _p, false); }\n";
        out += "    CType *_c_ptr() const noexcept { return " + field + "; }\n";
        out += "    explicit operator bool() const noexcept { return " + field + " != nullptr; }\n";
        // Gives up ownership without destroying, for handing an object to
        // something that will free it.
        out += "    CType *_release() noexcept { _owned = false; CType *_p = " + field + "; " + field + " = nullptr; return _p; }\n\n";

        out += "    ~" + name + "()\n    {\n";
        out += "        if (_owned && " + field + ") { ::" + c + "_Destroy(" + field + "); _owned = false; }\n";
        out += "        " + field + " = nullptr;\n";
        out += "    }\n\n";

        out += "    " + name + "(" + name + " &&_o) noexcept\n";
        if (base)
            out += "        : " + base->wrapper + "(static_cast<" + base->wrapper + " &&>(_o)), " + field + "(_o." + field + ")\n";
        else
            out += "        : " + field + "(_o." + field + ")\n";
        out += "    {\n";
        if (is_root)
            out += "        _owned = _o._owned; _o._owned = false;\n";
        out += "        _o." + field + " = nullptr;\n";
        out += "    }\n\n";

        out += "    " + name + " &operator=(" + name + " &&_o) noexcept\n    {\n";
        out += "        if (this == &_o) return *this;\n";
        out += "        if (_owned && " + field + ") { ::" + c + "_Destroy(" + field + "); _owned = false; }\n";
        out += "        " + field + " = _o." + field + ";\n";
        out += "        _o." + field + " = nullptr;\n";
        if (base)
            out += "        " + base->wrapper + "::operator=(static_cast<" + base->wrapper + " &&>(_o));\n";
        else
            out += "        _owned = _o._owned; _o._owned = false;\n";
        out += "        return *this;\n    }\n\n";

        const bool copyable = info.traits && bool(info.traits->copy_constructible);
        if (copyable)
        {
            const bool pass_by_enum = info.cls->kind == CI::ClassKind::uses_pass_by_enum;
            const std::string copy_call = pass_by_enum
                ? "::" + c + "_ConstructFromAnother(::Feather_PassBy_Copy, const_cast<CType *>(_o." + field + "))"
                : "::" + c + "_ConstructFromAnother(_o." + field + ")";
            out += "    " + name + "(const " + name + " &_o)\n";
            out += "        : " + name + "(::feather::detail::AdoptTag{}, _o." + field + " ? " + copy_call + " : nullptr, true) {}\n";
            out += "    " + name + " &operator=(const " + name + " &_o)\n";
            out += "    {\n        if (this != &_o) *this = " + name + "(_o);\n        return *this;\n    }\n\n";
        }
        else
        {
            out += "    " + name + "(const " + name + " &) = delete;\n";
            out += "    " + name + " &operator=(const " + name + " &) = delete;\n\n";
        }

        std::string decls, bodies;
        emitted_signatures.clear();

        for (const CI::ClassMethod &method : info.cls->methods)
        {
            const std::string comment = DocComment(method.comment, method.comment_extra_lifetimes);
            bool ok = true;

            if (const auto *ctor = std::get_if<CI::MethodKinds::Constructor>(&method.var))
            {
                if (ctor->is_copying_ctor)
                    continue; // Already handled as the copy constructor.
                ok = EmitFuncLike(decls, bodies, method, "create", true, true, &info, comment);
            }
            else if (const auto *op = std::get_if<CI::MethodKinds::Operator>(&method.var))
            {
                if (op->is_copying_assignment || op->is_post_incr_or_decr || !IsSupportedOperator(op->token))
                    continue;
                ok = EmitFuncLike(decls, bodies, method, "operator" + op->token, method.is_static, false, &info, comment);
            }
            else if (const auto *regular = std::get_if<CI::MethodKinds::Regular>(&method.var))
            {
                ok = EmitFuncLike(decls, bodies, method, regular->name, method.is_static, false, &info, comment);
            }
            else
            {
                continue; // Conversion operators are reachable by their named accessors.
            }

            if (ok)
                num_emitted++;
            else
                num_skipped++;
        }

        for (const CI::ClassField &f : info.cls->fields)
        {
            if (f.getter_const)
            {
                if (EmitFuncLike(decls, bodies, *f.getter_const, f.name, f.getter_const->is_static, false, &info,
                        DocComment(f.getter_const->comment, f.getter_const->comment_extra_lifetimes)))
                    num_emitted++;
                else
                    num_skipped++;
            }
            if (f.setter)
            {
                if (EmitFuncLike(decls, bodies, *f.setter, "set_" + f.name, f.setter->is_static, false, &info,
                        DocComment(f.setter->comment, f.setter->comment_extra_lifetimes)))
                    num_emitted++;
                else
                    num_skipped++;
            }
        }

        out += decls;
        out += "};\n";
        CloseNamespace(out, ns);
        out += "\n";

        pending_bodies[info.cpp_name] = std::move(bodies);
    }

    void Generator::EmitFreeFunctions(std::string &out)
    {
        // Grouped by namespace, declarations before definitions, so one free
        // function may call another regardless of order.
        std::map<std::string, std::string> decls_by_ns;
        std::map<std::string, std::string> bodies_by_ns;
        emitted_signatures.clear();

        for (const CI::Function &func : desc->functions)
        {
            const auto *regular = std::get_if<CI::FuncKinds::Regular>(&func.var);
            if (!regular)
                continue; // Free operators are reachable as members where they matter.

            const std::string ns = NamespaceOf(regular->name);
            std::string decls, bodies;
            if (!EmitFuncLike(decls, bodies, func, NameOf(regular->name), true, false, nullptr,
                    DocComment(func.comment, func.comment_extra_lifetimes)))
            {
                num_skipped++;
                continue;
            }
            num_emitted++;
            decls_by_ns[ns] += decls;
            bodies_by_ns[ns] += bodies;
        }

        for (const auto &[ns, text] : decls_by_ns)
        {
            OpenNamespace(out, ns);
            out += text;
            CloseNamespace(out, ns);
            out += "\n";
        }
        for (const auto &[ns, text] : bodies_by_ns)
        {
            OpenNamespace(out, ns);
            out += text;
            CloseNamespace(out, ns);
            out += "\n";
        }
    }

    std::string Generator::CoreHeader() const
    {
        const bool have_string = Find("std::string") != nullptr;

        std::string out;
        out += "#pragma once\n\n";
        out += "// Runtime support for the generated wrappers: the ownership tag, string\n";
        out += "// conversion, and raising an engine-side failure on this side of the call.\n\n";
        out += "#include <feather_helpers/common.h>\n";
        if (have_string)
            out += "#include <feather_helpers/std_string.h>\n";
        out += "\n#include <cstdio>\n#include <exception>\n#include <stdexcept>\n#include <string>\n\n";

        out += "// Whether this translation unit can throw. The engine builds with\n";
        out += "// exceptions off, and a plugin may too, so nothing here throws\n";
        out += "// unconditionally.\n";
        out += "#if defined(__cpp_exceptions) || defined(_CPPUNWIND)\n";
        out += "#define FEATHER_CPP_EXCEPTIONS 1\n";
        out += "#else\n";
        out += "#define FEATHER_CPP_EXCEPTIONS 0\n";
        out += "#endif\n\n";

        out += "namespace feather\n{\n";
        out += "    // Carries what the engine reported. An exception cannot propagate\n";
        out += "    // through the bindings' extern \"C\" frames, so a failure is recorded\n";
        out += "    // there and raised again on this side of the call.\n";
        out += "    class Error : public std::runtime_error\n    {\n      public:\n        using std::runtime_error::runtime_error;\n    };\n\n";

        out += "    namespace detail\n    {\n";
        out += "        // Throws where the caller allows it, and otherwise ends the process\n";
        out += "        // the way the engine's own fassert does -- a failure here means the\n";
        out += "        // engine already refused the call, so returning is not an option.\n";
        out += "        [[noreturn]] inline void fail(const std::string &message)\n        {\n";
        out += "#if FEATHER_CPP_EXCEPTIONS\n";
        out += "            throw ::feather::Error(message);\n";
        out += "#else\n";
        out += "            std::fprintf(stderr, \"feather: %s\\n\", message.c_str());\n";
        out += "            std::terminate();\n";
        out += "#endif\n        }\n\n";
        out += "        struct AdoptTag { explicit AdoptTag() = default; };\n\n";
        out += "        inline bool &pending_flag() { static thread_local bool flag = false; return flag; }\n";
        out += "        inline std::string &pending_message() { static thread_local std::string message; return message; }\n\n";
        out += "        // Stores and returns rather than throwing: this runs inside an\n";
        out += "        // extern \"C\" frame, which must not be unwound through.\n";
        out += "        inline void store_exception(const char *message)\n        {\n";
        out += "            pending_flag() = true;\n";
        out += "            pending_message() = message ? message : \"unknown error\";\n        }\n\n";
        out += "        inline bool install_exception_handler()\n        {\n";
        out += "            static const bool installed = []{ Feather_SetSimpleExceptionHandler(&store_exception); return true; }();\n";
        out += "            return installed;\n        }\n\n";
        out += "        inline void rethrow_pending()\n        {\n";
        out += "            if (!pending_flag()) return;\n";
        out += "            pending_flag() = false;\n";
        out += "            fail(pending_message());\n        }\n    }\n\n";

        out += "    // The engine's fassert, on this side of the boundary: same report,\n";
        out += "    // same halt, without needing the engine's headers.\n";
        out += "    inline void assert_that(bool condition, const std::string &message)\n    {\n";
        out += "        if (!condition) detail::fail(message);\n    }\n\n";
        out += "    namespace detail\n    {\n";

        if (have_string)
        {
            out += "        inline std::string to_string(const Feather_std_string *s)\n        {\n";
            out += "            if (!s) return {};\n";
            out += "            return std::string(Feather_std_string_data(s), Feather_std_string_size(s));\n        }\n\n";
            out += "        // Takes over a string the callee allocated for us.\n";
            out += "        inline std::string take_string(Feather_std_string *s)\n        {\n";
            out += "            std::string ret = to_string(s);\n";
            out += "            Feather_std_string_Destroy(s);\n";
            out += "            return ret;\n        }\n\n";
        }

        out += "        // Copies out a heap-allocated value of a type the consumer defines\n";
        out += "        // itself, then releases the engine's copy.\n";
        out += "        template <typename T, typename C, typename F>\n";
        out += "        T take_native(C *p, F destroy)\n        {\n";
        out += "            T value = *reinterpret_cast<const T *>(p);\n";
        out += "            destroy(p);\n";
        out += "            return value;\n        }\n";
        out += "    }\n}\n";
        return out;
    }

    std::string Generator::MathHeader() const
    {
        std::string out;
        out += "#pragma once\n\n";
        out += "// The math types cross as themselves: a plugin compiles the same SimpleMath\n";
        out += "// sources the engine did, so the layouts agree by construction. The\n";
        out += "// assertions check that against what the engine published, making a\n";
        out += "// mismatch a compile error rather than silently reinterpreted memory.\n\n";
        for (const std::string &header : native_headers)
            out += "#include <" + header + ">\n";
        out += "\n#include <cstddef>\n#include <type_traits>\n\n";

        for (const auto &[cpp_name, native] : opts.native_types)
        {
            const TypeInfo *info = Find(cpp_name);
            if (!info || !info->cls)
                throw std::runtime_error("`--native-type " + cpp_name + "` is not a class in the descriptor.");

            if (info->cls->size_and_alignment)
            {
                out += "static_assert(sizeof(" + cpp_name + ") == " + std::to_string(info->cls->size_and_alignment->size)
                    + " && alignof(" + cpp_name + ") == " + std::to_string(info->cls->size_and_alignment->alignment)
                    + ", \"" + cpp_name + " does not match the layout the engine published.\");\n";
                for (const CI::ClassField &f : info->cls->fields)
                {
                    if (!f.layout)
                        continue;
                    out += "static_assert(offsetof(" + cpp_name + ", " + f.name + ") == "
                        + std::to_string(f.layout->byte_offset) + ", \"" + cpp_name + "::" + f.name
                        + " is not where the engine published it.\");\n";
                }
            }
            else
            {
                // An opaque native has no published layout, so only the property
                // its pointer casts rely on can be checked.
                out += "static_assert(std::is_trivially_copyable_v<" + cpp_name + ">, \""
                    + cpp_name + " must be trivially copyable to cross as a pointer.\");\n";
            }
        }

        if (!opts.native_alias_namespace.empty())
        {
            out += "\nnamespace " + opts.native_alias_namespace + "\n{\n";
            for (const auto &[cpp_name, native] : opts.native_types)
                out += "    using " + NameOf(cpp_name) + " = " + cpp_name + ";\n";
            out += "}\n";
        }
        return out;
    }

    std::string Generator::MainHeader()
    {
        std::string out;
        out += "#pragma once\n\n";
        out += "// C++ wrappers over the engine's C bindings, generated by feather_gen_cpp\n";
        out += "// from the descriptor mrbind_gen_c published. Everything here resolves to\n";
        out += "// a feather_* C symbol, so a plugin built against it needs no engine\n";
        out += "// headers and shares no C++ ABI with the engine.\n\n";

        out += "#include <" + opts.cpp_dir_prefix + "/core.hpp>\n";
        if (!opts.native_types.empty())
            out += "#include <" + opts.cpp_dir_prefix + "/math.hpp>\n";
        for (const std::string &header : c_headers)
            out += "#include <" + header + ".h>\n";
        out += "\n#include <bit>\n#include <string>\n#include <string_view>\n#include <utility>\n\n";

        // Enums first: a class may take one by value.
        for (const auto &[cpp_name, info] : types)
        {
            if (info.cat == Cat::enum_ && !info.wrapper.empty())
                EmitEnum(out, info);
        }

        // Forward declarations first: a method may name any other wrapper, and
        // a declaration only needs the name.
        {
            std::map<std::string, std::string> fwd_by_ns;
            for (const TypeInfo *info : class_order)
                fwd_by_ns[NamespaceOf(info->wrapper)] += "    class " + NameOf(info->wrapper) + ";\n";
            for (const auto &[ns, text] : fwd_by_ns)
            {
                OpenNamespace(out, ns);
                out += text;
                CloseNamespace(out, ns);
            }
            out += "\n";
        }

        // Then every wrapper class, bases before derived, declarations only.
        for (const TypeInfo *info : class_order)
            EmitClassDecl(out, *info);

        out += "// Method definitions, after every class above is complete: a method may\n";
        out += "// return another wrapper by value.\n\n";
        for (const TypeInfo *info : class_order)
        {
            auto iter = pending_bodies.find(info->cpp_name);
            if (iter != pending_bodies.end())
                out += iter->second;
        }

        EmitFreeFunctions(out);
        return out;
    }

    void Generator::Run()
    {
        CollectTypes();
        OrderClasses();

        // Every generated C header that declares something we wrap.
        for (const auto &[cpp_name, info] : types)
        {
            if (info.cat == Cat::class_ && info.cls)
                c_headers.insert(info.cls->output_file.relative_name);
            else if (info.cat == Cat::enum_ && info.en)
                c_headers.insert(info.en->output_file.relative_name);
        }
        for (const CI::Function &func : desc->functions)
            c_headers.insert(func.output_file.relative_name);

        const std::filesystem::path dir = mrbind::MakePath(opts.output_dir) / mrbind::MakePath(opts.cpp_dir_prefix);
        std::filesystem::create_directories(dir);

        const std::string main_header = MainHeader();

        auto write = [&](const std::string &name, const std::string &contents)
        {
            const std::filesystem::path path = dir / mrbind::MakePath(name);
            std::ofstream out(path);
            if (!out)
                throw std::runtime_error("Failed to open for writing: " + mrbind::PathToString(path));
            out << contents;
            if (!out)
                throw std::runtime_error("Failed to write: " + mrbind::PathToString(path));
        };

        write("core.hpp", CoreHeader());
        if (!opts.native_types.empty())
            write("math.hpp", MathHeader());
        write("feather.hpp", main_header);
    }
}
