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

    // A reference-to-asset field. The visitor may reassign it (e.g. from an asset
    // browser). Default falls back to the read-only display so existing visitors keep
    // compiling.
    virtual void field_asset(std::string_view name,
                             mosaic::foundation::serialization::AssetID& value)
    {
        field_readonly(name, value.to_string());
    }

    // An editable free-text field. The visitor may reassign it (e.g. from a text
    // input widget). Default falls back to the read-only display so existing
    // visitors keep compiling.
    virtual void field_string(std::string_view name, std::string& value)
    {
        field_readonly(name, value);
    }

    // An editable integer field. The visitor may reassign it (e.g. from a drag widget).
    // Default falls back to the read-only display so existing visitors keep compiling --
    // and the fallback renders exactly what the generic arithmetic branch below used to, so
    // nothing that does not override this changes behaviour.
    //
    // `int` only, deliberately. The other integer types reflected in this engine are ids,
    // dimensions and versions (Image::width, NetReplicated::id, AssetMetadata::version) --
    // values a user should not be dragging. Widening this is one more branch in the adapter.
    virtual void field_int(std::string_view name, int& value)
    {
        field_readonly(name, std::to_string(value));
    }

    // A reference-to-entity field. Read-only (unlike field_asset/field_string, there is
    // no reassignment use case here yet -- an entity picker widget could add one later).
    // Default falls back to the read-only display so existing visitors keep compiling.
    virtual void field_entity(std::string_view name, mosaic::foundation::EntityID value)
    {
        field_readonly(name, std::to_string(value.value()));
    }

    // A reference-to-asset field carried inside a Handle<R>. Read-only: Handle<R> exposes
    // no mutable accessor for its AssetID (by design -- resolving through a Handle is a
    // different lifecycle than a bare AssetID field). Default falls back to the read-only
    // display so existing visitors keep compiling.
    virtual void field_handle_asset(std::string_view name,
                                    const mosaic::foundation::serialization::AssetID& asset_id)
    {
        field_readonly(name, asset_id.to_string());
    }
};

// mosaic::foundation::serialization::Handle<R> detection, kept local so this header
// needs only reference.hpp (not the serialization value machinery).
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
            visitor.field_entity(name, value);
        }
        else if constexpr (std::is_same_v<U, ser::AssetID>)
        {
            visitor.field_asset(name, value);
        }
        else if constexpr (is_handle_v<U>)
        {
            visitor.field_handle_asset(name, value.asset_id());
        }
        else if constexpr (std::is_same_v<U, bool>)
        {
            visitor.field_readonly(name, value ? "true" : "false");
        }
        else if constexpr (std::is_same_v<U, std::string>)
        {
            visitor.field_string(name, value);
        }
        else if constexpr (std::is_same_v<U, int>)
        {
            // Below the bool branch, which keeps its own true/false rendering. Worth being
            // precise about why that ordering is safe rather than merely conventional: a
            // branch broad enough to catch bool (is_integral_v, say) would NOT compile, let
            // alone misbehave, because field_int takes `int&` and a `bool&` cannot bind to
            // it. Verified with a mutation probe -- the mutant failed to build. So the
            // compiler enforces this, and the ro:flag assertion in field_visitor_test.cpp is
            // a cheap guard rather than the thing holding the line.
            visitor.field_int(name, value);
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
