#pragma once

#include <windows.h>

class CSObject
{
public:
    CSObject() { InitializeCriticalSection(&m_criticalSection); }
    ~CSObject() { DeleteCriticalSection(&m_criticalSection); }

    void lock()const { EnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    void unlock()const { LeaveCriticalSection((CRITICAL_SECTION*)&m_criticalSection); }
    bool tryLock()const { return TryEnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection) ? true : false; }

private:
    CRITICAL_SECTION m_criticalSection;
};
