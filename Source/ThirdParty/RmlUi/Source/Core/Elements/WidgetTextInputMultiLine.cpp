#include "WidgetTextInputMultiLine.h"
#include "../../../Include/RmlUi/Core/Dictionary.h"
#include "../../../Include/RmlUi/Core/ElementText.h"
#include <algorithm>

namespace Rml {

WidgetTextInputMultiLine::WidgetTextInputMultiLine(ElementFormControl* parent) : WidgetTextInput(parent) {}

WidgetTextInputMultiLine::~WidgetTextInputMultiLine() {}

void WidgetTextInputMultiLine::SanitizeValue(String& value)
{
	value.erase(std::remove_if(value.begin(), value.end(), [](char c) { return c == '\r' || c == '\t'; }), value.end());
}

// VaCuus homelab note (2026-11-18): reverted to upstream's empty LineBreak. The
// newline for Enter is synthesised by the EMBEDDER, not here -- VaCuusUIThread's
// drain already pairs KI_RETURN with ProcessTextInput('\n') exactly like the SDL
// backend (RmlUi_Platform_SDL.cpp:207-210). An insertion here as well produced a
// doubled newline per Enter, which desynchronised the IME's document positions
// (owner-visible as "the field cleared and would not take input"). Regression:
// VaCuus.Input.MultilineEnter.
void WidgetTextInputMultiLine::LineBreak() {}

} // namespace Rml
