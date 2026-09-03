#include "generator.h"

#include "common/filesystem.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace feather_gen
{
    namespace
    {
        [[nodiscard]] std::string Replace(std::string subject, std::string_view from, std::string_view to)
        {
            for (std::size_t pos = subject.find(from); pos != std::string::npos; pos = subject.find(from, pos + to.size()))
                subject.replace(pos, from.size(), to);
            return subject;
        }

        [[nodiscard]] bool StartsWith(std::string_view s, std::string_view p) {return s.size() >= p.size() && s.substr(0, p.size()) == p;}

        // C++ keywords a parameter name could collide with. The C++ side of a
        // binding may legally be named e.g. `operator`, the C side never is.
        [[nodiscard]] std::string SafeName(std::string name)
        {
            static const std::set<std::string> keywords = {
                "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class",
                "const", "constexpr", "continue", "default", "delete", "do", "double", "else", "enum", "explicit",
                "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
                "mutable", "namespace", "new", "not", "nullptr", "operator", "or", "private", "protected", "public",
                "register", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template", "this",
                "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual", "void",
                "volatile", "while", "xor",
            };
            if (name.empty())
                return "_arg";
            if (keywords.contains(name))
                return name + "_";
            return name;
        }

        // The last `::`-separated component of a qualified name.
        [[nodiscard]] std::string_view LastComponent(std::string_view name)
        {
            const std::size_t pos = name.rfind("::");
            return pos == std::string_view::npos ? name : name.substr(pos + 2);
        }
    }

    ParsedType ParseType(std::string_view spelling)
    {
        ParsedType ret;
        std::string s(spelling);

        // Trim, then peel the indirection off the right and the qualifier off
        // the left. Anything left over is the type's own name.
        auto trim = [](std::string &v)
        {
            while (!v.empty() && v.back() == ' ') v.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
        };

        trim(s);
        if (s.ends_with("&&"))
        {
            ret.form = ParsedType::Form::rref;
            s.resize(s.size() - 2);
        }
        else if (s.ends_with('&'))
        {
            ret.form = ParsedType::Form::lref;
            s.resize(s.size() - 1);
        }
        else if (s.ends_with('*'))
        {
            ret.form = ParsedType::Form::ptr;
            s.resize(s.size() - 1);
        }
        trim(s);

        if (StartsWith(s, "const "))
        {
            ret.is_const = true;
            s.erase(0, 6);
        }
        trim(s);

        ret.base = std::move(s);
        return ret;
    }

    const TypeInfo *Generator::Find(std::string_view cpp_name) const
    {
        auto iter = types.find(cpp_name);
        return iter == types.end() ? nullptr : &iter->second;
    }

    void Generator::CollectTypes()
    {
        for (const auto &[cpp_name, type_desc] : desc->cpp_types.Map())
        {
            TypeInfo info;
            info.cpp_name = cpp_name;
            info.traits = type_desc.traits ? &*type_desc.traits : nullptr;

            if (const auto *cl = std::get_if<CI::TypeKinds::Class>(&type_desc.var))
            {
                info.cat = Cat::class_;
                info.cls = cl;
                info.c_name = cl->c_name;
                info.exposed = cl->kind == CI::ClassKind::exposed_struct;
            }
            else if (const auto *en = std::get_if<CI::TypeKinds::Enum>(&type_desc.var))
            {
                info.cat = Cat::enum_;
                info.en = en;
                info.c_name = en->c_name;
            }
            else if (std::holds_alternative<CI::TypeKinds::Arithmetic>(type_desc.var))
            {
                info.cat = Cat::arithmetic;
            }
            else if (std::holds_alternative<CI::TypeKinds::Void>(type_desc.var))
            {
                info.cat = Cat::void_;
            }

            if (auto native = opts.native_types.find(cpp_name); native != opts.native_types.end())
            {
                if (info.cat != Cat::class_)
                    throw std::runtime_error("`--native-type " + cpp_name + "` names something that is not a class in the descriptor.");
                info.native = true;
                info.wrapper = cpp_name;
                native_headers.insert(native->second.header);
            }

            types.emplace(cpp_name, std::move(info));
        }

        // Wrapper names, in a second pass so every type is present first.
        for (auto &[cpp_name, info] : types)
        {
            if (info.cat == Cat::enum_)
            {
                // Enums keep their C++ nesting flattened, same as classes.
                info.wrapper = Replace(cpp_name, "::", "_");
                const std::size_t pos = cpp_name.find("::");
                if (pos != std::string::npos)
                    info.wrapper = cpp_name.substr(0, pos) + "::" + Replace(cpp_name.substr(pos + 2), "::", "_");
                continue;
            }

            if (info.cat != Cat::class_ || info.native)
                continue;

            // Reflection plumbing: a nested type whose name starts with an
            // underscore is an implementation detail of the FCLASS macro.
            if (cpp_name.find("::_") != std::string::npos)
                continue;

            if (info.exposed)
                continue; // Mirror structs are not implemented; only natives are exposed today.

            // Without a destructor the C bindings emit no _Destroy, so a
            // wrapper could neither own nor release one. Happens for a class
            // admitted only as somebody's base.
            if (!info.traits || !bool(info.traits->destructible))
                continue;

            const bool is_template = cpp_name.find('<') != std::string::npos;

            if (!is_template && (StartsWith(cpp_name, "feather::") || StartsWith(cpp_name, "nassimp::")))
            {
                const std::size_t pos = cpp_name.find("::");
                info.wrapper = cpp_name.substr(0, pos) + "::" + Replace(cpp_name.substr(pos + 2), "::", "_");
            }
            else
            {
                // Template instantiations and the std:: helpers have no name
                // that is both unique and spellable as a class; the C name
                // already is one, so it stands in for them.
                info.wrapper = "feather::c::" + info.c_name;
            }
            info.wrapped = true;
        }
    }

    void Generator::OrderClasses()
    {
        std::set<std::string> done;

        // Depth-first over direct bases, so a class is emitted after every base
        // it inherits from.
        auto visit = [&](auto &&self, const TypeInfo &info) -> void
        {
            if (!done.insert(info.cpp_name).second)
                return;

            for (const auto &base_name : info.cls->inheritance_info.bases_direct_combined.Vec())
            {
                if (const TypeInfo *base = Find(base_name); base && base->wrapped)
                    self(self, *base);
            }
            class_order.push_back(&info);
        };

        for (const auto &[cpp_name, info] : types)
        {
            if (info.wrapped)
                visit(visit, info);
        }
    }

    ParamBinding Generator::BindParam(const CI::FuncParam &param, std::size_t index) const
    {
        ParamBinding ret;
        struct Note
        {
            const Generator &g; const std::string &t; const ParamBinding &r;
            ~Note() { if (!r.ok) g.skip_reasons["param " + t]++; }
        } note{*this, param.cpp_type, ret};
        const std::string name = SafeName(param.name_or_placeholder.empty()
            ? "_arg" + std::to_string(index) : param.name_or_placeholder);
        const ParsedType type = ParseType(param.cpp_type);

        // A default argument that changes how the parameter is passed needs
        // the pass-by enum spelled out; not supported yet.
        if (param.default_arg_affects_parameter_passing)
            return ret;
        if (param.is_array_pointer)
            return ret;

        // Strings arrive as a (begin, end) pair. `uses_sugar` marks the
        // parameters whose C form is not the plain one for their type.
        if (param.uses_sugar && (type.base == "std::string_view" || type.base == "std::string"))
        {
            ret.ok = true;
            ret.decl = "std::string_view " + name;
            ret.arg = name + ".data(), " + name + ".data() + " + name + ".size()";
            return ret;
        }
        if (param.uses_sugar)
            return ret;

        if (type.base == "char" && type.form == ParsedType::Form::ptr && type.is_const)
        {
            ret.ok = true;
            ret.decl = "const char *" + name;
            ret.arg = name;
            return ret;
        }

        const TypeInfo *info = Find(type.base);
        if (!info)
            return ret;

        if (info->cat == Cat::arithmetic)
        {
            if (type.form == ParsedType::Form::rref)
                return ret;
            ret.ok = true;
            // Taken by value either way: that gives an addressable lvalue for
            // the forms the C side spells as a pointer.
            ret.decl = type.base + " " + name;
            ret.arg = type.IsIndirect() ? "&" + name : name;
            return ret;
        }

        if (info->cat == Cat::enum_)
        {
            if (type.IsIndirect())
                return ret;
            ret.ok = true;
            ret.decl = info->wrapper + " " + name;
            ret.arg = "static_cast<::" + info->c_name + ">(" + name + ")";
            return ret;
        }

        if (info->cat != Cat::class_)
            return ret;

        // A type the consumer defines itself: same layout by construction, so
        // it crosses as itself or as a pointer to itself.
        if (info->native || info->exposed)
        {
            if (!info->native)
                return ret;

            if (type.IsIndirect())
            {
                if (type.form == ParsedType::Form::rref)
                    return ret;
                ret.ok = true;
                ret.decl = (type.is_const ? "const " : "") + info->wrapper + " &" + name;
                ret.arg = "reinterpret_cast<" + (type.is_const ? std::string("const ") : std::string())
                    + "::" + info->c_name + " *>(&" + name + ")";
                return ret;
            }

            // By value. An exposed struct is passed as itself, an opaque one
            // through a pointer to a copy the callee reads.
            ret.ok = true;
            ret.decl = "const " + info->wrapper + " &" + name;
            if (info->exposed)
                ret.arg = "std::bit_cast<::" + info->c_name + ">(" + name + ")";
            else
                ret.arg = "reinterpret_cast<const ::" + info->c_name + " *>(&" + name + ")";
            return ret;
        }

        if (!info->wrapped)
            return ret;

        // Wrapped classes always cross as a pointer. By value additionally
        // needs the pass-by enum when the C side asks for one.
        if (type.IsIndirect())
        {
            if (type.form == ParsedType::Form::rref)
                return ret;
            ret.ok = true;
            ret.decl = (type.is_const ? "const " : "") + info->wrapper + " &" + name;
            ret.arg = name + "._c_ptr()";
            return ret;
        }

        ret.ok = true;
        ret.decl = "const " + info->wrapper + " &" + name;
        if (info->cls->kind == CI::ClassKind::uses_pass_by_enum)
            ret.arg = "::Feather_PassBy_Copy, const_cast<::" + info->c_name + " *>(" + name + "._c_ptr())";
        else
            ret.arg = name + "._c_ptr()";
        return ret;
    }

    ReturnBinding Generator::BindReturn(const CI::FuncReturn &ret_desc) const
    {
        ReturnBinding ret;
        struct Note
        {
            const Generator &g; const std::string &t; const ReturnBinding &r;
            ~Note() { if (!r.ok) g.skip_reasons["return " + t]++; }
        } note{*this, ret_desc.cpp_type, ret};
        const ParsedType type = ParseType(ret_desc.cpp_type);

        if (ret_desc.is_array_pointer)
            return ret;

        if (type.base == "void" && !type.IsIndirect())
        {
            ret.ok = true;
            ret.is_void = true;
            ret.type = "void";
            return ret;
        }

        // An owned string helper is copied out and released; a borrowed one is
        // only copied.
        if (ret_desc.uses_sugar && (type.base == "std::string" || type.base == "std::string_view"))
        {
            ret.ok = true;
            ret.type = "std::string";
            ret.expr = type.IsIndirect()
                ? "::feather::detail::to_string($)"
                : "::feather::detail::take_string($)";
            return ret;
        }
        if (type.base == "char" && type.form == ParsedType::Form::ptr && type.is_const)
        {
            ret.ok = true;
            ret.type = "const char *";
            return ret;
        }
        if (ret_desc.uses_sugar)
            return ret;

        const TypeInfo *info = Find(type.base);
        if (!info)
            return ret;

        if (info->cat == Cat::arithmetic)
        {
            if (type.form == ParsedType::Form::rref)
                return ret;
            ret.ok = true;
            ret.type = type.base;
            if (type.IsIndirect())
                ret.expr = "*$"; // Copied out: a reference into engine memory has no lifetime here.
            return ret;
        }

        if (info->cat == Cat::enum_)
        {
            if (type.form == ParsedType::Form::rref)
                return ret;
            ret.ok = true;
            ret.type = info->wrapper;
            ret.expr = type.IsIndirect()
                ? "static_cast<" + info->wrapper + ">(*$)"
                : "static_cast<" + info->wrapper + ">($)";
            return ret;
        }

        if (info->cat != Cat::class_)
            return ret;

        if (info->native)
        {
            ret.type = info->wrapper;
            if (type.IsIndirect())
            {
                // A borrowed pointer into engine memory; copied out, so the
                // caller never holds a reference whose lifetime it cannot see.
                ret.ok = true;
                ret.expr = "*reinterpret_cast<const " + info->wrapper + " *>($)";
                return ret;
            }
            ret.ok = true;
            if (info->exposed)
                ret.expr = "std::bit_cast<" + info->wrapper + ">($)";
            else
                // Returned by value means a heap copy owned by the caller.
                ret.expr = "::feather::detail::take_native<" + info->wrapper + ", ::" + info->c_name
                    + ">($, ::" + info->c_name + "_Destroy)";
            return ret;
        }

        if (!info->wrapped)
            return ret;

        ret.type = info->wrapper;
        // By value is a heap object the caller owns; by reference or pointer is
        // a borrowed view into something the engine still owns.
        ret.expr = type.IsIndirect()
            ? info->wrapper + "::_view($)"
            : info->wrapper + "::_adopt($)";
        if (type.is_const && type.IsIndirect())
            ret.expr = info->wrapper + "::_view(const_cast<::" + info->c_name + " *>($))";
        ret.ok = true;
        return ret;
    }

    bool Generator::EmitFuncLike(
        std::string &decls, std::string &bodies,
        const CI::BasicFuncLike &func, const std::string &wrapper_name,
        bool is_static, bool is_ctor, const TypeInfo *owner,
        const std::string &comment) const
    {
        std::vector<std::string> param_decls;
        std::vector<std::string> args;
        std::string pre, post;
        bool is_const_method = false;
        bool have_this = false;

        for (std::size_t i = 0; i < func.params.size(); i++)
        {
            const CI::FuncParam &param = func.params[i];

            if (param.is_this_param)
            {
                // Static functions and constructors carry a fake `this` that
                // exists only to keep the C generator's own checks happy.
                if (is_static)
                    continue;
                have_this = true;
                is_const_method = ParseType(param.cpp_type).is_const;
                args.push_back("_c_ptr()");
                continue;
            }

            const ParamBinding bound = BindParam(param, i);
            if (!bound.ok)
                return false;
            param_decls.push_back(bound.decl);
            args.push_back(bound.arg);
            pre += bound.pre;
            post += bound.post;
        }

        std::string ret_type;
        std::string ret_expr;
        bool is_void = false;

        if (is_ctor)
        {
            ret_type = "";
        }
        else
        {
            const ReturnBinding bound = BindReturn(func.ret);
            if (!bound.ok)
                return false;
            ret_type = bound.type;
            ret_expr = bound.expr;
            is_void = bound.is_void;
        }

        std::string param_list;
        for (std::size_t i = 0; i < param_decls.size(); i++)
            param_list += (i ? ", " : "") + param_decls[i];

        std::string arg_list;
        for (std::size_t i = 0; i < args.size(); i++)
            arg_list += (i ? ", " : "") + args[i];

        const std::string qualifier = (have_this && is_const_method) ? " const" : "";
        const std::string call = "::" + func.c_name + "(" + arg_list + ")";

        // Copying inherited members into a derived class can produce a member
        // the derived class also declares itself; C++ needs one of them.
        if (!emitted_signatures.insert(wrapper_name + "(" + param_list + ")" + qualifier).second)
            return true;

        // Declaration, inside the class body or, for a free function, in its
        // namespace -- where it needs `inline` rather than `static`.
        decls += comment;
        if (is_ctor)
        {
            decls += "    static " + owner->wrapper + " " + wrapper_name + "(" + param_list + ");\n";
        }
        else
        {
            decls += "    ";
            if (!owner)
                decls += "inline ";
            else if (is_static)
                decls += "static ";
            decls += ret_type + " " + wrapper_name + "(" + param_list + ")" + qualifier + ";\n";
        }

        // Definition, after every class is complete.
        const std::string scope = owner ? owner->wrapper + "::" : std::string();
        bodies += "inline ";
        if (is_ctor)
            bodies += owner->wrapper + " " + scope + wrapper_name + "(" + param_list + ")";
        else
            bodies += ret_type + " " + scope + wrapper_name + "(" + param_list + ")" + qualifier;
        bodies += "\n{\n";
        bodies += pre;

        if (is_ctor)
        {
            bodies += "    auto *_result = " + call + ";\n";
            bodies += post;
            bodies += "    ::feather::detail::rethrow_pending();\n";
            bodies += "    return " + owner->wrapper + "::_adopt(_result);\n";
        }
        else if (is_void)
        {
            bodies += "    " + call + ";\n";
            bodies += post;
            bodies += "    ::feather::detail::rethrow_pending();\n";
        }
        else if (post.empty())
        {
            bodies += "    auto _result = " + call + ";\n";
            bodies += "    ::feather::detail::rethrow_pending();\n";
            bodies += "    return " + Replace(ret_expr, "$", "_result") + ";\n";
        }
        else
        {
            bodies += "    auto _result = " + call + ";\n";
            bodies += post;
            bodies += "    ::feather::detail::rethrow_pending();\n";
            bodies += "    return " + Replace(ret_expr, "$", "_result") + ";\n";
        }
        bodies += "}\n\n";
        return true;
    }
}
