#include "GameTime.h"

GameTime::GameTime()
	: mSecondsPerCount(0.0), mDeltaTime(-1.0),mBaseTime(0),
	mPauseTime(0), mStopTime(0), mPrevTime(0), mCurrentTime(0), isStopped(false)
{
	__int64 countsPerSec;
	//获得计时器频率
	QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
	mSecondsPerCount = 1.0 / (double)countsPerSec;
}

float GameTime::TotalTime() const
{
	if (isStopped)
	{
		return (float)((mStopTime - mPauseTime - mBaseTime) * mSecondsPerCount);
	}
	else
	{
		return (float)((mCurrentTime - mPauseTime - mBaseTime) * mSecondsPerCount);
	}
}

float GameTime::DeltaTime() const
{
	return (float)mDeltaTime;
}

bool GameTime::IsStopped()
{
	return isStopped;
}

void GameTime::Reset()
{
	__int64 currTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currTime);

	mBaseTime = currTime;
	mPrevTime = currTime;
	mStopTime = 0;
	isStopped = false;
}

void GameTime::Start()
{
	__int64 startTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&startTime);
	if (isStopped)
	{
		mPauseTime += (startTime - mStopTime);
		mPrevTime = startTime;
		mStopTime = 0;
		isStopped = false;
	}

}

void GameTime::Stop()
{
	if (!isStopped)
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		mStopTime = currTime;
		isStopped = true;
	}
}

void GameTime::Tick()
{
	if (isStopped)
	{
		mDeltaTime = 0.0;
		return;
	}

	__int64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);
	mCurrentTime = currentTime;
	mDeltaTime = (mCurrentTime - mPrevTime) * mSecondsPerCount;
	mPrevTime = mCurrentTime;

	if (mDeltaTime < 0)
	{
		mDeltaTime = 0;
	}
}
