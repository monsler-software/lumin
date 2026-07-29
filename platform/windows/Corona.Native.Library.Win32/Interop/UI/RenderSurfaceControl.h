//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Core\Rtt_Macros.h"
#include "Interop\Event.h"
#include "Interop\HandledEventArgs.h"
#include "Control.h"
#include "Renderer\Rtt_BgfxSurfaceParams.h"
#include <memory>
#include <string>
#include <Windows.h>


namespace Interop { namespace UI {

/// <summary>Represents a control that can be rendered to via OpenGL.</summary>
class RenderSurfaceControl : public Control
{
	Rtt_CLASS_NO_COPIES(RenderSurfaceControl)

	public:
		#pragma region Public Event Types
		/// <summary>
		///  Defines a "RenderFrame" event type which is raised when this surface is requesting one frame to be drawn.
		/// </summary>
		typedef Event<RenderSurfaceControl&, HandledEventArgs&> RenderFrameEvent;

		#pragma endregion


		#pragma region Version Class
		/// <summary>
		///  Stores the major/minor version numbers of the rendering driver that is used to render to the control.
		/// </summary>
		class Version
		{
			public:
				/// <summary>Creates a new object with all version numbers initialized to zero.</summary>
				Version();

				/// <summary>Creates a new object initialized with the given major and minor version numbers.</summary>
				Version(int majorNumber, int minorNumber);

				/// <summary>Gets the rendering driver's version string. Can be null.</summary>
				/// <returns>
				///  <para>Returns the rendering driver's version string.</para>
				///  <para>Returns null or empty string if not assigned.</para>
				/// </returns>
				const char* GetString() const;

				/// <summary>Sets the rendering driver's version string.</summary>
				/// <param name="value">The version string. Can be null or empty.</param>
				void SetString(const char* value);

				/// <summary>Gets the rendering driver's "major" version number.</summary>
				/// <returns>Returns the driver's major version number. Returns zero if not assigned.</returns>
				int GetMajorNumber() const;

				/// <summary>Sets the rendering driver's "major" version number.</summary>
				/// <param name="value">The major version number.</param>
				void SetMajorNumber(int value);

				/// <summary>Gets the rendering driver's "minor" version number.</summary>
				/// <returns>Returns the driver's minor version number. Returns zero if not assigned.</returns>
				int GetMinorNumber() const;

				/// <summary>Sets the rendering driver's "minor" version number.</summary>
				/// <param name="value">The minor version number.</param>
				void SetMinorNumber(int value);

				/// <summary>Compares this object's version numbers with the given object's version numbers.</summary>
				/// <param name="version">The version object to be compared with.</param>
				/// <returns>
				///  <para>Returns a positive number if this version is greater than the given version.</para>
				///  <para>Returns zero if this version matches/equals the given version.</para>
				///  <para>Returns a negative number if this version is less than the given version.</para>
				/// </returns>
				int CompareTo(const Version& version) const;

			private:
				std::shared_ptr<std::string> fVersionString;
				int fMajorNumber;
				int fMinorNumber;
		};

		#pragma endregion

		#pragma region Params Class
		/// <summary>
		///  Construction parameters for the control, originating from user settings.
		/// </summary>
		class Params {
		public:
			Params();

			/// <summary>Set Vulkan as the preferred rendering backend.</summary>
			/// <param name="required">If true, we must use Vulkan, else fail; otherwise, we can fall back to OpenGL.</param>
			void SetVulkanWanted(bool required);
			
			/// <summary>Get the Vulkan-related preferred backend status.</summary>
			/// <returns>Returns true if Vulkan is the preferred backend, false otherwise.</returns>
			bool IsVulkanWanted() const;
			
			/// <summary>Get the Vulkan-related backend necessity status.</summary>
			/// <returns>Returns true if Vulkan must be the backend; if optional or not even requested, false.</returns>
			bool IsVulkanRequired() const;

			/// <summary>Set bgfx as the preferred rendering backend.</summary>
			/// <remarks>
			///  <para>
			///   Unlike Vulkan and OpenGL, bgfx is not given a context this surface made: it takes the window
			///   itself and makes its own. So there is nothing here to create or select, and the surface's job
			///   for bgfx is only to describe the window.
			///  </para>
			/// </remarks>
			/// <param name="required">If true, we must use bgfx, else fail; otherwise, we can fall back to OpenGL.</param>
			void SetBgfxWanted(bool required);

			/// <summary>Get the bgfx-related preferred backend status.</summary>
			/// <returns>Returns true if bgfx is the preferred backend, false otherwise.</returns>
			bool IsBgfxWanted() const;

			/// <summary>Get the bgfx-related backend necessity status.</summary>
			/// <returns>Returns true if bgfx must be the backend; if optional or not even requested, false.</returns>
			bool IsBgfxRequired() const;

		private:
			bool fWantVulkan;
			bool fRequireVulkan;
			bool fWantBgfx;
			bool fRequireBgfx;
		};

		#pragma region Constructors/Destructors
		/// <summary>Creates a new render surface which wraps the given window handle.</summary>
		/// <param name="windowHandle">
		///  <para>Handle to a Windows control to render to.</para>
		///  <para>Can be null, but then this render surface object will do nothing.</para>
		/// </param>
		RenderSurfaceControl(HWND windowHandle, const Params & params = Params());

		/// <summary>Destroys this object.</summary>
		virtual ~RenderSurfaceControl();

		#pragma endregion


		#pragma region Public Methods
		/// <summary>Determines if this control is currently able to render to its surface.</summary>
		/// <returns>
		///  <para>Returns true if this control is ready to render now.</para>
		///  <para>
		///   Returns false if this object is not referencing a control/window or if it failed
		///   to create a rendering context.
		///  </para>
		/// </returns>
		bool CanRender() const;

		/// <summary>
		///  Gets the &lt;major&gt;.&lt;minor&gt; version number of the rendering driver that is
		///  currently being used to render to this surface.
		/// </summary>
		/// <returns>
		///  <para>Returns the rendering driver's major and minor version numbers.</para>
		///  <para>
		///   Returns version numbers set to zero if this control is not referencing a window or if it
		///   failed to attach a rendering context to to it.
		///  </para>
		/// </returns>
		RenderSurfaceControl::Version GetRendererVersion() const;

		/// <summary>
		///  <para>Sets an event handler to be invoked when the surface is requesting a frame to be rendered to it.</para>
		///  <para>Note that this handler will typically be invoked after a call to the RequestRender() method.</para>
		/// </summary>
		/// <param name="handlerPointer">
		///  <para>The handler to be invoked when the surface is ready to have something rendered to it.</para>
		///  <para>Can be null, in which case the surface will draw a black screen when requested to render.</para>
		/// </param>
		void SetRenderFrameHandler(RenderFrameEvent::Handler *handlerPointer);

		/// <summary>
		///  <para>Makes this surface's rendering context the calling thread's current context.</para>
		///  <para>All subsequent rendering function calls will then be made to this surface's context.</para>
		/// </summary>
		void SelectRenderingContext();

		/// <summary>
		///  Swaps the rendering surface's back buffer with the front buffer, the last rendered content appear onscreen.
		/// </summary>
		void SwapBuffers();

		/// <summary>Requests this surface to render another frame.</summary>
		/// <remarks>
		///  Calling this method wil cause this surface to invoke the handler given to the SetRenderFramHandler()
		///  method once the surface is ready to have something drawn to it.
		/// </remarks>
		void RequestRender();

		/// <summary>A callback given first refusal on this surface's Windows messages.</summary>
		/// <remarks>
		///  For whatever the host draws over the surface -- the simulator's menu bar, so far. Return true to
		///  consume the message, in which case Corona never sees it: a click that opens a menu is not also a
		///  touch on the content underneath.
		/// </remarks>
		typedef bool (*MessageFilterProc)(void *userdata, UINT messageId, WPARAM wParam, LPARAM lParam);

		/// <summary>Sets the callback given first refusal on this surface's Windows messages.</summary>
		/// <param name="proc">The callback. Null removes whatever was set.</param>
		/// <param name="userdata">Passed back to the callback unread.</param>
		void SetMessageFilter(MessageFilterProc proc, void *userdata);

		/// <summary>Reserves the top of the surface for whatever the host draws over it.</summary>
		/// <remarks>
		///  <para>
		///   The simulator's menu bar is drawn into the same surface Corona renders into -- it has to be, since
		///   bgfx renders into one window -- so the top of that surface is not Corona's to use. This is how much
		///   of it is spoken for; Corona is told a surface that much shorter, and lays its content out below.
		///  </para>
		///  <para>
		///   The surface itself is not resized. bgfx still presents the whole window, which is what leaves the
		///   reserved strip somewhere for the host to draw.
		///  </para>
		/// </remarks>
		/// <param name="height">Height in pixels. Zero, the default, leaves the whole surface to Corona.</param>
		void SetOverlayHeight(int height);

		/// <summary>Gets how much of the top of the surface is reserved. See SetOverlayHeight().</summary>
		int GetOverlayHeight() const;

		bool IsUsingVulkanBackend() const { return !!fVulkanContext; }

		/// <summary>Determines whether the runtime should be pointed at the bgfx backend.</summary>
		/// <returns>Returns true if this surface described a window for bgfx to render into.</returns>
		bool IsUsingBgfxBackend() const { return fIsUsingBgfx; }

		void * GetBackendContext() const;

		#pragma endregion

	protected:
		/// <summary>
		///  <para>Called just after the "Destroying" event was raised.</para>
		///  <para>
		///   Performs final cleanup after the component's event handlers have performed their final operations on it.
		///  </para>
		/// </summary>
		virtual void OnRaisedDestroyingEvent();

	private:
		#pragma region FetchMultisampleFormatResult Struct
		/// <summary>Provides multisample format information returned by the FetchMultisampleFormat() method.</summary>
		struct FetchMultisampleFormatResult
		{
			/// <summary>Set true if the rendering driver/hardware supports multisampling. False if not.</summary>
			bool IsSupported;

			/// <summary>
			///  <para>
			///   Index to the best multisampling pixel format to be given to the Win32 ::SetPixelFormat() function.
			///  </para>
			///  <para>
			///   This field should be ignored if the "IsSupported" field is set false,
			///   indicating that multisampling is not supported.
			///  </para>
			/// </summary>
			int PixelFormatIndex;

			/// <summary>Creates a new result object initialized to "not supported".</summary>
			FetchMultisampleFormatResult()
			:	IsSupported(false),
				PixelFormatIndex(-1)
			{ }
		};

		#pragma endregion


		#pragma region Private Methods
		/// <summary>
		///  Determines if the rendering driver supports multisampling, and if so, provides the best pixel format.
		/// </summary>
		/// <returns>Returns the requested multisample format information.</returns>
		FetchMultisampleFormatResult FetchMultisampleFormat();

		/// <summary>
		///  <para>Creates a new rendering context for the currently referenced control.</para>
		///  <para>Will destroy the last context if still active.</para>
		/// </summary>
		void CreateContext(const Params & params);

		/// <summary>Destroys the last created rendering context.</summary>
		void DestroyContext();

		/// <summary>Called when a Windows message has been dispatched to this control.</summary>
		/// <param name="sender">Reference to this control.</param>
		/// <param name="arguments">
		///  <para>Provides the Windows message information.</para>
		///  <para>Call its SetHandled() and SetReturnValue() methods if this handler will be handling the message.</para>
		/// </param>
		void OnReceivedMessage(UIComponent& sender, HandleMessageEventArgs& arguments);

		/// <summary>Called when the control is requesting to have its surface re-painted.</summary>
		void OnPaint();

		#pragma endregion


		#pragma region Private Member Variables
		/// <summary>Handler to be invoked when the "ReceivedMessage" event has been raised.</summary>
		UIComponent::ReceivedMessageEvent::MethodHandler<RenderSurfaceControl> fReceivedMessageEventHandler;

		/// <summary>Pointer to one "RenderFrame" event handler.</summary>
		RenderFrameEvent::Handler* fRenderFrameEventHandlerPointer;

		/// <summary>Handle to the control's main device context that OpenGL will render to.</summary>
		HDC fMainDeviceContextHandle;

		/// <summary>Handle to OpenGL's rendering context.</summary>
		HGLRC fRenderingContextHandle;

		/// <summary>Stores the major/minor version number of the OpenGL driver that is rendering to this surface.</summary>
		RenderSurfaceControl::Version fRendererVersion;

		/// <summary>Vulkan analogue to the GL context, when chosen as the backend.</summary>
		void * fVulkanContext;

		/// <summary>How much of the top of the surface the host has reserved. See SetOverlayHeight().</summary>
		int fOverlayHeight;

		/// <summary>The host's first-refusal message callback, or null. See SetMessageFilter().</summary>
		MessageFilterProc fMessageFilterProc;

		/// <summary>Passed to fMessageFilterProc unread.</summary>
		void *fMessageFilterUserdata;

		/// <summary>Set true once this surface has described its window for bgfx.</summary>
		/// <remarks>
		///  There is no context to hold, unlike the other two backends, so this is what "the surface is a bgfx
		///  surface" is: bgfx itself is brought up later, by the renderer, out of the description below.
		/// </remarks>
		bool fIsUsingBgfx;

		/// <summary>What bgfx is given to reach this control's window. Only meaningful when fIsUsingBgfx.</summary>
		Rtt::BgfxSurfaceParams fBgfxSurfaceParams;

		#pragma endregion
};

} }	// namespace Interop::UI
