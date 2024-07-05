#pragma once
#include <Windows.h>
class GameTime
{

public:
	GameTime();

	float TotalTime() const;
	float DeltaTime() const;
	bool IsStopped();

	void Reset();
	void Start();
	void Stop();
	void Tick();

private:
	double mSecondsPerCount;
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPauseTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrentTime;
	
	bool isStopped;
};

