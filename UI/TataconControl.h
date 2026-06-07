#pragma once

#include <stdint.h>

#include "UI/GamepadEmu.h"

class TataconControl : public GamepadComponent {
public:
	TataconControl(float scale, UI::LayoutParams *layoutParams);

	void Draw(UIContext &dc) override;
	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	bool Touch(const TouchInput &touch) override;
	bool IsDownByTouch() const override { return pointerDownMask_ != 0; }

private:
	void PressPointer(int pointerId, uint32_t mask);
	void ReleasePointer(int pointerId);
	void ReleaseAll();
	uint32_t ActiveMaskExcept(int pointerId) const;
	uint32_t ZoneForTouch(float x, float y) const;

	ImageID image_ = ImageID("I_TATACON");
	float scale_ = 1.0f;
	uint32_t pointerDownMask_ = 0;
	uint32_t pointerButtons_[TOUCH_MAX_POINTERS]{};
};
