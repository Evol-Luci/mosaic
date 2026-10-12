#pragma once

#include "engine/reflection/field_visitor.hpp"
#include "foundation/serialization/reference.hpp"

namespace mosaic::studio
{

// Renders each visited field as an ImGui widget: floats are editable (DragFloat), ints are
// editable (DragInt), AssetID
// fields show their id plus an "Assign" button (writes the browser's current selection into
// the live field), everything else is a read-only label. Nested groups render as an
// indented, ID-scoped block so same-named leaves (e.g. position.x vs scale.x) don't collide.
class ImGuiFieldVisitor : public mosaic::engine::reflection::FieldVisitor
{
public:
    ImGuiFieldVisitor() = default;
    ImGuiFieldVisitor(mosaic::foundation::serialization::AssetID selected, bool has_selection)
        : selected_(selected), has_selection_(has_selection)
    {
    }

    void field_float(std::string_view name, float& value) override;
    void field_int(std::string_view name, int& value) override;
    void field_readonly(std::string_view name, const std::string& display) override;
    void field_asset(std::string_view name,
                     mosaic::foundation::serialization::AssetID& value) override;
    void field_string(std::string_view name, std::string& value) override;
    void begin_group(std::string_view name) override;
    void end_group() override;

    bool changed() const { return changed_; }

private:
    mosaic::foundation::serialization::AssetID selected_{};
    bool                                       has_selection_ = false;
    bool                                       changed_       = false;
};

} // namespace mosaic::studio
