#pragma once
#ifndef STRING_INL_H_
#error "Direct inclusion of this file is not allowed, include string.h"
// For the sake of sane code completion.
#include "string.h"
#endif

#include <type_traits>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

inline char* TStringBuilderBase::Preallocate(size_t size)
{
    if (Y_UNLIKELY(Current_ + size > End_)) {
        DoPreallocate(size);
    }
    return Current_;
}

inline size_t TStringBuilderBase::GetLength() const
{
    return Current_ ? Current_ - Begin_ : 0;
}

inline TStringBuf TStringBuilderBase::GetBuffer() const
{
    return TStringBuf(Begin_, Current_);
}

inline void TStringBuilderBase::Advance(size_t size)
{
    Current_ += size;
    Y_ASSERT(Current_ <= End_);
}

inline void TStringBuilderBase::AppendChar(char ch)
{
    *Preallocate(1) = ch;
    Advance(1);
}

inline void TStringBuilderBase::AppendChar(char ch, int n)
{
    Y_ASSERT(n >= 0);
    char* dst = Preallocate(n);
    ::memset(dst, ch, n);
    Advance(n);
}

inline void TStringBuilderBase::AppendString(TStringBuf str)
{
    char* dst = Preallocate(str.length());
    ::memcpy(dst, str.begin(), str.length());
    Advance(str.length());
}

inline void TStringBuilderBase::AppendString(const char* str)
{
    AppendString(TStringBuf(str));
}

inline void TStringBuilderBase::Reset()
{
    Begin_ = Current_ = End_ = nullptr;
    DoReset();
}

template <class... TArgs>
void TStringBuilderBase::AppendFormat(TStringBuf format, TArgs&& ... args)
{
    Format(this, format, std::forward<TArgs>(args)...);
}

template <class... TArgs, size_t FormatLength>
void TStringBuilderBase::AppendFormat(const char (&format)[FormatLength], TArgs&& ... args)
{
    Format(this, format, std::forward<TArgs>(args)...);
}

////////////////////////////////////////////////////////////////////////////////

inline TString TStringBuilder::Flush()
{
    Buffer_.resize(GetLength());
    auto result = std::move(Buffer_);
    Reset();
    return result;
}

inline void TStringBuilder::DoReset()
{
    Buffer_ = {};
}

inline void TStringBuilder::DoPreallocate(size_t size)
{
    size_t length = GetLength();
    size = std::max(size, MinBufferLength);
    Buffer_.ReserveAndResize(length + size);
    Begin_ = &*Buffer_.begin();
    Current_ = Begin_ + length;
    End_ = Current_ + size;
}

////////////////////////////////////////////////////////////////////////////////

inline void FormatValue(TStringBuilderBase* builder, const TStringBuilder& value, TStringBuf /*format*/)
{
    builder->AppendString(value.GetBuffer());
}

template <class T>
TString ToStringViaBuilder(const T& value, TStringBuf spec)
{
    TStringBuilder builder;
    FormatValue(&builder, value, spec);
    return builder.Flush();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
