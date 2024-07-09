#pragma once
#ifndef CAMERA_H
#define CAMERA_H


#include "stdafx.h"
class Camera
{
public:
	Camera();
	~Camera();

	// get¡¢set world camera position
	DirectX::XMVECTOR GetPosition() const;
	DirectX::XMFLOAT3 GetPosition3f() const;
	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& v);

	// get camera basis vectors
	DirectX::XMVECTOR GetRight() const;
	DirectX::XMVECTOR GetUp() const;
	DirectX::XMVECTOR GetLook() const;

	DirectX::XMFLOAT3 GetRight3f() const;
	DirectX::XMFLOAT3 GetUp3f() const;
	DirectX::XMFLOAT3 GetLook3f() const;

	// get frustum properties
	float GetNearZ() const;
	float GetFarZ() const;
	float GetAspect() const;
	float GetFovY() const;
	float GetFovX() const;

	// get near and far plane dimensions in view space coordinates
	float GetNearWindowWidth() const;
	float GetNearWindowHeight()const;
	float GetFarWindowWidth()const;
	float GetFarWindowHeight()const;

	// set frustum
	void SetLen(float fovY, float aspect, float zn, float zf);

	// define camera space via LookAt parameters
	void LookAt(DirectX::FXMVECTOR pos, DirectX::FXMVECTOR target, DirectX::FXMVECTOR worldUp);
	void LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

	// get view/project matrices
	DirectX::XMMATRIX GetView() const;
	DirectX::XMMATRIX GetProj() const;

	DirectX::XMFLOAT4X4 GetView4x4f()const;
	DirectX::XMFLOAT4X4 GetProj4x4f()const;

	// strafe/walk the camera a distance d
	// ×óÓÒÆ½ÒÆ
	void Strafe(float d);
	// Ç°ºó
	void Walk(float d);

	// rotate the camera
	void Pitch(float angle);
	// Yaw
	void RotateY(float angle);

	// after modifying camera position/orientation, call to rebuild the view matrix
	void UpdateViewMatrix();

private:

	// camera coordinate system with coordinates relative to world space
	DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 mLook = { 0.0f, 0.0f, 1.0f };

	// cache frustum properties
	float mNearZ = 0.0f;
	float mFarZ = 0.0f;
	float mAspect = 0.0f;
	float mFovY = 0.0;
	float mNearWindowHeight = 0.0f;
	float mfarWindowHeight = 0.0f;

	bool mViewDirty = true;

	// cache view/proj matrices
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();
};


#endif // !CAMERA_H
