/***************************************

	vector<T> compatible array template

	Copyright (c) 1995-2016 by Rebecca Ann Heineman <becky@burgerbecky.com>

	It is released under an MIT Open Source license. Please see LICENSE
	for license details. Yes, you can use it in a
	commercial title without paying anything, just give me a credit.
	Please? It's not like I'm asking you for money!

***************************************/

#ifndef __BRARRAY_H__
#define __BRARRAY_H__

#ifndef __BRTYPES_H__
#include "brtypes.h"
#endif

#ifndef __BRASSERT_H__
#include "brassert.h"
#endif

#ifndef __BRGLOBALMEMORYMANAGER_H__
#include "brglobalmemorymanager.h"
#endif

/* BEGIN */
namespace Burger {
template<class T>
class ClassArray {
	T *m_pData;				///< Pointer to the array of class instances
	WordPtr m_uSize;		///< Number of active elements in the array
	WordPtr m_uBufferSize;	///< Maximum number of elements in the array
	static void Destroy(T* pData,WordPtr uCount) {
		do {
			pData->~T();
			++pData;
		} while (--uCount);
	}
	static void Init(T* pData,WordPtr uCount) {
		do {
			new (pData) T();
			++pData;
		} while (--uCount);
	}
	static void Init(T* pData,WordPtr uCount,const T*pSource)
	{
		do {
			new (pData) T(pSource[0]);
			++pSource;
			++pData;
		} while (--uCount);
	}
	static void Copy(T* pData,WordPtr uCount,const T*pSource) {
		IntPtr iStep = 1;
		if (pData>pSource) {
			iStep = -1;
			pData+=uCount-1;
			pSource+=uCount-1;
		}
		do {
			pData[0] = pSource[0];
			pSource+=iStep;
			pData+=iStep;
		} while (--uCount);
	}
public:
	ClassArray() : m_pData(NULL), m_uSize(0), m_uBufferSize(0) {}
	ClassArray(WordPtr uDefault) : m_pData(NULL),m_uSize(uDefault),m_uBufferSize(uDefault) {
		// Anything?
		if (uDefault) {
			// Get the default buffer and die if failed in debug
			T *pData = static_cast<T*>(Alloc(sizeof(T) * uDefault));
			BURGER_ASSERT(pData);
			m_pData = pData;
			Init(pData,uDefault);
		}
	}
	ClassArray(const ClassArray<T>& rData) : m_pData(NULL),m_uSize(rData.size()),m_uBufferSize(rData.size())
	{
		WordPtr uCount = m_uSize;
		// Empty?
		if (uCount) {
			T *pData = static_cast<T*>(Alloc(sizeof(T) * uCount));
			m_pData = pData;
			Init(pData,uCount,rData.m_pData);
		}
	}
	~ClassArray() {
		clear();
	}
	BURGER_INLINE T *GetPtr(void) { return m_pData; }
	BURGER_INLINE const T *GetPtr(void) const { return m_pData; }
	BURGER_INLINE T &operator[](WordPtr uIndex) { BURGER_ASSERT(uIndex < m_uSize); return m_pData[uIndex]; }
	BURGER_INLINE const T &operator[](WordPtr uIndex) const { BURGER_ASSERT(uIndex < m_uSize); return m_pData[uIndex]; }
	BURGER_INLINE WordPtr capacity(void) const { return m_uBufferSize; }
	BURGER_INLINE static WordPtr max_size(void) { return (256*1024*1024) / sizeof(T); }
	BURGER_INLINE WordPtr size(void) const { return m_uSize; }
	BURGER_INLINE Word empty(void) const { return m_uSize==0; }
	BURGER_INLINE T& front(void) { return m_pData[0]; }
	BURGER_INLINE const T& front(void) const { return m_pData[0]; }
	BURGER_INLINE T& back(void) { return m_pData[m_uSize-1]; }
	BURGER_INLINE const T & back(void) const { return m_pData[m_uSize-1]; }
	void push_back(const T& rData) {
		WordPtr uSize = m_uSize;
		WordPtr uBufferSize = m_uBufferSize;
		// Outgrew the buffer?
		if (uSize>=uBufferSize) {
			// Increase the buffer size with a bit of slop
			// to reduce the number of memory reallocations
			++uBufferSize;
			reserve(uBufferSize + (uBufferSize>>1U));
		}
		// Copy the new object with a copy constructor
		new (m_pData+uSize) T(rData);
		m_uSize = uSize+1U;
	}
	void pop_back(void) {
		BURGER_ASSERT(m_uSize);
		m_pData[--m_uSize].~T();
	}
	void clear(void) {
		WordPtr uCount = m_uSize;
		if (uCount) {
			Destroy(m_pData,uCount);
		}
		// Dispose of the array
		Free(m_pData);
		m_pData = NULL;
		m_uBufferSize = 0;
		m_uSize = 0;
	}
	ClassArray<T>& operator=(const ClassArray<T>& rData) {
		if (&rData!=this) {
			clear();
			WordPtr uCount = rData.size();
			if (uCount) {
				m_uSize = uCount;
				m_uBufferSize = uCount;
				T *pData = static_cast<T*>(Alloc(sizeof(T) * uCount));
				m_pData = pData;
				Init(pData,uCount,rData.m_pData);
			}
		}
		return *this;
	}
	void remove_at(WordPtr uIndex) {
		WordPtr uSize = m_uSize;
		BURGER_ASSERT(uIndex < uSize);
		if (uSize == 1) {
			// Nuke it
			clear();
		} else {
			WordPtr uCount = (uSize-uIndex)-1;
			if (uCount) {
				Copy(m_pData+uIndex,uCount,m_pData+uIndex+1);
			}
			--uSize;
			m_pData[uSize].~T();	// Destroy the last entry
			m_uSize = uSize;
		}
	}
	void insert_at(WordPtr uIndex,const T & rData = T())
	{
		WordPtr uSize = m_uSize;
		BURGER_ASSERT(uIndex <= uSize);
		resize(uSize + 1);		// resize adjusts m_uSize to uSize+1
		if (uIndex < uSize) {
			Copy(m_pData+uIndex+1,uSize-uIndex,m_pData+uIndex);
		}
		// Copy the new entry
		m_pData[uIndex] = rData;
	}
	BURGER_INLINE Word remove(const T &rData) {
		WordPtr uSize = m_uSize;
		Word bResult = FALSE;
		if (uSize) {
			const T *pWork = m_pData;
			do {
				if (*pWork == rData) {
					remove_at(m_uSize-uSize);
					bResult = TRUE;
					break;
				}
				++pWork;
			} while (--uSize);
		}
		return bResult;
	}
	void append(const T *pSourceData,WordPtr uCount) {
		if (uCount) {
			WordPtr uSize = m_uSize;
			resize(uSize + uCount);
			Copy(m_pData+uSize,uCount,pSourceData);
		}
	}
	BURGER_INLINE void append(const ClassArray<T>& rData) { append(rData.m_pData,rData.size()); }
	void resize(WordPtr uNewSize) {
		if (!uNewSize) {
			clear();
		} else {
			WordPtr	uOldSize = m_uSize;
			// Destruct old elements (if we're shrinking).
			if (uNewSize<uOldSize) {
				Destroy(m_pData+uNewSize,uOldSize-uNewSize);
				// Mark the size of the new buffer
				m_uSize = uNewSize;
				// Disable the new element initializer
				uOldSize = uNewSize;
			}
			reserve(uNewSize);

			// Initialize the new elements (If needed)
			if (uOldSize<uNewSize) {
				Init(m_pData + uOldSize,uNewSize-uOldSize);
				// Save the increased size
				m_uSize = uNewSize;
			}
		}
	}
	void reserve(WordPtr uNewBufferSize) {
		// Resize the buffer.
		if (!uNewBufferSize) {
			clear();
		} else {
			// Will the reserve function shrink the valid buffer?
			WordPtr uSize = m_uSize;
			if (uSize>uNewBufferSize) {
				// Destroy the excess objects
				m_uSize = uNewBufferSize;
				Destroy(m_pData + uNewBufferSize,uSize-uNewBufferSize);
			}
			if (m_uBufferSize!=uNewBufferSize) {
				T *pNewData = static_cast<T*>(Alloc(sizeof(T)*uNewBufferSize));
				BURGER_ASSERT(pNewData);	// need to throw (or something) on malloc failure!
				if (uSize) {
					Init(pNewData,uSize,m_pData);
					Destroy(m_pData,uSize);
				}
				Free(m_pData);
				m_uBufferSize = uNewBufferSize;
				m_pData = pNewData;
			}
		}
	}
	typedef T* iterator;
	typedef const T* const_iterator;
	typedef T value_type;
	BURGER_INLINE iterator begin(void) { return m_pData; }
	BURGER_INLINE iterator end(void) { return m_pData + m_uSize; }
	BURGER_INLINE const_iterator begin(void) const { return m_pData; }
	BURGER_INLINE const_iterator end(void) const { return m_pData + m_uSize; }
	BURGER_INLINE const_iterator cbegin(void) const { return m_pData; }
	BURGER_INLINE const_iterator cend(void) const { return m_pData + m_uSize; }
	BURGER_INLINE void erase(const_iterator it) {
		WordPtr uIndex = it-m_pData;
		remove_at(uIndex);
	}
};
}
/* END */

#endif
