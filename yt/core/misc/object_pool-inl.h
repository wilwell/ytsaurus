#ifndef OBJECT_POOL_INL_H_
#error "Direct inclusion of this file is not allowed, include object_pool.h"
#endif
#undef OBJECT_POOL_INL_H_

#include <core/misc/mpl.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

template <class T>
TObjectPool<T>::TObjectPool()
    : PoolSize_(0)
{ }

template <class T>
typename TObjectPool<T>::TValuePtr TObjectPool<T>::Allocate()
{
    T* object = nullptr;
    if (PooledObjects_.Dequeue(&object)) {
        AtomicDecrement(PoolSize_);
    } else {
        object = AllocateInstance();
    }
    
    return TValuePtr(object, [] (T* object) {
        ObjectPool<T>().Reclaim(object);
    });
}

template <class T>
void TObjectPool<T>::Reclaim(T* obj)
{
    auto* header = GetHeader(obj);
    if (header->ExpireTime > TInstant::Now()) {
        DisposeInstance(obj);
        return;
    }

    TPooledObjectTraits<T>::Clean(obj);
    PooledObjects_.Enqueue(obj);
    if (AtomicIncrement(PoolSize_) > TPooledObjectTraits<T>::GetMaxPoolSize()) {
        T* objToDestroy;
        if (PooledObjects_.Dequeue(&objToDestroy)) {
            AtomicDecrement(PoolSize_);
            delete objToDestroy;
        }
    }
}

template <class T>
T* TObjectPool<T>::AllocateInstance()
{
    char* buffer = new char[sizeof (THeader) + sizeof (T)];
    auto* header = reinterpret_cast<THeader*>(buffer);
    auto* obj = reinterpret_cast<T*>(header + 1);
    new (obj) T();
    header->ExpireTime =
        TInstant::Now() +
        TPooledObjectTraits<T>::GetMaxLifetime() +
        RandomDuration(TPooledObjectTraits<T>::GetMaxLifetimeSplay());
    return obj;
}

template <class T>
void TObjectPool<T>::DisposeInstance(T* obj)
{
    obj->~T();
    auto* buffer = reinterpret_cast<char*>(obj) - sizeof (THeader);
    delete[] buffer;
}

tempalte <class T>
TObjectPool<T>::THeader TObjectPool<T>::GetHeader(T* obj)
{
    return reinterpret_cast<THeader*>(obj) - 1;
}

template <class T>
TObjectPool<T>& ObjectPool()
{
#ifdef _MSC_VER
    // XXX(babenko): MSVC (upto version 2013) does not support thread-safe static locals init :(
    static TAtomic lock = 0;
    static TAtomic pool = 0;
    while (!pool) {
        if (AtomicCas(&lock, 1, 0)) {
            if (!pool) {
                AtomicSet(pool, reinterpret_cast<intptr_t>(new TObjectPool<T>()));
            }
            AtomicSet(lock, 0);
        } else {
            SpinLockPause();
        }
    }
    return *reinterpret_cast<TObjectPool<T>*>(pool);
#else
    static TObjectPool<T> pool;
    return pool;
#endif
}

////////////////////////////////////////////////////////////////////////////////

template <class T>
struct TPooledObjectTraits<
    T,
    typename NMpl::TEnableIf<
        NMpl::TIsConvertible<T&, ::google::protobuf::MessageLite&> 
    >::TType
>
    : public TPooledObjectTraitsBase
{
    static void Clean(::google::protobuf::MessageLite* obj)
    {
        obj->Clear();
    }
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
