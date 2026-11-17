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

// VaCuus homelab patch (2026-11-18): Enter in a <textarea> inserts a newline.
// Upstream RmlUi leaves LineBreak empty here, so pressing Enter did nothing --
// and while an IME composition was open, the keystroke fell through to the IME
// (whose own Enter commits the raw pinyin), which read as "the text vanished".
// AddCharacters reuses the full insert path: sanitize (\n survives this class's
// SanitizeValue), selection replace, maxlength, cursor, change event.
void WidgetTextInputMultiLine::LineBreak()
{
	AddCharacters("\n");
}

} // namespace Rml
