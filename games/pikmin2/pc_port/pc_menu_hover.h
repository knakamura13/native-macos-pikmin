#ifndef PC_MENU_HOVER_H
#define PC_MENU_HOVER_H

// Hover lift for J2D button groups on screens that already draw their own buttons.
// Needs pc_menu_tap.h's PIKI_PC_MENU_TAP and the decomp's J2DPane and System.

#include "pc_menu_tap.h"
#if PIKI_PC_MENU_TAP
#include "JSystem/J2D/J2DPane.h"
#include "System.h"

// `owner` resets `prev` when a screen builds new panes, so a pane that was last seen
// hovered cannot leave the next screen's pane scaled.
struct PcHoverGroup {
	J2DPane* owner;
	float prev;
};

static inline J2DPane* pcFirstChild(J2DPane* container)
{
	if (!container) return nullptr;
	JSUTreeIterator<J2DPane> it = container->mTree.getFirstChild();
	return it != container->mTree.getEndChild() ? it.getObject() : nullptr;
}

// Scales a pane and its position about (cx, cy), both in the parent's space.
static inline void pcScalePane(J2DPane* pane, float cx, float cy, float ratio)
{
	pane->updateScale(pane->getScaleX() * ratio, pane->mScaleY * ratio);
	pane->mTranslateX = cx + (pane->mTranslateX - cx) * ratio;
	pane->mTranslateY = cy + (pane->mTranslateY - cy) * ratio;
}

// Lifts a button container while hovered. The children scale about the centre of the
// first child (the glyph), not about the container origin, which on some layouts is
// the screen corner and would drag the children sideways. A pane with no children
// scales about its own centre.
static inline void pcHoverLift(J2DPane* container, PcHoverGroup& g, bool over, float amount = 0.12f)
{
	if (!container) return;
	if (g.owner != container) {
		g.owner = container;
		g.prev  = 1.0f;
	}
	const float next = pc_menu_hover_scale(container, over, amount, sys->mDeltaTime);
	const float ratio = next / g.prev;
	g.prev = next;
	J2DPane* glyph = pcFirstChild(container);
	if (!glyph) {
		const float cx = container->mTranslateX + (container->mBounds.i.x + container->mBounds.f.x) * 0.5f * container->getScaleX();
		const float cy = container->mTranslateY + (container->mBounds.i.y + container->mBounds.f.y) * 0.5f * container->mScaleY;
		pcScalePane(container, cx, cy, ratio);
		return;
	}
	const float cx = glyph->mTranslateX + (glyph->mBounds.i.x + glyph->mBounds.f.x) * 0.5f * glyph->getScaleX();
	const float cy = glyph->mTranslateY + (glyph->mBounds.i.y + glyph->mBounds.f.y) * 0.5f * glyph->mScaleY;
	for (JSUTreeIterator<J2DPane> it = container->mTree.getFirstChild(); it != container->mTree.getEndChild(); ++it) {
		if (it.getObject()) pcScalePane(it.getObject(), cx, cy, ratio);
	}
}

// The pane a click on a button container is tested against: its glyph, or itself.
static inline J2DPane* pcHoverTarget(J2DPane* container)
{
	J2DPane* glyph = pcFirstChild(container);
	return glyph ? glyph : container;
}
#endif

#endif
