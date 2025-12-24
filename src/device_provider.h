//============ Copyright (c) Valve Corporation, All rights reserved. ============
#pragma once

#include <memory>

#include "hmd_device_driver.h"
#include "openvr_driver.h"
#include "rayneo_api.h"
#include <mutex>

// Forward declaration for HMD driver to avoid circular include complexities
class MyHMDControllerDeviceDriver; // already included but keep forward for clarity

// make sure your class is publicly inheriting vr::IServerTrackedDeviceProvider!
class MyDeviceProvider : public vr::IServerTrackedDeviceProvider
{
public:
	MyDeviceProvider() {
		// Initialize RayNeo context before creating devices (if we need early info).
		// InitRayneo();
	};
	vr::EVRInitError Init( vr::IVRDriverContext *pDriverContext ) override;
	const char *const *GetInterfaceVersions() override;

	void RunFrame() override;

	bool ShouldBlockStandbyMode() override;
	void EnterStandby() override;
	void LeaveStandby() override;

	void Cleanup() override;

private:
	std::unique_ptr<MyHMDControllerDeviceDriver> my_hmd_device_;

	// RayNeo context moved from device driver to provider
	RAYNEO_Context rayneo_ctx_ = nullptr;
	bool rayneo_started_ = false;
	std::thread rayneo_event_thread_;
	std::atomic<bool> rayneo_event_thread_running_{false};

	// IMU orientation state (quaternion, world space) updated from RayNeo IMU samples
	std::mutex imu_mutex_;
	float imu_q_w_ = 1.0f;
	float imu_q_x_ = 0.0f;
	float imu_q_y_ = 0.0f;
	float imu_q_z_ = 0.0f;
	uint32_t last_imu_tick_ = 0; // last sample tick for dt computation (assumed ms units)

	// Sensitivity scaling for gyro integration (runtime tunable via env var)
	float gyro_scale_ = 0.2f; // default reduces sensitivity to ~20%

	// Sleep state (set on RAYNEO_NOTIFY_SLEEP/WAKE) and recenter anchor
	std::atomic<bool> sleeping_{false};
	float recenter_q_w_ = 1.f;
	float recenter_q_x_ = 0.f;
	float recenter_q_y_ = 0.f;
	float recenter_q_z_ = 0.f;
	// Distinct button flags derived from RayNeo notifications
	std::atomic<bool> button_system_click_pending_{false};
	std::atomic<bool> button_trigger_click_pending_{false};
	std::atomic<bool> button_grip_click_pending_{false};
	std::atomic<bool> button_appmenu_click_pending_{false};

public:
	// Apply current orientation relative to recenter anchor
	void GetImuOrientation(float &w, float &x, float &y, float &z)
	{
		std::lock_guard<std::mutex> lock(imu_mutex_);
		// Compute relative quaternion q_rel = q_anchor^{-1} * q_current
		float aw = recenter_q_w_, ax = recenter_q_x_, ay = recenter_q_y_, az = recenter_q_z_;
		// Inverse of unit quaternion is conjugate
		float iw = aw; float ix = -ax; float iy = -ay; float iz = -az;
		// Multiply: iw,ix,iy,iz * imu_q_w_,imu_q_x_,imu_q_y_,imu_q_z_
		w = iw*imu_q_w_ - ix*imu_q_x_ - iy*imu_q_y_ - iz*imu_q_z_;
		x = iw*imu_q_x_ + ix*imu_q_w_ + iy*imu_q_z_ - iz*imu_q_y_;
		y = iw*imu_q_y_ - ix*imu_q_z_ + iy*imu_q_w_ + iz*imu_q_x_;
		z = iw*imu_q_z_ + ix*imu_q_y_ - iy*imu_q_x_ + iz*imu_q_w_;
	}

	bool IsSleeping() const { return sleeping_.load(); }
	bool ConsumeButtonNotifyPending() { return button_system_click_pending_.exchange(false); }
	bool ConsumeTriggerClickPending() { return button_trigger_click_pending_.exchange(false); }
	bool ConsumeGripClickPending() { return button_grip_click_pending_.exchange(false); }
	bool ConsumeAppMenuClickPending() { return button_appmenu_click_pending_.exchange(false); }

	// Recenter: store current orientation as anchor
	void Recenter()
	{
		std::lock_guard<std::mutex> lock(imu_mutex_);
		recenter_q_w_ = imu_q_w_;
		recenter_q_x_ = imu_q_x_;
		recenter_q_y_ = imu_q_y_;
		recenter_q_z_ = imu_q_z_;
	}


	void InitRayneo();
private:
	void RayneoEventLoop();
	void StartRayneoEventThread();
	void StopRayneo();
};

// Helper accessor (defined in device_provider.cpp) for other components (e.g., HMD driver)
MyDeviceProvider* GetMyDeviceProviderInstance();