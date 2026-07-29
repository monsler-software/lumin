//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_MenuBar_H__
#define _Rtt_MenuBar_H__

#include "Core/Rtt_Types.h"

#include <string>
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class BgfxUIDraw;

// ----------------------------------------------------------------------------

// One entry in a menu: a command, a separator, or a submenu.
//
// Which of the three it is comes from what is filled in rather than from a
// tag -- fSeparator makes it a rule, a non-empty fItems makes it a submenu,
// and anything else is a command. That keeps building a menu to naming the
// things that are actually on it.
struct MenuItem
{
	MenuItem()
	:	fCommand( -1 ),
		fSeparator( false ),
		fEnabled( true )
	{
	}

	std::string fLabel;

	// Shown right-aligned, purely as a reminder. The accelerator itself is the
	// platform's to notice: key events reach the simulator long before they
	// would reach a menu, and the platforms disagree about how a chord is even
	// spelled.
	std::string fShortcut;

	// Handed back to the callback when the item is chosen. Ignored on
	// separators and submenus.
	int fCommand;

	bool fSeparator;
	bool fEnabled;

	std::vector< MenuItem > fItems;
};

struct Menu
{
	std::string fTitle;
	std::vector< MenuItem > fItems;
};

// ----------------------------------------------------------------------------

// The simulator's menu bar, drawn by bgfx across the top of the window.
//
// Every platform used to grow its own -- an AppKit main menu on macOS, an MFC
// menu resource on Windows, a Dear ImGui bar on Linux -- so the same File menu
// existed three times, in three shapes, and behaved differently in each. Under
// bgfx none of the three can reach the window any more, and rather than write
// a fourth, this is the one bar all of them use. It knows nothing about any
// windowing system: the platform feeds it mouse positions and gets commands
// back.
//
// The bar occupies the top GetHeight() pixels of the window. The runtime's
// content is laid out beneath it, so the two never overlap -- but an open
// drop-down does hang over the content, which is why it draws last, over
// whatever the runtime rendered.
class MenuBar
{
	public:
		// Invoked when an item is chosen, with that item's fCommand. Called
		// from whichever mouse event closed the menu, so it is free to do
		// anything the platform would do in a click handler -- including tear
		// down the project, or the menu bar itself.
		typedef void (*CommandProc)( void* userdata, int command );

		MenuBar();
		~MenuBar();

		// ttf is a font file in memory, borrowed for the duration of the call.
		// bgfx must already be initialized. A bar that fails to initialize
		// draws nothing and swallows no input, so the simulator still runs.
		bool Initialize( const void* ttf, size_t ttfSize );
		void Finalize();

		bool IsInitialized() const;

		void SetCommandHandler( CommandProc proc, void* userdata );

		// Replaces the whole bar. Any open menu is closed, since the item it
		// referred to may not exist any more -- which is the point: the bar is
		// rebuilt when the simulator moves between the welcome screen and a
		// project, and those have different menus.
		void SetMenus( const std::vector< Menu >& menus );

		// The height of the bar itself, in pixels, which is how much room the
		// content below it has to leave. Fixed rather than measured so that
		// the window can be sized before bgfx exists to measure anything.
		static int GetHeight() { return 24; }

		// A line centred in the window below the bar, or empty for none. The
		// simulator shows "Suspended" through this. It rides along with the
		// bar rather than being drawn separately because the two share the one
		// batch, and because a suspended runtime draws nothing else at all.
		void SetStatusText( const char* text );

		// While something is disabled -- a modal dialog is up, say -- the bar
		// still draws, greyed, but stops responding.
		void SetEnabled( bool enabled );
		bool IsEnabled() const { return fEnabled; }

		// True while a drop-down is open, which is when the bar has taken over
		// the mouse: the platform should stop forwarding moves and clicks to
		// the runtime until this goes false again.
		bool IsOpen() const { return fOpenMenu >= 0; }

		// Whether a point belongs to the bar -- either inside the bar itself
		// or inside a drop-down it has open. What the platform tests before
		// handing an event to the runtime instead.
		bool ContainsPoint( int x, int y ) const;

		// Each returns true if the event was consumed. A consumed event must
		// not also be given to the runtime.
		bool OnMouseMove( int x, int y );
		bool OnMouseDown( int x, int y );
		bool OnMouseUp( int x, int y );

		// Closes whatever is open, without choosing anything: Escape, a click
		// elsewhere, the window losing focus.
		void Close();

		// Whether the bar would draw differently than it last did -- a new
		// item highlighted, a menu opened or closed.
		//
		// The chrome rides along in the runtime's frame, and the runtime only
		// renders when its own content changed; a static scene presents
		// nothing for as long as it stays static. So the host asks this after
		// feeding the bar input, and forces a frame when the answer is yes.
		// Without it the highlight only catches up with the cursor whenever
		// the content happens to redraw, which is what makes a menu feel like
		// it is lagging behind the mouse.
		bool NeedsRedraw() const;

		// Draws the bar and any open menu into `view`, over the window's whole
		// extent. Call once a frame, after the runtime has submitted its own
		// work, with a view id greater than any the runtime used.
		void Render( U16 view, U32 windowWidth, U32 windowHeight );

	private:
		// Where a menu, a submenu of it, and so on down are open or hovered.
		// The first element indexes fMenus, each one after it indexes the
		// fItems of whatever the previous named -- so the whole open chain is
		// one vector, and closing back to a level is a resize.
		typedef std::vector< int > Path;

		// A drop-down's box, worked out from the items in it. Recomputed
		// whenever the path changes rather than cached per menu, since a menu
		// is only ever measured while it is open.
		struct PopupBounds
		{
			float fX;
			float fY;
			float fWidth;
			float fHeight;
		};

		const std::vector< MenuItem >* ItemsAt( const Path& path, size_t depth ) const;
		PopupBounds BoundsFor( const Path& path, size_t depth ) const;

		// Which item of the popup at `depth` a point is over, or -1. Separators
		// never match, so dragging across one leaves the previous item hovered.
		int ItemAt( const Path& path, size_t depth, int x, int y ) const;

		// The deepest open popup containing the point, as a path, or an empty
		// path if none does. The deepest is what matters: submenus overlap
		// their parents, and the one on top is the one that gets the event.
		bool PathAtPoint( int x, int y, Path& out ) const;

		void MeasureTitles();
		float TitleWidth( size_t index ) const;
		float TitleX( size_t index ) const;

		void Choose( const Path& path );

		void RenderPopup( const Path& path, size_t depth );

		BgfxUIDraw* fDraw;

		std::vector< Menu > fMenus;

		// Laid out once per SetMenus, since neither the titles nor the font
		// change afterwards.
		std::vector< float > fTitleWidths;

		CommandProc fProc;
		void* fUserdata;

		std::string fStatusText;

		bool fEnabled;

		// -1 when nothing is open. Otherwise the index into fMenus of the
		// drop-down that is showing.
		int fOpenMenu;

		// The open chain below fOpenMenu: submenus that a hover walked into.
		Path fOpenPath;

		// What the mouse is over, which is drawn highlighted. A superset of
		// fOpenPath: an item that is not a submenu highlights without opening
		// anything.
		Path fHotPath;

		// What Render last put on screen. Compared against the live state
		// rather than having every mutation remember to raise a flag -- there
		// are a dozen places the hover changes, and one of them forgetting is
		// a bar that silently stops tracking the mouse.
		int fDrawnOpenMenu;
		Path fDrawnOpenPath;
		Path fDrawnHotPath;
		bool fDrawnEnabled;
		std::string fDrawnStatusText;
		bool fEverDrawn;

		// Set while the mouse is down inside the bar's own title row. A click
		// that opens a menu must not also close it on the way up, but a click
		// on an already-open title should -- so the release only acts on a
		// press it saw.
		bool fPressed;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_MenuBar_H__
