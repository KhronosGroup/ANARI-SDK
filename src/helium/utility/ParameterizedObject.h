// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AnariAny.h"
// anari
#include "anari/anari_cpp/Traits.h"
// stl
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace helium {

/*
 * Mixin that provides type-safe parameter storage and retrieval.
 * Parameters are stored as (name → AnariAny) pairs and accessed via strongly-
 * typed getParam<T>()/getParamObject<T>()/getParamString() methods. Device
 * objects inherit this to implement the pull model used during
 * commitParameters(): the object reads whatever parameters it needs rather than
 * receiving them as arguments. Object parameters do not affect lifetime on
 * their own — callers should store the result in an IntrusivePtr<T> to ensure
 * correct lifetime.
 */
struct ParameterizedObject
{
  ParameterizedObject() = default;
  virtual ~ParameterizedObject() = default;

  // Return true if there was a parameter set with the corresponding 'name'
  bool hasParam(const std::string &name) const;

  // Return true if there was a parameter set with the corresponding 'name' and
  // if it matches the corresponding type
  bool hasParam(const std::string &name, ANARIDataType type) const;

  // Set the value of the parameter 'name', or add it if it doesn't exist yet
  //
  // Returns 'true' if the value for that parameter actually changed
  bool setParam(const std::string &name, ANARIDataType type, const void *v);

  // Set the value of the parameter 'name', or add it if it doesn't exist yet
  //
  // Returns 'true' if the value for that parameter actually changed
  template <typename T>
  bool setParam(const std::string &name, const T &v);

  // Get the value of the parameter associated with 'name', or return
  // 'valueIfNotFound' if the parameter isn't set. This is strongly typed by
  // using the anari::ANARITypeFor<T> to get the ANARIDataType of the provided
  // C++ type 'T'. Implementations should specialize `anari::ANARITypeFor<>` for
  // things like vector math types and matricies. This function is not able to
  // access ANARIObject or ANARIString parameters, see special methods for
  // getting parameters of those types.
  template <typename T>
  T getParam(const std::string &name, T valIfNotFound) const;

  // Get the value of the parameter associated with 'name' and write it to
  // location 'v', returning whether the was actually read. Just like the
  // templated version above, this requires that 'type' exactly match what the
  // application set. This function also cannot get objects or strings.
  bool getParam(const std::string &name, ANARIDataType type, void *v) const;

  // Get the pointer to an object parameter (returns null if not present). While
  // ParameterizedObject will track object lifetime appropriately, accessing
  // an object parameter does _not_ influence lifetime considerations -- devices
  // should consider using `helium::IntrusivePtr<>` to guarantee correct
  // lifetime handling.
  template <typename T>
  T *getParamObject(const std::string &name) const;

  // Get a string parameter value
  std::string getParamString(
      const std::string &name, const std::string &valIfNotFound) const;

  // Get/Set the container holding the value of a parameter (default constructed
  // AnariAny if not present). Getting this container will create a copy of the
  // parameter value, which for objects will incur the correct ref count changes
  // accordingly (handled by AnariAny).
  AnariAny getParamDirect(const std::string &name) const;
  void setParamDirect(const std::string &name, const AnariAny &v);

  // Remove the value of the parameter associated with 'name'.
  //
  // Returns 'true' if anything actually happened
  bool removeParam(const std::string &name);

  // Remove all set parameters
  //
  // Returns 'true' if anything actually happened
  bool removeAllParams();

  // Snapshot the current (staging) parameter store into the committed store.
  // Called by the deferred-commit machinery at anariCommitParameters() time so
  // that a later commitParameters()/finalize() reads the state captured at the
  // commit call rather than the live store (which may receive further setParam
  // calls before the commit buffer is flushed). AnariAny's copy semantics
  // manage object-parameter ref counts, so the previous snapshot's object refs
  // are released and the new snapshot's refs acquired exactly once.
  void commitParameterSnapshot();

  // RAII scope that routes this object's (const) getters to its committed
  // snapshot for the scope's duration. The deferred-commit machinery wraps an
  // object's buffered commitParameters()/finalize() in one of these so those
  // reads see the state captured at commit time. Constructing the scope holds
  // m_commitReadMutex, which serializes the deferred read against a concurrent
  // commitParameterSnapshot() (a legal anariCommitParameters() while a frame
  // using the object is still in flight), keeping m_paramsCommitted stable for
  // the whole commit/finalize. The selector itself lives on the object (not a
  // thread-local) so it is robust to helium being statically linked into
  // multiple modules. Objects driven directly by a parent's commit (e.g. helide
  // World's internal zero group) get no scope of their own and so correctly
  // read their live staging store.
  struct ReadCommittedScope
  {
    explicit ReadCommittedScope(ParameterizedObject *obj);
    ~ReadCommittedScope();
    ReadCommittedScope(const ReadCommittedScope &) = delete;
    ReadCommittedScope &operator=(const ReadCommittedScope &) = delete;

   private:
    ParameterizedObject *m_obj;
  };

 protected:
  using Param = std::pair<std::string, AnariAny>;
  using ParameterList = std::vector<Param>;

  ParameterList::iterator params_begin();
  ParameterList::iterator params_end();

 private:
  // Data members //

  const Param *findParam(const std::string &name) const;
  Param *findParam(const std::string &name);

  // The store the getters read: the committed snapshot while a
  // ReadCommittedScope is active on this object, else the live staging store.
  const ParameterList &readParams() const;

  ParameterList m_paramsStaging;
  ParameterList m_paramsCommitted;
  // Guards m_paramsCommitted between the snapshot write
  // (commitParameterSnapshot, at anariCommitParameters() time) and the deferred
  // read (a ReadCommittedScope around commitParameters()/finalize() on the
  // flush thread). A dedicated mutex, not the object lock: frameReady() holds
  // the object lock while blocked on the flush, so reusing it here would
  // deadlock.
  std::mutex m_commitReadMutex;
  // Selects committed vs. staging reads (see ReadCommittedScope). Atomic
  // because the flush thread toggles it while an app-side getter may read it
  // (under the object lock) on another thread.
  std::atomic<bool> m_readCommitted{false};
};

// Inlined ParameterizedObject definitions ////////////////////////////////////

template <typename T>
inline bool ParameterizedObject::setParam(const std::string &name, const T &v)
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  return setParam(name, type, &v);
}

template <>
inline bool ParameterizedObject::setParam(
    const std::string &name, const std::string &v)
{
  return setParam(name, ANARI_STRING, v.c_str());
}

template <>
inline bool ParameterizedObject::setParam(
    const std::string &name, const bool &v)
{
  uint8_t b = v;
  return setParam(name, ANARI_BOOL, &b);
}

template <typename T>
inline T ParameterizedObject::getParam(
    const std::string &name, T valIfNotFound) const
{
  constexpr ANARIDataType type = anari::ANARITypeFor<T>::value;
  static_assert(!anari::isObject(type),
      "use ParameterizedObect::getParamObject() for getting objects");
  static_assert(type != ANARI_STRING && !std::is_same_v<T, std::string>,
      "use ParameterizedObject::getParamString() for getting strings");
  auto *p = findParam(name);
  return p && p->second.is(type) ? p->second.get<T>() : valIfNotFound;
}

template <>
inline bool ParameterizedObject::getParam(
    const std::string &name, bool valIfNotFound) const
{
  auto *p = findParam(name);
  return p && p->second.is(ANARI_BOOL) ? p->second.get<bool>() : valIfNotFound;
}

template <typename T>
inline T *ParameterizedObject::getParamObject(const std::string &name) const
{
  auto *p = findParam(name);
  return p ? p->second.getObject<T>() : nullptr;
}

} // namespace helium
