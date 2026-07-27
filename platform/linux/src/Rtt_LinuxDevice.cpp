//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"
#include "Rtt_LinuxDevice.h"
#include <sys/utsname.h>
#include <SDL2/SDL.h>

namespace Rtt
{
	static utsname uts;

#pragma region Constructors / Destructors
	LinuxDevice::LinuxDevice(Rtt_Allocator& allocator)
		: fAllocator(allocator),
		fInputDeviceManager(&allocator),
		fOrientation(DeviceOrientation::kUnknown)
	{
	}

	LinuxDevice::~LinuxDevice()
	{
	}

#pragma endregion

#pragma region Public Member Functions

	void LinuxDevice::SetOrientation(DeviceOrientation::Type orientation)
	{
		fOrientation = orientation;
	}

	void LinuxDevice::Vibrate() const
	{
	}

	void LinuxDevice::SetAccelerometerInterval(U32 frequency) const
	{
	}

	void LinuxDevice::SetGyroscopeInterval(U32 frequency) const
	{
	}

	bool LinuxDevice::HasEventSource(EventType type) const
	{
		bool hasEventSource = false;

		switch (type)
		{
		case MPlatformDevice::kGyroscopeEvent:
			break;
		case MPlatformDevice::kOrientationEvent:
		case MPlatformDevice::kLocationEvent:
		case MPlatformDevice::kHeadingEvent:
		case MPlatformDevice::kMultitouchEvent:
		case MPlatformDevice::kKeyEvent:
		case MPlatformDevice::kAccelerometerEvent:
			hasEventSource = true;
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
		}
		return hasEventSource;
	}

	void LinuxDevice::BeginNotifications(EventType type) const
	{
		fTracker.BeginNotifications(type);

		switch (type)
		{
		case MPlatformDevice::kOrientationEvent:
		case MPlatformDevice::kLocationEvent:
		case MPlatformDevice::kAccelerometerEvent:
		case MPlatformDevice::kGyroscopeEvent:
		case MPlatformDevice::kHeadingEvent:
		case MPlatformDevice::kMultitouchEvent:
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
		}
	}

	void LinuxDevice::EndNotifications(EventType type) const
	{
		fTracker.EndNotifications(type);

		switch (type)
		{
		case MPlatformDevice::kOrientationEvent:
		case MPlatformDevice::kLocationEvent:
		case MPlatformDevice::kAccelerometerEvent:
		case MPlatformDevice::kGyroscopeEvent:
		case MPlatformDevice::kHeadingEvent:
		case MPlatformDevice::kMultitouchEvent:
			break;
		default:
			Rtt_ASSERT_NOT_REACHED();
			break;
		}
	}

	bool LinuxDevice::DoesNotify(EventType type) const
	{
		return fTracker.DoesNotify(type);
	}

	const char* LinuxDevice::GetModel() const
	{
		uname(&uts);
		return uts.nodename ? uts.nodename : "";		// ubuntu
	}

	const char* LinuxDevice::GetName() const
	{
		uname(&uts);
		return uts.release ? uts.release : "";
	}

	const char* LinuxDevice::GetUniqueIdentifier(IdentifierType t) const
	{
		const char* result = "";

		switch (t)
		{
		case MPlatformDevice::kDeviceIdentifier:
			break;
		case MPlatformDevice::kHardwareIdentifier:
			break;
		case MPlatformDevice::kOSIdentifier:
			break;
		case MPlatformDevice::kUdidIdentifier:
			break;
		default:
			break;
		}
		return result;
	}

	MPlatformDevice::EnvironmentType LinuxDevice::GetEnvironment() const
	{
#ifdef Rtt_SIMULATOR
		return kSimulatorEnvironment;
#endif
		return kDeviceEnvironment;
	}

	const char* LinuxDevice::GetPlatformName() const
	{
		uname(&uts);
		return uts.sysname ? uts.sysname : "";
	}

	const char* LinuxDevice::GetPlatformVersion() const
	{
		uname(&uts);
		return uts.version ? uts.version : "";
	}

	const char* LinuxDevice::GetProductName() const
	{
		return "";
	}

	const char* LinuxDevice::GetArchitectureInfo() const
	{
		uname(&uts);
		return uts.machine ? uts.machine : "";
	}

	PlatformInputDeviceManager& LinuxDevice::GetInputDeviceManager()
	{
		return fInputDeviceManager;
	}

	void LinuxDevice::SetLocationAccuracy(Real meters) const
	{
	}

	void LinuxDevice::SetLocationThreshold(Real meters) const
	{
	}

	DeviceOrientation::Type LinuxDevice::GetOrientation() const
	{
		return fOrientation;
	}

	Real LinuxDevice::GetScreenDpi() const
	{
		// A testing override, since the highdpi path is otherwise unreachable
		// on an ordinary 96 dpi monitor.
		if (const char* forced = getenv("LUMIN_FORCE_DPI"))
		{
			return Rtt_FloatToReal((float)atof(forced));
		}

		// Diagonal DPI, which is what SDL reports most consistently across the
		// X11 and Wayland backends. A display that does not report its physical
		// size gives 0, and the caller then renders one unit per pixel.
		float diagonal = 0.0f;

		if (SDL_GetDisplayDPI(0, &diagonal, NULL, NULL) != 0)
		{
			return Rtt_REAL_0;
		}

		return Rtt_FloatToReal(diagonal);
	}

	const char* LinuxDevice::GetPlatform() const
	{
		uname(&uts);
		return uts.sysname ? uts.sysname : "";
	}

	const char* LinuxDevice::GetManufacturer() const
	{
		return "Solar2D";
	}

#pragma endregion
}; // namespace Rtt
