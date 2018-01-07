#pragma once

//------------------------------------------------------------------------------
class DwCriticalSection
{
public:
    DwCriticalSection();
    ~DwCriticalSection();

    inline void lock()const;
    inline void unlock()const;
    inline bool tryLock()const;

private:
    CRITICAL_SECTION m_criticalSection;
};// end of DwCriticalSection

//------------------------------------------------------------------------------
/**
*/
inline void DwCriticalSection::lock()const
{
    EnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection);
}

//------------------------------------------------------------------------------
/**
*/
inline void DwCriticalSection::unlock()const
{
    LeaveCriticalSection((CRITICAL_SECTION*)&m_criticalSection);
}

//------------------------------------------------------------------------------
/**
*/
inline bool DwCriticalSection::tryLock()const
{
    return TryEnterCriticalSection((CRITICAL_SECTION*)&m_criticalSection) ? true : false;
}

//------------------------------------------------------------------------------
class DwCSLocker
{
public:
    explicit DwCSLocker(const DwCriticalSection *cs);
    ~DwCSLocker();

private:
    DwCriticalSection *m_cs;
};// end of DwCSLocker