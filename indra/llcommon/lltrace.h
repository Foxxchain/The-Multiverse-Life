/** 
 * @file lltrace.h
 * @brief Runtime statistics accumulation.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLTRACE_H
#define LL_LLTRACE_H

#include "stdtypes.h"
#include "llpreprocessor.h"

#include "llmemory.h"
#include "llrefcount.h"
#include "llunit.h"
#include "llapr.h"
#include "llthreadlocalstorage.h"
#include "lltimer.h"

#include <list>

#define LL_RECORD_BLOCK_TIME(block_timer) LLTrace::TimeBlock::Recorder LL_GLUE_TOKENS(block_time_recorder, __COUNTER__)(block_timer);

namespace LLTrace
{
class Recording;

typedef LLUnit<LLUnits::Bytes, F64>			Bytes;
typedef LLUnit<LLUnits::Kibibytes, F64>		Kibibytes;
typedef LLUnit<LLUnits::Mibibytes, F64>		Mibibytes;
typedef LLUnit<LLUnits::Gibibytes, F64>		Gibibytes;
typedef LLUnit<LLUnits::Bits, F64>			Bits;
typedef LLUnit<LLUnits::Kibibits, F64>		Kibibits;
typedef LLUnit<LLUnits::Mibibits, F64>		Mibibits;
typedef LLUnit<LLUnits::Gibibits, F64>		Gibibits;

typedef LLUnit<LLUnits::Seconds, F64>		Seconds;
typedef LLUnit<LLUnits::Milliseconds, F64>	Milliseconds;
typedef LLUnit<LLUnits::Minutes, F64>		Minutes;
typedef LLUnit<LLUnits::Hours, F64>			Hours;
typedef LLUnit<LLUnits::Milliseconds, F64>	Milliseconds;
typedef LLUnit<LLUnits::Microseconds, F64>	Microseconds;
typedef LLUnit<LLUnits::Nanoseconds, F64>	Nanoseconds;

typedef LLUnit<LLUnits::Meters, F64>		Meters;
typedef LLUnit<LLUnits::Kilometers, F64>	Kilometers;
typedef LLUnit<LLUnits::Centimeters, F64>	Centimeters;
typedef LLUnit<LLUnits::Millimeters, F64>	Millimeters;

void init();
void cleanup();
bool isInitialized();

const LLThreadLocalPointer<class ThreadRecorder>& get_thread_recorder();
void set_thread_recorder(class ThreadRecorder*);

class MasterThreadRecorder& getUIThreadRecorder();

template<typename ACCUMULATOR>
class AccumulatorBuffer : public LLRefCount
{
	typedef AccumulatorBuffer<ACCUMULATOR> self_t;
	static const U32 DEFAULT_ACCUMULATOR_BUFFER_SIZE = 64;
private:
	struct StaticAllocationMarker { };

	AccumulatorBuffer(StaticAllocationMarker m)
	:	mStorageSize(0),
		mStorage(NULL)
	{}

public:

	AccumulatorBuffer(const AccumulatorBuffer& other = *getDefaultBuffer())
	:	mStorageSize(0),
		mStorage(NULL)
	{
		resize(other.mStorageSize);
		for (S32 i = 0; i < sNextStorageSlot; i++)
		{
			mStorage[i] = other.mStorage[i];
		}
	}

	~AccumulatorBuffer()
	{
		if (isPrimary())
		{
			LLThreadLocalSingletonPointer<ACCUMULATOR>::setInstance(NULL);
		}
		delete[] mStorage;
	}

	LL_FORCE_INLINE ACCUMULATOR& operator[](size_t index) 
	{ 
		return mStorage[index]; 
	}

	LL_FORCE_INLINE const ACCUMULATOR& operator[](size_t index) const
	{ 
		return mStorage[index]; 
	}

	void addSamples(const AccumulatorBuffer<ACCUMULATOR>& other, bool append = true)
	{
		llassert(mStorageSize >= sNextStorageSlot && other.mStorageSize > sNextStorageSlot);
		for (size_t i = 0; i < sNextStorageSlot; i++)
		{
			mStorage[i].addSamples(other.mStorage[i], append);
		}
	}

	void copyFrom(const AccumulatorBuffer<ACCUMULATOR>& other)
	{
		llassert(mStorageSize >= sNextStorageSlot && other.mStorageSize > sNextStorageSlot);
		for (size_t i = 0; i < sNextStorageSlot; i++)
		{
			mStorage[i] = other.mStorage[i];
		}
	}

	void reset(const AccumulatorBuffer<ACCUMULATOR>* other = NULL)
	{
		llassert(mStorageSize >= sNextStorageSlot);
		for (size_t i = 0; i < sNextStorageSlot; i++)
		{
			mStorage[i].reset(other ? &other->mStorage[i] : NULL);
		}
	}

	void flush()
	{
		llassert(mStorageSize >= sNextStorageSlot);
		for (size_t i = 0; i < sNextStorageSlot; i++)
		{
			mStorage[i].flush();
		}
	}

	void makePrimary()
	{
		LLThreadLocalSingletonPointer<ACCUMULATOR>::setInstance(mStorage);
	}

	bool isPrimary() const
	{
		return LLThreadLocalSingletonPointer<ACCUMULATOR>::getInstance() == mStorage;
	}

	LL_FORCE_INLINE static ACCUMULATOR* getPrimaryStorage() 
	{ 
		ACCUMULATOR* accumulator = LLThreadLocalSingletonPointer<ACCUMULATOR>::getInstance();
		return accumulator ? accumulator : sDefaultBuffer->mStorage;
	}

	// NOTE: this is not thread-safe.  We assume that slots are reserved in the main thread before any child threads are spawned
	size_t reserveSlot()
	{
#ifndef LL_RELEASE_FOR_DOWNLOAD
		if (LLTrace::isInitialized())
		{
			llerrs << "Attempting to declare trace object after program initialization.  Trace objects should be statically initialized." << llendl;
		}
#endif
		size_t next_slot = sNextStorageSlot++;
		if (next_slot >= mStorageSize)
		{
			resize(mStorageSize + (mStorageSize >> 2));
		}
		llassert(mStorage && next_slot < mStorageSize);
		return next_slot;
	}

	void resize(size_t new_size)
	{
		if (new_size <= mStorageSize) return;

		ACCUMULATOR* old_storage = mStorage;
		mStorage = new ACCUMULATOR[new_size];
		if (old_storage)
		{
			for (S32 i = 0; i < mStorageSize; i++)
			{
				mStorage[i] = old_storage[i];
			}
		}
		mStorageSize = new_size;
		delete[] old_storage;

		self_t* default_buffer = getDefaultBuffer();
		if (this != default_buffer
			&& new_size > default_buffer->size())
		{
			//NB: this is not thread safe, but we assume that all resizing occurs during static initialization
			default_buffer->resize(new_size);
		}
	}

	size_t size() const
	{
		return sNextStorageSlot;
	}

	static self_t* getDefaultBuffer()
	{
		static bool sInitialized = false;
		if (!sInitialized)
		{
			// this buffer is allowed to leak so that trace calls from global destructors have somewhere to put their data
			// so as not to trigger an access violation
			sDefaultBuffer = new AccumulatorBuffer(StaticAllocationMarker());
			sInitialized = true;
			sDefaultBuffer->resize(DEFAULT_ACCUMULATOR_BUFFER_SIZE);
		}
		return sDefaultBuffer;
	}

private:
	ACCUMULATOR*	mStorage;
	size_t			mStorageSize;
	static size_t	sNextStorageSlot;
	static self_t*	sDefaultBuffer;
};

template<typename ACCUMULATOR> size_t AccumulatorBuffer<ACCUMULATOR>::sNextStorageSlot = 0;
template<typename ACCUMULATOR> AccumulatorBuffer<ACCUMULATOR>* AccumulatorBuffer<ACCUMULATOR>::sDefaultBuffer = NULL;

template<typename ACCUMULATOR>
class TraceType 
:	 public LLInstanceTracker<TraceType<ACCUMULATOR>, std::string>
{
public:
	TraceType(const char* name, const char* description = NULL)
	:	LLInstanceTracker<TraceType<ACCUMULATOR>, std::string>(name),
		mName(name),
		mDescription(description ? description : ""),
		mAccumulatorIndex(AccumulatorBuffer<ACCUMULATOR>::getDefaultBuffer()->reserveSlot())
	{}

	LL_FORCE_INLINE ACCUMULATOR* getPrimaryAccumulator() const
	{
		ACCUMULATOR* accumulator_storage = AccumulatorBuffer<ACCUMULATOR>::getPrimaryStorage();
		return &accumulator_storage[mAccumulatorIndex];
	}

	size_t getIndex() const { return mAccumulatorIndex; }

	virtual const char* getUnitLabel() { return ""; }

	const std::string& getName() const { return mName; }

protected:
	const std::string	mName;
	const std::string	mDescription;
	const size_t		mAccumulatorIndex;
};

class EventAccumulator
{
public:
	typedef F64 value_t;
	typedef F64 mean_t;

	EventAccumulator()
	:	mSum(0),
		mMin((std::numeric_limits<F64>::max)()),
		mMax((std::numeric_limits<F64>::min)()),
		mMean(0),
		mVarianceSum(0),
		mNumSamples(0),
		mLastValue(0)
	{}

	void record(F64 value)
	{
		mNumSamples++;
		mSum += value;
		// NOTE: both conditions will hold on first pass through
		if (value < mMin)
		{
			mMin = value;
		}
		if (value > mMax)
		{
			mMax = value;
		}
		F64 old_mean = mMean;
		mMean += (value - old_mean) / (F64)mNumSamples;
		mVarianceSum += (value - old_mean) * (value - mMean);
		mLastValue = value;
	}

	void addSamples(const EventAccumulator& other, bool append)
	{
		if (other.mNumSamples)
		{
			mSum += other.mSum;

			// NOTE: both conditions will hold first time through
			if (other.mMin < mMin) { mMin = other.mMin; }
			if (other.mMax > mMax) { mMax = other.mMax; }

			// combine variance (and hence standard deviation) of 2 different sized sample groups using
			// the following formula: http://www.mrc-bsu.cam.ac.uk/cochrane/handbook/chapter_7/7_7_3_8_combining_groups.htm
			F64 n_1 = (F64)mNumSamples,
				n_2 = (F64)other.mNumSamples;
			F64 m_1 = mMean,
				m_2 = other.mMean;
			F64 v_1 = mVarianceSum / mNumSamples,
				v_2 = other.mVarianceSum / other.mNumSamples;
			if (n_1 == 0)
			{
				mVarianceSum = other.mVarianceSum;
			}
			else if (n_2 == 0)
			{
				// don't touch variance
				// mVarianceSum = mVarianceSum;
			}
			else
			{
				mVarianceSum = (F64)mNumSamples
								* ((((n_1 - 1.f) * v_1)
									+ ((n_2 - 1.f) * v_2)
									+ (((n_1 * n_2) / (n_1 + n_2))
										* ((m_1 * m_1) + (m_2 * m_2) - (2.f * m_1 * m_2))))
									/ (n_1 + n_2 - 1.f));
			}

			F64 weight = (F64)mNumSamples / (F64)(mNumSamples + other.mNumSamples);
			mNumSamples += other.mNumSamples;
			mMean = mMean * weight + other.mMean * (1.f - weight);
			if (append) mLastValue = other.mLastValue;
		}
	}

	void reset(const EventAccumulator* other)
	{
		mNumSamples = 0;
		mSum = 0;
		mMin = std::numeric_limits<F64>::max();
		mMax = std::numeric_limits<F64>::min();
		mMean = 0;
		mVarianceSum = 0;
		mLastValue = other ? other->mLastValue : 0;
	}

	void flush() {}

	F64	getSum() const { return mSum; }
	F64	getMin() const { return mMin; }
	F64	getMax() const { return mMax; }
	F64	getLastValue() const { return mLastValue; }
	F64	getMean() const { return mMean; }
	F64 getStandardDeviation() const { return sqrtf(mVarianceSum / mNumSamples); }
	U32 getSampleCount() const { return mNumSamples; }

private:
	F64	mSum,
		mMin,
		mMax,
		mLastValue;

	F64	mMean,
		mVarianceSum;

	U32	mNumSamples;
};


class SampleAccumulator
{
public:
	typedef F64 value_t;
	typedef F64 mean_t;

	SampleAccumulator()
	:	mSum(0),
		mMin((std::numeric_limits<F64>::max)()),
		mMax((std::numeric_limits<F64>::min)()),
		mMean(0),
		mVarianceSum(0),
		mLastSampleTimeStamp(LLTimer::getTotalSeconds()),
		mTotalSamplingTime(0),
		mNumSamples(0),
		mLastValue(0),
		mHasValue(false)
	{}

	void sample(F64 value)
	{
		LLUnitImplicit<LLUnits::Seconds, F64> time_stamp = LLTimer::getTotalSeconds();
		LLUnitImplicit<LLUnits::Seconds, F64> delta_time = time_stamp - mLastSampleTimeStamp;
		mLastSampleTimeStamp = time_stamp;

		if (mHasValue)
		{
			mTotalSamplingTime += delta_time;
			mSum += mLastValue * delta_time;

			// NOTE: both conditions will hold first time through
			if (value < mMin) { mMin = value; }
			if (value > mMax) { mMax = value; }

			F64 old_mean = mMean;
			mMean += (delta_time / mTotalSamplingTime) * (mLastValue - old_mean);
			mVarianceSum += delta_time * (mLastValue - old_mean) * (mLastValue - mMean);
		}

		mLastValue = value;
		mNumSamples++;
		mHasValue = true;
	}

	void addSamples(const SampleAccumulator& other, bool append)
	{
		if (other.mTotalSamplingTime)
		{
			mSum += other.mSum;

			// NOTE: both conditions will hold first time through
			if (other.mMin < mMin) { mMin = other.mMin; }
			if (other.mMax > mMax) { mMax = other.mMax; }

			// combine variance (and hence standard deviation) of 2 different sized sample groups using
			// the following formula: http://www.mrc-bsu.cam.ac.uk/cochrane/handbook/chapter_7/7_7_3_8_combining_groups.htm
			F64 n_1 = mTotalSamplingTime,
				n_2 = other.mTotalSamplingTime;
			F64 m_1 = mMean,
				m_2 = other.mMean;
			F64 v_1 = mVarianceSum / mTotalSamplingTime,
				v_2 = other.mVarianceSum / other.mTotalSamplingTime;
			if (n_1 == 0)
			{
				mVarianceSum = other.mVarianceSum;
			}
			else if (n_2 == 0)
			{
				// variance is unchanged
				// mVarianceSum = mVarianceSum;
			}
			else
			{
				mVarianceSum =	mTotalSamplingTime
								* ((((n_1 - 1.f) * v_1)
									+ ((n_2 - 1.f) * v_2)
									+ (((n_1 * n_2) / (n_1 + n_2))
										* ((m_1 * m_1) + (m_2 * m_2) - (2.f * m_1 * m_2))))
									/ (n_1 + n_2 - 1.f));
			}

			llassert(other.mTotalSamplingTime > 0);
			F64 weight = mTotalSamplingTime / (mTotalSamplingTime + other.mTotalSamplingTime);
			mNumSamples += other.mNumSamples;
			mTotalSamplingTime += other.mTotalSamplingTime;
			mMean = (mMean * weight) + (other.mMean * (1.0 - weight));
			if (append)
			{
				mLastValue = other.mLastValue;
				mLastSampleTimeStamp = other.mLastSampleTimeStamp;
				mHasValue |= other.mHasValue;
			}
		}
	}

	void reset(const SampleAccumulator* other)
	{
		mNumSamples = 0;
		mSum = 0;
		mMin = std::numeric_limits<F64>::max();
		mMax = std::numeric_limits<F64>::min();
		mMean = other ? other->mLastValue : 0;
		mVarianceSum = 0;
		mLastSampleTimeStamp = LLTimer::getTotalSeconds();
		mTotalSamplingTime = 0;
		mLastValue = other ? other->mLastValue : 0;
		mHasValue = other ? other->mHasValue : false;
	}

	void flush()
	{
		LLUnitImplicit<LLUnits::Seconds, F64> time_stamp = LLTimer::getTotalSeconds();
		LLUnitImplicit<LLUnits::Seconds, F64> delta_time = time_stamp - mLastSampleTimeStamp;

		if (mHasValue)
		{
			mSum += mLastValue * delta_time;
			mTotalSamplingTime += delta_time;
		}
		mLastSampleTimeStamp = time_stamp;
	}

	F64	getSum() const { return mSum; }
	F64	getMin() const { return mMin; }
	F64	getMax() const { return mMax; }
	F64	getLastValue() const { return mLastValue; }
	F64	getMean() const { return mMean; }
	F64 getStandardDeviation() const { return sqrtf(mVarianceSum / mTotalSamplingTime); }
	U32 getSampleCount() const { return mNumSamples; }

private:
	F64	mSum,
		mMin,
		mMax,
		mLastValue;

	bool mHasValue;

	F64	mMean,
		mVarianceSum;

	LLUnitImplicit<LLUnits::Seconds, F64>	mLastSampleTimeStamp,
											mTotalSamplingTime;

	U32	mNumSamples;
};

class CountAccumulator
{
public:
	typedef F64 value_t;
	typedef F64 mean_t;

	CountAccumulator()
	:	mSum(0),
		mNumSamples(0)
	{}

	void add(F64 value)
	{
		mNumSamples++;
		mSum += value;
	}

	void addSamples(const CountAccumulator& other, bool /*append*/)
	{
		mSum += other.mSum;
		mNumSamples += other.mNumSamples;
	}

	void reset(const CountAccumulator* other)
	{
		mNumSamples = 0;
		mSum = 0;
	}

	void flush() {}

	F64	getSum() const { return mSum; }

	U32 getSampleCount() const { return mNumSamples; }

private:
	F64	mSum;

	U32	mNumSamples;
};

class TimeBlockAccumulator
{
public:
	typedef LLUnit<LLUnits::Seconds, F64> value_t;
	typedef LLUnit<LLUnits::Seconds, F64> mean_t;
	typedef TimeBlockAccumulator self_t;

	// fake classes that allows us to view different facets of underlying statistic
	struct CallCountFacet 
	{
		typedef U32 value_t;
		typedef F32 mean_t;
	};

	struct SelfTimeFacet
	{
		typedef LLUnit<LLUnits::Seconds, F64> value_t;
		typedef LLUnit<LLUnits::Seconds, F64> mean_t;
	};

	TimeBlockAccumulator();
	void addSamples(const self_t& other, bool /*append*/);
	void reset(const self_t* other);
	void flush() {}

	//
	// members
	//
	U64							mStartTotalTimeCounter,
								mTotalTimeCounter,
								mSelfTimeCounter;
	U32							mCalls;
	class TimeBlock*			mParent;		// last acknowledged parent of this time block
	class TimeBlock*			mLastCaller;	// used to bootstrap tree construction
	U16							mActiveCount;	// number of timers with this ID active on stack
	bool						mMoveUpTree;	// needs to be moved up the tree of timers at the end of frame

};

template<>
class TraceType<TimeBlockAccumulator::CallCountFacet>
:	public TraceType<TimeBlockAccumulator>
{
public:

	TraceType(const char* name, const char* description = "")
	:	TraceType<TimeBlockAccumulator>(name, description)
	{}
};

template<>
class TraceType<TimeBlockAccumulator::SelfTimeFacet>
	:	public TraceType<TimeBlockAccumulator>
{
public:

	TraceType(const char* name, const char* description = "")
		:	TraceType<TimeBlockAccumulator>(name, description)
	{}
};

class TimeBlock;
class TimeBlockTreeNode
{
public:
	TimeBlockTreeNode();

	void setParent(TimeBlock* parent);
	TimeBlock* getParent() { return mParent; }

	TimeBlock*					mBlock;
	TimeBlock*					mParent;	
	std::vector<TimeBlock*>		mChildren;
	bool						mNeedsSorting;
};


template <typename T = F64>
class EventStatHandle
:	public TraceType<EventAccumulator>
{
public:
	typedef typename F64 storage_t;
	typedef TraceType<EventAccumulator> trace_t;

	EventStatHandle(const char* name, const char* description = NULL)
	:	trace_t(name, description)
	{}

	/*virtual*/ const char* getUnitLabel() { return LLGetUnitLabel<T>::getUnitLabel(); }

};

template<typename T, typename VALUE_T>
void record(EventStatHandle<T>& measurement, VALUE_T value)
{
	T converted_value(value);
	measurement.getPrimaryAccumulator()->record(LLUnits::rawValue(converted_value));
}

template <typename T = F64>
class SampleStatHandle
:	public TraceType<SampleAccumulator>
{
public:
	typedef F64 storage_t;
	typedef TraceType<SampleAccumulator> trace_t;

	SampleStatHandle(const char* name, const char* description = NULL)
	:	trace_t(name, description)
	{}

	/*virtual*/ const char* getUnitLabel() { return LLGetUnitLabel<T>::getUnitLabel(); }
};

template<typename T, typename VALUE_T>
void sample(SampleStatHandle<T>& measurement, VALUE_T value)
{
	T converted_value(value);
	measurement.getPrimaryAccumulator()->sample(LLUnits::rawValue(converted_value));
}

template <typename T = F64>
class CountStatHandle
:	public TraceType<CountAccumulator>
{
public:
	typedef typename F64 storage_t;
	typedef TraceType<CountAccumulator> trace_t;

	CountStatHandle(const char* name, const char* description = NULL) 
	:	trace_t(name)
	{}

	/*virtual*/ const char* getUnitLabel() { return LLGetUnitLabel<T>::getUnitLabel(); }
};

template<typename T, typename VALUE_T>
void add(CountStatHandle<T>& count, VALUE_T value)
{
	T converted_value(value);
	count.getPrimaryAccumulator()->add(LLUnits::rawValue(converted_value));
}


struct MemStatAccumulator
{
	typedef MemStatAccumulator self_t;

	// fake classes that allows us to view different facets of underlying statistic
	struct AllocationCountFacet 
	{
		typedef U32 value_t;
		typedef F32 mean_t;
	};

	struct DeallocationCountFacet 
	{
		typedef U32 value_t;
		typedef F32 mean_t;
	};

	struct ChildMemFacet
	{
		typedef LLUnit<LLUnits::Bytes, F64> value_t;
		typedef LLUnit<LLUnits::Bytes, F64> mean_t;
	};

	MemStatAccumulator()
	:	mAllocatedCount(0),
		mDeallocatedCount(0)
	{}

	void addSamples(const MemStatAccumulator& other, bool append)
	{
		mSize.addSamples(other.mSize, append);
		mChildSize.addSamples(other.mChildSize, append);
		mAllocatedCount += other.mAllocatedCount;
		mDeallocatedCount += other.mDeallocatedCount;
	}

	void reset(const MemStatAccumulator* other)
	{
		mSize.reset(other ? &other->mSize : NULL);
		mChildSize.reset(other ? &other->mChildSize : NULL);
		mAllocatedCount = 0;
		mDeallocatedCount = 0;
	}

	void flush() 
	{
		mSize.flush();
		mChildSize.flush();
	}

	SampleAccumulator	mSize,
						mChildSize;
	int					mAllocatedCount,
						mDeallocatedCount;
};


template<>
class TraceType<MemStatAccumulator::AllocationCountFacet>
:	public TraceType<MemStatAccumulator>
{
public:

	TraceType(const char* name, const char* description = "")
	:	TraceType<MemStatAccumulator>(name, description)
	{}
};

template<>
class TraceType<MemStatAccumulator::DeallocationCountFacet>
:	public TraceType<MemStatAccumulator>
{
public:

	TraceType(const char* name, const char* description = "")
	:	TraceType<MemStatAccumulator>(name, description)
	{}
};

template<>
class TraceType<MemStatAccumulator::ChildMemFacet>
	:	public TraceType<MemStatAccumulator>
{
public:

	TraceType(const char* name, const char* description = "")
		:	TraceType<MemStatAccumulator>(name, description)
	{}
};

class MemStatHandle : public TraceType<MemStatAccumulator>
{
public:
	typedef TraceType<MemStatAccumulator> trace_t;
	MemStatHandle(const char* name)
	:	trace_t(name)
	{}

	/*virtual*/ const char* getUnitLabel() { return "B"; }

	TraceType<MemStatAccumulator::AllocationCountFacet>& allocationCount() 
	{ 
		return static_cast<TraceType<MemStatAccumulator::AllocationCountFacet>&>(*(TraceType<MemStatAccumulator>*)this);
	}

	TraceType<MemStatAccumulator::DeallocationCountFacet>& deallocationCount() 
	{ 
		return static_cast<TraceType<MemStatAccumulator::DeallocationCountFacet>&>(*(TraceType<MemStatAccumulator>*)this);
	}

	TraceType<MemStatAccumulator::ChildMemFacet>& childMem() 
	{ 
		return static_cast<TraceType<MemStatAccumulator::ChildMemFacet>&>(*(TraceType<MemStatAccumulator>*)this);
	}
};

// measures effective memory footprint of specified type
// specialize to cover different types

template<typename T>
struct MemFootprint
{
	static size_t measure(const T& value)
	{
		return sizeof(T);
	}

	static size_t measure()
	{
		return sizeof(T);
	}
};

template<typename T>
struct MemFootprint<T*>
{
	static size_t measure(const T* value)
	{
		if (!value)
		{
			return 0;
		}
		return MemFootprint<T>::measure(*value);
	}

	static size_t measure()
	{
		return MemFootprint<T>::measure();
	}
};

template<typename T>
struct MemFootprint<std::basic_string<T> >
{
	static size_t measure(const std::basic_string<T>& value)
	{
		return value.capacity() * sizeof(T);
	}

	static size_t measure()
	{
		return sizeof(std::basic_string<T>);
	}
};

template<typename T>
struct MemFootprint<std::vector<T> >
{
	static size_t measure(const std::vector<T>& value)
	{
		return value.capacity() * MemFootprint<T>::measure();
	}

	static size_t measure()
	{
		return sizeof(std::vector<T>);
	}
};

template<typename T>
struct MemFootprint<std::list<T> >
{
	static size_t measure(const std::list<T>& value)
	{
		return value.size() * (MemFootprint<T>::measure() + sizeof(void*) * 2);
	}

	static size_t measure()
	{
		return sizeof(std::list<T>);
	}
};

template<typename DERIVED, size_t ALIGNMENT = LL_DEFAULT_HEAP_ALIGN>
class MemTrackable
{
	template<typename TRACKED, typename TRACKED_IS_TRACKER>
	struct TrackMemImpl;

	typedef MemTrackable<DERIVED> mem_trackable_t;

public:
	typedef void mem_trackable_tag_t;

	virtual ~MemTrackable()
	{
		memDisclaim(mMemFootprint);
	}

	void* operator new(size_t size) 
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() + (F64)size);
			accumulator->mAllocatedCount++;
		}

		return ::operator new(size);
	}

	void operator delete(void* ptr, size_t size)
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() - (F64)size);
			accumulator->mAllocatedCount--;
			accumulator->mDeallocatedCount++;
		}
		::operator delete(ptr);
	}

	void *operator new [](size_t size)
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() + (F64)size);
			accumulator->mAllocatedCount++;
		}

		return ::operator new[](size);
	}

	void operator delete[](void* ptr, size_t size)
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() - (F64)size);
			accumulator->mAllocatedCount--;
			accumulator->mDeallocatedCount++;
		}
		::operator delete[](ptr);
	}

	// claim memory associated with other objects/data as our own, adding to our calculated footprint
	template<typename CLAIM_T>
	CLAIM_T& memClaim(CLAIM_T& value)
	{
		TrackMemImpl<CLAIM_T>::claim(*this, value);
		return value;
	}

	template<typename CLAIM_T>
	const CLAIM_T& memClaim(const CLAIM_T& value)
	{
		TrackMemImpl<CLAIM_T>::claim(*this, value);
		return value;
	}


	void memClaimAmount(size_t size)
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		mMemFootprint += size;
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() + (F64)size);
		}
	}

	// remove memory we had claimed from our calculated footprint
	template<typename CLAIM_T>
	CLAIM_T& memDisclaim(CLAIM_T& value)
	{
		TrackMemImpl<CLAIM_T>::disclaim(*this, value);
		return value;
	}

	template<typename CLAIM_T>
	const CLAIM_T& memDisclaim(const CLAIM_T& value)
	{
		TrackMemImpl<CLAIM_T>::disclaim(*this, value);
		return value;
	}

	void memDisclaimAmount(size_t size)
	{
		MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
		if (accumulator)
		{
			accumulator->mSize.sample(accumulator->mSize.getLastValue() - (F64)size);
		}
	}

private:
	size_t mMemFootprint;

	template<typename TRACKED, typename TRACKED_IS_TRACKER = void>
	struct TrackMemImpl
	{
		static void claim(mem_trackable_t& tracker, const TRACKED& tracked)
		{
			MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
			if (accumulator)
			{
				size_t footprint = MemFootprint<TRACKED>::measure(tracked);
				accumulator->mSize.sample(accumulator->mSize.getLastValue() + (F64)footprint);
				tracker.mMemFootprint += footprint;
			}
		}

		static void disclaim(mem_trackable_t& tracker, const TRACKED& tracked)
		{
			MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
			if (accumulator)
			{
				size_t footprint = MemFootprint<TRACKED>::measure(tracked);
				accumulator->mSize.sample(accumulator->mSize.getLastValue() - (F64)footprint);
				tracker.mMemFootprint -= footprint;
			}
		}
	};

	template<typename TRACKED>
	struct TrackMemImpl<TRACKED, typename TRACKED::mem_trackable_tag_t>
	{
		static void claim(mem_trackable_t& tracker, TRACKED& tracked)
		{
			MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
			if (accumulator)
			{
				accumulator->mChildSize.sample(accumulator->mChildSize.getLastValue() + (F64)MemFootprint<TRACKED>::measure(tracked));
			}
		}

		static void disclaim(mem_trackable_t& tracker, TRACKED& tracked)
		{
			MemStatAccumulator* accumulator = DERIVED::sMemStat.getPrimaryAccumulator();
			if (accumulator)
			{
				accumulator->mChildSize.sample(accumulator->mChildSize.getLastValue() - (F64)MemFootprint<TRACKED>::measure(tracked));
			}
		}
	};
};

}
#endif // LL_LLTRACE_H
