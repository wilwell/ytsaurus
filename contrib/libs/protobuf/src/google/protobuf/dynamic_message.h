// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.
//
// Defines an implementation of Message which can emulate types which are not
// known at compile-time.

#ifndef GOOGLE_PROTOBUF_DYNAMIC_MESSAGE_H__
#define GOOGLE_PROTOBUF_DYNAMIC_MESSAGE_H__

#include <algorithm>
#include <memory>
#include <vector>

#include "y_absl/container/flat_hash_map.h"
#include "y_absl/synchronization/mutex.h"
#include "google/protobuf/message.h"
#include "google/protobuf/port.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/repeated_field.h"

#ifdef SWIG
#error "You cannot SWIG proto headers"
#endif

// Must be included last.
#include "google/protobuf/port_def.inc"

namespace google {
namespace protobuf {

// Defined in other files.
class Descriptor;      // descriptor.h
class DescriptorPool;  // descriptor.h

// Constructs implementations of Message which can emulate types which are not
// known at compile-time.
//
// Sometimes you want to be able to manipulate protocol types that you don't
// know about at compile time.  It would be nice to be able to construct
// a Message object which implements the message type given by any arbitrary
// Descriptor.  DynamicMessage provides this.
//
// As it turns out, a DynamicMessage needs to construct extra
// information about its type in order to operate.  Most of this information
// can be shared between all DynamicMessages of the same type.  But, caching
// this information in some sort of global map would be a bad idea, since
// the cached information for a particular descriptor could outlive the
// descriptor itself.  To avoid this problem, DynamicMessageFactory
// encapsulates this "cache".  All DynamicMessages of the same type created
// from the same factory will share the same support data.  Any Descriptors
// used with a particular factory must outlive the factory.
//
// The thread safety for this class is subtle, see comments around GetPrototype
// for details
class PROTOBUF_EXPORT DynamicMessageFactory : public MessageFactory {
 public:
  // Construct a DynamicMessageFactory that will search for extensions in
  // the DescriptorPool in which the extendee is defined.
  DynamicMessageFactory();

  // Construct a DynamicMessageFactory that will search for extensions in
  // the given DescriptorPool.
  //
  // DEPRECATED:  Use CodedInputStream::SetExtensionRegistry() to tell the
  //   parser to look for extensions in an alternate pool.  However, note that
  //   this is almost never what you want to do.  Almost all users should use
  //   the zero-arg constructor.
  DynamicMessageFactory(const DescriptorPool* pool);
  DynamicMessageFactory(const DynamicMessageFactory&) = delete;
  DynamicMessageFactory& operator=(const DynamicMessageFactory&) = delete;

  ~DynamicMessageFactory() override;

  // Call this to tell the DynamicMessageFactory that if it is given a
  // Descriptor d for which:
  //   d->file()->pool() == DescriptorPool::generated_pool(),
  // then it should delegate to MessageFactory::generated_factory() instead
  // of constructing a dynamic implementation of the message.  In theory there
  // is no down side to doing this, so it may become the default in the future.
  void SetDelegateToGeneratedFactory(bool enable) {
    delegate_to_generated_factory_ = enable;
  }

  // implements MessageFactory ---------------------------------------

  // Given a Descriptor, constructs the default (prototype) Message of that
  // type.  You can then call that message's New() method to construct a
  // mutable message of that type.
  //
  // Calling this method twice with the same Descriptor returns the same
  // object.  The returned object remains property of the factory and will
  // be destroyed when the factory is destroyed.  Also, any objects created
  // by calling the prototype's New() method share some data with the
  // prototype, so these must be destroyed before the DynamicMessageFactory
  // is destroyed.
  //
  // The given descriptor must outlive the returned message, and hence must
  // outlive the DynamicMessageFactory.
  //
  // The method is thread-safe.
  const Message* GetPrototype(const Descriptor* type) override;

 private:
  const DescriptorPool* pool_;
  bool delegate_to_generated_factory_;

  struct TypeInfo;
  y_absl::flat_hash_map<const Descriptor*, const TypeInfo*> prototypes_;
  mutable y_absl::Mutex prototypes_mutex_;

  friend class DynamicMessage;
  const Message* GetPrototypeNoLock(const Descriptor* type);
};

// Helper for computing a sorted list of map entries via reflection.
class PROTOBUF_EXPORT DynamicMapSorter {
 public:
  static std::vector<const Message*> Sort(const Message& message, int map_size,
                                          const Reflection* reflection,
                                          const FieldDescriptor* field) {
    std::vector<const Message*> result;
    result.reserve(map_size);
    RepeatedFieldRef<Message> map_field =
        reflection->GetRepeatedFieldRef<Message>(message, field);
    for (auto it = map_field.begin(); it != map_field.end(); ++it) {
      result.push_back(&*it);
    }
    MapEntryMessageComparator comparator(field->message_type());
    std::stable_sort(result.begin(), result.end(), comparator);
    // Complain if the keys aren't in ascending order.
#ifndef NDEBUG
    for (size_t j = 1; j < static_cast<size_t>(map_size); j++) {
      if (!comparator(result[j - 1], result[j])) {
        Y_ABSL_LOG(ERROR) << (comparator(result[j], result[j - 1])
                                ? "internal error in map key sorting"
                                : "map keys are not unique");
      }
    }
#endif
    return result;
  }

 private:
  class PROTOBUF_EXPORT MapEntryMessageComparator {
   public:
    explicit MapEntryMessageComparator(const Descriptor* descriptor)
        : field_(descriptor->field(0)) {}

    bool operator()(const Message* a, const Message* b) {
      const Reflection* reflection = a->GetReflection();
      switch (field_->cpp_type()) {
        case FieldDescriptor::CPPTYPE_BOOL: {
          bool first = reflection->GetBool(*a, field_);
          bool second = reflection->GetBool(*b, field_);
          return first < second;
        }
        case FieldDescriptor::CPPTYPE_INT32: {
          arc_i32 first = reflection->GetInt32(*a, field_);
          arc_i32 second = reflection->GetInt32(*b, field_);
          return first < second;
        }
        case FieldDescriptor::CPPTYPE_INT64: {
          arc_i64 first = reflection->GetInt64(*a, field_);
          arc_i64 second = reflection->GetInt64(*b, field_);
          return first < second;
        }
        case FieldDescriptor::CPPTYPE_UINT32: {
          arc_ui32 first = reflection->GetUInt32(*a, field_);
          arc_ui32 second = reflection->GetUInt32(*b, field_);
          return first < second;
        }
        case FieldDescriptor::CPPTYPE_UINT64: {
          arc_ui64 first = reflection->GetUInt64(*a, field_);
          arc_ui64 second = reflection->GetUInt64(*b, field_);
          return first < second;
        }
        case FieldDescriptor::CPPTYPE_STRING: {
          TProtoStringType first = reflection->GetString(*a, field_);
          TProtoStringType second = reflection->GetString(*b, field_);
          return first < second;
        }
        default:
          Y_ABSL_DLOG(FATAL) << "Invalid key for map field.";
          return true;
      }
    }

   private:
    const FieldDescriptor* field_;
  };
};

}  // namespace protobuf
}  // namespace google

#include "google/protobuf/port_undef.inc"

#endif  // GOOGLE_PROTOBUF_DYNAMIC_MESSAGE_H__
