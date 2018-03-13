#pragma once

template<typename UT>
class com_ptr
{
public:
    com_ptr() {}
    com_ptr(std::nullptr_t)
    {
    }
    com_ptr(const com_ptr<UT> & another)
    {
        if(another._ptr)
        {
            _ptr = another._ptr;
            if(_ptr)
                _ptr->AddRef();
        }
    }

    explicit com_ptr(UT * ptr):_ptr(ptr){}
    ~com_ptr()
    {
        if (_ptr)
            _ptr->Release();
    }

    UT ** operator & ()
    {
        return &_ptr;
    }

    UT * operator ->() const
    {
        return _ptr;
    }

    operator bool() const { return !!_ptr; }

    com_ptr<UT> & operator=(const com_ptr<UT> & another)
    {
        if (_ptr)
        {
            _ptr->Release();
            _ptr = nullptr;
        }

        if (another._ptr)
        {
            another._ptr->AddRef();
            _ptr = another._ptr;
        }
        return *this;
    }

    void reset()
    {
        if (_ptr)
        {
            _ptr->Release();
            _ptr = nullptr;
        }
    }

    void reset(UT * ptr)
    {
        if (_ptr)
        {
            _ptr->Release();
            _ptr = nullptr;
        }
        if (ptr)
            ptr->AddRef();
        _ptr = ptr;
    }

    UT * get() { return _ptr; }
    const UT * get() const { return _ptr; }

    bool operator == (const com_ptr<UT> & another) const
    {
        return _ptr == another._ptr;
    }

    bool operator != (const com_ptr<UT> & another) const
    {
        return _ptr != another._ptr;
    }

private:
    UT * _ptr = nullptr;
};

