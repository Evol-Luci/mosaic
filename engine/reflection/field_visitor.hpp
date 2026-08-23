#pragma once

#include <string>
#include <string_view>
#include <type_traits>

#include "foundation/core/entity_id.hpp"
#include "foundation/reflection/reflect.hpp"
#include "foundation/serialization/reference.hpp" // AssetID, Handle

namespace mosaic::engine::reflection
{

// A visitor over a reflected object's fields. `field_float` gets a mutable reference (the
// one editable leaf); everything else is presented read-only via a formatted string.
// Nested reflectables (Vec3, Quat, ...) are bracketed by begin_group/end_group. No ImGui,
// no studio dependency — this is the shared reflected-property surface.
class FieldVisitor
{
public:
    virtual ~FieldVisitor() = default;
    virtual void field_float(std::string_view name, float& value) = 0;
    virtual void field_readonly(std::string_view name, const std::string& display) = 0;
    virtual void begin_group(std::string_view name) = 0;
    virtual void end_group() = 0;
};

// std::vector<mosaic::foundation::serialization::Handle<R>> detection, kept local so this
// header needs only reference.hpp (not the serialization value machinery).
template <class> inline constexpr bool is_handle_v = false;
template <class R>
inline constexpr bool is_handle_v<mosaic::foundation::serialization::Handle<R>> = true;

// Bridges reflect()'s templated member(name, field) to the virtual FieldVisitor. The
// if constexpr ladder mirrors serialization's encode(); every serialization-supported
// field type maps to a visitor call, so the adapter is total (no compile error on an
// unhandled field — it renders read-only).
struct FieldVisitorAdapter
{
    FieldVisitor& visitor;

    template <class M>
    void member(std::string_view name, M& value)
    {
        namespace ser = mosaic::foundation::serialization;
        using U = std::remove_cv_t<M>;
        if constexpr (std::is_same_v<U, float>)
        {
            visitor.field_float(name, value);
        }
        else if constexpr (std::is_same_v<U, mosaic::foundation::EntityID>)
        {
            visitor.field_readonly(name, std::to_string(value.value()));
        }
        else if constexpr (std::is_same_v<U, ser::AssetID>)
        {
            visitor.field_readonly(name, value.to_string());
        }
        else if constexpr (is_handle_v<U>)
        {
            visitor.field_readonly(name, value.asset_id().to_string());
        }
        else if constexpr (std::is_same_v<U, bool>)
        {
            visitor.field_readonly(name, value ? "true" : "false");
        }
        else if constexpr (std::is_same_v<U, std::string>)
        {
            visitor.field_readonly(name, value);
        }
        else if constexpr (std::is_arithmetic_v<U>)
        {
            visitor.field_readonly(name, std::to_string(value));
        }
        else if constexpr (mosaic::foundation::reflection::Reflectable<U>)
        {
            visitor.begin_group(name);
            reflect(*this, value);
            visitor.end_group();
        }
        else
        {
            visitor.field_readonly(name, "<unsupported>");
        }
    }
};

} // namespace mosaic::engine::reflection
