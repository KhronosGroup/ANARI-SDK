// Copyright 2023-2025 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace remote {

struct Parameter
{
  char *name;
  ANARIDataType type;
};

// RAII List type storing parameters that can be
// converted to {const char *, type} pairs
class ParameterList
{
 public:
  ParameterList() = default;

  // initialize from list of params
  // last  pointer must be NULL!
  ParameterList(const Parameter *parameter)
  {
    for (; parameter && parameter->name != nullptr; parameter++) {
      this->push_back(std::string(parameter->name), parameter->type);
    }

    if (parameter != nullptr)
      value.push_back({nullptr, parameter->type});
    else
      value.push_back({nullptr, ANARI_UNKNOWN});
  }

  ParameterList(const ParameterList &other)
  {
    for (size_t i = 0; i < other.value.size(); ++i) {
      if (other.value[i].name == nullptr)
        value.push_back({nullptr, other.value[i].type});
      else
        this->push_back(std::string(other.value[i].name), other.value[i].type);
    }
  }

  ParameterList(ParameterList &&other)
  {
    value = std::move(other.value);
    other.value.clear();
  }

  ~ParameterList()
  {
    for (size_t i = 0; i < value.size(); ++i) {
      delete[] value[i].name;
    }
  }

  ParameterList &operator=(const ParameterList &other)
  {
    if (&other != this) {
      for (size_t i = 0; i < other.value.size(); ++i) {
        if (other.value[i].name == nullptr)
          value.push_back({nullptr, other.value[i].type});
        else
          this->push_back(std::string(other.value[i].name), other.value[i].type);
      }
    }
    return *this;
  }

  ParameterList &operator=(ParameterList &&other)
  {
    if (&other != this) {
      value = std::move(other.value);
    }
    return *this;
  }

  void push_back(std::string name, ANARIDataType type)
  {
    if (name.length() > 0) {
      char *val = new char[name.length() + 1];
      memcpy(val, name.data(), name.length());
      val[name.length()] = '\0';
      value.push_back({val, type});
    } else {
      value.push_back({nullptr, type});
    }
  }

  const Parameter *data() const
  {
    return (const Parameter *)value.data();
  }

  size_t size() const
  {
    return value.size();
  }

 private:
  std::vector<Parameter> value;
};

} // namespace remote
