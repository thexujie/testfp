#pragma once

template<typename UT>
class com_ptr
{
public:
    com_ptr() {}
    com_ptr(com_ptr<UT> & another)
    {
        if(another._ptr)
        {
            another._ptr->AddRef();
            _ptr = another._ptr;
        }
    }

    com_ptr(UT * ptr):_ptr(ptr){}
    ~com_ptr()
    {
        if (_ptr)
            _ptr->Release();
    }

    UT ** operator & ()
    {
        return &_ptr;
    }

    UT *& operator ->()
    {
        return _ptr;
    }

    operator bool() const { return !!_ptr; }

    com_ptr<UT> & operator=(com_ptr<UT> & another)
    {
        if (another._ptr)
        {
            another._ptr->AddRef();
            _ptr = another._ptr;
        }
        return *this;
    }

private:
    UT * _ptr = nullptr;
};

