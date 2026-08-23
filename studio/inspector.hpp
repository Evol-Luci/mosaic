#pragma once

#include "engine/reflection/field_visitor.hpp"

namespace mosaic::studio
{

// Renders each visited field as an ImGui widget: floats are editable (DragFloat, edits the
// live field in place), everything else is a read-only label. Nested groups render as an
// indented, ID-scoped block so same-named leaves (e.g. position.x vs scale.x) don't collide.
class ImGuiFieldVisitor : public mosaic::engine::reflection::FieldVisitor
{
public:
    void field_float(std::string_view name, float& value) override;
    void field_readonly(std::string_view name, const std::string& display) override;
    void begin_group(std::string_view name) override;
    void end_group() override;
};

} // namespace mosaic::studio
