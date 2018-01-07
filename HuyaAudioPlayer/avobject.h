#pragma once

template<typename T, void (*ReleaseFunc)(void *)>
class avobject
{
public:
    avobject():ptr(NULL){}
    avobject(T * p) : ptr(p){}
    ~avobject()
    {
        if(ptr)
            ReleaseFunc(ptr);
    }

    avobject & attach(T * p)
    {
        if(ptr)
            ReleaseFunc(ptr);
        ptr = p;
        return *this;
    }

    T * detech()
    {
        T * p = ptr;
        ptr = 0;
        return p;
    }

    void free()
    {
        if(ptr)
            ReleaseFunc(ptr);
    }

    T ** operator & () { return &ptr; }
    T * operator -> () { return ptr; }
    operator T *() { return ptr; }
    operator const T *() const { return ptr;}

    T * ptr;
};

template<typename T, void(*ReleaseFunc)(T *)>
class avobject2
{
public:
    avobject2() :ptr(NULL) {}
    avobject2(T * p) : ptr(p) {}
    ~avobject2()
    {
        if(ptr)
            ReleaseFunc(ptr);
    }

    avobject2 & attach(T * p)
    {
        if(ptr)
            ReleaseFunc(ptr);
        ptr = p;
        return *this;
    }

    T * detech()
    {
        T * p = ptr;
        ptr = 0;
        return p;
    }

    void free()
    {
        if(ptr)
            ReleaseFunc(ptr);
    }

    T ** operator & () { return &ptr; }
    T * operator -> () { return ptr; }
    operator T *() { return ptr; }
    operator const T *() const { return ptr; }

    T * ptr;
};

template<typename T, void(*ReleaseFunc)(T **)>
class avobject3
{
public:
    avobject3() :ptr(NULL) {}
    avobject3(T * p) : ptr(p) {}
    ~avobject3()
    {
        if(ptr)
            ReleaseFunc(&ptr);
    }

    avobject3 & attach(T * p)
    {
        if(ptr)
            ReleaseFunc(ptr);
        ptr = p;
        return *this;
    }

    T * detech()
    {
        T * p = ptr;
        ptr = 0;
        return p;
    }

    void free()
    {
        if(ptr)
            ReleaseFunc(ptr);
    }

    T ** operator & () { return &ptr; }
    T * operator -> () { return ptr; }
    operator T *() { return ptr; }
    operator const T *() const { return ptr; }

    T * ptr;
};

