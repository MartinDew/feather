#pragma once

// Defining ECS components and systems from a plugin.
//
// Hand-written, not generated: core/world/scripted_abi.h carries FEATHER_NO_BIND
// because mrbind has no spelling for a function pointer, so the flat C entry
// points are redeclared here -- the same arrangement the C# bootstrap uses.
// What this adds over them is only ergonomics: std types, real enums, and a
// failure that raises itself instead of a buffer the caller has to check.

#include <feather_cpp/core.hpp>
#include <feather_cpp/math.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

extern "C"
{
    // Mirrors core/world/scripted_abi.h. Values cross as doubles, however many
    // a field needs: one for bool/int/float, two for a vec2, three for a vec3,
    // four for a color.
    typedef void (*FeatherScriptSystemFn)(
        void *user_data, std::uint64_t entity, void *const *components,
        std::int32_t component_count, double delta_time);

    std::uint64_t feather_script_define_component(
        const char *name, std::int32_t field_count, const char *const *field_names,
        const std::uint8_t *field_types, char *error, std::int32_t error_size);

    std::uint64_t feather_script_define_system(
        const char *name, std::int32_t component_count, const char *const *component_names,
        std::uint8_t phase, FeatherScriptSystemFn callback, void *user_data,
        char *error, std::int32_t error_size);

    std::uint64_t feather_script_create_entity(const char *name);
    std::int32_t feather_script_add_component(std::uint64_t entity, const char *component_name);
    void *feather_script_component_handle(std::uint64_t entity, const char *component_name);
    std::int32_t feather_script_field_count(const char *component_name);
    std::int32_t feather_script_field_info(
        const char *component_name, std::int32_t index, const char **out_name, std::uint8_t *out_type);
    std::int32_t feather_script_get_field(
        void *component_handle, const char *component_name, const char *field_name,
        double *values, std::int32_t max_values, std::int32_t *out_count);
    std::int32_t feather_script_set_field(
        void *component_handle, const char *component_name, const char *field_name,
        const double *values, std::int32_t count);
}

namespace feather::ecs
{
    // Only trivially-copyable types: a scripted component lives in flecs'
    // zero-initialized storage, which runs no destructor.
    enum class FieldType : std::uint8_t
    {
        Bool = 0, Int = 1, Float = 2, Vec2 = 3, Vec3 = 4, Color = 5,
    };

    enum class Phase : std::uint8_t
    {
        OnLoad = 0, PostLoad = 1, PreUpdate = 2, OnUpdate = 3,
        OnValidate = 4, PostUpdate = 5, PreStore = 6, OnStore = 7,
    };

    struct Field
    {
        std::string name;
        FieldType type = FieldType::Float;
    };

    // One component of one entity. Inside a system callback it is valid only
    // for that call; obtained from view() it is valid until the entity's
    // archetype changes, so it is meant to be used and dropped.
    class ComponentView
    {
        void *_handle = nullptr;
        const char *_component = nullptr;

      public:
        ComponentView() = default;
        ComponentView(void *handle, const char *component) noexcept
            : _handle(handle), _component(component) {}

        [[nodiscard]] explicit operator bool() const noexcept { return _handle != nullptr; }
        [[nodiscard]] void *handle() const noexcept { return _handle; }

        [[nodiscard]] bool get_bool(std::string_view field) const { return _read(field, 1)[0] != 0.0; }
        [[nodiscard]] std::int32_t get_int(std::string_view field) const { return static_cast<std::int32_t>(_read(field, 1)[0]); }
        [[nodiscard]] float get_float(std::string_view field) const { return static_cast<float>(_read(field, 1)[0]); }

        [[nodiscard]] Vector2 get_vec2(std::string_view field) const
        {
            const auto v = _read(field, 2);
            return Vector2(static_cast<float>(v[0]), static_cast<float>(v[1]));
        }
        [[nodiscard]] Vector3 get_vec3(std::string_view field) const
        {
            const auto v = _read(field, 3);
            return Vector3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
        }
        [[nodiscard]] Color get_color(std::string_view field) const
        {
            const auto v = _read(field, 4);
            return Color(static_cast<float>(v[0]), static_cast<float>(v[1]),
                static_cast<float>(v[2]), static_cast<float>(v[3]));
        }

        void set(std::string_view field, bool value) const { const double v[] = {value ? 1.0 : 0.0}; _write(field, v, 1); }
        void set(std::string_view field, std::int32_t value) const { const double v[] = {double(value)}; _write(field, v, 1); }
        void set(std::string_view field, float value) const { const double v[] = {double(value)}; _write(field, v, 1); }
        void set(std::string_view field, const Vector2 &value) const { const double v[] = {value.x, value.y}; _write(field, v, 2); }
        void set(std::string_view field, const Vector3 &value) const { const double v[] = {value.x, value.y, value.z}; _write(field, v, 3); }
        void set(std::string_view field, const Color &value) const { const double v[] = {value.x, value.y, value.z, value.w}; _write(field, v, 4); }

      private:
        [[noreturn]] static void detail_fail(const std::string &message) { ::feather::detail::fail(message); }

        [[nodiscard]] std::array<double, 4> _read(std::string_view field, std::int32_t expected) const
        {
            std::array<double, 4> values{};
            std::int32_t count = 0;
            const std::string name(field);
            if (!::feather_script_get_field(_handle, _component, name.c_str(), values.data(), 4, &count))
                detail_fail("cannot read field '" + name + "' of component '" + _component + "'");
            if (count != expected)
                detail_fail("field '" + name + "' of component '" + _component + "' is not the type it was read as");
            return values;
        }

        void _write(std::string_view field, const double *values, std::int32_t count) const
        {
            const std::string name(field);
            if (!::feather_script_set_field(_handle, _component, name.c_str(), values, count))
                detail_fail("cannot write field '" + name + "' of component '" + _component + "'");
        }
    };

    // What a system is handed for one matching entity. The views are valid only
    // for the duration of the call.
    struct Invocation
    {
        std::uint64_t entity = 0;
        std::span<const ComponentView> components;
        double delta_time = 0.0;
    };

    using SystemCallback = std::function<void(const Invocation &)>;

    namespace detail
    {
        // The engine keeps the callback's address for the life of the process
        // and never frees it, so it is deliberately leaked rather than owned by
        // something whose destruction order we cannot see.
        struct SystemState
        {
            SystemCallback callback;
            std::vector<std::string> component_names;
        };

        inline void system_trampoline(
            void *user_data, std::uint64_t entity, void *const *components,
            std::int32_t component_count, double delta_time)
        {
            auto *state = static_cast<SystemState *>(user_data);

            std::vector<ComponentView> views;
            views.reserve(std::size_t(component_count));
            for (std::int32_t i = 0; i < component_count; i++)
                views.emplace_back(components[i], state->component_names[std::size_t(i)].c_str());

            const Invocation invocation{
                .entity = entity,
                .components = views,
                .delta_time = delta_time,
            };

            // Nothing may escape into the engine's C frame above us. With
            // exceptions off there is nothing that could.
#if FEATHER_CPP_EXCEPTIONS
            try
            {
                state->callback(invocation);
            }
            catch (...)
            {
            }
#else
            state->callback(invocation);
#endif
        }
    }

    // Registers a new component type. Throws feather::Error with the engine's
    // own message if the name is taken or a field type cannot be stored.
    inline std::uint64_t define_component(std::string_view name, std::span<const Field> fields)
    {
        std::vector<std::string> owned_names;
        std::vector<const char *> name_ptrs;
        std::vector<std::uint8_t> types;
        owned_names.reserve(fields.size());
        for (const Field &f : fields)
            owned_names.push_back(f.name);
        for (const std::string &n : owned_names)
            name_ptrs.push_back(n.c_str());
        for (const Field &f : fields)
            types.push_back(static_cast<std::uint8_t>(f.type));

        char error[512] = {};
        const std::string owned(name);
        const std::uint64_t id = ::feather_script_define_component(
            owned.c_str(), std::int32_t(fields.size()), name_ptrs.data(), types.data(), error, sizeof(error));
        if (id == 0)
            ::feather::detail::fail(error[0] ? error : "cannot define component '" + owned + "'");
        return id;
    }

    // Registers a system over the named components, scripted or built in.
    inline std::uint64_t define_system(
        std::string_view name, std::span<const std::string> components, Phase phase, SystemCallback callback)
    {
        auto *state = new detail::SystemState{
            .callback = std::move(callback),
            .component_names = std::vector<std::string>(components.begin(), components.end()),
        };

        std::vector<const char *> name_ptrs;
        name_ptrs.reserve(state->component_names.size());
        for (const std::string &n : state->component_names)
            name_ptrs.push_back(n.c_str());

        char error[512] = {};
        const std::string owned(name);
        const std::uint64_t id = ::feather_script_define_system(
            owned.c_str(), std::int32_t(name_ptrs.size()), name_ptrs.data(),
            static_cast<std::uint8_t>(phase), &detail::system_trampoline, state, error, sizeof(error));
        if (id == 0)
        {
            delete state;
            ::feather::detail::fail(error[0] ? error : "cannot define system '" + owned + "'");
        }
        return id;
    }

    inline std::uint64_t create_entity(std::string_view name = {})
    {
        const std::string owned(name);
        const std::uint64_t entity = ::feather_script_create_entity(owned.empty() ? nullptr : owned.c_str());
        if (entity == 0)
            ::feather::detail::fail("cannot create entity '" + owned + "'");
        return entity;
    }

    inline void add_component(std::uint64_t entity, std::string_view component)
    {
        const std::string owned(component);
        if (!::feather_script_add_component(entity, owned.c_str()))
            ::feather::detail::fail("cannot add component '" + owned + "' to the entity");
    }

    // A handle to one component of a live entity. Empty if the entity does not
    // have that component.
    [[nodiscard]] inline ComponentView view(std::uint64_t entity, const char *component)
    {
        return ComponentView(::feather_script_component_handle(entity, component), component);
    }

    // The fields a component was registered with, in order.
    [[nodiscard]] inline std::vector<Field> fields_of(const char *component)
    {
        const std::int32_t count = ::feather_script_field_count(component);
        if (count < 0)
            ::feather::detail::fail(std::string("no component named '") + component + "'");

        std::vector<Field> ret;
        ret.reserve(std::size_t(count));
        for (std::int32_t i = 0; i < count; i++)
        {
            const char *field_name = nullptr;
            std::uint8_t type = 0;
            if (!::feather_script_field_info(component, i, &field_name, &type))
                ::feather::detail::fail(std::string("cannot describe field ") + std::to_string(i) + " of '" + component + "'");
            ret.push_back(Field{.name = field_name ? field_name : "", .type = static_cast<FieldType>(type)});
        }
        return ret;
    }
}
