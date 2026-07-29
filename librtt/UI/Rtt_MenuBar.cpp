//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"
#include "Core/Rtt_Math.h"

#include "UI/Rtt_MenuBar.h"
#include "UI/Rtt_BgfxUIDraw.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// A dark bar, because the simulator's chrome sits against content of any
// colour and a dark strip reads as chrome everywhere. abgr, as bgfx packs it.
static const U32 kBarColor = 0xff2b2b2b;
static const U32 kPopupColor = 0xff333333;
static const U32 kPopupBorderColor = 0xff555555;
static const U32 kHighlightColor = 0xff5a4633; // a muted blue in abgr
static const U32 kTextColor = 0xffe6e6e6;
static const U32 kDisabledTextColor = 0xff808080;
static const U32 kShortcutColor = 0xff9a9a9a;
static const U32 kSeparatorColor = 0xff555555;

// The em size the glyphs are baked at. Chosen against GetHeight() so that a
// title sits centred in the bar with a little room above and below.
static const float kFontPixelHeight = 14.f;

// Room either side of a title in the bar.
static const float kTitlePadding = 10.f;

// A drop-down's own insets: the gap between its border and the leftmost text,
// and the gap above the first item and below the last.
static const float kPopupPaddingX = 8.f;
static const float kPopupPaddingY = 4.f;

// How much taller a row is than the text in it.
static const float kRowExtraHeight = 6.f;

// The gap between an item's label and its shortcut, which is what stops a long
// label from running into "Ctrl+Shift+Alt+B".
static const float kShortcutGap = 24.f;

// The arrow a submenu is marked with, and the room reserved for it.
static const char* kSubmenuMarker = ">";
static const float kSubmenuMarkerGap = 16.f;

static const float kSeparatorHeight = 7.f;

// How far a submenu overlaps the parent it grew out of. A little, so that the
// mouse crossing between them never passes over neither.
static const float kSubmenuOverlap = 3.f;

// ----------------------------------------------------------------------------

MenuBar::MenuBar()
:	fDraw( NULL ),
	fProc( NULL ),
	fUserdata( NULL ),
	fEnabled( true ),
	fOpenMenu( -1 ),
	fDrawnOpenMenu( -1 ),
	fDrawnEnabled( true ),
	fEverDrawn( false ),
	fPressed( false )
{
}

bool
MenuBar::NeedsRedraw() const
{
	if ( !IsInitialized() )
	{
		return false;
	}

	return !fEverDrawn
		|| fDrawnOpenMenu != fOpenMenu
		|| fDrawnOpenPath != fOpenPath
		|| fDrawnHotPath != fHotPath
		|| fDrawnEnabled != fEnabled
		|| fDrawnStatusText != fStatusText;
}

MenuBar::~MenuBar()
{
	Finalize();
}

bool
MenuBar::Initialize( const void* ttf, size_t ttfSize )
{
	if ( NULL != fDraw )
	{
		return fDraw->IsInitialized();
	}

	fDraw = new BgfxUIDraw;

	if ( !fDraw->Initialize( ttf, ttfSize, kFontPixelHeight ) )
	{
		delete fDraw;
		fDraw = NULL;

		return false;
	}

	// The titles could not be measured before there was a font to measure
	// them with, so anything SetMenus was given beforehand is laid out now.
	MeasureTitles();

	return true;
}

void
MenuBar::Finalize()
{
	delete fDraw;
	fDraw = NULL;

	// Nothing of what was drawn survives the bgfx that drew it.
	fEverDrawn = false;

	Close();
}

bool
MenuBar::IsInitialized() const
{
	return NULL != fDraw && fDraw->IsInitialized();
}

void
MenuBar::SetCommandHandler( CommandProc proc, void* userdata )
{
	fProc = proc;
	fUserdata = userdata;
}

void
MenuBar::SetMenus( const std::vector< Menu >& menus )
{
	Close();

	fMenus = menus;

	// The items themselves are not part of what NeedsRedraw compares -- it
	// watches interaction state -- so a new set of menus has to say so.
	fEverDrawn = false;

	MeasureTitles();
}

// Not part of NeedsRedraw's comparison in itself: the status text is, and a
// change to it is what makes suspending visible.
void
MenuBar::SetStatusText( const char* text )
{
	fStatusText = ( NULL != text ) ? text : "";
}

void
MenuBar::SetEnabled( bool enabled )
{
	if ( !enabled )
	{
		Close();
	}

	fEnabled = enabled;
}

void
MenuBar::Close()
{
	fOpenMenu = -1;
	fOpenPath.clear();
	fHotPath.clear();
	fPressed = false;
}

// ----------------------------------------------------------------------------
// Layout

void
MenuBar::MeasureTitles()
{
	fTitleWidths.assign( fMenus.size(), 0.f );

	if ( !IsInitialized() )
	{
		return;
	}

	for ( size_t i = 0; i < fMenus.size(); ++i )
	{
		fTitleWidths[i] = fDraw->MeasureText( fMenus[i].fTitle.c_str() ) + 2.f * kTitlePadding;
	}
}

float
MenuBar::TitleWidth( size_t index ) const
{
	return index < fTitleWidths.size() ? fTitleWidths[index] : 0.f;
}

float
MenuBar::TitleX( size_t index ) const
{
	float x = 0.f;

	for ( size_t i = 0; i < index && i < fTitleWidths.size(); ++i )
	{
		x += fTitleWidths[i];
	}

	return x;
}

const std::vector< MenuItem >*
MenuBar::ItemsAt( const Path& path, size_t depth ) const
{
	// depth 0 is a menu's own items; each level after it steps into the
	// submenu the previous level named.
	if ( path.empty() || path[0] < 0 || size_t( path[0] ) >= fMenus.size() )
	{
		return NULL;
	}

	const std::vector< MenuItem >* items = &fMenus[path[0]].fItems;

	for ( size_t i = 1; i <= depth; ++i )
	{
		if ( i >= path.size() || path[i] < 0 || size_t( path[i] ) >= items->size() )
		{
			return NULL;
		}

		const MenuItem& item = (*items)[path[i]];

		if ( item.fItems.empty() )
		{
			return NULL;
		}

		items = &item.fItems;
	}

	return items;
}

MenuBar::PopupBounds
MenuBar::BoundsFor( const Path& path, size_t depth ) const
{
	PopupBounds bounds = { 0.f, 0.f, 0.f, 0.f };

	const std::vector< MenuItem >* items = ItemsAt( path, depth );

	if ( NULL == items || !IsInitialized() )
	{
		return bounds;
	}

	const float rowHeight = fDraw->GetLineHeight() + kRowExtraHeight;

	float widest = 0.f;
	float height = 2.f * kPopupPaddingY;

	for ( size_t i = 0; i < items->size(); ++i )
	{
		const MenuItem& item = (*items)[i];

		if ( item.fSeparator )
		{
			height += kSeparatorHeight;
			continue;
		}

		height += rowHeight;

		float width = fDraw->MeasureText( item.fLabel.c_str() );

		if ( !item.fShortcut.empty() )
		{
			width += kShortcutGap + fDraw->MeasureText( item.fShortcut.c_str() );
		}

		if ( !item.fItems.empty() )
		{
			width += kSubmenuMarkerGap + fDraw->MeasureText( kSubmenuMarker );
		}

		if ( width > widest )
		{
			widest = width;
		}
	}

	bounds.fWidth = widest + 2.f * kPopupPaddingX;
	bounds.fHeight = height;

	if ( 0 == depth )
	{
		// A menu's own drop-down hangs from its title in the bar.
		bounds.fX = TitleX( size_t( path[0] ) );
		bounds.fY = float( GetHeight() );

		return bounds;
	}

	// A submenu grows out of the right edge of the row that opened it, level
	// with that row.
	const PopupBounds parent = BoundsFor( path, depth - 1 );
	const std::vector< MenuItem >* parentItems = ItemsAt( path, depth - 1 );

	bounds.fX = parent.fX + parent.fWidth - kSubmenuOverlap;
	bounds.fY = parent.fY + kPopupPaddingY;

	if ( NULL != parentItems && depth < path.size() )
	{
		for ( int i = 0; i < path[depth] && size_t( i ) < parentItems->size(); ++i )
		{
			bounds.fY += (*parentItems)[i].fSeparator ? kSeparatorHeight : rowHeight;
		}
	}

	return bounds;
}

int
MenuBar::ItemAt( const Path& path, size_t depth, int x, int y ) const
{
	const std::vector< MenuItem >* items = ItemsAt( path, depth );

	if ( NULL == items || !IsInitialized() )
	{
		return -1;
	}

	const PopupBounds bounds = BoundsFor( path, depth );

	if ( float( x ) < bounds.fX || float( x ) >= bounds.fX + bounds.fWidth
		|| float( y ) < bounds.fY || float( y ) >= bounds.fY + bounds.fHeight )
	{
		return -1;
	}

	const float rowHeight = fDraw->GetLineHeight() + kRowExtraHeight;

	float top = bounds.fY + kPopupPaddingY;

	for ( size_t i = 0; i < items->size(); ++i )
	{
		const float height = (*items)[i].fSeparator ? kSeparatorHeight : rowHeight;

		if ( float( y ) >= top && float( y ) < top + height )
		{
			// A separator is part of the popup but not a target, so the caller
			// sees "inside, over nothing".
			return (*items)[i].fSeparator ? -1 : int( i );
		}

		top += height;
	}

	return -1;
}

bool
MenuBar::PathAtPoint( int x, int y, Path& out ) const
{
	out.clear();

	if ( fOpenMenu < 0 )
	{
		return false;
	}

	Path path;

	path.push_back( fOpenMenu );

	// Walked from the deepest open popup outwards, since a submenu is drawn
	// over its parent and so wins wherever the two overlap.
	for ( size_t depth = fOpenPath.size(); ; --depth )
	{
		Path candidate( path );

		candidate.insert( candidate.end(), fOpenPath.begin(), fOpenPath.begin() + depth );

		const PopupBounds bounds = BoundsFor( candidate, depth );

		if ( float( x ) >= bounds.fX && float( x ) < bounds.fX + bounds.fWidth
			&& float( y ) >= bounds.fY && float( y ) < bounds.fY + bounds.fHeight )
		{
			const int index = ItemAt( candidate, depth, x, y );

			out = candidate;

			if ( index >= 0 )
			{
				out.push_back( index );
			}

			return true;
		}

		if ( 0 == depth )
		{
			break;
		}
	}

	return false;
}

// ----------------------------------------------------------------------------
// Input

bool
MenuBar::ContainsPoint( int x, int y ) const
{
	if ( !IsInitialized() || fMenus.empty() )
	{
		return false;
	}

	if ( y >= 0 && y < GetHeight() && x >= 0 )
	{
		return true;
	}

	Path path;

	return PathAtPoint( x, y, path );
}

bool
MenuBar::OnMouseMove( int x, int y )
{
	if ( !IsInitialized() || !fEnabled || fMenus.empty() )
	{
		return false;
	}

	const bool inBar = y >= 0 && y < GetHeight() && x >= 0;

	if ( inBar )
	{
		// Which title the mouse is over. With a menu already open, moving
		// across the bar switches to the neighbour under the cursor, which is
		// what every menu bar does once one is showing.
		const float fx = float( x );

		float left = 0.f;

		for ( size_t i = 0; i < fMenus.size(); ++i )
		{
			const float width = TitleWidth( i );

			if ( fx >= left && fx < left + width )
			{
				fHotPath.assign( 1, int( i ) );

				if ( fOpenMenu >= 0 && fOpenMenu != int( i ) )
				{
					fOpenMenu = int( i );
					fOpenPath.clear();
				}

				return true;
			}

			left += width;
		}

		fHotPath.clear();

		return fOpenMenu >= 0;
	}

	if ( fOpenMenu < 0 )
	{
		return false;
	}

	Path path;

	if ( !PathAtPoint( x, y, path ) )
	{
		// Outside every popup: nothing is hovered, but the menu stays open --
		// the mouse is allowed to leave and come back.
		fHotPath.clear();

		return true;
	}

	fHotPath = path;

	// The open chain follows the hover. Everything deeper than the item under
	// the cursor closes, and the item itself opens if it is a submenu -- so
	// walking down a menu opens and closes submenus as it goes, with no click.
	fOpenPath.assign( path.begin() + 1, path.end() );

	if ( !fOpenPath.empty() )
	{
		const std::vector< MenuItem >* items = ItemsAt( path, fOpenPath.size() - 1 );

		if ( NULL != items && size_t( fOpenPath.back() ) < items->size()
			&& (*items)[fOpenPath.back()].fItems.empty() )
		{
			// Not a submenu, so it is hovered but opens nothing.
			fOpenPath.pop_back();
		}
	}

	return true;
}

bool
MenuBar::OnMouseDown( int x, int y )
{
	if ( !IsInitialized() || !fEnabled || fMenus.empty() )
	{
		return false;
	}

	const bool inBar = y >= 0 && y < GetHeight() && x >= 0;

	if ( inBar )
	{
		const float fx = float( x );

		float left = 0.f;

		for ( size_t i = 0; i < fMenus.size(); ++i )
		{
			const float width = TitleWidth( i );

			if ( fx >= left && fx < left + width )
			{
				// A press on the open menu's own title closes it; on any other
				// title it opens that one.
				if ( fOpenMenu == int( i ) )
				{
					Close();
				}
				else
				{
					fOpenMenu = int( i );
					fOpenPath.clear();
					fHotPath.assign( 1, int( i ) );
					fPressed = true;
				}

				return true;
			}

			left += width;
		}

		// The empty stretch of bar to the right of the last title.
		Close();

		return true;
	}

	if ( fOpenMenu < 0 )
	{
		return false;
	}

	Path path;

	if ( !PathAtPoint( x, y, path ) )
	{
		// A click outside an open menu dismisses it and is swallowed: the
		// click that closes a menu should not also press whatever was beneath
		// it.
		Close();

		return true;
	}

	// Chosen on release rather than here, so that press-drag-release picks the
	// item let go over -- the other way a menu is used.
	fHotPath = path;

	return true;
}

bool
MenuBar::OnMouseUp( int x, int y )
{
	if ( !IsInitialized() || !fEnabled || fMenus.empty() )
	{
		return false;
	}

	if ( fOpenMenu < 0 )
	{
		return false;
	}

	const bool inBar = y >= 0 && y < GetHeight() && x >= 0;

	if ( inBar )
	{
		// The release of the click that opened this menu. It stays open, so
		// that click-then-click works as well as press-drag-release.
		fPressed = false;

		return true;
	}

	fPressed = false;

	Path path;

	if ( !PathAtPoint( x, y, path ) )
	{
		Close();

		return true;
	}

	Choose( path );

	return true;
}

void
MenuBar::Choose( const Path& path )
{
	// Nothing under the cursor, or a submenu's own row: neither is a command,
	// and neither should close the menu.
	if ( path.size() < 2 )
	{
		return;
	}

	const std::vector< MenuItem >* items = ItemsAt( path, path.size() - 2 );

	if ( NULL == items || size_t( path.back() ) >= items->size() )
	{
		return;
	}

	const MenuItem& item = (*items)[path.back()];

	if ( item.fSeparator || !item.fEnabled || !item.fItems.empty() )
	{
		return;
	}

	const int command = item.fCommand;

	// Closed before the callback runs, not after: the handler is free to
	// rebuild the bar -- opening a project replaces every menu on it -- and
	// closing afterwards would be reaching into whatever replaced this.
	Close();

	if ( NULL != fProc && command >= 0 )
	{
		fProc( fUserdata, command );
	}
}

// ----------------------------------------------------------------------------
// Rendering

void
MenuBar::Render( U16 view, U32 windowWidth, U32 windowHeight )
{
	if ( !IsInitialized() || 0 == windowWidth || 0 == windowHeight )
	{
		return;
	}

	if ( fMenus.empty() && fStatusText.empty() )
	{
		return;
	}

	fDrawnOpenMenu = fOpenMenu;
	fDrawnOpenPath = fOpenPath;
	fDrawnHotPath = fHotPath;
	fDrawnEnabled = fEnabled;
	fDrawnStatusText = fStatusText;
	fEverDrawn = true;

	// Drawn over the whole window rather than just the strip the bar sits in.
	// Scoping the overlay's view rect to that strip looked like an obvious win
	// -- the pass costs its area, not what is drawn in it -- but it made the
	// bar disappear, and the measurement that motivated it turned out to be
	// noise: the window it was taken in was presenting at about one frame a
	// second whether the bar was drawn or not.
	fDraw->Begin( bgfx::ViewId( view ), windowWidth, windowHeight );

	const float height = fMenus.empty() ? 0.f : float( GetHeight() );

	fDraw->Rect( 0.f, 0.f, float( windowWidth ), height, kBarColor );

	const U32 titleColor = fEnabled ? kTextColor : kDisabledTextColor;
	const float textTop = ( height - fDraw->GetLineHeight() ) * 0.5f;

	float left = 0.f;

	for ( size_t i = 0; i < fMenus.size(); ++i )
	{
		const float width = TitleWidth( i );

		const bool highlighted = fEnabled
			&& ( fOpenMenu == int( i ) || ( !fHotPath.empty() && fHotPath[0] == int( i ) && fOpenMenu < 0 ) );

		if ( highlighted )
		{
			fDraw->Rect( left, 0.f, width, height, kHighlightColor );
		}

		fDraw->Text( left + kTitlePadding, textTop, fMenus[i].fTitle.c_str(), titleColor );

		left += width;
	}

	if ( !fStatusText.empty() )
	{
		// Centred in the content area, which is the window minus the bar.
		const float textWidth = fDraw->MeasureText( fStatusText.c_str() );

		fDraw->Text(
			( float( windowWidth ) - textWidth ) * 0.5f,
			height + ( float( windowHeight ) - height - fDraw->GetLineHeight() ) * 0.5f,
			fStatusText.c_str(), kTextColor );
	}

	// After the status text, so a drop-down opened while suspended is over it
	// rather than under.
	if ( fOpenMenu >= 0 )
	{
		Path path;

		path.push_back( fOpenMenu );

		// Outermost first, so that each submenu paints over the parent it
		// overlaps -- the batch has no depth test, only order.
		for ( size_t depth = 0; depth <= fOpenPath.size(); ++depth )
		{
			Path open( path );

			open.insert( open.end(), fOpenPath.begin(), fOpenPath.begin() + depth );

			RenderPopup( open, depth );
		}
	}

	fDraw->End();
}

void
MenuBar::RenderPopup( const Path& path, size_t depth )
{
	const std::vector< MenuItem >* items = ItemsAt( path, depth );

	if ( NULL == items )
	{
		return;
	}

	const PopupBounds bounds = BoundsFor( path, depth );

	// A one-pixel border, drawn as a filled rect with the body over it, since
	// an outline is four more quads for the same result.
	fDraw->Rect( bounds.fX - 1.f, bounds.fY - 1.f, bounds.fWidth + 2.f, bounds.fHeight + 2.f, kPopupBorderColor );
	fDraw->Rect( bounds.fX, bounds.fY, bounds.fWidth, bounds.fHeight, kPopupColor );

	const float rowHeight = fDraw->GetLineHeight() + kRowExtraHeight;
	const float textInset = ( rowHeight - fDraw->GetLineHeight() ) * 0.5f;

	// Which row of this popup is hovered. Only the popup the hover actually
	// landed in highlights, so a parent stops highlighting its submenu's row
	// only when the mouse leaves that submenu entirely -- which it does not,
	// since fHotPath names the deepest popup under the cursor.
	int hot = -1;

	if ( fHotPath.size() == depth + 2 )
	{
		bool sameBranch = true;

		for ( size_t i = 0; i <= depth && sameBranch; ++i )
		{
			sameBranch = i < path.size() && fHotPath[i] == path[i];
		}

		if ( sameBranch )
		{
			hot = fHotPath.back();
		}
	}

	// The row that opened the submenu below this one stays highlighted too,
	// so the trail from the bar down to the open submenu reads as one path.
	if ( fOpenPath.size() > depth )
	{
		hot = fOpenPath[depth];
	}

	float top = bounds.fY + kPopupPaddingY;

	for ( size_t i = 0; i < items->size(); ++i )
	{
		const MenuItem& item = (*items)[i];

		if ( item.fSeparator )
		{
			fDraw->Rect( bounds.fX + kPopupPaddingX, top + kSeparatorHeight * 0.5f,
				bounds.fWidth - 2.f * kPopupPaddingX, 1.f, kSeparatorColor );

			top += kSeparatorHeight;

			continue;
		}

		if ( hot == int( i ) && item.fEnabled )
		{
			fDraw->Rect( bounds.fX, top, bounds.fWidth, rowHeight, kHighlightColor );
		}

		const U32 color = item.fEnabled ? kTextColor : kDisabledTextColor;

		fDraw->Text( bounds.fX + kPopupPaddingX, top + textInset, item.fLabel.c_str(), color );

		if ( !item.fShortcut.empty() )
		{
			const float width = fDraw->MeasureText( item.fShortcut.c_str() );

			fDraw->Text( bounds.fX + bounds.fWidth - kPopupPaddingX - width, top + textInset,
				item.fShortcut.c_str(), item.fEnabled ? kShortcutColor : kDisabledTextColor );
		}
		else if ( !item.fItems.empty() )
		{
			const float width = fDraw->MeasureText( kSubmenuMarker );

			fDraw->Text( bounds.fX + bounds.fWidth - kPopupPaddingX - width, top + textInset,
				kSubmenuMarker, color );
		}

		top += rowHeight;
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
