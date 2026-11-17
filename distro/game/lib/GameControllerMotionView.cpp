/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BGameControllerMotionView, built from the Appearance Kit's BTiltLevel and
 * BAxisBar (libr5ui.so, docs/APPEARANCE-KIT.md).
 */


#include "GameControllerMotionView.h"

#include <AxisBar.h>
#include <Font.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TiltLevel.h>

#include <new>
#include <string.h>


using BAppearanceKit::BAxisBar;
using BAppearanceKit::BTiltLevel;


static const float kDefaultTurnRange = 360.0f;


struct BGameControllerMotionView::Private {
	BTiltLevel*	level;
	BAxisBar*	pitch;
	BAxisBar*	roll;
	BAxisBar*	yaw;
	float		turnRange;
};


static BStringView*
make_label(const char* name, const char* text, bool bold)
{
	BStringView* label = new BStringView(name, text);
	if (bold) {
		BFont font(be_bold_font);
		label->SetFont(&font);
	}
	return label;
}


BGameControllerMotionView::BGameControllerMotionView(const char* name)
	:
	BView(name, 0),
	fPrivate(new(std::nothrow) Private)
{
	memset(_reserved, 0, sizeof(_reserved));
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	if (fPrivate == NULL)
		return;
	fPrivate->turnRange = kDefaultTurnRange;
	fPrivate->level = new BTiltLevel("tilt");
	fPrivate->pitch = new BAxisBar("pitch");
	fPrivate->roll = new BAxisBar("roll");
	fPrivate->yaw = new BAxisBar("yaw");

	BStringView* tilt = make_label("tilt label", "Tilt", true);
	BStringView* turn = make_label("turn label", "Turn", true);
	tilt->SetAlignment(B_ALIGN_CENTER);
	turn->SetAlignment(B_ALIGN_CENTER);
	tilt->SetExplicitAlignment(BAlignment(B_ALIGN_HORIZONTAL_CENTER,
		B_ALIGN_TOP));
	turn->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING, 0.0f)
			.Add(fPrivate->level)
			.Add(tilt)
		.End()
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING, 1.0f)
			.AddGlue()
			.AddGrid(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
				.Add(make_label("pitch label", "Pitch", true), 0, 0)
				.Add(fPrivate->pitch, 1, 0)
				.Add(make_label("roll label", "Roll", true), 0, 1)
				.Add(fPrivate->roll, 1, 1)
				.Add(make_label("yaw label", "Yaw", true), 0, 2)
				.Add(fPrivate->yaw, 1, 2)
				.Add(turn, 1, 3)
			.End()
			.AddGlue()
		.End();
}


BGameControllerMotionView::~BGameControllerMotionView()
{
	delete fPrivate;
}


void
BGameControllerMotionView::SetMotion(const game_controller_motion& motion)
{
	if (fPrivate == NULL)
		return;
	float range = fPrivate->turnRange;
	fPrivate->level->SetValue(motion.acceleration[0],
		motion.acceleration[2]);
	fPrivate->pitch->SetValue(motion.rotation[0] / range);
	fPrivate->yaw->SetValue(motion.rotation[1] / range);
	fPrivate->roll->SetValue(motion.rotation[2] / range);
}


void
BGameControllerMotionView::Reset()
{
	if (fPrivate == NULL)
		return;
	fPrivate->level->SetValue(0, 0);
	fPrivate->pitch->SetValue(0);
	fPrivate->roll->SetValue(0);
	fPrivate->yaw->SetValue(0);
}


void
BGameControllerMotionView::SetTurnRange(float degreesPerSecond)
{
	if (fPrivate != NULL && degreesPerSecond > 0)
		fPrivate->turnRange = degreesPerSecond;
}


float
BGameControllerMotionView::TurnRange() const
{
	return fPrivate != NULL ? fPrivate->turnRange : kDefaultTurnRange;
}


void BGameControllerMotionView::_ReservedGameControllerMotionView1() {}
void BGameControllerMotionView::_ReservedGameControllerMotionView2() {}
void BGameControllerMotionView::_ReservedGameControllerMotionView3() {}
void BGameControllerMotionView::_ReservedGameControllerMotionView4() {}
