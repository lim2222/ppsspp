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
	void PressPointer(int pointerId, uint32_t mask, double time);
	void ReleasePointer(int pointerId);
	void ReleaseAll();
	uint32_t ActiveMaskExcept(int pointerId) const;
	uint32_t ZoneForTouch(float x, float y) const;

	static bool IsLeftSide(uint32_t mask);
	static bool IsHead(uint32_t mask);

	ImageID image_ = ImageID("I_TATACON");
	float scale_ = 1.0f;
	uint32_t pointerDownMask_ = 0;
	uint32_t pointerButtons_[TOUCH_MAX_POINTERS]{};

	// Hit history, used to recognise a two-finger big note.
	double lastHitTime_ = 0.0;
	double prevHitTime_ = 0.0;
	uint32_t lastHitMask_ = 0;
	bool lastPairWasBig_ = false;
};
