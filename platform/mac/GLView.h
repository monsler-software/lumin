//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#import <AppKit/AppKit.h>
#import "GLViewDelegate.h"

//#import <AppKit/NSView.h>

#include "Rtt_DeviceOrientation.h"

namespace Rtt
{
	class ApplePlatform;
	class Runtime;
} 

@class NSImage;
@class SPILDTopLayerView;

// Under bgfx this is a plain view.
//
// bgfx creates the graphics context itself, out of the window, and an NSOpenGLView would be a second context
// on the same surface -- two renderers taking turns presenting it, which looks exactly like the flicker it
// is. The view's job either way is the same: hand Corona its size, its input, and a place to draw.
#if defined( Rtt_USE_BGFX )
@interface GLView : NSView
#else
@interface GLView : NSOpenGLView
#endif
{
	Rtt::Runtime* fRuntime;
	NSPoint fStartPosition;
	float fFirstClickTime;
	NSTimeInterval fTapDelay;

	id< GLViewDelegate > fDelegate;
	U8 fNumTaps;
	Rtt::DeviceOrientation::Type fOrientation;

	NSRect nativeFrameRect; // currently only settable via initWithFrame:
	CGFloat zoomLevel; // only for bookkeeping purposes
	SPILDTopLayerView* suspendedOverlay;
    BOOL isReady;
    BOOL swapped;
    BOOL isSimulatorView;
	BOOL shouldInvalidate;
    NSMutableArray *fCursorRects;
	NSTrackingRectTag trackingRectTag;
	int numCursorHides;
#if Rtt_AUTHORING_SIMULATOR
	float lastTouchPressure;
	NSPoint lastPressurePoint;
#endif
}

@property (nonatomic, readwrite, getter=runtime, setter=setRuntime:) Rtt::Runtime *fRuntime;
@property (nonatomic, readwrite, getter=tapDelay, setter=setTapDelay:) NSTimeInterval fTapDelay;
@property (nonatomic, assign) CGFloat zoomLevel;
@property (nonatomic, assign) CGFloat scaleFactor;
@property (nonatomic, assign) BOOL isReady;
@property (nonatomic, assign) BOOL swapped;
@property (nonatomic, assign) BOOL isSimulatorView;
@property (nonatomic, assign) BOOL sendAllMouseEvents;
@property (nonatomic, assign) BOOL inFullScreenTransition;
@property (nonatomic, assign) BOOL isResizable;
@property (nonatomic, assign) BOOL allowOverlay;
@property (nonatomic, assign) BOOL cursorHidden;
@property (nonatomic, assign) NSPoint initialLocation;

+ (NSOpenGLPixelFormat*) basicPixelFormat;

- (void)setDelegate:(id< GLViewDelegate >)delegate;

// This method will force the Corona OpenGL renderer to redraw everything.
- (void) invalidate;
- (void) restoreWindowProperties;

- (Rtt::DeviceOrientation::Type)orientation;
- (void)setOrientation:(Rtt::DeviceOrientation::Type)newValue;
- (void)adjustPoint:(NSPoint*)p;
- (NSPoint)pointForEvent:(NSEvent*)event;

- (CGFloat) viewportWidth;
- (CGFloat) viewportHeight;

- (CGFloat)deviceWidth;
- (CGFloat)deviceHeight;


//- (CGFloat)uprightWidth;
//- (CGFloat)uprightHeight;

// Brings the renderer up once there is a window to render into, and tells the delegate.
//
// Called from prepareOpenGL under OpenGL, which is AppKit's own "the context is ready" hook, and from
// viewDidMoveToWindow under bgfx, which has no such hook because it has no context of AppKit's.
- (void) prepareRenderer;

// How much of the top of this view the simulator's menu bar takes.
//
// The bar is drawn by bgfx into the same surface Corona renders into, because bgfx renders into one window.
// So this view is that much taller than the content, and everything measured against the content -- the
// viewport, where a click landed -- has to take it off first. Zero without bgfx, and zero on a view that is
// not the simulator's.
- (CGFloat) menuBarHeight;

- (void) suspendNativeDisplayObjects:(bool) showOverlay;
- (void) resumeNativeDisplayObjects;

- (void) setCursor:(const char *) cursorName forRect:(NSRect) bounds;
- (void) hideCursor;
- (void) showCursor;
@end
