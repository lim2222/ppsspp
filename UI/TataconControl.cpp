#include "UI/TataconControl.h"

#include <algorithm>

#include "Common/Data/Color/RGBAUtil.h"
#include "Common/System/System.h"
#include "Common/System/Display.h"
#include "Common/UI/Context.h"
#include "Common/UI/Screen.h"  
#include "Core/Config.h"
#include "Core/HLE/sceCtrl.h"
extern ScreenManager *g_screenManager;

TataconControl::TataconControl(float scale, UI::LayoutParams *layoutParams)
	: GamepadComponent("Taiko drum", layoutParams), scale_(scale) {
}

void TataconControl::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage *image = dc.Draw()->GetAtlas()->getImage(image_);
	if (image) {
		w = image->w * scale_;
		h = image->h * scale_;
	} else {
		w = 0.0f;
		h = 0.0f;
	}
}

void TataconControl::Draw(UIContext &dc) {
	// Skip drawing if TouchControlVisibilityScreen is on top to prevent blocking UI
	if (g_screenManager && g_screenManager->topScreen() && strcmp(g_screenManager->topScreen()->tag(), "TouchControlVisibility") == 0) {
		return;
	}
	float opacity = GamepadGetOpacity();
	if (opacity <= 0.0f) {
		return;
	}

	float scale = scale_;
	float drawY = bounds_.centerY();
	
	bool centerPressed = false;
	bool rimPressed = false;
	for (int i = 0; i < TOUCH_MAX_POINTERS; i++) {
		if (pointerDownMask_ & (1 << i)) {
			uint32_t btn = pointerButtons_[i];
			if (btn == CTRL_UP || btn == CTRL_DOWN || btn == CTRL_CIRCLE || btn == CTRL_CROSS) {
				centerPressed = true;
			}
			if (btn == CTRL_LTRIGGER || btn == CTRL_RTRIGGER) {
				rimPressed = true;
			}
		}
	}

	if (centerPressed || rimPressed) {
		scale *= 1.0f;
		opacity = std::min(opacity * 1.15f, 1.0f);
		drawY += 10.0f;
	}

	// Draw base tatacon

	float visualW = 800.0f * scale;
	float visualH = 512.0f * scale;

	float drumCenterX = bounds_.centerX();
	float drumCenterY = bounds_.centerY() - (visualH * 0.01f);
	
	float drumRadius = visualW * 0.18f;
	float rimRadius  = visualW * 0.45f;

	// Rim hit effect
	if (rimPressed) {
		float rimSize = rimRadius * 1.0f;
		Bounds rimBounds(
			drumCenterX - rimSize * 0.5f,
			drumCenterY - rimSize * 0.5f,
			rimSize, rimSize);
		dc.Draw()->DrawImageStretch(ImageID("I_ROUND"), rimBounds,
			colorAlpha(0xDFDEB8, std::min(opacity * 0.45f, 1.0f)));
	}
	
	dc.Draw()->DrawImage(image_, bounds_.centerX(), drawY, scale, colorAlpha(0xFFFFFF, opacity), ALIGN_CENTER);

	// Center hit effect
	if (centerPressed) {
		float centerW = drumRadius * 1.5f;
		float centerH = drumRadius * 2.0f * 0.63f;

		// Horizontal offset of the center hit glow (positive = move right).
		// Calibrated per orientation: portrait needed a right nudge (the glow
		// otherwise sat left of drum center); in landscape the glow is already
		// centered, so keep it at ~0 there. Tune each independently.
		float centerOffsetX;
		if (g_display.GetDeviceOrientation() == DeviceOrientation::Portrait)
			centerOffsetX = drumRadius * 0.01f;   // portrait: small right nudge
		else
			centerOffsetX = 0.0f;                 // landscape: no shift

		Bounds centerBounds(
			drumCenterX - centerW * 0.5f + centerOffsetX,
			drumCenterY - centerH * 0.5f,
			centerW, centerH);
		dc.Draw()->DrawImageStretch(ImageID("I_ROUND"), centerBounds,
			colorAlpha(0x96ADFB, std::min(opacity * 1.45f, 1.0f)));
	}
}

bool TataconControl::Touch(const TouchInput &touch) {
	if (touch.flags & TouchInputFlags::RELEASE_ALL) {
		ReleaseAll();
		return false;
	}

	_dbg_assert_(touch.id >= 0 && touch.id < TOUCH_MAX_POINTERS);
	if (touch.id < 0 || touch.id >= TOUCH_MAX_POINTERS) {
		return false;
	}

	if (touch.flags & TouchInputFlags::CANCEL) {
		ReleasePointer(touch.id);
		return false;
	}

	if (touch.flags & TouchInputFlags::DOWN) {
		// If this pointer is already claimed by another button, ignore it.
		if (IsPointerUsed(touch.id)) {
			return false;
		}
		const uint32_t mask = ZoneForTouch(touch.x, touch.y);
		if (mask != 0) {
			PressPointer(touch.id, mask);
			return true;
		}
	}

	if (touch.flags & TouchInputFlags::MOVE) {
		if (!(touch.flags & TouchInputFlags::MOUSE) || touch.buttons) {
			const uint32_t mask = ZoneForTouch(touch.x, touch.y);
			if (mask != pointerButtons_[touch.id]) {
				ReleasePointer(touch.id);
				if (mask != 0) {
					// Also re-check usedPointerMask on MOVE.
					if (!(IsPointerUsed(touch.id))) {
						PressPointer(touch.id, mask);
					}
				}
			}
			return mask != 0;
		}
	}

	if (touch.flags & TouchInputFlags::UP) {
		ReleasePointer(touch.id);
	}

	return false;
}

void TataconControl::PressPointer(int pointerId, uint32_t mask) {
	const uint32_t alreadyDown = ActiveMaskExcept(pointerId);
	pointerDownMask_ |= 1 << pointerId;
	pointerButtons_[pointerId] = mask;
	if (g_Config.bHapticFeedback) {
		System_Vibrate(HAPTIC_VIRTUAL_KEY);
	}
	__CtrlUpdateButtons(mask & ~alreadyDown, 0);
}

void TataconControl::ReleasePointer(int pointerId) {
	const uint32_t mask = pointerButtons_[pointerId];
	pointerButtons_[pointerId] = 0;
	pointerDownMask_ &= ~(1 << pointerId);
	const uint32_t stillDown = ActiveMaskExcept(-1);
	if (mask != 0) {
		__CtrlUpdateButtons(0, mask & ~stillDown);
	}
}

void TataconControl::ReleaseAll() {
	uint32_t mask = 0;
	for (int i = 0; i < TOUCH_MAX_POINTERS; i++) {
		mask |= pointerButtons_[i];
		pointerButtons_[i] = 0;
	}
	if (mask != 0) {
		__CtrlUpdateButtons(0, mask);
	}
	pointerDownMask_ = 0;
}

uint32_t TataconControl::ActiveMaskExcept(int pointerId) const {
	uint32_t mask = 0;
	for (int i = 0; i < TOUCH_MAX_POINTERS; i++) {
		if (i != pointerId) {
			mask |= pointerButtons_[i];
		}
	}
	return mask;
}

uint32_t TataconControl::ZoneForTouch(float x, float y) const {
	float visualW = 800.0f * scale_;
	float visualH = 512.0f * scale_;
	
	float cx = bounds_.centerX(); 
	float cy = bounds_.centerY() + (visualH * 0.0f);

	float dx = x - cx;
	float dy = y - cy;
	float r = sqrtf(dx * dx + dy * dy);

	float drumRadius = visualW * 0.13f;
	float rimRadius  = visualW * 0.45f;
	float lrRadius   = visualW * 0.50f;

	if (r > lrRadius) return 0;

	if (r < drumRadius) {
		if (dx < 0.0f) return dy < 0.0f ? CTRL_UP : CTRL_DOWN;
		else           return dy < 0.0f ? CTRL_CIRCLE : CTRL_CROSS;
	}

	if (r < rimRadius || fabsf(dx) > drumRadius) {
		return dx < 0.0f ? CTRL_LTRIGGER : CTRL_RTRIGGER;
	}

	return dx < 0.0f ? CTRL_LTRIGGER : CTRL_RTRIGGER;
}
