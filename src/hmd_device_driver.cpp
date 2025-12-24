//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "hmd_device_driver.h"

#include "driverlog.h"
#include "vrmath.h"
#include <string.h>
#include "display_edid_finder.h"
#include "device_provider.h" // need full definition for GetImuOrientation
#include <cmath>

// Let's create some variables for strings used in getting settings.
// This is the section where all of the settings we want are stored. A section name can be anything,
// but if you want to store driver specific settings, it's best to namespace the section with the driver identifier
// ie "<my_driver>_<section>" to avoid collisions
static const char *my_hmd_main_settings_section = "driver_simplehmd";
static const char *my_hmd_display_settings_section = "simplehmd_display";

MyHMDControllerDeviceDriver::MyHMDControllerDeviceDriver()
{
	// Keep track of whether Activate() has been called
	is_active_ = false;

	// Wait (up to 5 seconds) for the 3D mode display to appear with EDID (product=980, serial=17).
	// Switching to 3D reportedly changes the display's identifiers to these values.
	std::optional<DisplayEdidInfo> edidMatch;
	{
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			try {
				edidMatch = DisplayEdidFinder::FindDisplayByEdid(980, 17);
			} catch(...) {
				DriverLog("Exception while polling EDID (ignored)");
			}
			if (edidMatch) break;
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
		if (edidMatch) {
			DriverLog("EDID (3D) display detected after wait: instance='%s' product=%u serial=%u name='%s'", edidMatch->device_instance_id.c_str(), edidMatch->product_code, edidMatch->serial_number, edidMatch->monitor_name.c_str());
		} else {
			DriverLog("EDID (product=980 serial=17) not present within 5s wait; using fallback settings.");
		}
	}

	// We have our model number and serial number stored in SteamVR settings. We need to get them and do so here.
	// Other IVRSettings methods (to get int32, floats, bools) return the data, instead of modifying, but strings are
	// different.
	// char model_number[ 1024 ];
	// vr::VRSettings()->GetString( my_hmd_main_settings_section, "model_number", model_number, sizeof( model_number ) );
	// my_hmd_model_number_ = model_number;

	// // Get our serial number depending on our "handedness"
	// char serial_number[ 1024 ];
	// vr::VRSettings()->GetString( my_hmd_main_settings_section, "serial_number", serial_number, sizeof( serial_number ) );
	// my_hmd_serial_number_ = serial_number;

	// Here's an example of how to use our logging wrapper around IVRDriverLog
	// In SteamVR logs (SteamVR Hamburger Menu > Developer Settings > Web console) drivers have a prefix of
	// "<driver_name>:". You can search this in the top search bar to find the info that you've logged.
	DriverLog( "My Dummy HMD Model Number: %s", my_hmd_model_number_.c_str() );
	DriverLog( "My Dummy HMD Serial Number: %s", my_hmd_serial_number_.c_str() );

	// Display settings derived from EDID if available (preferred timing). Fallback to hardcoded defaults.
	uint32_t winX = 2560;
	uint32_t winY = 370;
	uint32_t width = 1920;
	uint32_t height = 1080;
	uint32_t renderW = width;
	uint32_t renderH = height;

	// Use previously found EDID match (product 980 serial 17) to adjust width/height if preferred mode parsed.
	if (edidMatch) {
		// Resolution from EDID preferred timing
		if (edidMatch->preferred_width && edidMatch->preferred_height) {
			width = edidMatch->preferred_width;
			height = edidMatch->preferred_height;
			renderW = width;
			renderH = height;
			DriverLog("RayNeo Using EDID preferred mode %ux%u", width, height);
		}
		DisplayEdidInfo tmp = *edidMatch;
		if (DisplayEdidFinder::PopulateDesktopCoordinates(tmp)) {
			winX = static_cast<uint32_t>(tmp.desktop_x);
			winY = static_cast<uint32_t>(tmp.desktop_y);
			DriverLog("RayNeo Using monitor desktop origin (%d,%d)", tmp.desktop_x, tmp.desktop_y);
		}
	}
	std::this_thread::sleep_for(std::chrono::seconds(2));

	MyHMDDisplayDriverConfiguration display_configuration{
		static_cast<int32_t>(winX), static_cast<int32_t>(winY),
		static_cast<int32_t>(width), static_cast<int32_t>(height),
		static_cast<int32_t>(renderW), static_cast<int32_t>(renderH)
	};
	// display_configuration.window_x = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "window_x" );
	// display_configuration.window_y = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "window_y" );

	// display_configuration.window_width = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "window_width" );
	// display_configuration.window_height = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "window_height" );

	// display_configuration.render_width = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "render_width" );
	// display_configuration.render_height = vr::VRSettings()->GetInt32( my_hmd_display_settings_section, "render_height" );

	// Instantiate our display component
	my_display_component_ = std::make_unique< MyHMDDisplayComponent >( display_configuration );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver after our
//  IServerTrackedDeviceProvider calls IVRServerDriverHost::TrackedDeviceAdded.
//-----------------------------------------------------------------------------
vr::EVRInitError MyHMDControllerDeviceDriver::Activate( uint32_t unObjectId )
{
	// Let's keep track of our device index. It'll be useful later.
	// Also, if we re-activate, be sure to set this.
	device_index_ = unObjectId;

	// Set a member to keep track of whether we've activated yet or not
	is_active_ = true;

	// For keeping track of frame number for animating motion.
	frame_number_ = 0;

	// Properties are stored in containers, usually one container per device index. We need to get this container to set
	// The properties we want, so we call this to retrieve a handle to it.
	vr::PropertyContainerHandle_t container = vr::VRProperties()->TrackedDeviceToPropertyContainer( device_index_ );

	// Let's begin setting up the properties now we've got our container.
	// A list of properties available is contained in vr::ETrackedDeviceProperty.

	// First, let's set the model number.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ModelNumber_String, my_hmd_model_number_.c_str() );

	// Set controller type to match our input profile controller_type
	vr::VRProperties()->SetStringProperty( container, vr::Prop_ControllerType_String, "rayneo_hmd" );

	// Next, display settings

	// Get the ipd of the user from SteamVR settings
	const float ipd = vr::VRSettings()->GetFloat( vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_IPD_Float );
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_UserIpdMeters_Float, ipd );

	// For HMDs, it's required that a refresh rate is set otherwise VRCompositor will fail to start.
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_DisplayFrequency_Float, 60.0f );

	// The distance from the user's eyes to the display in meters. This is used for reprojection.
	// vr::VRProperties()->SetFloatProperty( container, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f );
	vr::VRProperties()->SetFloatProperty(container, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.02f);

	// How long from the compositor to submit a frame to the time it takes to display it on the screen.
	vr::VRProperties()->SetFloatProperty( container, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.11f );

	// avoid "not fullscreen" warnings from vrmonitor
	vr::VRProperties()->SetBoolProperty( container, vr::Prop_IsOnDesktop_Bool, true );

	vr::VRProperties()->SetBoolProperty(container, vr::Prop_DisplayDebugMode_Bool, false);

	// Now let's set up our inputs
	// This tells the UI what to show the user for bindings for this controller,
	// As well as what default bindings should be for legacy apps.
	// Note, we can use the wildcard {<driver_name>} to match the root folder location
	// of our driver.
	vr::VRProperties()->SetStringProperty( container, vr::Prop_InputProfilePath_String, "{rayneo}/input/rayneo_hmd_profile.json" );
	DriverLog("Set input profile path to: {rayneo}/input/rayneo_hmd_profile.json");

	// Let's set up handles for all of our components.
	// Even though these are also defined in our input profile,
	// We need to get handles to them to update the inputs.
	// Per IVRDriverInput documentation, CreateBooleanComponent returns EVRInputError.
	vr::EVRInputError err;
	err = vr::VRDriverInput()->CreateBooleanComponent( container, "/input/system/touch", &my_input_handles_[ MyComponent_system_touch ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create system/touch component: %d", err);
	} else {
		DriverLog("Created system/touch, handle=%llu", my_input_handles_[MyComponent_system_touch]);
	}
	err = vr::VRDriverInput()->CreateBooleanComponent( container, "/input/system/click", &my_input_handles_[ MyComponent_system_click ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create system/click component: %d", err);
	} else {
		DriverLog("Created system/click, handle=%llu", my_input_handles_[MyComponent_system_click]);
	}
	// Additional RayNeo button mappings
	err = vr::VRDriverInput()->CreateBooleanComponent( container, "/input/application_menu/click", &my_input_handles_[ MyComponent_application_menu_click ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create application_menu/click component: %d", err);
	} else {
		DriverLog("Created application_menu/click, handle=%llu", my_input_handles_[MyComponent_application_menu_click]);
	}
	err = vr::VRDriverInput()->CreateBooleanComponent( container, "/input/grip/click", &my_input_handles_[ MyComponent_grip_click ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create grip/click component: %d", err);
	} else {
		DriverLog("Created grip/click, handle=%llu", my_input_handles_[MyComponent_grip_click]);
	}
	err = vr::VRDriverInput()->CreateBooleanComponent( container, "/input/trigger/click", &my_input_handles_[ MyComponent_trigger_click ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create trigger/click component: %d", err);
	} else {
		DriverLog("Created trigger/click, handle=%llu", my_input_handles_[MyComponent_trigger_click]);
	}

	// Create haptic output component for vibration feedback
	// Per IVRDriverInput docs: haptic events will arrive as VREvent_Input_HapticVibration
	err = vr::VRDriverInput()->CreateHapticComponent( container, "/output/haptic", &my_input_handles_[ MyComponent_haptic ] );
	if (err != vr::VRInputError_None) {
		DriverLog("Failed to create haptic component: %d", err);
	} else {
		DriverLog("Created haptic, handle=%llu", my_input_handles_[MyComponent_haptic]);
	}

	my_pose_update_thread_ = std::thread( &MyHMDControllerDeviceDriver::MyPoseUpdateThread, this );

	// We've activated everything successfully!
	// Basic static properties already set above; no duplicate container variable.

	// RayNeo lifecycle moved to MyDeviceProvider (context + event thread). Device activation no longer starts RayNeo.

	// Let's tell SteamVR that by saying we don't have any errors.
	return vr::VRInitError_None;
}


//-----------------------------------------------------------------------------
// Purpose: If you're an HMD, this is where you would return an implementation
// of vr::IVRDisplayComponent, vr::IVRVirtualDisplay or vr::IVRDirectModeComponent.
//-----------------------------------------------------------------------------
void *MyHMDControllerDeviceDriver::GetComponent( const char *pchComponentNameAndVersion )
{
	if ( strcmp( pchComponentNameAndVersion, vr::IVRDisplayComponent_Version ) == 0 )
	{
		return my_display_component_.get();
	}

	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when a debug request has been made from an application to the driver.
// What is in the response and request is up to the application and driver to figure out themselves.
//-----------------------------------------------------------------------------
void MyHMDControllerDeviceDriver::DebugRequest( const char *pchRequest, char *pchResponseBuffer, uint32_t unResponseBufferSize )
{
	if ( unResponseBufferSize >= 1 )
		pchResponseBuffer[ 0 ] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: This is never called by vrserver in recent OpenVR versions,
// but is useful for giving data to vr::VRServerDriverHost::TrackedDevicePoseUpdated.
//-----------------------------------------------------------------------------
vr::DriverPose_t MyHMDControllerDeviceDriver::GetPose()
{
	// Let's retrieve the Hmd pose to base our controller pose off.

	// First, initialize the struct that we'll be submitting to the runtime to tell it we've updated our pose.
	vr::DriverPose_t pose = { 0 };

	// These need to be set to be valid quaternions. The device won't appear otherwise.
	pose.qWorldFromDriverRotation.w = 1.f;
	pose.qDriverFromHeadRotation.w = 1.f;

	// Obtain orientation from provider's IMU integration if available
	float qw=1.f, qx=0.f, qy=0.f, qz=0.f;
	if (auto *prov = GetMyDeviceProviderInstance()) {
		prov->GetImuOrientation(qw,qx,qy,qz);
		// Validate quaternion (normalize, fallback to identity if degenerate)
		float nrm = std::sqrt(qw*qw + qx*qx + qy*qy + qz*qz);
		if (nrm > 0.00001f && std::isfinite(nrm)) {
			qw /= nrm; qx /= nrm; qy /= nrm; qz /= nrm;
		} else {
			qw = 1.f; qx = qy = qz = 0.f;
		}
	}
	pose.qRotation.w = qw;
	pose.qRotation.x = qx;
	pose.qRotation.y = qy;
	pose.qRotation.z = qz;

	// Position (simple demo). If sleeping, keep fixed.
	bool sleeping = false;
	if (auto *prov = GetMyDeviceProviderInstance()) sleeping = prov->IsSleeping();
	pose.vecPosition[0] = 0.0f;
	pose.vecPosition[1] = sleeping ? 1.0f : 1.5f;
	pose.vecPosition[2] = 0.0f;

	// The pose we provide: when sleeping, mark invalid/out-of-range to hint standby.
	pose.poseIsValid = !sleeping;
	if (sleeping) {
		pose.result = vr::TrackingResult_Running_OutOfRange;
	}

	// Our device is always connected.
	// In reality with physical devices, when they get disconnected,
	// set this to false and icons in SteamVR will be updated to show the device is disconnected
	pose.deviceIsConnected = true;

	// The state of our tracking. For our virtual device, it's always going to be ok,
	// but this can get set differently to inform the runtime about the state of the device's tracking
	// and update the icons to inform the user accordingly.
	pose.result = vr::TrackingResult_Running_OK;

	// For HMDs we want to apply rotation/motion prediction
	pose.shouldApplyHeadModel = true;

	return pose;
}

void MyHMDControllerDeviceDriver::MyPoseUpdateThread()
{
	while ( is_active_ )
	{
		// Inform the vrserver that our tracked device's pose has updated, giving it the pose returned by our GetPose().
		vr::VRServerDriverHost()->TrackedDevicePoseUpdated( device_index_, GetPose(), sizeof( vr::DriverPose_t ) );

		// Update our pose every five milliseconds.
		// In reality, you should update the pose whenever you have new data from your device.
		std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should enter standby mode.
// The device should be put into whatever low power mode it has.
// We don't really have anything to do here, so let's just log something.
//-----------------------------------------------------------------------------
void MyHMDControllerDeviceDriver::EnterStandby()
{
	DriverLog( "HMD has been put into standby." );
}

//-----------------------------------------------------------------------------
// Purpose: This is called by vrserver when the device should deactivate.
// This is typically at the end of a session
// The device should free any resources it has allocated here.
//-----------------------------------------------------------------------------
void MyHMDControllerDeviceDriver::Deactivate()
{
	// Let's join our pose thread that's running
	// by first checking then setting is_active_ to false to break out
	// of the while loop, if it's running, then call .join() on the thread
	if ( is_active_.exchange( false ) )
	{
		my_pose_update_thread_.join();
	}

	// RayNeo teardown moved to MyDeviceProvider.

	// unassign our controller index (we don't want to be calling vrserver anymore after Deactivate() has been called
	device_index_ = vr::k_unTrackedDeviceIndexInvalid;
}


//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when its RunFrame() method gets called.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
void MyHMDControllerDeviceDriver::MyRunFrame()
{
	frame_number_++;
	// Update our inputs here using IVRDriverInput::UpdateBooleanComponent
	// Per IVRDriverInput documentation:
	// - UpdateBooleanComponent should be called whenever the current state of an input component changes
	// - fTimeOffset parameter is relative to now (negative=past, positive=future)
	// - Should include transmission latency from physical hardware
	// Sleep signaling now handled via pose flags in GetPose()

	// Check for new button presses from RayNeo hardware
	if (auto *prov = GetMyDeviceProviderInstance()) {
		// System button - starts press sequence
		if (prov->ConsumeButtonNotifyPending()) {
			DriverLog("[HMD] System button event - starting press");
			button_system_frames_remaining_ = 30;
		}
		// Trigger button
		if (prov->ConsumeTriggerClickPending()) {
			DriverLog("[HMD] Trigger button event - starting press");
			button_trigger_frames_remaining_ = 30;
		}
		// Grip button
		if (prov->ConsumeGripClickPending()) {
			DriverLog("[HMD] Grip button event - starting press");
			button_grip_frames_remaining_ = 30;
		}
		// App menu button - double click detection for recenter
		if (prov->ConsumeAppMenuClickPending()) {
			auto now = std::chrono::steady_clock::now();
			auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_brightness_click_time_).count();
			
			if (brightness_waiting_for_double_ && time_since_last < 1000) {
				// Double click detected - do recenter
				DriverLog("[HMD] Brightness DOUBLE CLICK - triggering recenter");
				prov->Recenter();
				brightness_waiting_for_double_ = false;
				brightness_single_click_delay_ = 0; // Cancel any pending single click
			} else {
				// First click - start waiting for potential double click
				DriverLog("[HMD] Brightness click - waiting for potential double click");
				brightness_waiting_for_double_ = true;
				brightness_single_click_delay_ = 200; // Wait 10 frames (~50ms) for double click
				last_brightness_click_time_ = now;
			}
		}
	}

	// Process delayed single click for brightness button
	if (brightness_single_click_delay_ > 0) {
		brightness_single_click_delay_--;
		if (brightness_single_click_delay_ == 0 && brightness_waiting_for_double_) {
			// No double click came - process as single click (application menu)
			DriverLog("[HMD] Brightness SINGLE CLICK - application menu");
			button_appmenu_frames_remaining_ = 30;
			brightness_waiting_for_double_ = false;
		}
	}

	// Update button states based on frame counters
	// System button
	if (my_input_handles_[MyComponent_system_click] != vr::k_ulInvalidInputComponentHandle) {
		bool pressed = (button_system_frames_remaining_ > 0);
		vr::VRDriverInput()->UpdateBooleanComponent(my_input_handles_[MyComponent_system_click], pressed, 0.0);
		if (button_system_frames_remaining_ > 0) button_system_frames_remaining_--;
	}

	// Trigger button
	if (my_input_handles_[MyComponent_trigger_click] != vr::k_ulInvalidInputComponentHandle) {
		bool pressed = (button_trigger_frames_remaining_ > 0);
		vr::VRDriverInput()->UpdateBooleanComponent(my_input_handles_[MyComponent_trigger_click], pressed, 0.0);
		if (button_trigger_frames_remaining_ > 0) button_trigger_frames_remaining_--;
	}

	// Grip button
	if (my_input_handles_[MyComponent_grip_click] != vr::k_ulInvalidInputComponentHandle) {
		bool pressed = (button_grip_frames_remaining_ > 0);
		vr::VRDriverInput()->UpdateBooleanComponent(my_input_handles_[MyComponent_grip_click], pressed, 0.0);
		if (button_grip_frames_remaining_ > 0) button_grip_frames_remaining_--;
	}

	// App menu button
	if (my_input_handles_[MyComponent_application_menu_click] != vr::k_ulInvalidInputComponentHandle) {
		bool pressed = (button_appmenu_frames_remaining_ > 0);
		vr::VRDriverInput()->UpdateBooleanComponent(my_input_handles_[MyComponent_application_menu_click], pressed, 0.0);
		if (button_appmenu_frames_remaining_ > 0) button_appmenu_frames_remaining_--;
	}

	
}


//-----------------------------------------------------------------------------
// Purpose: This is called by our IServerTrackedDeviceProvider when it pops an event off the event queue.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
void MyHMDControllerDeviceDriver::MyProcessEvent( const vr::VREvent_t &vrevent )
{
	// Handle haptic vibration events per IVRDriverInput documentation
	switch ( vrevent.eventType )
	{
		case vr::VREvent_Input_HapticVibration:
		{
			// Verify the event is intended for our haptic component
			if ( vrevent.data.hapticVibration.componentHandle == my_input_handles_[ MyComponent_haptic ] )
			{
				// Extract haptic parameters from the event
				float duration = vrevent.data.hapticVibration.fDurationSeconds;
				float frequency = vrevent.data.hapticVibration.fFrequency;
				float amplitude = vrevent.data.hapticVibration.fAmplitude;

				DriverLog( "Haptic event: Duration=%.2fs, Frequency=%.2fHz, Amplitude=%.2f",
					duration, frequency, amplitude );

				// TODO: Forward haptic event to RayNeo hardware
				// For now, just log the event
			}
			break;
		}
		default:
			break;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Our IServerTrackedDeviceProvider needs our serial number to add us to vrserver.
// It's not part of the ITrackedDeviceServerDriver interface, we created it ourselves.
//-----------------------------------------------------------------------------
const std::string &MyHMDControllerDeviceDriver::MyGetSerialNumber()
{
	return my_hmd_serial_number_;
}

//-----------------------------------------------------------------------------
// DISPLAY DRIVER METHOD DEFINITIONS
//-----------------------------------------------------------------------------

MyHMDDisplayComponent::MyHMDDisplayComponent( const MyHMDDisplayDriverConfiguration &config )
	: config_( config )
{
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor if this display is considered an on-desktop display.
//-----------------------------------------------------------------------------
bool MyHMDDisplayComponent::IsDisplayOnDesktop()
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: To as vrcompositor to search for this display.
//-----------------------------------------------------------------------------
bool MyHMDDisplayComponent::IsDisplayRealDisplay()
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: To inform the rest of the vr system what the recommended target size should be
//-----------------------------------------------------------------------------
void MyHMDDisplayComponent::GetRecommendedRenderTargetSize( uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnWidth = config_.render_width;
	*pnHeight = config_.render_height;
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor how the screens should be organized.
//-----------------------------------------------------------------------------
void MyHMDDisplayComponent::GetEyeOutputViewport( vr::EVREye eEye, uint32_t *pnX, uint32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnY = 0;

	// Each eye will have half the window
	*pnWidth = config_.window_width / 2;

	// Each eye will have the full height
	*pnHeight = config_.window_height;

	if ( eEye == vr::Eye_Left )
	{
		// Left eye viewport on the left half of the window
		*pnX = 0;
	}
	else
	{
		// Right eye viewport on the right half of the window
		*pnX = config_.window_width / 2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: To inform the compositor what the projection parameters are for this HMD.
//-----------------------------------------------------------------------------
void MyHMDDisplayComponent::GetProjectionRaw( vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom )
{
	*pfLeft = -1.0;
	*pfRight = 1.0;
	*pfTop = -1.0;
	*pfBottom = 1.0;
}

//-----------------------------------------------------------------------------
// Purpose: To compute the distortion properties for a given uv in an image.
//-----------------------------------------------------------------------------
vr::DistortionCoordinates_t MyHMDDisplayComponent::ComputeDistortion( vr::EVREye eEye, float fU, float fV )
{
	vr::DistortionCoordinates_t coordinates{};
	coordinates.rfBlue[ 0 ] = fU;
	coordinates.rfBlue[ 1 ] = fV;
	coordinates.rfGreen[ 0 ] = fU;
	coordinates.rfGreen[ 1 ] = fV;
	coordinates.rfRed[ 0 ] = fU;
	coordinates.rfRed[ 1 ] = fV;
	return coordinates;
}

//-----------------------------------------------------------------------------
// Purpose: To inform vrcompositor what the window bounds for this virtual HMD are.
//-----------------------------------------------------------------------------
void MyHMDDisplayComponent::GetWindowBounds( int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight )
{
	*pnX = config_.window_x;
	*pnY = config_.window_y;
	*pnWidth = static_cast<uint32_t>(config_.window_width);
	*pnHeight = static_cast<uint32_t>(config_.window_height);
}

bool MyHMDDisplayComponent::ComputeInverseDistortion(vr::HmdVector2_t* pResult, vr::EVREye eEye, uint32_t unChannel, float fU, float fV)
{
	//Return false to let SteamVR infer an estimate from ComputeDistortion
	return false;
}